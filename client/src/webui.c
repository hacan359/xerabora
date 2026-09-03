#include "webui.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "log.h"
#include "ra.h"
#include "raweb.h"
#include "ui_page.h"
#include "version.h"

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>
#endif

/* Session state the page renders. Written by the client as things
   happen, read when a request comes in; both happen on the same thread,
   so no locking. */
static struct
{
    char console_ip[32];
    int connected;
    int logged_in;
    int has_webapi;
    char user[64];
    char serial[16];
    char hash[33];
    char title[128];
    char status[24];
    unsigned long packets;
    unsigned long frames;
    unsigned long gaps;
    unsigned long dupes;
    unsigned long torn;
    unsigned watch_parts;
    unsigned watch_bytes;
    unsigned watch_addresses;
    time_t started;

    /* Set when the page's layout is out of date (new game, unlock, status
       change): the next push carries the full state. In between, one small
       delta per snapshot keeps the numbers fresh and the DOM in place. */
    int dirty;

    struct
    {
        unsigned id;
        char title[96];
        char badge[16];
        unsigned points;
        time_t at;
    } unlocks[16];
    int unlock_count;
} g;

/* The page is the only way to end a windowless process gracefully. */
static volatile int g_quit = 0;

static void web_cache_expire(void);

int webui_quit_requested(void)
{
    return g_quit;
}

void webui_set_login(int ok, const char *user)
{
    g.dirty = 1;
    g.logged_in = ok;
    snprintf(g.user, sizeof(g.user), "%s", user != NULL ? user : "");
}

void webui_set_webapi(int ok)
{
    g.dirty = 1;
    g.has_webapi = ok;
}

void webui_set_console(const char *ip, int connected)
{
    g.dirty = 1;
    if (ip != NULL)
        snprintf(g.console_ip, sizeof(g.console_ip), "%s", ip);
    if (connected && !g.connected)
        g.started = time(NULL);
    g.connected = connected;
}

void webui_set_game(const char *serial, const char *hash, const char *title)
{
    g.dirty = 1;
    snprintf(g.serial, sizeof(g.serial), "%s", serial != NULL ? serial : "");
    snprintf(g.hash, sizeof(g.hash), "%s", hash != NULL ? hash : "");
    snprintf(g.title, sizeof(g.title), "%s", title != NULL ? title : "");
}

void webui_note_unlock(unsigned id, const char *title, const char *badge, unsigned points)
{
    int i;

    /* Newest first, oldest pushed off the end. */
    for (i = (int)(sizeof(g.unlocks) / sizeof(g.unlocks[0])) - 1; i > 0; i--)
        g.unlocks[i] = g.unlocks[i - 1];

    g.unlocks[0].id = id;
    snprintf(g.unlocks[0].title, sizeof(g.unlocks[0].title), "%s", title != NULL ? title : "");
    snprintf(g.unlocks[0].badge, sizeof(g.unlocks[0].badge), "%s", badge != NULL ? badge : "");
    g.unlocks[0].points = points;
    g.unlocks[0].at = time(NULL);
    if (g.unlock_count < (int)(sizeof(g.unlocks) / sizeof(g.unlocks[0])))
        g.unlock_count++;
    /* The cached Web API payloads now lie about this game: the next
       /game, /library and /me requests must ask the server again. */
    web_cache_expire();
    g.dirty = 1;
}

void webui_note_packet(void)
{
    g.packets++;
}

void webui_set_status(const char *status)
{
    g.dirty = 1;
    snprintf(g.status, sizeof(g.status), "%s", status != NULL ? status : "");
}

void webui_set_stats(unsigned long frames, unsigned long gaps,
                     unsigned long dupes, unsigned long torn,
                     unsigned parts, unsigned bytes, unsigned addresses)
{
    g.frames = frames;
    g.gaps = gaps;
    g.dupes = dupes;
    g.torn = torn;
    g.watch_parts = parts;
    g.watch_bytes = bytes;
    g.watch_addresses = addresses;
}

/* ---- The listener ------------------------------------------------------- */

/* The port that answered, which is not always the one asked
   for: another copy of this client may already be running. */
static int g_port;

int webui_port(void)
{
    return g_port;
}

static int g_lan;
static int g_rebind;

void webui_set_lan(int on)
{
    g_lan = on ? 1 : 0;
}

int webui_lan(void)
{
    return g_lan;
}

/* The address another device would type. A connected UDP socket does
   not send anything; it only picks the interface that routes to the
   console, or to the internet when no console has been seen yet. */
static void lan_address(char *out, size_t size)
{
    struct sockaddr_in to, me;
    socklen_t len = sizeof(me);
    sock_t s;

    out[0] = '\0';
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == SOCK_INVALID)
        return;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    to.sin_addr.s_addr = g.console_ip[0] != '\0' ? inet_addr(g.console_ip) : INADDR_NONE;
    if (to.sin_addr.s_addr == INADDR_NONE)
        to.sin_addr.s_addr = inet_addr("1.1.1.1");
    if (connect(s, (struct sockaddr *)&to, sizeof(to)) == 0 &&
        getsockname(s, (struct sockaddr *)&me, &len) == 0)
        snprintf(out, size, "%s", inet_ntoa(me.sin_addr));
    sock_close(s);
}

