#include "console.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "log.h"
#include "protocol.h"
#include "ra.h"
#include "version.h"
#include "watchlist.h"

/* ---- Serial to hash --------------------------------------------------
   The console computes the image hash when the player checks a game in
   the menu and sends it with the serial. Telemetry carries only the
   serial, so the pair is remembered here and saved to the config
   directory, one "SERIAL HASH" pair per line. */

struct game_row
{
    char serial[16];
    char hash[33];
};

static struct game_row g_games[64];
static int g_games_count = 0;

static int valid_hash(const char *hash)
{
    int i;

    for (i = 0; i < 32; i++)
        if (!isxdigit((unsigned char)hash[i]))
            return 0;
    return hash[32] == '\0';
}

static void save_games(void)
{
    char path[600];
    FILE *f;
    int i;

    if (config_games_path(path, sizeof(path)) != 0)
        return;
    f = fopen(path, "w");
    if (f == NULL)
        return;
    for (i = 0; i < g_games_count; i++)
        fprintf(f, "%s %s\n", g_games[i].serial, g_games[i].hash);
    fclose(f);
}

void console_load_games(void)
{
    char path[600], line[128], serial[16], hash[40];
    FILE *f;

    if (config_games_path(path, sizeof(path)) != 0)
        return;
    f = fopen(path, "r");
    if (f == NULL)
        return;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (sscanf(line, "%15s %39s", serial, hash) == 2 && valid_hash(hash) &&
            g_games_count < (int)(sizeof(g_games) / sizeof(g_games[0]))) {
            snprintf(g_games[g_games_count].serial, sizeof(g_games[0].serial), "%s", serial);
            snprintf(g_games[g_games_count].hash, sizeof(g_games[0].hash), "%s", hash);
            g_games_count++;
        }
    }
    fclose(f);
    if (g_games_count > 0)
        log_info("%d known game%s loaded from %s", g_games_count, g_games_count == 1 ? "" : "s", path);
}

void console_remember_game(const char *serial, const char *hash)
{
    int i;

    if (serial == NULL || hash == NULL || serial[0] == '\0' || !valid_hash(hash))
        return;

    for (i = 0; i < g_games_count; i++) {
        if (strcmp(g_games[i].serial, serial) == 0) {
            if (strcmp(g_games[i].hash, hash) != 0) {
                snprintf(g_games[i].hash, sizeof(g_games[i].hash), "%s", hash);
                save_games();
            }
            return;
        }
    }

    if (g_games_count >= (int)(sizeof(g_games) / sizeof(g_games[0])))
        return;

    snprintf(g_games[g_games_count].serial, sizeof(g_games[0].serial), "%s", serial);
    snprintf(g_games[g_games_count].hash, sizeof(g_games[0].hash), "%s", hash);
    g_games_count++;
    log_info("remembered %s = %s", serial, hash);
    save_games();
}

const char *console_hash_for(const char *serial)
{
    int i;

    for (i = 0; i < g_games_count; i++)
        if (strcmp(g_games[i].serial, serial) == 0)
            return g_games[i].hash;

    return NULL;
}

/* ---- Replies ---------------------------------------------------------- */

static size_t pad_reply(char *buf, size_t len, size_t cap)
{
    size_t want = (len + PS2RA_REPLY_ALIGN - 1) / PS2RA_REPLY_ALIGN * PS2RA_REPLY_ALIGN;

    if (want < PS2RA_REPLY_MIN)
        want = PS2RA_REPLY_MIN;
    if (want > cap)
        return len;

    memset(buf + len, ' ', want - len);
    return want;
}

static void send_reply(sock_t sock, char *buf, size_t len, size_t cap, const struct sockaddr_in *to)
{
    sendto(sock, buf, (int)pad_reply(buf, len, cap), 0, (const struct sockaddr *)to, sizeof(*to));
}

/* The console puts its own address in the request; the reply goes there,
   not to the datagram's source, which NAT may have rewritten. A request
   without a usable address is answered to its source. */
static void pick_target(struct sockaddr_in *to, const struct sockaddr_in *from, const char *ip, int port)
{
    unsigned long addr = (ip[0] != '\0') ? inet_addr(ip) : INADDR_NONE;

    *to = *from;
    if (port > 0 && port < 65536 && addr != INADDR_NONE && addr != 0) {
        memset(to, 0, sizeof(*to));
        to->sin_family = AF_INET;
        to->sin_port = htons((unsigned short)port);
        to->sin_addr.s_addr = (unsigned int)addr;
    }
}

/* Watch list serialized for the console, cached by hash: the console
   repeats a request up to twelve times, and the RA server should be
   asked once. */
static unsigned char g_serve[RA_SNAP_MAX_BYTES * 8];
static int g_serve_len = 0;
static char g_serve_hash[33] = "";
static char g_unknown_hash[33] = ""; /* last hash the RA server rejected */

