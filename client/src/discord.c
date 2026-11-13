#include "discord.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "log.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#ifndef DISCORD_APP_ID
#define DISCORD_APP_ID ""
#endif

/* Discord takes five presence updates in twenty seconds. */
#define DISCORD_MIN_GAP 5
#define DISCORD_RETRY   60

#define OP_HANDSHAKE 0
#define OP_FRAME     1
#define OP_CLOSE     2
#define OP_PING      3
#define OP_PONG      4

#ifdef _WIN32
static HANDLE g_pipe = INVALID_HANDLE_VALUE;
#define PIPE_OK (g_pipe != INVALID_HANDLE_VALUE)
#else
static int g_sock = -1;
#define PIPE_OK (g_sock >= 0)
#endif

static int g_enabled = 1;
static int g_ready;       /* Discord answered the handshake */
static time_t g_last_send;
static time_t g_last_try;
static time_t g_since;    /* when the current game started */

static char g_title[160];
static char g_want[1800]; /* the activity to show, "" for none */
static char g_sent[1800];
static int g_sent_any;

/* Bytes read from Discord and not yet parsed, and how much of an
   oversized reply is still to be thrown away. */
static unsigned char g_in[8192];
static unsigned g_in_len;
static unsigned g_in_skip;

int discord_available(void)
{
    return DISCORD_APP_ID[0] != '\0';
}

int discord_enabled(void)
{
    return g_enabled;
}

/* ---- The pipe ----------------------------------------------------------- */

static void discord_close(void)
{
#ifdef _WIN32
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
#else
    if (g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }
#endif
    g_ready = 0;
    g_in_len = 0;
    g_in_skip = 0;
    g_sent_any = 0;
}

#ifndef _WIN32
/* Tries the ten endpoints of one directory. */
static int discord_open_in(const char *dir, const char *sub)
{
    int i;

    for (i = 0; i < 10; i++) {
        struct sockaddr_un addr;

        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        if (snprintf(addr.sun_path, sizeof(addr.sun_path), "%s%s/discord-ipc-%d", dir, sub, i) >=
            (int)sizeof(addr.sun_path))
            return 0;

        g_sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (g_sock < 0)
            return 0;
        if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            return 1;
        close(g_sock);
        g_sock = -1;
    }
    return 0;
}
#endif

/* Opens the first of Discord's ten endpoints, 0 to 9, that answers.
   Also looks in the Flatpak and Snap socket folders. */
static int discord_open(void)
{
#ifdef _WIN32
    int i;

    for (i = 0; i < 10; i++) {
        char name[64];

        snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d", i);
        g_pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (g_pipe != INVALID_HANDLE_VALUE)
            return 1;
    }
    return 0;
#else
    static const char *subs[] = {"", "/app/com.discordapp.Discord", "/snap.discord"};
    const char *dirs[3];
    int d, s;

    dirs[0] = getenv("XDG_RUNTIME_DIR");
    dirs[1] = getenv("TMPDIR");
    dirs[2] = "/tmp";

    for (d = 0; d < 3; d++) {
        if (dirs[d] == NULL || dirs[d][0] == '\0')
            continue;
        for (s = 0; s < 3; s++)
            if (discord_open_in(dirs[d], subs[s]))
                return 1;
    }
    return 0;
#endif
}

/* One frame: opcode, length, payload. Both numbers are little-endian. */
static int discord_send(unsigned opcode, const char *json, unsigned len)
{
    unsigned char head[8];

    if (!PIPE_OK)
        return 0;

    head[0] = (unsigned char)opcode;
    head[1] = (unsigned char)(opcode >> 8);
    head[2] = (unsigned char)(opcode >> 16);
    head[3] = (unsigned char)(opcode >> 24);
    head[4] = (unsigned char)len;
    head[5] = (unsigned char)(len >> 8);
    head[6] = (unsigned char)(len >> 16);
    head[7] = (unsigned char)(len >> 24);

#ifdef _WIN32
    {
        DWORD wrote = 0;

        if (!WriteFile(g_pipe, head, 8, &wrote, NULL) || wrote != 8 ||
            !WriteFile(g_pipe, json, len, &wrote, NULL) || wrote != len) {
            discord_close();
            return 0;
        }
    }
#else
    {
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif

        if (send(g_sock, head, 8, flags) != 8 || send(g_sock, json, len, flags) != (int)len) {
            discord_close();
            return 0;
        }
    }
#endif
    return 1;
}