sock_t webui_start(int port)
{
    struct sockaddr_in addr;
    sock_t s;
    int tries;

    /* Up to five ports from the one asked for. A second copy of the
       client, or anything else sitting on 18280, then costs a number
       rather than the whole interface. */
    for (tries = 0; tries < 5; tries++, port++) {
        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == SOCK_INVALID)
            return SOCK_INVALID;

#ifndef _WIN32
        /* POSIX only: on Windows SO_REUSEADDR lets a second bind succeed on
           a held port and the two sockets fight for connections, which
           looks like a page that refuses to load. */
        {
            int on = 1;

            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
        }
#endif

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        /* Loopback unless the user asked for the network: the page
           carries the settings and the off switch, so opening it to
           every interface is a choice, not an accident. */
        addr.sin_addr.s_addr = htonl(g_lan ? INADDR_ANY : INADDR_LOOPBACK);
        addr.sin_port = htons((unsigned short)port);

        if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == 0 && listen(s, 8) == 0) {
            g_port = port;
            if (g_lan) {
                char ip[48];

                lan_address(ip, sizeof(ip));
                if (ip[0] != '\0')
                    log_info("interface open to the network at http://%s:%d/", ip, port);
            }
            return s;
        }

        sock_close(s);
    }

    log_warn("no free port for the interface near %d; running without it", port - 5);
    return SOCK_INVALID;
}

sock_t webui_rebind(sock_t listener)
{
    int port;

    if (!g_rebind)
        return listener;
    g_rebind = 0;
    port = g_port;
    if (listener != SOCK_INVALID)
        sock_close(listener);
    listener = webui_start(port);
    if (listener == SOCK_INVALID)
        log_warn("the interface could not be reopened on port %d", port);
    return listener;
}

/* When set, GET / serves this file from disk instead of the built-in
   page: the edit-refresh loop for working on the interface. */
static char g_ui_file[512];

void webui_set_ui_file(const char *path)
{
    snprintf(g_ui_file, sizeof(g_ui_file), "%s", path != NULL ? path : "");
}

/* The page that asked for the live stream, if any. One is enough: a
   second tab gets the poll, which is fine for a second tab. */
static sock_t g_stream = SOCK_INVALID;
static unsigned long g_stream_sent;

void webui_stop(sock_t listener)
{
    if (g_stream != SOCK_INVALID) {
        sock_close(g_stream);
        g_stream = SOCK_INVALID;
    }
    if (listener != SOCK_INVALID)
        sock_close(listener);
}

/* ---- JSON out ----------------------------------------------------------- */

/* Escapes what JSON forbids in a string. Titles arrive from the RA
   server and from game memory, so nothing is assumed about them. */
static void json_str(char **p, char *end, const char *s)
{
    char *o = *p;

    if (o < end)
        *o++ = '"';
    for (; s != NULL && *s != '\0' && o + 8 < end; s++) {
        unsigned char c = (unsigned char)*s;

        switch (c) {
        case '"':  *o++ = '\\'; *o++ = '"'; break;
        case '\\': *o++ = '\\'; *o++ = '\\'; break;
        case '\n': *o++ = '\\'; *o++ = 'n'; break;
        case '\r': *o++ = '\\'; *o++ = 'r'; break;
        case '\t': *o++ = '\\'; *o++ = 't'; break;
        default:
            if (c < 0x20)
                o += snprintf(o, (size_t)(end - o), "\\u%04x", c);
            else
                *o++ = (char)c;
        }
    }
    if (o < end)
        *o++ = '"';
    *p = o;
}

static void json_field(char **p, char *end, const char *key, const char *value)
{
    *p += snprintf(*p, (size_t)(end - *p), "\"%s\":", key);
    json_str(p, end, value);
}

/* The whole state as one object. Small enough to build in a buffer;
   the achievement list is the only part that can grow, and it is
   capped by the set size. */