static int serve_load(rc_client_t *client, const char *hash)
{
    if (strcmp(g_serve_hash, hash) == 0 && g_serve_len > 0)
        return 1;

    g_serve_len = 0;
    g_serve_hash[0] = '\0';

    if (!ra_load_game(client, hash))
        return 0;
    if (!watchlist_build(client))
        return 0;

    g_serve_len = watchlist_serialize(g_serve, sizeof(g_serve));
    if (g_serve_len == 0) {
        log_warn("watch list does not fit in the reply buffer");
        return 0;
    }

    snprintf(g_serve_hash, sizeof(g_serve_hash), "%s", hash);
    log_info("watch list ready for the console: %d addresses, %d bytes", watchlist_count(), g_serve_len);
    return 1;
}

int console_serve(sock_t sock, const char *pkt, size_t len,
                  const struct sockaddr_in *from, rc_client_t *client)
{
    char hash[64], serial[32], ip[32], reply[PS2RA_CHUNK + 64];
    struct sockaddr_in to;
    int port = 0, idx = 0, n;

    (void)len;
    hash[0] = serial[0] = ip[0] = '\0';

    if (strncmp(pkt, "RAP1 ", 5) == 0) {
        if (sscanf(pkt + 5, "%31s %d", ip, &port) != 2)
            return 1;
        pick_target(&to, from, ip, port);
        /* The version lets the console's link test show what answered. */
        n = snprintf(reply, sizeof(reply), "RAO1 OK %s/%s", PS2RA_NAME, PS2RA_VERSION);
        send_reply(sock, reply, (size_t)n, sizeof(reply), &to);
        log_info("console found at %s:%d", ip, port);
        return 2;
    }

    if (strncmp(pkt, "RAQ1 ", 5) == 0) {
        int got = sscanf(pkt + 5, "%63s %31s %31s %d", hash, serial, ip, &port);

        if (got < 1 || !valid_hash(hash))
            return 1;
        pick_target(&to, from, ip, port);
        if (got >= 2)
            console_remember_game(serial, hash);

        if (strcmp(g_serve_hash, hash) == 0 && g_serve_len > 0) {
            int chunks = (g_serve_len + PS2RA_CHUNK - 1) / PS2RA_CHUNK;
            char title[64] = "";
            unsigned total = 0, unlocked = 0, unsupported = 0;

            /* The counts and title are extras for the console's notice.
               The list size and chunk count come first, so older console
               builds that read only those two numbers still work; a
               console that also reads the three counts gets them, and
               one that reads just the first count (total) still works
               because it comes before unlocked and unsupported. */
            ra_game_summary(client, title, sizeof(title), &total, &unlocked, &unsupported);
            n = snprintf(reply, sizeof(reply), "RAA1 OK %d %d %u %u %u %s",
                         g_serve_len, chunks, total, unlocked, unsupported, title);
            log_info("console asked about %s: ready, %d bytes", hash, g_serve_len);
        } else if (strcmp(g_unknown_hash, hash) == 0) {
            n = snprintf(reply, sizeof(reply), "RAA1 NO");
            log_info("console asked about %s: unknown to RetroAchievements", hash);
        } else {
            /* Identification is a live server request and takes seconds.
               Tell the console to wait so it does not give up; the real
               answer goes out on its next retry. */
            n = snprintf(reply, sizeof(reply), "RAA1 WAIT");
            send_reply(sock, reply, (size_t)n, sizeof(reply), &to);
            /* Only a definite "no such game" is cached. A server or
               network failure is retried on the console's next request. */
            if (!serve_load(client, hash) && ra_last_load_result() == RC_NO_GAME_LOADED)
                snprintf(g_unknown_hash, sizeof(g_unknown_hash), "%s", hash);
            return 1;
        }

        send_reply(sock, reply, (size_t)n, sizeof(reply), &to);
        return 1;
    }

    if (strncmp(pkt, "RAG1 ", 5) == 0) {
        int hdr, take;

        if (sscanf(pkt + 5, "%63s %d %31s %d", hash, &idx, ip, &port) < 2 || !valid_hash(hash))
            return 1;
        pick_target(&to, from, ip, port);

        if (!serve_load(client, hash) || idx < 0 || idx * PS2RA_CHUNK >= g_serve_len)
            return 1;

        take = g_serve_len - idx * PS2RA_CHUNK;
        if (take > PS2RA_CHUNK)
            take = PS2RA_CHUNK;

        hdr = snprintf(reply, sizeof(reply), "RAC1 %d %d ", idx, take);
        memcpy(reply + hdr, &g_serve[idx * PS2RA_CHUNK], (size_t)take);
        send_reply(sock, reply, (size_t)hdr + (size_t)take, sizeof(reply), &to);
        return 1;
    }

    return 0;
}
