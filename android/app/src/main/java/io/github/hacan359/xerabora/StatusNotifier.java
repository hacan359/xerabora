package io.github.hacan359.xerabora;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.service.notification.StatusBarNotification;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The fixed notification (what the console is doing, the game, progress
 * and playtime) and one notification per unlocked achievement. Lives in
 * the service; everything runs on the main thread except the downloads.
 */
final class StatusNotifier {
    static final int ONGOING_ID = 1;

    private static final String TAG = "xerabora";
    private static final String CHANNEL_STATUS = "engine";
    private static final String CHANNEL_UNLOCKS = "unlocks";
    private static final String CHANNEL_EVENTS = "events";
    private static final String UNLOCK_TAG = "unlock";
    private static final int MASTERY_ID = 2;
    private static final int UNLOCK_SUMMARY_ID = 4;
    /* Every notification has a group of its own kind: an app's loose
       notifications get bundled by the system, the status one included,
       and swiping the bundle took the status away with the unlocks. */
    private static final String GROUP_STATUS = "status";
    private static final String GROUP_UNLOCKS = "unlocks";
    private static final String GROUP_EVENTS = "events";
    private static final int SUMMARY_LINES = 6;
    /* One slot: a new console event replaces the last one. */
    private static final int EVENT_ID = 3;
    private static final long EVENT_TIMEOUT_MS = 20 * 1000L;
    /* A discovery with no game behind it this long after is the menu's
       "RA: test PC connection"; a game start sends one too. */
    private static final long TEST_WAIT_MS = 6000;
    /* The console repeats its questions; one pop-up per answer. */
    private static final long REPEAT_MS = 30 * 1000L;
    /* A link that drops and returns this fast is the same game still running. */
    private static final long BLIP_MS = 60 * 1000L;
    /* The A of the icon. */
    private static final int GOLD = 0xffe5a823;
    /* "PS2 found" stays up this long after a discovery without a game. */
    private static final long FOUND_MS = 3 * 60 * 1000L;
    /* Back within this long on the same game: the same session. */
    private static final long RESUME_MS = 10 * 60 * 1000L;
    /* How long an unlock waits for its badge before it shows anyway. */
    private static final long BADGE_WAIT_MS = 2500;

    /** One report from the client (status.c). */
    static final class Status {
        boolean connected, portBusy, loggedIn;
        String serial = "", status = "", title = "", image = "", presence = "";
        int gameId, unlocked, total, pointsUnlocked, pointsTotal;
    }

    private final Service service;
    private final NotificationManager nm;
    private final SharedPreferences playtime;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService downloads = Executors.newSingleThreadExecutor();
    private final Map<String, Bitmap> icons = new HashMap<>();
    private final Set<String> loading = new HashSet<>();
    private final PendingIntent open;
    private final PendingIntent quit;
    private final PendingIntent repost;
    /* The unlocks still in the shade, newest last, with when each was
       posted: the group's summary. */
    private final LinkedHashMap<Integer, String> unlocksShown = new LinkedHashMap<>();
    private final Map<Integer, Long> unlockPostedAt = new HashMap<>();

    private Status status = new Status();
    private boolean reported;
    private boolean stopped;
    private long discoveredAt;

    /* Pop-ups: the pending "test PC connection" answer, the last one shown
       (to drop repeats), and which link the game start was announced for. */
    private Runnable pendingTest;
    private String lastEvent = "";
    private long lastEventAt;
    private int link;
    private int announcedLink = -1;

    /* The play session: one game, from its first snapshot to the console
       going quiet, pauses under RESUME_MS included. */
    private String sessionSerial = "";
    private String sessionName = "";
    private long sessionStart;
    private long sessionEnd;
    private long countedUntil;

    private final Runnable minuteTick = new Runnable() {
        @Override
        public void run() {
            savePlaytime();
            render();
            main.postDelayed(this, 60 * 1000L);
        }
    };