/* Reads what Discord has sent so far and returns at once. 0 when the
   pipe is gone. */
static int discord_pull(void)
{
    for (;;) {
        unsigned room = (unsigned)sizeof(g_in) - g_in_len;
        unsigned got;

        if (room == 0)
            return 1;
#ifdef _WIN32
        {
            DWORD avail = 0, n = 0;

            if (!PeekNamedPipe(g_pipe, NULL, 0, NULL, &avail, NULL))
                return 0;
            if (avail == 0)
                return 1;
            if (!ReadFile(g_pipe, g_in + g_in_len, avail < room ? avail : room, &n, NULL) || n == 0)
                return 0;
            got = (unsigned)n;
        }
#else
        {
            long n = (long)recv(g_sock, g_in + g_in_len, room, MSG_DONTWAIT);

            if (n == 0)
                return 0;
            if (n < 0)
                return 1; /* nothing waiting */
            got = (unsigned)n;
        }
#endif
        if (g_in_skip > 0) {
            unsigned drop = got < g_in_skip ? got : g_in_skip;

            g_in_skip -= drop;
            memmove(g_in + g_in_len, g_in + g_in_len + drop, got - drop);
            got -= drop;
        }
        g_in_len += got;
    }
}

/* Whether the JSON carries this key with this string value. */
static int json_says(const char *json, const char *key, const char *value)
{
    char pat[64];

    snprintf(pat, sizeof(pat), "\"%s\":\"%s\"", key, value);
    return strstr(json, pat) != NULL;
}

/* Handles every complete reply in the buffer. */
static void discord_read(void)
{
    if (!discord_pull()) {
        log_info("discord: the connection closed");
        discord_close();
        return;
    }

    while (g_in_len >= 8) {
        unsigned opcode = (unsigned)g_in[0] | ((unsigned)g_in[1] << 8);
        unsigned len = (unsigned)g_in[4] | ((unsigned)g_in[5] << 8) | ((unsigned)g_in[6] << 16) |
                       ((unsigned)g_in[7] << 24);
        char *body = (char *)g_in + 8;
        char saved;

        /* A reply that cannot fit is dropped as it arrives. */
        if (len > sizeof(g_in) - 9) {
            g_in_skip = len - (g_in_len - 8);
            g_in_len = 0;
            return;
        }
        if (g_in_len < 8 + len)
            return;

        saved = body[len];
        body[len] = '\0';

        if (opcode == OP_PING) {
            discord_send(OP_PONG, body, len);
        } else if (opcode == OP_CLOSE) {
            log_warn("discord: refused: %.200s", body);
            discord_close();
            return;
        } else if (opcode == OP_FRAME && json_says(body, "evt", "READY")) {
            g_ready = 1;
            log_info("discord: connected");
        } else if (opcode == OP_FRAME && json_says(body, "evt", "ERROR")) {
            log_warn("discord: %.300s", body);
        }

        body[len] = saved;
        memmove(g_in, g_in + 8 + len, g_in_len - 8 - len);
        g_in_len -= 8 + len;
    }
}

/* ---- Presence ----------------------------------------------------------- */

static void discord_connect(void)
{
    char json[96];
    int n;

    g_last_try = time(NULL);
    if (!discord_open())
        return;
    n = snprintf(json, sizeof(json), "{\"v\":1,\"client_id\":\"%s\"}", DISCORD_APP_ID);
    discord_send(OP_HANDSHAKE, json, (unsigned)n);
}

/* Appends "key":"value", escaped, and a comma. Leaves out a value
   shorter than two characters. */
