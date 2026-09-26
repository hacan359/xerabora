package io.github.hacan359.xerabora;

import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.Process;
import android.util.Log;

import java.io.IOException;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;

/**
 * Runs the client in the foreground, in the ":engine" process, and holds
 * what keeps it hearing the console with the screen off: a wake lock, the
 * Wi-Fi locks and the multicast lock (the console finds the client by
 * broadcast).
 */
public final class EngineService extends Service {
    static final String ACTION_STOP = BuildConfig.APPLICATION_ID + ".STOP";
    static final String ACTION_REPOST = BuildConfig.APPLICATION_ID + ".REPOST";
    static final String EVENT_READY = BuildConfig.APPLICATION_ID + ".READY";
    static final String EVENT_STOPPED = BuildConfig.APPLICATION_ID + ".STOPPED";
    static final String EXTRA_PORT = "port";
    static final int UI_PORT = 18280;

    private static final String TAG = "xerabora";
    /* The desktop main thread has 8 MB of stack; so does this one. */
    private static final long ENGINE_STACK = 8L << 20;

    private static volatile EngineService instance;
    private static volatile int uiPort;

    private final Handler main = new Handler(Looper.getMainLooper());
    private final List<WifiManager.WifiLock> wifiLocks = new ArrayList<>();
    private PowerManager.WakeLock wakeLock;
    private WifiManager.MulticastLock multicastLock;
    private StatusNotifier notifier;
    private Thread engine;
    private boolean exiting;

    @Override
    public void onCreate() {
        super.onCreate();
        instance = this;
        startInForeground();
        acquireLocks();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null ? intent.getAction() : null;
        if (ACTION_STOP.equals(action)) {
            requestStop();
        } else if (ACTION_REPOST.equals(action)) {
            // The status notification was swiped away: it comes straight back.
            // Without a running client there is nothing to show.
            if (engine == null) {
                exit(0);
            } else {
                notifier.repost();
            }
        } else if (engine == null) {
            startEngine();
        } else if (uiPort != 0) {
            announce(uiPort);
        }
        // A client the system killed is not brought back behind the
        // player's back: opening the app starts it again.
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        instance = null;
        releaseLocks();
        super.onDestroy();
    }

    /** From native code, on the client's thread: the page is served. */
    static void uiReady(int port) {
        uiPort = port;
        EngineService s = instance;
        if (s != null) {
            s.announce(port);
        }
    }

    /* From native code, on the client's thread: hand over to the main one. */

    static void status(StatusNotifier.Status s) {
        EngineService svc = instance;
        if (svc != null) {
            svc.main.post(() -> svc.notifier.onStatus(s));
        }
    }

    static void unlock(int id, String title, String description, String image, int points,
                       int unlocked, int total) {
        EngineService svc = instance;
        if (svc != null) {
            svc.main.post(() -> svc.notifier.onUnlock(id, title, description, image, points, unlocked, total));
        }
    }

    static void discovery(String consoleIp) {
        EngineService svc = instance;
        if (svc != null) {
            svc.main.post(() -> svc.notifier.onDiscovery(consoleIp));
        }
    }

    static void check(boolean ok, String title, int total, int unlocked, int unsupported, String reason) {
        EngineService svc = instance;
        if (svc != null) {
            svc.main.post(() -> svc.notifier.onCheck(ok, title, total, unlocked, unsupported, reason));
        }
    }

    private void announce(int port) {
        sendBroadcast(new Intent(EVENT_READY).setPackage(getPackageName()).putExtra(EXTRA_PORT, port));
    }

    private void startEngine() {
        final String home = getFilesDir().getAbsolutePath();
        final String tmp = getCacheDir().getAbsolutePath();
        final String[] args = {"--ui-port", String.valueOf(UI_PORT)};

        engine = new Thread(null, () -> {
            int rc;
            try {
                rc = Native.run(home, tmp, args);
            } catch (Throwable t) {
                Log.e(TAG, "the client did not start", t);
                rc = -1;
            }
            final int code = rc;
            main.post(() -> exit(code));
        }, "xerabora", ENGINE_STACK);
        engine.start();
    }