    StatusNotifier(Service service, PendingIntent open, PendingIntent quit, PendingIntent repost) {
        this.service = service;
        this.open = open;
        this.quit = quit;
        this.repost = repost;
        nm = service.getSystemService(NotificationManager.class);
        playtime = service.getSharedPreferences("playtime", Context.MODE_PRIVATE);

        nm.createNotificationChannel(new NotificationChannel(CHANNEL_STATUS,
                service.getString(R.string.channel_engine), NotificationManager.IMPORTANCE_LOW));
        NotificationChannel unlocks = new NotificationChannel(CHANNEL_UNLOCKS,
                service.getString(R.string.channel_unlocks), NotificationManager.IMPORTANCE_HIGH);
        // The client plays its own achievement sound; the channel only
        // vibrates, so an unlock is never heard twice.
        unlocks.setSound(null, null);
        unlocks.enableVibration(true);
        unlocks.enableLights(true);
        unlocks.setLightColor(GOLD);
        nm.createNotificationChannel(unlocks);
        NotificationChannel events = new NotificationChannel(CHANNEL_EVENTS,
                service.getString(R.string.channel_events), NotificationManager.IMPORTANCE_HIGH);
        // The client already plays its connect sound.
        events.setSound(null, null);
        events.enableVibration(false);
        nm.createNotificationChannel(events);
        main.postDelayed(minuteTick, 60 * 1000L);
    }

    /** The notification startForeground() shows before the first report. */
    Notification initial() {
        return build();
    }

    void onStatus(Status s) {
        long now = System.currentTimeMillis();
        boolean playing = s.connected && !s.serial.isEmpty();

        if (s.connected && !status.connected) {
            // Telemetry: whatever the last discovery was, it was a game starting.
            link++;
            cancelTest();
            if (s.serial.equals(sessionSerial) && sessionEnd != 0 && now - sessionEnd < BLIP_MS) {
                announcedLink = link;
            }
        }

        if (playing) {
            boolean resume = s.serial.equals(sessionSerial) && sessionEnd != 0 && now - sessionEnd < RESUME_MS;
            if (!s.serial.equals(sessionSerial) || (sessionEnd != 0 && !resume)) {
                savePlaytime();
                sessionSerial = s.serial;
                sessionStart = now;
                sessionEnd = 0;
                countedUntil = now;
                announcedLink = -1;
            } else if (resume) {
                // The pause does not count.
                sessionStart += now - sessionEnd;
                sessionEnd = 0;
                countedUntil = now;
            }
            sessionName = !s.title.isEmpty() ? s.title : s.serial;
        } else if (status.connected && sessionEnd == 0 && !sessionSerial.isEmpty()) {
            savePlaytime();
            sessionEnd = now;
        }

        status = s;
        reported = true;
        render();
        if (playing && announcedLink != link) {
            announceGame(s);
        }
    }

    void onDiscovery(String consoleIp) {
        discoveredAt = System.currentTimeMillis();
        render();
        main.postDelayed(this::render, FOUND_MS + 1000);

        cancelTest();
        pendingTest = () -> {
            pendingTest = null;
            if (!status.connected) {
                event("found", service.getString(R.string.event_found_title),
                        service.getString(R.string.event_found_text, consoleIp), null);
            }
        };
        main.postDelayed(pendingTest, TEST_WAIT_MS);
    }

    /** "RA: check game support" in the OPL menu got its answer. */
    void onCheck(boolean ok, String title, int total, int unlocked, int unsupported, String reason) {
        if (ok) {
            String text;
            if (title.isEmpty()) {
                text = service.getString(R.string.event_check_ready);
            } else {
                text = service.getString(R.string.event_check_ok_text, title, total, unlocked);
                if (unsupported > 0) {
                    text += " · " + service.getString(R.string.event_check_unsupported, unsupported);
                }
            }
            event("check-ok " + title, service.getString(R.string.event_check_ok_title), text, null);
        } else {
            event("check-no " + reason, service.getString(R.string.event_check_no_title),
                    reason.isEmpty() ? service.getString(R.string.event_check_no_text) : reason, null);
        }
    }

    /* The game is up: its name, icon and progress, or why it has none. */
    private void announceGame(Status s) {
        String name = !s.title.isEmpty() ? s.title : s.serial;
        String line;
        if (s.gameId != 0) {
            line = service.getString(R.string.event_playing_text, service.getString(R.string.status_progress,
                    s.unlocked, s.total, s.pointsUnlocked, s.pointsTotal));
        } else if (s.status.equals("no-hash")) {
            line = service.getString(R.string.status_no_hash);
        } else if (s.status.equals("telemetry-only")) {
            line = service.getString(R.string.status_telemetry_only);
        } else {
            return; // still identifying: wait for the set
        }
        announcedLink = link;
        String key = "game " + s.serial + " " + link;
        event(key, name, line, s.gameId != 0 ? s.image : null);
    }