static int build_state(char *buf, size_t size, rc_client_t *client)
{
    char *p = buf, *end = buf + size;
    int i;

    p += snprintf(p, (size_t)(end - p), "{");

    p += snprintf(p, (size_t)(end - p), "\"version\":\"%s\",", XERABORA_VERSION);

    p += snprintf(p, (size_t)(end - p), "\"login\":{\"ok\":%s,\"webapi\":%s,",
                  g.logged_in ? "true" : "false", g.has_webapi ? "true" : "false");
    json_field(&p, end, "user", g.user);
    p += snprintf(p, (size_t)(end - p), "},");

    p += snprintf(p, (size_t)(end - p), "\"console\":{\"connected\":%s,", g.connected ? "true" : "false");
    json_field(&p, end, "ip", g.console_ip);
    p += snprintf(p, (size_t)(end - p), ",");
    json_field(&p, end, "status", g.status);
    p += snprintf(p, (size_t)(end - p),
                  ",\"packets\":%lu,\"frames\":%lu,\"gaps\":%lu,\"dupes\":%lu,\"torn\":%lu"
                  ",\"watch\":{\"parts\":%u,\"bytes\":%u,\"addresses\":%u},\"seconds\":%ld},",
                  g.packets, g.frames, g.gaps, g.dupes, g.torn,
                  g.watch_parts, g.watch_bytes, g.watch_addresses,
                  g.started != 0 ? (long)(time(NULL) - g.started) : 0L);

    {
        char ip[48] = "", url[80] = "";

        if (g_lan) {
            lan_address(ip, sizeof(ip));
            if (ip[0] != '\0')
                snprintf(url, sizeof(url), "http://%s:%d/", ip, g_port);
        }
        p += snprintf(p, (size_t)(end - p), "\"lan\":{\"on\":%s,", g_lan ? "true" : "false");
        json_field(&p, end, "url", url);
        p += snprintf(p, (size_t)(end - p), "},");
    }

    /* The RA game id lets the page pull the same game's Web API payload
       (types, median times) and lay it over the live list. */
    {
        const rc_client_game_t *info = client != NULL ? rc_client_get_game_info(client) : NULL;

        p += snprintf(p, (size_t)(end - p), "\"game\":{\"id\":%u,",
                      info != NULL ? info->id : 0);
    }
    json_field(&p, end, "serial", g.serial);
    p += snprintf(p, (size_t)(end - p), ",");
    json_field(&p, end, "hash", g.hash);
    p += snprintf(p, (size_t)(end - p), ",");
    json_field(&p, end, "title", g.title);

    /* Achievements come from rc_client, which is where the live
       progress of a measured achievement lives -- the one thing the
       Web API cannot tell us. */
    p += snprintf(p, (size_t)(end - p), ",\"achievements\":[");
    if (client != NULL) {
        rc_client_achievement_list_t *list =
            rc_client_create_achievement_list(client,
                                              RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
                                              RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
        int first = 1;

        if (list != NULL) {
            unsigned b, a;

            for (b = 0; b < list->num_buckets; b++) {
                for (a = 0; a < list->buckets[b].num_achievements; a++) {
                    const rc_client_achievement_t *ach = list->buckets[b].achievements[a];

                    if (ach->id >= WEBUI_WARNING_ACH_ID)
                        continue;
                    if (end - p < 512)
                        break;
                    if (!first)
                        p += snprintf(p, (size_t)(end - p), ",");
                    first = 0;

                    p += snprintf(p, (size_t)(end - p), "{\"id\":%u,", ach->id);
                    json_field(&p, end, "title", ach->title);
                    p += snprintf(p, (size_t)(end - p), ",");
                    json_field(&p, end, "description", ach->description);
                    p += snprintf(p, (size_t)(end - p), ",");
                    json_field(&p, end, "badge", ach->badge_name);
                    p += snprintf(p, (size_t)(end - p), ",");
                    json_field(&p, end, "measured", ach->measured_progress);
                    p += snprintf(p, (size_t)(end - p),
                                  ",\"points\":%u,\"state\":%u,\"percent\":%.4f,\"type\":%u,\"bucket\":%u}",
                                  ach->points, ach->state, ach->measured_percent,
                                  (unsigned)ach->type, list->buckets[b].bucket_type);
                }
            }
            rc_client_destroy_achievement_list(list);
        }
    }
    p += snprintf(p, (size_t)(end - p), "]");
    /* Leaderboards the console is tracking right now: the value moves
       with the game, and nothing outside a memory feed can show it. */
    p += snprintf(p, (size_t)(end - p), ",\"tracking\":[");
    if (client != NULL) {
        rc_client_leaderboard_list_t *lbs =
            rc_client_create_leaderboard_list(client,
                                              RC_CLIENT_LEADERBOARD_LIST_GROUPING_TRACKING);
        int first = 1;

        if (lbs != NULL) {
            unsigned b, l;

            for (b = 0; b < lbs->num_buckets; b++) {
                for (l = 0; l < lbs->buckets[b].num_leaderboards; l++) {
                    const rc_client_leaderboard_t *lb = lbs->buckets[b].leaderboards[l];

                    if (lb->state != RC_CLIENT_LEADERBOARD_STATE_ACTIVE || end - p < 320)
                        continue;
                    if (!first)
                        p += snprintf(p, (size_t)(end - p), ",");
                    first = 0;

                    p += snprintf(p, (size_t)(end - p), "{\"id\":%u,", lb->id);
                    json_field(&p, end, "title", lb->title);
                    p += snprintf(p, (size_t)(end - p), ",");
                    json_field(&p, end, "value", lb->tracker_value);
                    p += snprintf(p, (size_t)(end - p), "}");
                }
            }
            rc_client_destroy_leaderboard_list(lbs);
        }
    }
    p += snprintf(p, (size_t)(end - p), "]},");

    p += snprintf(p, (size_t)(end - p), "\"unlocks\":[");
    for (i = 0; i < g.unlock_count && end - p > 256; i++) {
        if (i > 0)
            p += snprintf(p, (size_t)(end - p), ",");
        p += snprintf(p, (size_t)(end - p), "{\"id\":%u,", g.unlocks[i].id);
        json_field(&p, end, "title", g.unlocks[i].title);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "badge", g.unlocks[i].badge);
        p += snprintf(p, (size_t)(end - p), ",\"points\":%u,\"ago\":%ld}",
                      g.unlocks[i].points, (long)(time(NULL) - g.unlocks[i].at));
    }
    p += snprintf(p, (size_t)(end - p), "]");

    p += snprintf(p, (size_t)(end - p), "}");
    return (int)(p - buf);
}

