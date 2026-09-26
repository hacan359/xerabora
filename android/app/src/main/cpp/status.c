/*
  What the notification shows, taken from the calls the client already
  makes to fill its page: the console link, the game, the pipeline
  status, each unlock, and the per-snapshot push. All are defined in
  webui.c / console.c and called from main.c, ra.c and follow.c, so a
  link-time wrap (CMakeLists.txt) sees every one without touching them.

  Everything here runs on the client's main thread, the one that owns
  rc_client, and never while the identification worker loads a set.
*/
#include "glue.h"

#include "console.h"
#include "rc_client.h"
#include "webui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Declared with the header's own types: a signature change upstream
   breaks this build instead of silently miscalling. */
__typeof__(webui_set_console) __real_webui_set_console, __wrap_webui_set_console;
__typeof__(webui_set_game) __real_webui_set_game, __wrap_webui_set_game;
__typeof__(webui_set_game_title) __real_webui_set_game_title, __wrap_webui_set_game_title;
__typeof__(webui_set_status) __real_webui_set_status, __wrap_webui_set_status;
__typeof__(webui_note_unlock) __real_webui_note_unlock, __wrap_webui_note_unlock;
__typeof__(webui_push) __real_webui_push, __wrap_webui_push;
__typeof__(console_serve) __real_console_serve, __wrap_console_serve;

#define BADGE_URL "https://media.retroachievements.org/Badge/%s.png"
/* Rich presence moves every few frames; the notification needs less. */
#define PRESENCE_EVERY 5

static struct {
    rc_client_t *client;
    int logged_in;
    int connected;
    int port_busy;
    char serial[16];
    char status[24];
    time_t next_check;
    time_t last_sent;
    char sent[768];
    char sent_presence[256];
} g = {.logged_in = 1}; /* until rc_client says otherwise: no false "sign in" */

/* The loaded set is the console's game: main.c says "active" (or "stale",
   a set that loaded but no longer matches the console's watch list). */
static int set_loaded(void)
{
    return g.client != NULL && !console_ident_busy() &&
           (strcmp(g.status, "active") == 0 || strcmp(g.status, "stale") == 0);
}

static void summary(unsigned *unlocked, unsigned *total)
{
    rc_client_user_game_summary_t sum;

    *unlocked = *total = 0;
    if (!set_loaded())
        return;
    rc_client_get_user_game_summary(g.client, &sum);
    *unlocked = sum.num_unlocked_achievements;
    *total = sum.num_core_achievements;
}

static void report(int force)
{
    struct glue_status s;
    char key[sizeof(g.sent)];
    time_t now = time(NULL);

    memset(&s, 0, sizeof(s));
    s.connected = g.connected;
    s.port_busy = g.port_busy;
    s.serial = g.serial;
    s.status = g.status;
    s.title = "";
    s.image = "";
    s.presence = "";

    /* While a set loads the worker owns rc_client: keep the last answer. */
    if (g.client != NULL && !console_ident_busy())
        g.logged_in = rc_client_get_user_info(g.client) != NULL;
    s.logged_in = g.logged_in;

    if (set_loaded()) {
        const rc_client_game_t *game = rc_client_get_game_info(g.client);
        static char image[256];
        static char presence[256];

        if (game != NULL && game->id != 0) {
            rc_client_user_game_summary_t sum;

            rc_client_get_user_game_summary(g.client, &sum);
            s.game_id = game->id;
            s.title = game->title != NULL ? game->title : "";
            if (rc_client_game_get_image_url(game, image, sizeof(image)) == RC_OK)
                s.image = image;
            s.unlocked = sum.num_unlocked_achievements;
            s.total = sum.num_core_achievements;
            s.points_unlocked = sum.points_unlocked;
            s.points_total = sum.points_core;
            presence[0] = '\0';
            if (rc_client_has_rich_presence(g.client))
                rc_client_get_rich_presence_message(g.client, presence, sizeof(presence));
            s.presence = presence;
        }
    }

    snprintf(key, sizeof(key), "%d|%d|%d|%s|%s|%u|%s|%s|%u|%u|%u|%u", s.connected, s.port_busy,
             s.logged_in, s.serial, s.status, s.game_id, s.title, s.image, s.unlocked, s.total,
             s.points_unlocked, s.points_total);
    if (!force && strcmp(key, g.sent) == 0 &&
        (strcmp(s.presence, g.sent_presence) == 0 || now - g.last_sent < PRESENCE_EVERY))
        return;

    snprintf(g.sent, sizeof(g.sent), "%s", key);
    snprintf(g.sent_presence, sizeof(g.sent_presence), "%s", s.presence);
    g.last_sent = now;
    glue_status(&s);
}