static void add_text(char **p, const char *end, const char *key, const char *value)
{
    if (value == NULL || strlen(value) < 2 || *p + strlen(key) + 8 >= end)
        return;

    *p += snprintf(*p, (size_t)(end - *p), "\"%s\":\"", key);
    for (; *value != '\0' && *p + 8 < end; value++) {
        if (*value == '"' || *value == '\\') {
            *(*p)++ = '\\';
            *(*p)++ = *value;
        } else if ((unsigned char)*value >= 0x20) {
            *(*p)++ = *value;
        }
    }
    *p += snprintf(*p, (size_t)(end - *p), "\",");
}

/* Drops the comma the last field left behind. */
static void end_object(char **p, const char *end, const char *close)
{
    if ((*p)[-1] == ',')
        (*p)--;
    *p += snprintf(*p, (size_t)(end - *p), "%s", close);
}

void discord_set(const struct discord_presence *pr)
{
    char *p = g_want, *end = g_want + sizeof(g_want) - 64;

    if (pr == NULL || pr->title == NULL || pr->title[0] == '\0') {
        g_want[0] = '\0';
        g_title[0] = '\0';
        return;
    }

    if (strcmp(g_title, pr->title) != 0) {
        snprintf(g_title, sizeof(g_title), "%s", pr->title);
        g_since = time(NULL);
    }

    /* status_display_type 2: the member list shows the game, not the
       application's name. */
    p += snprintf(p, (size_t)(end - p), "{\"type\":0,\"status_display_type\":2,");
    add_text(&p, end, "details", pr->title);
    add_text(&p, end, "details_url", pr->game_url);
    add_text(&p, end, "state", pr->state);
    p += snprintf(p, (size_t)(end - p), "\"timestamps\":{\"start\":%ld},\"assets\":{", (long)g_since);
    add_text(&p, end, "large_image", pr->icon_url);
    add_text(&p, end, "large_text", pr->icon_text);
    add_text(&p, end, "large_url", pr->game_url);
    add_text(&p, end, "small_image", pr->badge_url);
    add_text(&p, end, "small_text", pr->badge_text);
    end_object(&p, end, "},");
    if (pr->profile_url != NULL && pr->profile_url[0] != '\0') {
        p += snprintf(p, (size_t)(end - p), "\"buttons\":[{\"label\":\"RetroAchievements profile\",");
        add_text(&p, end, "url", pr->profile_url);
        end_object(&p, end, "}],");
    }
    end_object(&p, end, "}");
}

/* Sends the presence when it differs from the one Discord has. */
static void discord_flush(void)
{
    char json[2000];
    int n, pid;

    if (g_sent_any && strcmp(g_want, g_sent) == 0)
        return;
    if (!g_sent_any && g_want[0] == '\0')
        return;
    if (time(NULL) - g_last_send < DISCORD_MIN_GAP)
        return;

#ifdef _WIN32
    pid = (int)GetCurrentProcessId();
#else
    pid = (int)getpid();
#endif

    /* The nonce has to be there and has to be unique; the clock is
       unique enough at one update in five seconds. No activity clears
       the presence. */
    if (g_want[0] != '\0')
        n = snprintf(json, sizeof(json), "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%ld\",\"args\":{\"pid\":%d,\"activity\":%s}}",
                     (long)time(NULL), pid, g_want);
    else
        n = snprintf(json, sizeof(json), "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%ld\",\"args\":{\"pid\":%d}}",
                     (long)time(NULL), pid);

    if (discord_send(OP_FRAME, json, (unsigned)n)) {
        g_last_send = time(NULL);
        snprintf(g_sent, sizeof(g_sent), "%s", g_want);
        g_sent_any = 1;
    }
}

void discord_set_enabled(int on)
{
    g_enabled = on ? 1 : 0;
    if (!g_enabled)
        discord_close(); /* Discord drops the presence with the pipe */
    else
        g_last_try = 0;
}

void discord_shutdown(void)
{
    discord_close();
}

void discord_tick(void)
{
    if (!discord_available() || !g_enabled)
        return;

    if (!PIPE_OK) {
        /* Discord may have been started after us, or restarted. */
        if (time(NULL) - g_last_try >= DISCORD_RETRY)
            discord_connect();
        return;
    }

    discord_read();
    if (PIPE_OK && g_ready)
        discord_flush();
}