    private void cancelTest() {
        if (pendingTest != null) {
            main.removeCallbacks(pendingTest);
            pendingTest = null;
        }
    }

    /**
     * A pop-up on the events channel. {@code key} drops a repeat of the same
     * event; {@code image}, when given, is fetched and the pop-up updated.
     */
    private void event(String key, String title, String text, String image) {
        long now = System.currentTimeMillis();
        if (stopped || (key.equals(lastEvent) && now - lastEventAt < REPEAT_MS)) {
            return;
        }
        lastEvent = key;
        lastEventAt = now;
        nm.notify(EVENT_ID, eventNotification(title, text, image != null ? icons.get(image) : null, false));
        if (image != null && !image.isEmpty() && !icons.containsKey(image)) {
            fetch(image, () -> {
                if (!stopped && key.equals(lastEvent)) {
                    nm.notify(EVENT_ID, eventNotification(title, text, icons.get(image), true));
                }
            });
        }
    }

    private Notification eventNotification(String title, String text, Bitmap icon, boolean again) {
        Notification.Builder b = new Notification.Builder(service, CHANNEL_EVENTS)
                .setSmallIcon(R.drawable.ic_notify)
                .setColor(GOLD)
                .setContentTitle(title)
                .setContentText(text)
                .setStyle(new Notification.BigTextStyle().bigText(text))
                .setCategory(Notification.CATEGORY_STATUS)
                .setShowWhen(true)
                .setAutoCancel(true)
                .setOnlyAlertOnce(again)
                .setGroup(GROUP_EVENTS)
                .setTimeoutAfter(EVENT_TIMEOUT_MS)
                .setContentIntent(open);
        if (icon != null) {
            b.setLargeIcon(icon);
        }
        return b.build();
    }

    void onUnlock(int id, String title, String description, String image, int points, int unlocked, int total) {
        final boolean[] posted = {false};
        Runnable post = () -> {
            Bitmap badge = icons.get(image);
            if (stopped || (posted[0] && badge == null)) {
                return;
            }
            nm.notify(UNLOCK_TAG, id, unlockNotification(title, description, badge, points, unlocked, total,
                    posted[0]));
            if (!posted[0]) {
                unlocksShown.put(id, title);
                unlockPostedAt.put(id, System.currentTimeMillis());
                postUnlockSummary();
            }
            posted[0] = true;
        };

        if (image.isEmpty() || icons.containsKey(image)) {
            post.run();
        } else {
            fetch(image, post);
            main.postDelayed(() -> {
                if (!posted[0]) {
                    post.run();
                }
            }, BADGE_WAIT_MS);
        }

        if (total > 0 && unlocked == total) {
            nm.notify(MASTERY_ID, new Notification.Builder(service, CHANNEL_UNLOCKS)
                    .setSmallIcon(R.drawable.ic_notify)
                    .setColor(GOLD)
                    .setContentTitle(service.getString(R.string.mastery_title))
                    .setContentText(service.getString(R.string.mastery_text, sessionName, total))
                    .setGroup(GROUP_UNLOCKS)
                    .setGroupAlertBehavior(Notification.GROUP_ALERT_CHILDREN)
                    .setContentIntent(open)
                    .setAutoCancel(true)
                    .build());
        }
    }

    /** The status notification was swiped away (Android 14 allows it). */
    void repost() {
        render();
    }