void __wrap_webui_set_console(const char *ip, int connected)
{
    __real_webui_set_console(ip, connected);
    /* main.c's way of saying UDP 18194 is taken by another program. */
    g.port_busy = !connected && ip != NULL && strcmp(ip, "port busy") == 0;
    g.connected = connected;
    report(1);
}

void __wrap_webui_set_game(const char *serial, const char *hash, const char *title)
{
    __real_webui_set_game(serial, hash, title);
    snprintf(g.serial, sizeof(g.serial), "%s", serial != NULL ? serial : "");
    g.status[0] = '\0';
    report(1);
}

void __wrap_webui_set_game_title(const char *title)
{
    __real_webui_set_game_title(title);
    report(1);
}

void __wrap_webui_set_status(const char *status)
{
    __real_webui_set_status(status);
    snprintf(g.status, sizeof(g.status), "%s", status != NULL ? status : "");
    report(0);
}

void __wrap_webui_note_unlock(unsigned id, const char *title, const char *badge, unsigned points)
{
    const rc_client_achievement_t *ach = NULL;
    char url[256] = "";
    unsigned unlocked, total;

    __real_webui_note_unlock(id, title, badge, points);
    if (id >= WEBUI_WARNING_ACH_ID)
        return;

    if (g.client != NULL && !console_ident_busy())
        ach = rc_client_get_achievement_info(g.client, id);
    if (ach == NULL ||
        rc_client_achievement_get_image_url(ach, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED, url, sizeof(url)) != RC_OK) {
        if (badge != NULL && badge[0] != '\0')
            snprintf(url, sizeof(url), BADGE_URL, badge);
    }
    summary(&unlocked, &total);
    glue_unlock(id, title != NULL ? title : "", ach != NULL && ach->description != NULL ? ach->description : "",
                url, points, unlocked, total);
    report(1);
}

void __wrap_webui_push(rc_client_t *client)
{
    time_t now;

    __real_webui_push(client);
    if (client != NULL)
        g.client = client;
    /* Once per snapshot while a game runs, once a second otherwise:
       look at most once a second. */
    now = time(NULL);
    if (now >= g.next_check) {
        g.next_check = now + 1;
        report(0);
    }
}

int __wrap_console_serve(sock_t sock, const char *pkt, size_t len,
                         const struct sockaddr_in *from, rc_client_t *client)
{
    int r = __real_console_serve(sock, pkt, len, from, client);

    /* RAP1 <console-ip> <port>: the console found this client ("RA: test PC
       connection", or a game with a set starting). */
    if (r == 2) {
        char ip[32] = "";

        if (sscanf(pkt + 5, "%31s", ip) != 1)
            ip[0] = '\0';
        glue_discovery(ip);
    }
    return r;
}

/* The console's "RA: check game support" gets its answer as RAA1 OK or NO
   (PROTOCOL.md); the phone shows the same answer. The reply is padded
   with spaces. */
static void trim(char *s)
{
    size_t n = strlen(s);

    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\0' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
}

void status_datagram(const void *buf, size_t len)
{
    char msg[256];

    if (len > 8 && len < sizeof(msg) && memcmp(buf, "RAA1 ", 5) == 0) {
        memcpy(msg, buf, len);
        msg[len] = '\0';
        trim(msg);
        if (strncmp(msg, "RAA1 OK ", 8) == 0) {
            unsigned total = 0, unlocked = 0, unsupported = 0;
            int bytes, chunks, used = 0;

            if (sscanf(msg + 8, "%d %d %u %u %u %n", &bytes, &chunks, &total, &unlocked, &unsupported,
                       &used) >= 5 && used > 0)
                glue_check(1, msg + 8 + used, total, unlocked, unsupported, "");
            else
                glue_check(1, "", 0, 0, 0, "");
        } else if (strncmp(msg, "RAA1 NO", 7) == 0) {
            glue_check(0, "", 0, 0, 0, msg[7] == ' ' ? msg + 8 : "");
        }
    }
}
