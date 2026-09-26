# xeRAbora for Android

The xeRAbora client running entirely on an Android phone, no PC: OPL-RA finds the phone on the Wi-Fi
exactly as it finds a PC, and the phone talks to RetroAchievements, unlocks the achievements and shows
the same page as the PC client.

## Install and use

1. Download `xerabora-android.apk` from the [releases](https://github.com/hacan359/xerabora/releases) and install it (Android 8.0 or
   later). The first time, Android asks you to allow installs from the browser.
2. Open the app. It asks for notifications and for running without battery optimization. Allow both:
   that is what keeps the client alive with the screen off in the middle of a game.
3. On the page's **SETTINGS** tab, sign in to RetroAchievements and paste the Web API key, as on the PC.
   The page opens in the phone's language (English, Portuguese or Spanish); the menu in its header
   changes it.
4. On the PS2, with the phone on the **same Wi-Fi** as the console: **RA: test PC connection** has to show
   the phone's address. Then **RA: check game support**, then the game.

The status notification shows what the console is doing: idle, found, the running game with its icon,
achievements, points, session time and rich presence. Every unlock gets a notification of its own, and
the link test, "check game support" and a game starting show up as pop-ups. **Quit** in the
notification, or **QUIT** on the page, ends the client. The back button opens a menu: **Minimize** keeps
it running, **Quit** turns it off, **About and licenses** shows the credits, and **Send log** hands the
end of `xerabora.log` to any app (for a bug report without a PC).

With the screen on during *test PC connection* and while the game starts, it works on any phone. The
console finds the client by broadcast, and the app holds a *multicast lock* to receive it with the
screen off, but some phones filter broadcasts all the same.

**Updates.** When it opens (at most every 6 h) the app looks at this repository's releases, pre-releases
included, and offers **Install** when one carries an APK for a later version. The first time, Android
asks to allow installs from xeRAbora. The login is kept.

## How it works

The app builds the client as it is: nothing under `client/` knows about Android. The differences are
backends, as `http_winhttp.c` is for Windows, and link-time wraps:

| File | What it does |
|---|---|
| `app/src/main/cpp/CMakeLists.txt` | Reads the `SRC` and `RC_SRC` lists from `client/Makefile`, so a new client file is built here too. Compiles `main.c` as `xerabora_main`. |
| `http_android.c` | `http.h` through `HttpURLConnection` (the system's TLS, instead of libcurl). |
| `sound_android.c` | `sound.h` through `SoundPool` (instead of paplay/aplay). WAVs in `sounds/` replace the defaults, as on the PC. |
| `hooks.c` | `-Wl,--wrap`: the client does not quit 15 s after its page closes (the service decides), and instead of opening a browser it tells the app the port. |
| `status.c` | `-Wl,--wrap` on the calls the client already makes to fill its page (console, game, status, unlock, push) and on `console_serve`: this feeds the notifications. It also reads the `RAA1` answers to "RA: check game support". |
| `net_guard.c` | A send to the page waits at most 250 ms. Android freezes the app's UI process in the background, and `webui.c`'s blocking `send()` then stalled the whole client (no telemetry, no discovery, no unlocks). |
| `jni_bridge.c` | `HOME` in the app's directory, stdout to logcat, then `main`. |
| `StatusNotifier.java` | The status notification, one per unlock, and the pop-ups. |
| `EngineService.java` | Foreground service in its own `:engine` process (the client keeps static state, so every start is a new process), with the wake, Wi-Fi and multicast locks. |
| `MainActivity.java` | The client's page in a WebView on `127.0.0.1:18280`, which counts as "this machine", so login and QUIT work. |
| `UpdateChecker.java` | The in-app updater. |
| `tools/fake_ps2.py` | Plays the console's side of discovery, to test a phone without the PS2. |
| `tools/make_icons.py` | Rebuilds the launcher icon from `docs/icon.png`. |

**What the app relies on in the client,** so a change there that breaks the Android build is easy to
place:

- the `NAME := ...` assignments in `client/Makefile`;
- the `http.h` and `sound.h` APIs;
- the wrapped functions (`webui_page_gone`, `webui_open_browser`, `webui_set_console`, `webui_set_game`,
  `webui_set_game_title`, `webui_set_status`, `webui_note_unlock`, `webui_push`, `console_serve`), which
  must keep being called from other files than the one that defines them. They are declared with the
  header's own types, so a changed signature fails the build instead of miscalling;
- the `RAA1` reply format from `protocol/PROTOCOL.md`.

## Build

Needs JDK 21, the Android SDK (platform 36), NDK 28.2.13676358 and CMake 3.22.1.

```
git submodule update --init third_party/rcheevos
cd android
./gradlew assembleDebug
```

The APK lands in `android/app/build/outputs/apk/debug/`. The log: `adb logcat -s xerabora`. The full
`xerabora.log` is in `files/.config/xerabora/` in the app's storage. The versionCode is the commit count
and the version name comes from `client/src/version.h`.

## Signing (for releases)

Android installs an update only over an APK signed with the same key, so releases need one key, kept
for good. CI signs with it when four repository secrets exist; without them it still builds, and the
release goes out without an APK. To create the key and the secrets once:

```
keytool -genkeypair -keystore xerabora-release.jks -storetype PKCS12 -alias xerabora \
        -keyalg RSA -keysize 4096 -validity 36500 -dname "CN=xerabora"
base64 -w0 xerabora-release.jks | gh secret set ANDROID_KEYSTORE_B64
gh secret set ANDROID_KEYSTORE_PASSWORD    # the password keytool asked for
gh secret set ANDROID_KEY_ALIAS --body xerabora
gh secret set ANDROID_KEY_PASSWORD         # the same password
```

Keep `xerabora-release.jks` and its password somewhere safe: a release signed with another key cannot
be installed over the previous one, and every user would have to uninstall first, losing the saved
login. A local release build is signed the same way through the `XERABORA_KEYSTORE`,
`XERABORA_KEYSTORE_PASSWORD`, `XERABORA_KEY_ALIAS` and `XERABORA_KEY_PASSWORD` environment variables.

## Credits

Android app by MrRexD ([oMrRexD](https://github.com/oMrRexD)). It is part of xeRAbora and shares its
MIT license (`client/LICENSE`); the app shows it, with rcheevos', under **About and licenses**.