    /* The unlocks bundle: "5 achievements unlocked" and the newest titles.
       Unlocks the player already swiped away no longer count; the shade
       lists a new one a moment after notify(), so the last few seconds'
       stay regardless. */
    private void postUnlockSummary() {
        Set<Integer> active = new HashSet<>();
        for (StatusBarNotification n : nm.getActiveNotifications()) {
            if (UNLOCK_TAG.equals(n.getTag())) {
                active.add(n.getId());
            }
        }
        long settled = System.currentTimeMillis() - 5000;
        unlocksShown.keySet().removeIf(id -> !active.contains(id) && unlockPostedAt.getOrDefault(id, 0L) < settled);
        unlockPostedAt.keySet().retainAll(unlocksShown.keySet());

        List<String> titles = new ArrayList<>(unlocksShown.values());
        Collections.reverse(titles);
        int count = titles.size();
        Notification.InboxStyle inbox = new Notification.InboxStyle();
        for (int i = 0; i < Math.min(count, SUMMARY_LINES); i++) {
            inbox.addLine(titles.get(i));
        }
        if (count > SUMMARY_LINES) {
            inbox.setSummaryText("+" + (count - SUMMARY_LINES));
        }
        String heading = service.getResources().getQuantityString(R.plurals.unlocks_summary, count, count);
        nm.notify(UNLOCK_SUMMARY_ID, new Notification.Builder(service, CHANNEL_UNLOCKS)
                .setSmallIcon(R.drawable.ic_notify)
                .setColor(GOLD)
                .setContentTitle(heading)
                .setContentText(String.join(", ", titles))
                .setStyle(inbox.setBigContentTitle(heading))
                .setGroup(GROUP_UNLOCKS)
                .setGroupSummary(true)
                .setGroupAlertBehavior(Notification.GROUP_ALERT_CHILDREN)
                .setOnlyAlertOnce(true)
                .setAutoCancel(true)
                .setContentIntent(open)
                .build());
    }

    /** The client stopped: what was played so far is kept. */
    void stop() {
        stopped = true;
        savePlaytime();
        main.removeCallbacksAndMessages(null);
        downloads.shutdownNow();
    }

    /* ---- The fixed notification ------------------------------------------ */

    private void render() {
        if (!stopped) {
            nm.notify(ONGOING_ID, build());
        }
    }

    private Notification build() {
        Status s = status;
        long now = System.currentTimeMillis();
        Notification.Builder b = new Notification.Builder(service, CHANNEL_STATUS)
                .setSmallIcon(R.drawable.ic_notify)
                .setColor(GOLD)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setShowWhen(false)
                .setGroup(GROUP_STATUS)
                .setDeleteIntent(repost)
                .setContentIntent(open)
                .addAction(new Notification.Action.Builder((Icon) null,
                        service.getString(R.string.action_quit), quit).build());
        if (Build.VERSION.SDK_INT >= 31) {
            b.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        }

        if (!reported) {
            return b.setContentTitle(service.getString(R.string.app_name))
                    .setContentText(service.getString(R.string.waiting)).build();
        }
        if (s.portBusy) {
            return text(b, R.string.status_port_title, service.getString(R.string.status_port_text)).build();
        }
        if (!s.loggedIn) {
            return text(b, R.string.status_login_title, service.getString(R.string.status_login_text)).build();
        }

        if (s.connected) {
            String name = !s.title.isEmpty() ? s.title : s.serial;
            if (sessionStart != 0 && sessionEnd == 0) {
                b.setUsesChronometer(true).setWhen(sessionStart).setShowWhen(true);
            }
            if (s.gameId != 0) {
                return playing(b, s).build();
            }
            if (name.isEmpty()) {
                return text(b, R.string.status_connected_title,
                        service.getString(R.string.status_connected_text)).build();
            }
            int line;
            switch (s.status) {
                case "identifying":
                    line = R.string.status_identifying;
                    break;
                case "no-hash":
                    line = R.string.status_no_hash;
                    break;
                case "telemetry-only":
                    line = R.string.status_telemetry_only;
                    break;
                default:
                    line = R.string.status_connected_text;
                    break;
            }
            return b.setContentTitle(name).setContentText(service.getString(line))
                    .setStyle(new Notification.BigTextStyle().bigText(service.getString(line))).build();
        }

        if (discoveredAt != 0 && now - discoveredAt < FOUND_MS) {
            return text(b, R.string.status_found_title, service.getString(R.string.status_found_text)).build();
        }
        if (!sessionSerial.isEmpty() && sessionEnd != 0) {
            return text(b, R.string.status_off_title, service.getString(R.string.status_last_game,
                    sessionName, duration(sessionEnd - sessionStart))).build();
        }
        return text(b, R.string.status_off_title, service.getString(R.string.status_off_text)).build();
    }

