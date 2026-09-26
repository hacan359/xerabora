package io.github.hacan359.xerabora;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.PowerManager;
import android.provider.Settings;
import android.text.util.Linkify;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.window.OnBackInvokedDispatcher;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

/**
 * The client's own page, in a WebView. The page is served by the client on
 * 127.0.0.1, so it counts as "this machine": login, key and QUIT work.
 */
public final class MainActivity extends Activity {
    static final String PREFS = "app";
    /* --bg of the page, so nothing flashes white before it loads. */
    private static final int BACKGROUND = 0xff04101c;
    private static final int ASK_NOTIFICATIONS = 1;

    private WebView web;
    private volatile int port = EngineService.UI_PORT;
    private boolean pageLoaded;
    private volatile boolean probing;

    private final BroadcastReceiver events = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (EngineService.EVENT_STOPPED.equals(intent.getAction())) {
                finishAndRemoveTask();
                return;
            }
            int announced = intent.getIntExtra(EngineService.EXTRA_PORT, port);
            if (announced != port || !pageLoaded) {
                port = announced;
                loadPage();
            }
        }
    };

    @Override
    @SuppressWarnings("deprecation") // the pre-Android 11 insets
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        // A companion screen: it stays on while it is in front.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(BACKGROUND);
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets i = insets.getInsets(WindowInsets.Type.systemBars()
                        | WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
                v.setPadding(i.left, i.top, i.right, i.bottom);
            } else {
                v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            }
            return insets;
        });

        web = new WebView(this);
        web.setBackgroundColor(BACKGROUND);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        web.setWebViewClient(new PageClient());
        root.addView(web, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        setContentView(root);
        showWaiting();

        IntentFilter filter = new IntentFilter();
        filter.addAction(EngineService.EVENT_READY);
        filter.addAction(EngineService.EVENT_STOPPED);
        registerReceiver(events, filter, Context.RECEIVER_NOT_EXPORTED);

        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::goBack);
        }

        startForegroundService(new Intent(this, EngineService.class));
        waitForPage();
        askNotifications();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        // From the notification: make sure the client is up.
        startForegroundService(new Intent(this, EngineService.class));
    }

    @Override
    protected void onResume() {
        super.onResume();
        UpdateChecker.maybeCheck(this);
    }

    @Override
    protected void onDestroy() {
        probing = false;
        unregisterReceiver(events);
        web.destroy();
        super.onDestroy();
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        goBack();
    }

    private void goBack() {
        if (web.canGoBack()) {
            web.goBack();
            return;
        }
        CharSequence[] items = {
                getString(R.string.menu_minimize),
                getString(R.string.menu_quit),
                getString(R.string.share_log),
                getString(R.string.menu_about),
        };
        new AlertDialog.Builder(this)
                .setTitle(R.string.app_name)
                .setItems(items, (d, which) -> {
                    switch (which) {
                        case 0:
                            moveTaskToBack(true);
                            break;
                        case 1:
                            quitAll();
                            break;
                        case 2:
                            shareLog();
                            break;
                        default:
                            showAbout();
                            break;
                    }
                })
                .show();
    }

    /* Who made what, and the licenses the APK carries (assets/licenses,
       collected from the repository by the build). */
    private void showAbout() {
        StringBuilder text = new StringBuilder(getString(R.string.about_body, BuildConfig.VERSION_NAME));
        String[][] licenses = {
                {"xeRAbora", "xerabora.txt"},
                {"rcheevos", "rcheevos.txt"},
        };
        for (String[] license : licenses) {
            text.append("\n\n— ").append(license[0]).append(" —\n\n").append(asset("licenses/" + license[1]));
        }

        TextView view = new TextView(this);
        int pad = (int) (20 * getResources().getDisplayMetrics().density);
        view.setPadding(pad, pad / 2, pad, pad / 2);
        view.setText(text);
        view.setTextSize(13);
        Linkify.addLinks(view, Linkify.WEB_URLS);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(view);
        new AlertDialog.Builder(this)
                .setTitle(R.string.menu_about)
                .setView(scroll)
                .setPositiveButton(android.R.string.ok, null)
                .show();
    }

    private String asset(String path) {
        try (InputStream in = getAssets().open(path)) {
            return new String(HttpBridge.readAll(in), StandardCharsets.UTF_8).trim();
        } catch (IOException e) {
            return e.toString();
        }
    }

    /* The end of the client's log, as text to any app that takes it: what
       to send when something went wrong and no PC is at hand. The page's
       own polling is left out. */
    private void shareLog() {
        File log = new File(getFilesDir(), ".config/xerabora/xerabora.log");
        StringBuilder text = new StringBuilder();
        try (RandomAccessFile f = new RandomAccessFile(log, "r")) {
            long from = Math.max(0, f.length() - 400 * 1024);
            byte[] buf = new byte[(int) (f.length() - from)];
            f.seek(from);
            f.readFully(buf);
            String[] lines = new String(buf, StandardCharsets.UTF_8).split("\n");
            for (int i = from > 0 ? 1 : 0; i < lines.length; i++) {
                if (!lines[i].contains(" http GET /state") && !lines[i].contains(" http GET /events")) {
                    text.append(lines[i]).append('\n');
                }
            }
        } catch (IOException e) {
            text.append(e);
        }
        // An intent carries at most a few hundred KB: keep the newest part.
        int keep = 90 * 1024;
        String body = text.length() > keep ? text.substring(text.length() - keep) : text.toString();
        Intent send = new Intent(Intent.ACTION_SEND)
                .setType("text/plain")
                .putExtra(Intent.EXTRA_SUBJECT, "xerabora.log " + BuildConfig.VERSION_NAME)
                .putExtra(Intent.EXTRA_TEXT, body);
        startActivity(Intent.createChooser(send, getString(R.string.share_log)));
    }

    /** Everything off: the client, its notification and this screen. */
    private void quitAll() {
        startService(StatusNotifier.quitIntent(this));
        finishAndRemoveTask();
    }

    /* ---- The page ------------------------------------------------------- */

    private String pageUrl() {
        return "http://127.0.0.1:" + port + "/";
    }

    private void loadPage() {
        probing = false;
        pageLoaded = false;
        web.loadUrl(pageUrl());
    }

    private void showWaiting() {
        String html = "<html><head><meta name=viewport content='width=device-width'></head>"
                + "<body style='margin:0;height:100vh;display:flex;align-items:center;justify-content:center;"
                + "background:#04101c;color:#8fa3bf;font:16px sans-serif'>"
                + getString(R.string.waiting) + "</body></html>";
        web.loadDataWithBaseURL(null, html, "text/html", "utf-8", null);
    }

    /* The engine announces the port, but a start can race the receiver:
       knock on the port too, and load the page once it answers. */
    private void waitForPage() {
        if (probing) {
            return;
        }
        probing = true;
        new Thread(() -> {
            while (probing) {
                try (Socket s = new Socket()) {
                    s.connect(new InetSocketAddress("127.0.0.1", port), 300);
                    runOnUiThread(() -> {
                        if (probing) {
                            loadPage();
                        }
                    });
                    return;
                } catch (IOException e) {
                    try {
                        Thread.sleep(500);
                    } catch (InterruptedException ie) {
                        return;
                    }
                }
            }
        }, "xerabora-probe").start();
    }

    private final class PageClient extends WebViewClient {
        private boolean failed;

        @Override
        public void onPageStarted(WebView view, String url, android.graphics.Bitmap favicon) {
            failed = false;
        }

        @Override
        public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
            if (request.isForMainFrame()) {
                failed = true;
                // After this load has finished failing, not inside it.
                web.post(() -> {
                    showWaiting();
                    waitForPage();
                });
            }
        }

        @Override
        public void onPageFinished(WebView view, String url) {
            if (!failed && pageUrl().equals(url)) {
                pageLoaded = true;
                seedLanguage();
            }
        }

        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            Uri u = request.getUrl();
            if ("127.0.0.1".equals(u.getHost()) || "localhost".equals(u.getHost())) {
                return false;
            }
            // retroachievements.org and the like: the phone's browser.
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, u));
            } catch (ActivityNotFoundException ignored) {
                // nothing can open it
            }
            return true;
        }
    }

    /* The page starts in English and keeps the language picked in its
       header menu (localStorage "lang"). Once, before anything was
       picked, start it in the phone's language instead; the menu rules
       from then on. */
    private void seedLanguage() {
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        if (prefs.getBoolean("lang_seeded", false)) {
            return;
        }
        prefs.edit().putBoolean("lang_seeded", true).apply();
        String code = pageLanguage();
        if (code.isEmpty()) {
            return;
        }
        web.evaluateJavascript("(function(){try{if(localStorage.getItem('lang')===null){"
                + "localStorage.setItem('lang','" + code + "');return 1}}catch(e){}return 0})()", result -> {
                    if ("1".equals(result)) {
                        web.reload();
                    }
                });
    }

    /** The phone's language as one of the page's codes; "" is English. */
    private String pageLanguage() {
        String language = getResources().getConfiguration().getLocales().get(0).getLanguage();
        switch (language) {
            case "pt":
                return "pt-BR";
            case "es":
                return "es";
            default:
                return "";
        }
    }

    /* ---- Permissions ---------------------------------------------------- */

    private void askNotifications() {
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, ASK_NOTIFICATIONS);
        } else {
            askBattery();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == ASK_NOTIFICATIONS) {
            askBattery();
        }
    }

    /* Once: battery optimisation is what kills a client in the middle of a
       game on the more aggressive phones. */
    private void askBattery() {
        PowerManager pm = getSystemService(PowerManager.class);
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        if (pm.isIgnoringBatteryOptimizations(getPackageName()) || prefs.getBoolean("asked_battery", false)) {
            return;
        }
        prefs.edit().putBoolean("asked_battery", true).apply();
        new AlertDialog.Builder(this)
                .setTitle(R.string.battery_title)
                .setMessage(R.string.battery_text)
                .setPositiveButton(R.string.battery_ok, (d, w) -> {
                    try {
                        startActivity(new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                                Uri.parse("package:" + getPackageName())));
                    } catch (ActivityNotFoundException ignored) {
                        // no such screen on this phone
                    }
                })
                .setNegativeButton(R.string.later, null)
                .show();
    }
}
