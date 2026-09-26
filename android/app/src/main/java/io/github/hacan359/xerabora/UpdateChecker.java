package io.github.hacan359.xerabora;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInstaller;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Looks for a newer APK among the releases of the repository the app was
 * built from, pre-releases included (every xeRAbora release so far is one),
 * and installs it over this one. The newest release that carries an APK
 * decides: it is newer when its tag names a later version than the one
 * this build carries (v0.1.0-alpha.12 after 0.1.0-alpha.11), or, for a tag
 * ending in -r<build>, a larger build number than this versionCode.
 */
final class UpdateChecker {
    private static final String TAG = "xerabora";
    private static final long INTERVAL_MS = 6L * 3600 * 1000;
    private static final String KEY_CHECKED = "update_checked";
    private static final Pattern BUILD = Pattern.compile("-r(\\d+)$");
    private static final Pattern NUMBER = Pattern.compile("\\d+");

    private static boolean running;

    private UpdateChecker() {
    }

    private static final class Release {
        String name;
        String apkUrl;
        boolean newer;
    }

    /** At most every six hours, and never on a debug build. */
    static void maybeCheck(Activity activity) {
        if (BuildConfig.DEBUG || running) {
            return;
        }
        SharedPreferences prefs = activity.getSharedPreferences(MainActivity.PREFS, Context.MODE_PRIVATE);
        long now = System.currentTimeMillis();
        if (now - prefs.getLong(KEY_CHECKED, 0) < INTERVAL_MS) {
            return;
        }
        running = true;
        new Thread(() -> {
            Release r = newest();
            if (r != null) {
                prefs.edit().putLong(KEY_CHECKED, now).apply();
            }
            activity.runOnUiThread(() -> {
                running = false;
                if (r != null && r.newer && !activity.isFinishing() && !activity.isDestroyed()) {
                    offer(activity, r);
                }
            });
        }, "xerabora-update").start();
    }