    private Notification.Builder playing(Notification.Builder b, Status s) {
        String progress = s.total > 0 && s.unlocked == s.total
                ? service.getString(R.string.status_mastered, s.total)
                : service.getString(R.string.status_progress, s.unlocked, s.total, s.pointsUnlocked, s.pointsTotal);
        StringBuilder big = new StringBuilder(progress);
        if (s.status.equals("stale")) {
            big.append('\n').append(service.getString(R.string.status_stale));
        }
        if (!s.presence.isEmpty()) {
            big.append('\n').append(s.presence);
        }
        big.append('\n').append(service.getString(R.string.status_total_time, duration(totalPlaytime(s.serial))));

        b.setContentTitle(s.title)
                .setContentText(progress)
                .setStyle(new Notification.BigTextStyle().bigText(big))
                .setProgress(Math.max(s.total, 1), s.unlocked, false);
        Bitmap icon = icons.get(s.image);
        if (icon != null) {
            b.setLargeIcon(icon);
        } else if (!s.image.isEmpty()) {
            fetch(s.image, this::render);
        }
        return b;
    }

    private Notification.Builder text(Notification.Builder b, int title, String text) {
        return b.setContentTitle(service.getString(title)).setContentText(text)
                .setStyle(new Notification.BigTextStyle().bigText(text));
    }

    private Notification unlockNotification(String title, String description, Bitmap badge, int points,
                                            int unlocked, int total, boolean again) {
        String progress = total > 0 ? service.getString(R.string.unlock_progress, unlocked, total) : "";
        String big = description.isEmpty() ? progress : progress.isEmpty() ? description
                : description + "\n" + progress;
        Notification.Builder b = new Notification.Builder(service, CHANNEL_UNLOCKS)
                .setSmallIcon(R.drawable.ic_notify)
                .setColor(GOLD)
                .setContentTitle(title)
                .setContentText(description.isEmpty() ? progress : description)
                .setSubText(service.getString(R.string.unlock_sub, points))
                .setStyle(new Notification.BigTextStyle().bigText(big))
                .setCategory(Notification.CATEGORY_EVENT)
                .setShowWhen(true)
                .setAutoCancel(true)
                .setOnlyAlertOnce(again)
                .setGroup(GROUP_UNLOCKS)
                .setGroupAlertBehavior(Notification.GROUP_ALERT_CHILDREN)
                .setContentIntent(open);
        if (badge != null) {
            b.setLargeIcon(badge);
        }
        return b.build();
    }

    /* ---- Playtime --------------------------------------------------------- */

    /** Adds the running session's new time to its game's total. */
    private void savePlaytime() {
        if (sessionSerial.isEmpty() || sessionEnd != 0) {
            return;
        }
        long now = System.currentTimeMillis();
        long add = (now - countedUntil) / 1000;
        if (add <= 0) {
            return;
        }
        String key = "s_" + sessionSerial;
        playtime.edit().putLong(key, playtime.getLong(key, 0) + add).apply();
        countedUntil += add * 1000;
    }

    private long totalPlaytime(String serial) {
        long saved = playtime.getLong("s_" + serial, 0) * 1000;
        if (serial.equals(sessionSerial) && sessionEnd == 0) {
            saved += System.currentTimeMillis() - countedUntil;
        }
        return saved;
    }

    private String duration(long ms) {
        long minutes = Math.max(0, ms / 60000);
        if (minutes < 60) {
            return service.getString(R.string.duration_minutes, minutes);
        }
        return service.getString(R.string.duration_hours, minutes / 60, minutes % 60);
    }

    /* ---- Pictures --------------------------------------------------------- */

    /** Downloads an icon once; then runs {@code done} on the main thread. */
    private void fetch(String url, Runnable done) {
        if (stopped || loading.contains(url) || icons.containsKey(url)) {
            return;
        }
        loading.add(url);
        downloads.execute(() -> {
            Bitmap bm = download(url);
            main.post(() -> {
                loading.remove(url);
                if (bm != null) {
                    icons.put(url, bm);
                    done.run();
                }
            });
        });
    }

    private static Bitmap download(String url) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setConnectTimeout(10000);
            c.setReadTimeout(15000);
            c.setRequestProperty("User-Agent", "xerabora-android/" + BuildConfig.VERSION_NAME);
            if (c.getResponseCode() != 200) {
                return null;
            }
            try (InputStream in = c.getInputStream()) {
                return BitmapFactory.decodeStream(in);
            }
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "icon " + url + ": " + e);
            return null;
        } finally {
            if (c != null) {
                c.disconnect();
            }
        }
    }

    static Intent quitIntent(Context context) {
        return new Intent(context, EngineService.class).setAction(EngineService.ACTION_STOP);
    }
}