/* Between full states only the moving numbers go out: counters, measured
   progress, trackers. The layout stays, so CSS transitions survive. Full
   state once a second and on anything structural (g.dirty). */
static int build_delta(char *buf, size_t size, rc_client_t *client)
{
    char *p = buf, *end = buf + size;

    p += snprintf(p, (size_t)(end - p),
                  "{\"delta\":1,\"console\":{\"frames\":%lu,\"gaps\":%lu,\"dupes\":%lu,"
                  "\"torn\":%lu,\"seconds\":%ld}",
                  g.frames, g.gaps, g.dupes, g.torn,
                  g.started != 0 ? (long)(time(NULL) - g.started) : 0L);

    p += snprintf(p, (size_t)(end - p), ",\"live\":[");
    if (client != NULL) {
        rc_client_achievement_list_t *list =
            rc_client_create_achievement_list(client,
                                              RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
                                              RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
        int first = 1;

        if (list != NULL) {
            unsigned b, a;

            for (b = 0; b < list->num_buckets; b++) {
                for (a = 0; a < list->buckets[b].num_achievements; a++) {
                    const rc_client_achievement_t *ach = list->buckets[b].achievements[a];

                    if (ach->state != RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE)
                        continue;
                    if (ach->measured_progress[0] == '\0' && ach->measured_percent <= 0)
                        continue;
                    if (end - p < 192)
                        break;
                    if (!first)
                        p += snprintf(p, (size_t)(end - p), ",");
                    first = 0;

                    p += snprintf(p, (size_t)(end - p), "{\"id\":%u,\"m\":", ach->id);
                    json_str(&p, end, ach->measured_progress);
                    p += snprintf(p, (size_t)(end - p), ",\"p\":%.4f}", ach->measured_percent);
                }
            }
            rc_client_destroy_achievement_list(list);
        }
    }

    p += snprintf(p, (size_t)(end - p), "],\"tracking\":[");
    if (client != NULL) {
        rc_client_leaderboard_list_t *lbs =
            rc_client_create_leaderboard_list(client,
                                              RC_CLIENT_LEADERBOARD_LIST_GROUPING_TRACKING);
        int first = 1;

        if (lbs != NULL) {
            unsigned b, l;

            for (b = 0; b < lbs->num_buckets; b++) {
                for (l = 0; l < lbs->buckets[b].num_leaderboards; l++) {
                    const rc_client_leaderboard_t *lb = lbs->buckets[b].leaderboards[l];

                    if (lb->state != RC_CLIENT_LEADERBOARD_STATE_ACTIVE || end - p < 192)
                        continue;
                    if (!first)
                        p += snprintf(p, (size_t)(end - p), ",");
                    first = 0;

                    p += snprintf(p, (size_t)(end - p), "{\"id\":%u,\"v\":", lb->id);
                    json_str(&p, end, lb->tracker_value);
                    p += snprintf(p, (size_t)(end - p), "}");
                }
            }
            rc_client_destroy_leaderboard_list(lbs);
        }
    }
    p += snprintf(p, (size_t)(end - p), "]}");
    return (int)(p - buf);
}

/* ---- The Web API, cached: the page polls, the RA server must not. Each
   answer is kept for a while and served from memory. */

#define LIBRARY_TTL 120 /* seconds */
#define GAME_TTL 30

static struct raweb_game g_lib[500];
static int g_lib_count;
static time_t g_lib_at;

static struct raweb_profile g_me;
static time_t g_me_at;

static struct raweb_game_progress g_gp;
static struct raweb_achievement g_gp_ach[600];
static int g_gp_count;
static unsigned g_gp_id;
static time_t g_gp_at;

/* Median unlock times, the order players take a set in --
   CLIENT.md calls it the honest sort for "what to chase next". */
static unsigned g_gp_med_ids[600], g_gp_med_secs[600];
static int g_gp_med_count;

static int build_profile(char *buf, size_t size)
{
    char *p = buf, *end = buf + size;

    if (time(NULL) - g_me_at > LIBRARY_TTL) {
        raweb_profile(&g_me);
        g_me_at = time(NULL);
    }

    p += snprintf(p, (size_t)(end - p), "{");
    json_field(&p, end, "user", g_me.user);
    p += snprintf(p, (size_t)(end - p), ",");
    json_field(&p, end, "motto", g_me.motto);
    p += snprintf(p, (size_t)(end - p), ",");
    json_field(&p, end, "presence", g_me.rich_presence);
    p += snprintf(p, (size_t)(end - p), ",\"points\":%u,\"softcore\":%u,\"rank\":%u,\"ranked\":%u}",
                  g_me.points, g_me.softcore_points, g_me.rank, g_me.total_ranked);
    return (int)(p - buf);
}

static int build_library(char *buf, size_t size)
{
    char *p = buf, *end = buf + size;
    int i;

    if (g_lib_count == 0 || time(NULL) - g_lib_at > LIBRARY_TTL) {
        g_lib_count = raweb_completion_progress(g_lib, (int)(sizeof(g_lib) / sizeof(g_lib[0])));
        g_lib_at = time(NULL);
    }

    p += snprintf(p, (size_t)(end - p), "{\"games\":[");
    for (i = 0; i < g_lib_count && end - p > 512; i++) {
        if (i > 0)
            p += snprintf(p, (size_t)(end - p), ",");
        p += snprintf(p, (size_t)(end - p), "{\"id\":%u,", g_lib[i].id);
        json_field(&p, end, "title", g_lib[i].title);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "console", g_lib[i].console);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "icon", g_lib[i].icon);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "award", g_lib[i].award);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "played", g_lib[i].last_played);
        p += snprintf(p, (size_t)(end - p),
                      ",\"total\":%u,\"awarded\":%u,\"hardcore\":%u,\"mastered\":%s}",
                      g_lib[i].achievements, g_lib[i].awarded, g_lib[i].awarded_hardcore,
                      g_lib[i].mastered ? "true" : "false");
    }
    p += snprintf(p, (size_t)(end - p), "]}");
    return (int)(p - buf);
}