    /* /releases/latest skips pre-releases, so the list it is. */
    private static Release newest() {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL("https://api.github.com/repos/" + BuildConfig.UPDATE_REPO
                    + "/releases?per_page=10").openConnection();
            c.setConnectTimeout(10000);
            c.setReadTimeout(15000);
            c.setRequestProperty("Accept", "application/vnd.github+json");
            c.setRequestProperty("User-Agent", "xerabora-android/" + BuildConfig.VERSION_NAME);
            if (c.getResponseCode() != 200) {
                return null;
            }
            JSONArray releases = new JSONArray(new String(HttpBridge.readAll(c.getInputStream()),
                    StandardCharsets.UTF_8));
            for (int i = 0; i < releases.length(); i++) {
                JSONObject json = releases.getJSONObject(i);
                String apk = apkUrl(json);
                if (json.optBoolean("draft") || apk == null) {
                    continue;
                }
                String tag = json.optString("tag_name");
                Release r = new Release();
                r.name = json.optString("name", tag);
                r.apkUrl = apk;
                r.newer = isNewer(tag);
                return r;
            }
            return null;
        } catch (IOException | JSONException | RuntimeException e) {
            Log.w(TAG, "update check: " + e);
            return null;
        } finally {
            if (c != null) {
                c.disconnect();
            }
        }
    }

    private static String apkUrl(JSONObject release) throws JSONException {
        JSONArray assets = release.optJSONArray("assets");
        for (int i = 0; assets != null && i < assets.length(); i++) {
            JSONObject a = assets.getJSONObject(i);
            if (a.optString("name").endsWith(".apk")) {
                return a.optString("browser_download_url");
            }
        }
        return null;
    }

    static boolean isNewer(String tag) {
        Matcher m = BUILD.matcher(tag);
        if (m.find()) {
            return Long.parseLong(m.group(1)) > BuildConfig.VERSION_CODE;
        }
        String version = tag.startsWith("v") ? tag.substring(1) : tag;
        return compareVersions(version, BuildConfig.UPSTREAM_VERSION) > 0;
    }

    /**
     * Semantic-version precedence: 0.1.0-alpha.2 before 0.1.0-alpha.12
     * before 0.1.0. Build metadata after '+' does not count.
     */
    static int compareVersions(String a, String b) {
        String[] x = a.split("\\+", 2)[0].split("-", 2);
        String[] y = b.split("\\+", 2)[0].split("-", 2);
        int c = compareIds(x[0].split("\\."), y[0].split("\\."), true);
        if (c != 0) {
            return c;
        }
        if (x.length == 1 || y.length == 1) {
            // A release outranks the pre-releases of the same version.
            return Integer.compare(y.length, x.length);
        }
        return compareIds(x[1].split("\\."), y[1].split("\\."), false);
    }

    private static int compareIds(String[] a, String[] b, boolean core) {
        for (int i = 0; i < Math.max(a.length, b.length); i++) {
            String p = i < a.length ? a[i] : null;
            String q = i < b.length ? b[i] : null;
            if (p == null || q == null) {
                if (!core) {
                    // alpha before alpha.1: fewer identifiers come first.
                    return p == null ? -1 : 1;
                }
                p = p == null ? "0" : p;
                q = q == null ? "0" : q;
            }
            boolean pn = NUMBER.matcher(p).matches();
            boolean qn = NUMBER.matcher(q).matches();
            int c;
            if (pn && qn) {
                c = Long.compare(Long.parseLong(p), Long.parseLong(q));
            } else if (pn != qn) {
                c = pn ? -1 : 1; // numbers before words
            } else {
                c = p.compareTo(q);
            }
            if (c != 0) {
                return c;
            }
        }
        return 0;
    }

    private static void offer(Activity activity, Release r) {
        new AlertDialog.Builder(activity)
                .setTitle(R.string.update_title)
                .setMessage(activity.getString(R.string.update_text, r.name))
                .setPositiveButton(R.string.update_install, (d, w) -> install(activity, r))
                .setNegativeButton(R.string.later, null)
                .show();
    }

    private static void install(Activity activity, Release r) {
        if (!activity.getPackageManager().canRequestPackageInstalls()) {
            // Allowed once, in the system settings; the offer comes back on return.
            activity.getSharedPreferences(MainActivity.PREFS, Context.MODE_PRIVATE)
                    .edit().remove(KEY_CHECKED).apply();
            Toast.makeText(activity, R.string.update_allow, Toast.LENGTH_LONG).show();
            activity.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:" + activity.getPackageName())));
            return;
        }

        AlertDialog progress = new AlertDialog.Builder(activity)
                .setMessage(R.string.update_downloading)
                .setCancelable(false)
                .show();
        Context app = activity.getApplicationContext();
        new Thread(() -> {
            String error = downloadAndCommit(app, r);
            activity.runOnUiThread(() -> {
                progress.dismiss();
                if (error != null) {
                    Toast.makeText(app, app.getString(R.string.update_failed, error), Toast.LENGTH_LONG).show();
                }
            });
        }, "xerabora-download").start();
    }

    /** Streams the APK into an install session; null when committed. */
    private static String downloadAndCommit(Context app, Release r) {
        PackageInstaller installer = app.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params =
                new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(app.getPackageName());
        int id = -1;
        HttpURLConnection c = null;

        try {
            id = installer.createSession(params);
            try (PackageInstaller.Session session = installer.openSession(id)) {
                c = (HttpURLConnection) new URL(r.apkUrl).openConnection();
                c.setConnectTimeout(15000);
                c.setReadTimeout(30000);
                c.setRequestProperty("User-Agent", "xerabora-android/" + BuildConfig.VERSION_NAME);
                if (c.getResponseCode() != 200) {
                    installer.abandonSession(id);
                    return "HTTP " + c.getResponseCode();
                }
                long length = c.getContentLengthLong();
                try (InputStream in = c.getInputStream();
                     OutputStream out = session.openWrite("base.apk", 0, length > 0 ? length : -1)) {
                    byte[] buf = new byte[65536];
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        out.write(buf, 0, n);
                    }
                    session.fsync(out);
                }
                int flags = PendingIntent.FLAG_UPDATE_CURRENT
                        | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
                PendingIntent done = PendingIntent.getBroadcast(app, id,
                        new Intent(app, InstallReceiver.class), flags);
                session.commit(done.getIntentSender());
            }
            return null;
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "update install: " + e);
            if (id != -1) {
                try {
                    installer.abandonSession(id);
                } catch (RuntimeException ignored) {
                    // already gone
                }
            }
            return e.getMessage() != null ? e.getMessage() : e.toString();
        } finally {
            if (c != null) {
                c.disconnect();
            }
        }
    }
}
