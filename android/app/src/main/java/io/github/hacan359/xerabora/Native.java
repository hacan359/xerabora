package io.github.hacan359.xerabora;

import java.nio.charset.StandardCharsets;

/** The upstream client, compiled into libxerabora.so. */
final class Native {
    static {
        System.loadLibrary("xerabora");
    }

    private Native() {
    }

    /**
     * Runs the client's main() until it exits (QUIT on the page, or Quit in
     * the notification). Blocks; call it from a thread of its own.
     *
     * @param home becomes $HOME: the login, the key and the log live under it
     */
    static native int run(String home, String tmp, String[] args);

    /* ---- Called from native code, on the client's thread ------------------ */

    /** The page is served. */
    static void onUiReady(int port) {
        EngineService.uiReady(port);
    }

    /** status.c: the console, the game and its progress changed. */
    static void onStatus(int flags, byte[] serial, byte[] status, int gameId, byte[] title, byte[] image,
                         byte[] presence, int unlocked, int total, int pointsUnlocked, int pointsTotal) {
        StatusNotifier.Status s = new StatusNotifier.Status();
        s.connected = (flags & 1) != 0;
        s.portBusy = (flags & 2) != 0;
        s.loggedIn = (flags & 4) != 0;
        s.serial = text(serial);
        s.status = text(status);
        s.gameId = gameId;
        s.title = text(title);
        s.image = text(image);
        s.presence = text(presence);
        s.unlocked = unlocked;
        s.total = total;
        s.pointsUnlocked = pointsUnlocked;
        s.pointsTotal = pointsTotal;
        EngineService.status(s);
    }

    /** status.c: an achievement was unlocked. */
    static void onUnlock(int id, byte[] title, byte[] description, byte[] image, int points,
                         int unlocked, int total) {
        EngineService.unlock(id, text(title), text(description), text(image), points, unlocked, total);
    }

    /** status.c: the console sent a discovery (RAP1). */
    static void onDiscovery(byte[] consoleIp) {
        EngineService.discovery(text(consoleIp));
    }

    /** status.c: the answer the console got to "RA: check game support". */
    static void onCheck(int ok, byte[] title, int total, int unlocked, int unsupported, byte[] reason) {
        EngineService.check(ok != 0, text(title), total, unlocked, unsupported, text(reason));
    }

    private static String text(byte[] utf8) {
        return utf8 != null ? new String(utf8, StandardCharsets.UTF_8) : "";
    }
}