/* After an unlock: the game, the shelf and the points all moved. */
static void web_cache_expire(void)
{
    g_gp_at = 0;
    g_lib_at = 0;
    g_me_at = 0;
}

static int build_game(char *buf, size_t size, unsigned id)
{
    char *p = buf, *end = buf + size;
    int i;

    if (id != g_gp_id || time(NULL) - g_gp_at > GAME_TTL) {
        g_gp_count = raweb_game_progress(id, &g_gp, g_gp_ach,
                                         (int)(sizeof(g_gp_ach) / sizeof(g_gp_ach[0])));
        g_gp_med_count = raweb_median_times(id, g_gp_med_ids, g_gp_med_secs,
                                            (int)(sizeof(g_gp_med_ids) / sizeof(g_gp_med_ids[0])));
        g_gp_id = id;
        g_gp_at = time(NULL);
    }

    p += snprintf(p, (size_t)(end - p), "{\"id\":%u,", id);
    json_field(&p, end, "title", g_gp.title);
    p += snprintf(p, (size_t)(end - p), ",");
    json_field(&p, end, "console", g_gp.console);
    p += snprintf(p, (size_t)(end - p), ",");
    json_field(&p, end, "icon", g_gp.image_icon);
    p += snprintf(p, (size_t)(end - p), ",\"total\":%u,\"awarded\":%u,\"achievements\":[",
                  g_gp.achievements_total, g_gp.awarded);

    for (i = 0; i < g_gp_count && end - p > 512; i++) {
        if (i > 0)
            p += snprintf(p, (size_t)(end - p), ",");
        p += snprintf(p, (size_t)(end - p), "{\"id\":%u,", g_gp_ach[i].id);
        json_field(&p, end, "title", g_gp_ach[i].title);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "description", g_gp_ach[i].description);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "badge", g_gp_ach[i].badge);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "type", g_gp_ach[i].type);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "earned", g_gp_ach[i].date_earned);
        {
            unsigned median = 0;
            int m;

            for (m = 0; m < g_gp_med_count; m++)
                if (g_gp_med_ids[m] == g_gp_ach[i].id) {
                    median = g_gp_med_secs[m];
                    break;
                }
            p += snprintf(p, (size_t)(end - p),
                          ",\"points\":%u,\"order\":%u,\"players\":%u,\"median\":%u}",
                          g_gp_ach[i].points, g_gp_ach[i].display_order,
                          g_gp_ach[i].num_awarded, median);
        }
    }
    p += snprintf(p, (size_t)(end - p), "]}");
    return (int)(p - buf);
}

static struct raweb_leaderboard g_lb[300];
static int g_lb_count;
static unsigned g_lb_id;
static time_t g_lb_at;

static int build_leaderboards(char *buf, size_t size, unsigned id)
{
    char *p = buf, *end = buf + size;
    int i;

    if (id != g_lb_id || time(NULL) - g_lb_at > GAME_TTL) {
        g_lb_count = raweb_game_leaderboards(id, g_lb, (int)(sizeof(g_lb) / sizeof(g_lb[0])));
        g_lb_id = id;
        g_lb_at = time(NULL);
    }

    p += snprintf(p, (size_t)(end - p), "{\"boards\":[");
    for (i = 0; i < g_lb_count && end - p > 512; i++) {
        if (i > 0)
            p += snprintf(p, (size_t)(end - p), ",");
        p += snprintf(p, (size_t)(end - p), "{\"id\":%u,", g_lb[i].id);
        json_field(&p, end, "title", g_lb[i].title);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "description", g_lb[i].description);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "format", g_lb[i].format);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "topUser", g_lb[i].top_user);
        p += snprintf(p, (size_t)(end - p), ",");
        json_field(&p, end, "topScore", g_lb[i].top_score);
        p += snprintf(p, (size_t)(end - p), ",\"lowerBetter\":%s}",
                      g_lb[i].lower_is_better ? "true" : "false");
    }
    p += snprintf(p, (size_t)(end - p), "]}");
    return (int)(p - buf);
}

/* ---- Settings posted from the page: small urlencoded forms to loopback.
   The password goes on to the RA server the way --user sent it. Nothing
   here is reachable off the machine. */