    /** Quit in the notification or the app: QUIT, the way the page does it. */
    private void requestStop() {
        if (engine == null || exiting) {
            exit(0);
            return;
        }
        final int port = uiPort != 0 ? uiPort : UI_PORT;
        new Thread(() -> postQuit(port), "xerabora-stop").start();
        main.postDelayed(() -> exit(-1), 5000);
    }

    private static void postQuit(int port) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL("http://127.0.0.1:" + port + "/quit").openConnection();
            c.setConnectTimeout(2000);
            c.setReadTimeout(3000);
            c.setDoOutput(true);
            c.setRequestMethod("POST");
            c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
            c.setFixedLengthStreamingMode(0);
            try (OutputStream out = c.getOutputStream()) {
                out.flush();
            }
            c.getResponseCode();
        } catch (IOException e) {
            Log.w(TAG, "quit: " + e);
        } finally {
            if (c != null) {
                c.disconnect();
            }
        }
    }

    private void exit(int rc) {
        if (exiting) {
            return;
        }
        exiting = true;
        Log.i(TAG, "the client stopped (" + rc + ")");
        sendBroadcast(new Intent(EVENT_STOPPED).setPackage(getPackageName()));
        notifier.stop();
        releaseLocks();
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
        // The client keeps static state that a second main() would inherit:
        // the next start gets a new process.
        main.postDelayed(() -> Process.killProcess(Process.myPid()), 300);
    }

    private void startInForeground() {
        PendingIntent open = PendingIntent.getActivity(this, 0,
                new Intent(this, MainActivity.class), PendingIntent.FLAG_IMMUTABLE);
        PendingIntent quit = PendingIntent.getService(this, 1,
                StatusNotifier.quitIntent(this), PendingIntent.FLAG_IMMUTABLE);
        // Android 14 lets the user swipe away even a foreground service's
        // notification; this puts it back.
        PendingIntent repost = PendingIntent.getService(this, 2,
                new Intent(this, EngineService.class).setAction(ACTION_REPOST), PendingIntent.FLAG_IMMUTABLE);
        notifier = new StatusNotifier(this, open, quit, repost);

        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(StatusNotifier.ONGOING_ID, notifier.initial(),
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
        } else {
            startForeground(StatusNotifier.ONGOING_ID, notifier.initial());
        }
    }

    @SuppressWarnings("deprecation")
    private void acquireLocks() {
        PowerManager pm = getSystemService(PowerManager.class);
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "xerabora:engine");
        wakeLock.setReferenceCounted(false);
        wakeLock.acquire();

        WifiManager wm = getApplicationContext().getSystemService(WifiManager.class);
        if (wm == null) {
            return;
        }
        // HIGH_PERF keeps the radio out of power save with the screen off
        // (Android 14 and later treat it as LOW_LATENCY); LOW_LATENCY adds
        // the lowest latency while the app is in front.
        addWifiLock(wm, WifiManager.WIFI_MODE_FULL_HIGH_PERF);
        if (Build.VERSION.SDK_INT >= 29) {
            addWifiLock(wm, WifiManager.WIFI_MODE_FULL_LOW_LATENCY);
        }
        multicastLock = wm.createMulticastLock("xerabora");
        multicastLock.setReferenceCounted(false);
        multicastLock.acquire();
    }

    private void addWifiLock(WifiManager wm, int mode) {
        WifiManager.WifiLock lock = wm.createWifiLock(mode, "xerabora:" + mode);
        lock.setReferenceCounted(false);
        lock.acquire();
        wifiLocks.add(lock);
    }

    private void releaseLocks() {
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        for (WifiManager.WifiLock lock : wifiLocks) {
            if (lock.isHeld()) {
                lock.release();
            }
        }
        wifiLocks.clear();
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
    }
}
