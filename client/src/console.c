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

/* ---- The console's address, from discovery or any later packet, so the
   client can speak first (an unlock notice) through the socket its
   telemetry arrives on. */
static struct sockaddr_in g_console;
/* The subnet broadcast (/24 assumed): in play the console answers no ARP,
   Windows marks it Unreachable and unicasts die on the PC. Everything
   pushed goes out both ways. */
static struct sockaddr_in g_console_bcast;
static int g_console_known = 0;
static sock_t g_console_sock = SOCK_INVALID;

/* ---- Replies ---------------------------------------------------------- */

static size_t pad_reply(char *buf, size_t len, size_t cap)
{
    size_t want = (len + XERABORA_REPLY_ALIGN - 1) / XERABORA_REPLY_ALIGN * XERABORA_REPLY_ALIGN;

    if (want < XERABORA_REPLY_MIN)
        want = XERABORA_REPLY_MIN;
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
    char hash[64], serial[32], ip[32], reply[XERABORA_CHUNK + 64];
    struct sockaddr_in to;
    int port = 0, idx = 0, n;

    (void)len;
    hash[0] = serial[0] = ip[0] = '\0';

    if (strncmp(pkt, "RAP1 ", 5) == 0) {
        if (sscanf(pkt + 5, "%31s %d", ip, &port) != 2)
            return 1;
        pick_target(&to, from, ip, port);
        /* The version lets the console's link test show what answered. */
        n = snprintf(reply, sizeof(reply), "RAO1 OK %s/%s", XERABORA_NAME, XERABORA_VERSION);
        send_reply(sock, reply, (size_t)n, sizeof(reply), &to);
        log_info("console found at %s:%d", ip, port);
        g_console = to;
        g_console_bcast = to;
        g_console_bcast.sin_addr.s_addr |= htonl(0xFF); /* /24 assumed */
        /* The unicast dies on the PC when Windows has the console's
           neighbor entry as Unreachable (it stopped answering ARP once
           the game booted). The broadcast copy needs no ARP. */
        send_reply(sock, reply, (size_t)n, sizeof(reply), &g_console_bcast);
        g_console_known = 1;
        g_console_sock = sock;
        return 2;
    }

    if (strncmp(pkt, "RAH1 ", 5) == 0 || strncmp(pkt, "RAK1 ", 5) == 0 ||
        strncmp(pkt, "RAK2 ", 5) == 0)
        console_learn(sock, from);

    if (strncmp(pkt, "RAH1 ", 5) == 0) {
        /* The console's ten-second heartbeat: what its receive side has
           seen since the game started. Zero datagrams while the client
           is sending names the broken link. */
        unsigned rx = 0, rau = 0, rab = 0;
        char mask[16] = "", done[4] = "";

        sscanf(pkt + 5, "%u %u %u %15s %3s", &rx, &rau, &rab, mask, done);
        log_info("console heartbeat: %u datagrams in, %u unlock notices, %u badge chunks, mask %s, badge done %s",
                 rx, rau, rab, mask, done);
        return 1;
    }

    if (strncmp(pkt, "RAK2 ", 5) == 0) {
        /* The console's word on the badge: where its EE buffer is and
           what the SIF DMA returned. Sent once, when the sixteenth
           chunk completes the picture. */
        char ee[16] = "", dma[16] = "";

        sscanf(pkt + 5, "%15s %15s", ee, dma);
        log_info("console assembled the badge (EE buffer %s, DMA %s)", ee, dma);
        return 1;
    }

    if (strncmp(pkt, "RAK1 ", 5) == 0) {
        /* The console's word on an unlock notice: which one, where its
           EE buffer is, and what the DMA returned. */
        unsigned seq = 0;
        char ee[16] = "", dma[16] = "";

        sscanf(pkt + 5, "%u %15s %15s", &seq, ee, dma);
        log_info("console acknowledged unlock notice %u (EE buffer %s, DMA %s)", seq, ee, dma);
        return 1;
    }

    if (strncmp(pkt, "RAQ1 ", 5) == 0) {
        int got = sscanf(pkt + 5, "%63s %31s %31s %d", hash, serial, ip, &port);

        if (got < 1 || !valid_hash(hash))
            return 1;
        pick_target(&to, from, ip, port);
        if (got >= 2)
            console_remember_game(serial, hash);

        if (strcmp(g_serve_hash, hash) == 0 && g_serve_len > 0) {
            int chunks = (g_serve_len + XERABORA_CHUNK - 1) / XERABORA_CHUNK;
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

        if (!serve_load(client, hash) || idx < 0 || idx * XERABORA_CHUNK >= g_serve_len)
            return 1;

        take = g_serve_len - idx * XERABORA_CHUNK;
        if (take > XERABORA_CHUNK)
            take = XERABORA_CHUNK;

        hdr = snprintf(reply, sizeof(reply), "RAC1 %d %d ", idx, take);
        memcpy(reply + hdr, &g_serve[idx * XERABORA_CHUNK], (size_t)take);
        send_reply(sock, reply, (size_t)hdr + (size_t)take, sizeof(reply), &to);
        return 1;
    }

    return 0;
}

/* ---- Unlock notice ------------------------------------------------------ */

void console_learn(sock_t sock, const struct sockaddr_in *from)
{
    if (g_console_known && g_console.sin_addr.s_addr == from->sin_addr.s_addr &&
        g_console.sin_port == from->sin_port)
        return;
    g_console = *from;
    g_console_bcast = *from;
    g_console_bcast.sin_addr.s_addr |= htonl(0xFF); /* /24 assumed */
    g_console_known = 1;
    g_console_sock = sock;
    log_info("console at %s:%d, learned from its own packets",
             inet_ntoa(from->sin_addr), ntohs(from->sin_port));
}

int console_notify_unlock(unsigned id, unsigned points)
{
    char msg[64];
    int n;

    if (!g_console_known || g_console_sock == SOCK_INVALID) {
        log_warn("no console address yet, the unlock notice stays here");
        return 0;
    }

    n = snprintf(msg, sizeof(msg), "RAU1 %u %u", id, points);
    send_reply(g_console_sock, msg, (size_t)n, sizeof(msg), &g_console);
    send_reply(g_console_sock, msg, (size_t)n, sizeof(msg), &g_console_bcast);
    return 1;
}

/* ---- Badge push: one 512-byte slice per datagram behind an 11-byte fixed-
   width header ("RAB1 03 16 "), so the IOP parses two digit fields. Lab
   feature. */
int console_send_badge_chunk(const unsigned char *px, int idx)
{
    char msg[16 + XERABORA_BADGE_CHUNK];
    int n;

    if (!g_console_known || g_console_sock == SOCK_INVALID)
        return 0;
    if (idx < 0 || idx >= XERABORA_BADGE_CHUNKS)
        return 0;

    n = snprintf(msg, sizeof(msg), "RAB1 %02d %02d ", idx, XERABORA_BADGE_CHUNKS);
    memcpy(msg + n, px + (size_t)idx * XERABORA_BADGE_CHUNK, XERABORA_BADGE_CHUNK);
    /* Not send_reply: padding would put garbage behind the pixels. */
    sendto(g_console_sock, msg, n + XERABORA_BADGE_CHUNK, 0,
           (const struct sockaddr *)&g_console, sizeof(g_console));
    sendto(g_console_sock, msg, n + XERABORA_BADGE_CHUNK, 0,
           (const struct sockaddr *)&g_console_bcast, sizeof(g_console_bcast));
    return 1;
}