static void urldecode(char *s)
{
    char *o = s;

    for (; *s != '\0'; s++, o++) {
        if (*s == '+') {
            *o = ' ';
        } else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], '\0' };

            *o = (char)strtol(hex, NULL, 16);
            s += 2;
        } else {
            *o = *s;
        }
    }
    *o = '\0';
}

/* One field of an urlencoded form body. Returns 1 when present. */
static int form_field(const char *body, const char *name, char *out, size_t size)
{
    size_t nlen = strlen(name);
    const char *p = body;

    out[0] = '\0';
    while (p != NULL && *p != '\0') {
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            const char *v = p + nlen + 1;
            const char *e = strchr(v, '&');
            size_t len = e != NULL ? (size_t)(e - v) : strlen(v);

            if (len >= size)
                len = size - 1;
            memcpy(out, v, len);
            out[len] = '\0';
            urldecode(out);
            return 1;
        }
        p = strchr(p, '&');
        if (p != NULL)
            p++;
    }
    return 0;
}

static void respond(sock_t c, const char *type, const char *body, int len);

/* Returns 1 when the request was one of the settings posts. */
static int serve_settings(sock_t c, const char *req, const char *body, rc_client_t *client)
{
    char out[192];
    int n;

    if (strncmp(req, "POST /apikey", 12) == 0) {
        char key[128], user[128] = "", token[256];

        form_field(body, "key", key, sizeof(key));
        if (key[0] == '\0') {
            n = snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"the key is empty\"}");
        } else if (config_save_apikey(key) != 0) {
            n = snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"could not save the key\"}");
        } else {
            /* The user endpoints need to know who is asking. */
            if (!config_load_credentials(user, sizeof(user), token, sizeof(token)) && g.user[0] != '\0')
                snprintf(user, sizeof(user), "%s", g.user);
            raweb_set_credentials(user, key);
            webui_set_webapi(1);
            /* The page refetches at once, past yesterday's cache. */
            g_me_at = 0;
            g_lib_at = 0;
            log_info("web api key saved from the page");
            n = snprintf(out, sizeof(out), "{\"ok\":true}");
        }
        respond(c, "application/json; charset=utf-8", out, n);
        return 1;
    }

    if (strncmp(req, "POST /login", 11) == 0) {
        char user[128], password[256];

        form_field(body, "user", user, sizeof(user));
        form_field(body, "password", password, sizeof(password));

        if (client == NULL) {
            n = snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"not available in ui-only mode\"}");
        } else if (user[0] == '\0' || password[0] == '\0') {
            n = snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"enter both name and password\"}");
        } else {
            int rc = ra_login_with_password(client, user, password);

            if (rc == RC_OK) {
                const rc_client_user_t *me = rc_client_get_user_info(client);
                char key[128];

                if (config_save_credentials(user, me->token) != 0)
                    log_warn("could not save the login token; you will be asked again next time");
                webui_set_login(1, me->display_name != NULL ? me->display_name : user);
                if (config_load_apikey(key, sizeof(key))) {
                    raweb_set_credentials(user, key);
                    g_me_at = 0;
                    g_lib_at = 0;
                }
                n = snprintf(out, sizeof(out), "{\"ok\":true}");
            } else if (rc == RC_INVALID_CREDENTIALS || rc == RC_EXPIRED_TOKEN || rc == RC_ACCESS_DENIED) {
                n = snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"wrong name or password\"}");
            } else {
                n = snprintf(out, sizeof(out),
                             "{\"ok\":false,\"error\":\"the RetroAchievements server could not be reached\"}");
            }
        }
        memset(password, 0, sizeof(password));
        respond(c, "application/json; charset=utf-8", out, n);
        return 1;
    }

    if (strncmp(req, "POST /lan", 9) == 0) {
        char on[8] = "";

        form_field(body, "on", on, sizeof(on));
        g_lan = on[0] == '1';
        if (config_save_lan(g_lan) != 0)
            log_warn("could not save the network setting; it holds until exit");
        g_rebind = 1;
        log_info(g_lan ? "interface opened to the network from the page"
                       : "interface closed to the network from the page");
        n = snprintf(out, sizeof(out), "{\"ok\":true}");
        respond(c, "application/json; charset=utf-8", out, n);
        return 1;
    }

    if (strncmp(req, "POST /quit", 10) == 0) {
        g_quit = 1;
        log_info("quit requested from the page");
        n = snprintf(out, sizeof(out), "{\"ok\":true}");
        respond(c, "application/json; charset=utf-8", out, n);
        return 1;
    }

    if (strncmp(req, "POST /logout", 12) == 0) {
        if (client != NULL)
            rc_client_logout(client);
        config_forget_credentials();
        webui_set_login(0, "");
        log_info("signed out from the page");
        n = snprintf(out, sizeof(out), "{\"ok\":true}");
        respond(c, "application/json; charset=utf-8", out, n);
        return 1;
    }

    return 0;
}

/* ---- One request -------------------------------------------------------- */

static void send_all(sock_t c, const char *data, int len)
{
    int sent = 0;

    while (sent < len) {
        int n = (int)send(c, data + sent, len - sent, 0);

        if (n <= 0)
            return;
        sent += n;
    }
}

static void respond(sock_t c, const char *type, const char *body, int len)
{
    char head[256];
    int n;

    n = snprintf(head, sizeof(head),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %d\r\n"
                 "Cache-Control: no-store\r\n"
                 "Connection: close\r\n\r\n",
                 type, len);
    send_all(c, head, n);
    send_all(c, body, len);
}

void webui_serve(sock_t listener, rc_client_t *client)
{
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    char req[2048];
    sock_t c;
    int n = 0;

    c = accept(listener, (struct sockaddr *)&from, &fromlen);
    if (c == SOCK_INVALID)
        return;

    /* One request: headers, plus the body of a settings POST, which may
       need a second read. Anything larger than this buffer is cut short and
       falls through to "not found". */
    for (;;) {
        char *hdr_end;
        int r = (int)recv(c, req + n, (int)sizeof(req) - 1 - n, 0);

        if (r <= 0)
            break;
        n += r;
        req[n] = '\0';

        hdr_end = strstr(req, "\r\n\r\n");
        if (hdr_end == NULL) {
            if (n >= (int)sizeof(req) - 1)
                break;
            continue;
        }
        if (strncmp(req, "POST ", 5) != 0)
            break;
        {
            const char *cl = strstr(req, "Content-Length:");
            int want = cl != NULL ? atoi(cl + 15) : 0;
            int have = n - (int)(hdr_end + 4 - req);

            if (have >= want || n >= (int)sizeof(req) - 1)
                break;
        }
    }
    if (n <= 0) {
        sock_close(c);
        return;
    }

    if (strncmp(req, "POST ", 5) == 0) {
        char *body = strstr(req, "\r\n\r\n");

        /* Another device may watch; only this machine changes the
           settings or turns the client off. */
        if (from.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
            const char *fb = "{\"ok\":false,\"error\":\"settings change only on the PC running xerabora\"}";
            char head[160];
            int hn = snprintf(head, sizeof(head),
                              "HTTP/1.1 403 Forbidden\r\nContent-Type: application/json; charset=utf-8\r\n"
                              "Content-Length: %d\r\nConnection: close\r\n\r\n", (int)strlen(fb));

            send_all(c, head, hn);
            send_all(c, fb, (int)strlen(fb));
            sock_close(c);
            return;
        }
        if (body == NULL || !serve_settings(c, req, body + 4, client)) {
            const char *nf = "not found";

            send_all(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\n", 63);
            send_all(c, nf, 9);
        }
        sock_close(c);
        return;
    }

    if (strncmp(req, "GET /events", 11) == 0) {
        /* The stream stays open; everything else about this connection
           is done here. A previous stream is dropped: one page at a
           time gets the live feed. */
        const char *head =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: keep-alive\r\n\r\n";

        if (g_stream != SOCK_INVALID)
            sock_close(g_stream);
        send_all(c, head, (int)strlen(head));
        g_stream = c;
        g.dirty = 1; /* a fresh page needs the full layout first */
        webui_push(client);
        return; /* deliberately not closed */
    }

    if (strncmp(req, "GET /profile", 12) == 0 ||
        strncmp(req, "GET /library", 12) == 0 ||
        strncmp(req, "GET /boards", 11) == 0 ||
        strncmp(req, "GET /game", 9) == 0) {
        char *body = malloc(512 * 1024);

        if (body != NULL) {
            int len;

            if (strncmp(req, "GET /profile", 12) == 0) {
                len = build_profile(body, 512 * 1024);
            } else if (strncmp(req, "GET /library", 12) == 0) {
                len = build_library(body, 512 * 1024);
            } else if (strncmp(req, "GET /boards", 11) == 0) {
                const char *q = strstr(req, "id=");

                len = build_leaderboards(body, 512 * 1024,
                                         q != NULL ? (unsigned)strtoul(q + 3, NULL, 10) : 0);
            } else {
                const char *q = strstr(req, "id=");

                len = build_game(body, 512 * 1024,
                                 q != NULL ? (unsigned)strtoul(q + 3, NULL, 10) : 0);
            }

            respond(c, "application/json; charset=utf-8", body, len);
            free(body);
        }
    } else if (strncmp(req, "GET /state", 10) == 0) {
        char *body = malloc(256 * 1024);

        if (body != NULL) {
            int len = build_state(body, 256 * 1024, client);

            respond(c, "application/json; charset=utf-8", body, len);
            free(body);
        }
    } else if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index", 10) == 0) {
        /* --ui-file: the page straight from disk, for working on it.
           Edit, refresh, see -- no re-embed, no rebuild. The shipped
           client never sets this and serves the built-in copy. */
        char *page = NULL;

        if (g_ui_file[0] != '\0') {
            FILE *f = fopen(g_ui_file, "rb");

            if (f != NULL) {
                long sz;

                fseek(f, 0, SEEK_END);
                sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (sz > 0 && sz < 4 * 1024 * 1024) {
                    page = malloc((size_t)sz + 1);
                    if (page != NULL) {
                        if ((long)fread(page, 1, (size_t)sz, f) == sz) {
                            page[sz] = '\0';
                        } else {
                            free(page);
                            page = NULL;
                        }
                    }
                }
                fclose(f);
            }
            if (page == NULL)
                log_warn("could not read %s; serving the built-in page", g_ui_file);
        }

        if (page != NULL) {
            respond(c, "text/html; charset=utf-8", page, (int)strlen(page));
            free(page);
        } else {
            respond(c, "text/html; charset=utf-8", ui_page_html, (int)strlen(ui_page_html));
        }
    } else {
        const char *nf = "not found";

        send_all(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\n", 63);
        send_all(c, nf, 9);
    }

    sock_close(c);
}

/* Server-sent events: one long-lived response written whenever the state
   moves; the browser reconnects by itself. Once per snapshot while a game
   runs. */
void webui_push(rc_client_t *client)
{
    static time_t last_full;
    char *body;
    char head[64];
    int len, n;
    time_t now;

    if (g_stream == SOCK_INVALID)
        return;

    body = malloc(256 * 1024);
    if (body == NULL)
        return;

    /* Full layout once a second and on anything structural; between
       those, one small delta per snapshot with just the moving
       numbers. */
    now = time(NULL);
    if (g.dirty || now - last_full >= 1) {
        len = build_state(body, 256 * 1024, client);
        last_full = now;
        g.dirty = 0;
    } else {
        len = build_delta(body, 256 * 1024, client);
    }
    n = snprintf(head, sizeof(head), "data: ");

    /* A dead page shows up as a failed write; drop it and let the
       browser reconnect on its own. */
    if (send(g_stream, head, n, 0) <= 0 ||
        send(g_stream, body, len, 0) <= 0 ||
        send(g_stream, "\n\n", 2, 0) <= 0) {
        sock_close(g_stream);
        g_stream = SOCK_INVALID;
    } else {
        g_stream_sent++;
    }

    free(body);
}

/* ---- Stream output ------------------------------------------------------ */

static char g_obs_dir[512];

void webui_set_obs_dir(const char *dir)
{
    snprintf(g_obs_dir, sizeof(g_obs_dir), "%s", dir != NULL ? dir : "");
}

static void obs_file(const char *name, const char *fmt, ...)
{
    char path[600];
    va_list ap;
    FILE *f;

#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\%s", g_obs_dir, name);
#else
    snprintf(path, sizeof(path), "%s/%s", g_obs_dir, name);
#endif

    f = fopen(path, "w");
    if (f == NULL)
        return;

    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

void webui_write_obs(rc_client_t *client)
{
    char path[600];
    char *body;
    FILE *f;
    unsigned total = 0, done = 0;

    if (g_obs_dir[0] == '\0')
        return;

    if (client != NULL) {
        rc_client_user_game_summary_t sum;

        rc_client_get_user_game_summary(client, &sum);
        total = sum.num_core_achievements;
        done = sum.num_unlocked_achievements;
    }

    /* One value per file: that is what an OBS text source wants, and it
       lets a layout pick exactly the pieces it needs. */
    obs_file("game.txt", "%s", g.title[0] != '\0' ? g.title : g.serial);
    obs_file("progress.txt", "%u / %u", done, total);
    obs_file("percent.txt", "%u%%", total > 0 ? done * 100 / total : 0);
    obs_file("last-unlock.txt", "%s", g.unlock_count > 0 ? g.unlocks[0].title : "");
    obs_file("last-points.txt", "%s", g.unlock_count > 0 ? "" : "");
    if (g.unlock_count > 0)
        obs_file("last-points.txt", "+%u", g.unlocks[0].points);
    obs_file("session.txt", "%ld min", g.started != 0 ? (long)(time(NULL) - g.started) / 60 : 0L);
    obs_file("console.txt", "%s", g.connected ? "connected" : "waiting");

    /* And the whole state, for a layout that would rather do its own
       arranging -- the same JSON the page gets. */
    body = malloc(256 * 1024);
    if (body == NULL)
        return;
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\data.json", g_obs_dir);
#else
    snprintf(path, sizeof(path), "%s/data.json", g_obs_dir);
#endif
    f = fopen(path, "w");
    if (f != NULL) {
        int len = build_state(body, 256 * 1024, client);

        fwrite(body, 1, (size_t)len, f);
        fclose(f);
    }
    free(body);
}

void webui_open_browser(int port)
{
    char url[64];

    snprintf(url, sizeof(url), "http://127.0.0.1:%d/", port);

#ifdef _WIN32
    {
        char args[128];

        /* ShellExecute, not system("start"): no cmd.exe in the middle and
           no console flashing past. Application mode (Edge, ships with
           Windows) first; otherwise the default browser. */
        snprintf(args, sizeof(args), "--app=%s --window-size=580,800", url);
        if ((INT_PTR)ShellExecuteA(NULL, "open", "msedge.exe", args, NULL, SW_SHOWNORMAL) <= 32)
            ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    }
#else
    {
        char cmd[512];

        snprintf(cmd, sizeof(cmd),
                 "(google-chrome --app=%s --window-size=580,800 || chromium --app=%s || xdg-open %s) >/dev/null 2>&1 &",
                 url, url, url);
        if (system(cmd) != 0)
            log_info("open %s in a browser", url);
    }
#endif

    log_info("interface at %s", url);
}
