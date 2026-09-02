/*
  xerabora: RetroAchievements client for a real PlayStation 2.

  The console runs a fork of Open PS2 Loader that reads game memory
  every frame and streams snapshots over UDP. This program receives
  them, feeds rcheevos, and lets rc_client talk to the RetroAchievements
  server: identification, the achievement set, unlocks.

  Memory is exposed to rcheevos sparsely: only the addresses in the
  watch list exist. Achievements that read through a pointer stay active
  but cannot unlock: such a read returns 0.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "badge_data.h"
#include "config.h"
#include "console.h"
#include "discord.h"
#include "http.h"
#include "log.h"
#include "platform.h"
#include "protocol.h"
#include "ra.h"
#include "raweb.h"
#ifdef XERABORA_GUI
#include "ui.h"
#endif
#include "snapshot.h"
#include "sound.h"
#include "version.h"
#include "watchlist.h"
#include "webui.h"

static void usage(void)
{
    printf("xerabora %s - RetroAchievements client for PlayStation 2 hardware\n"
           "\n"
           "usage: xerabora [options]\n"
           "  --user NAME        RetroAchievements username\n"
           "  --password PASS    password; prompted for when omitted. Visible to other\n"
           "                     processes; scripts should set XERABORA_PASSWORD instead\n"
           "  --logout           forget the saved login and exit\n"
           "  --api-key KEY      save the Web API key from your RA profile settings and exit\n"
           "  --library          print your library from the Web API and exit\n"
           "  --achievements ID  print a game's achievements from the Web API and exit\n"
           "  --leaderboards ID  print a game's leaderboards from the Web API and exit\n"
           "  --recent           print your recent unlocks from the Web API and exit\n"
           "  --window           open the window (window builds only)\n"
           "  --ui-port N        port for the interface page (default %d)\n"
           "  --no-ui            do not serve the interface page\n"
           "  --ui-only          serve the interface and nothing else, for working on it\n"
           "  --ui-file PATH     serve the page from this HTML file instead of the\n"
           "                     built-in copy: edit, refresh, see\n"
           "  --obs DIR          write stream labels and data.json into DIR as things change\n"
           "  --hashes ID        print the image hashes RA knows for a game and exit\n"
           "  --port N           UDP port to listen on (default %d)\n"
           "  --game SERIAL=HASH remember an image hash for a game serial\n"
           "  --no-sound         no notification sounds\n"
           "  --console          open a console window for this run (the log is always\n"
           "                     also in xerabora.log next to the saved login)\n"
           "  --trace            log every server request and rcheevos message\n"
           "  --test-unlock      send the console a fake unlock notice every 20 s, for bench runs\n"
           "  --badge <file>     8 KB raw 64x64 PSMCT16 badge to push to the console (lab builds)\n"
           "  --help             this text\n"
           "\n"
           "First run: just start it. The page opens by itself; sign in and paste\n"
           "your Web API key on its SETTINGS tab. Only the login token is saved.\n"
           "Sounds: connect.wav, disconnect.wav, achievement.wav in the 'sounds'\n"
           "folder next to the saved login replace the built-in ones.\n",
           XERABORA_VERSION, XERABORA_UI_PORT, XERABORA_DEFAULT_PORT);
}

/* Saved token only; nothing is asked unprompted. Most of the client works
   on the Web API key, so a missing login is a state, not a stop. Returns 1
   when logged in; --user asks for a password. */
static int login(rc_client_t *client, const char *arg_user, const char *arg_password)
{
    char user[128] = "", token[256] = "", password[256] = "";
    int rc;

    if (arg_user == NULL && config_load_credentials(user, sizeof(user), token, sizeof(token))) {
        log_detail("logging in as %s with the saved token", user);
        rc = ra_login_with_token(client, user, token);
        if (rc == RC_OK)
            return 1;
        if (rc != RC_INVALID_CREDENTIALS && rc != RC_EXPIRED_TOKEN && rc != RC_ACCESS_DENIED) {
            log_warn("RetroAchievements could not be reached; unlocks are off for now");
            return 0;
        }
        log_detail("the saved login was rejected");
        return 0;
    }

    if (arg_user == NULL) {
        log_detail("no saved login");
        return 0;
    }

    snprintf(user, sizeof(user), "%s", arg_user);

    if (arg_password == NULL)
        arg_password = getenv("XERABORA_PASSWORD");

    if (arg_password != NULL && arg_password[0] != '\0') {
        snprintf(password, sizeof(password), "%s", arg_password);
    } else {
        printf("Password: ");
        fflush(stdout);
        if (platform_read_password(password, sizeof(password)) != 0 || password[0] == '\0')
            return 0;
    }

    rc = ra_login_with_password(client, user, password);
    memset(password, 0, sizeof(password));
    if (rc != RC_OK)
        return 0;

    if (config_save_credentials(user, rc_client_get_user_info(client)->token) == 0) {
        char path[600];

        config_credentials_path(path, sizeof(path));
        log_info("login token saved to %s", path);
    } else {
        log_warn("could not save the login token; you will be asked again next time");
    }

    return 1;
}

/* Link to the console: up while telemetry keeps arriving. Discovery
   alone (the console found the PC) sounds the connect note but does not
   arm the timeout: the menu's link test sends discovery without any
   telemetry to follow. */
#define LINK_TIMEOUT_MS 5000

static int g_link_up = 0;
static unsigned long g_link_silent_ms = 0;
static time_t g_last_discovery = 0;

static void link_discovered(void)
{
    g_last_discovery = time(NULL);
    sound_play(SOUND_CONNECT);
}

static void link_seen(const char *ip)
{
    g_link_silent_ms = 0;
    if (!g_link_up) {
        g_link_up = 1;
        log_info("console connected");
        webui_set_console(ip, 1);
        /* The connect note already played at discovery a moment ago. */
        if (time(NULL) - g_last_discovery > 60)
            sound_play(SOUND_CONNECT);
    }
}

static void link_idle(unsigned long waited_ms)
{
    if (!g_link_up)
        return;
    g_link_silent_ms += waited_ms;
    if (g_link_silent_ms >= LINK_TIMEOUT_MS) {
        g_link_up = 0;
        log_warn("console connection lost: no packets for %d seconds", LINK_TIMEOUT_MS / 1000);
        webui_set_console(NULL, 0);
        sound_play(SOUND_DISCONNECT);
    }
}

/* Another xerabora serving its page on a loopback UI port? Only our own
   page answers /state with the console block; `skip` is this process's own
   listener. */
static int find_running_ui(int skip)
{
    int port;

    for (port = XERABORA_UI_PORT; port < XERABORA_UI_PORT + 5; port++) {
        struct sockaddr_in a;
        char buf[512];
        const char req[] = "GET /state HTTP/1.0\r\n\r\n";
        sock_t s;
        int n;

        if (port == skip)
            continue;
        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == SOCK_INVALID)
            return 0;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((unsigned short)port);
        if (connect(s, (struct sockaddr *)&a, sizeof(a)) != 0) {
            sock_close(s);
            continue;
        }
        send(s, req, sizeof(req) - 1, 0);
        n = (int)recv(s, buf, sizeof(buf) - 1, 0);
        sock_close(s);
        if (n > 0) {
            buf[n] = '\0';
            if (strstr(buf, "\"console\":{\"connected\"") != NULL)
                return port;
        }
    }
    return 0;
}

/* Set by Ctrl-C; the loop ends and queued unlocks get a chance to go out. */
static volatile int g_stop = 0;

/* Off by default: real unlocks notify the console through ra.c. The
   flag turns the repeating fake notice on for bench runs. */
static int g_test_unlock = 0;
static time_t g_test_unlock_at = 0;

static void test_unlock_tick(void)
{
    if (g_test_unlock_at == 0 || time(NULL) < g_test_unlock_at)
        return;
    /* Every 20 seconds for the whole run: the screen can be looked at
       at any time, rather than only when a single notice fired. */
    g_test_unlock_at = time(NULL) + 20;
    log_info("test: sending a fake unlock notice to the console");
    console_notify_unlock(0, 0);
}

/* The badge: 8 KB of raw 64x64 PSMCT16, pushed after the first snapshot one
   chunk per loop pass, three passes against loss. Only with --badge. */
#define BADGE_PASSES 3

static unsigned char g_badge[XERABORA_BADGE_BYTES];
static int g_badge_loaded = 0;
static int g_badge_next = -1; /* next chunk to send; -1 means idle */
static int g_badge_pass = 0;
static time_t g_badge_again = 0; /* when idle, restart the push at this time */

static void badge_load(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        log_warn("badge file %s cannot be opened; no badge this session", path);
        return;
    }
    if (fread(g_badge, 1, sizeof(g_badge), f) != sizeof(g_badge)) {
        log_warn("badge file %s is not %d bytes of raw PSMCT16; ignored",
                 path, XERABORA_BADGE_BYTES);
        fclose(f);
        return;
    }
    fclose(f);
    g_badge_loaded = 1;
    log_info("badge loaded from %s", path);
}

static void badge_start(void)
{
    if (!g_badge_loaded)
        return;
    g_badge_next = 0;
    g_badge_pass = 0;
    log_info("pushing the badge to the console (%d chunks, %d passes)",
             XERABORA_BADGE_CHUNKS, BADGE_PASSES);
}

static void badge_tick(void)
{
    static unsigned calls;

    if (g_badge_next < 0) {
        if (g_badge_again != 0 && time(NULL) >= g_badge_again)
            badge_start();
        return;
    }

    /* Every fourth pass: this loop runs per received packet, about 120
       a second on NFS, and chunks at that rate overflow the console's
       small receive queue. */
    if (++calls & 3)
        return;

    console_send_badge_chunk(g_badge, g_badge_next);
    g_badge_next++;

    if (g_badge_next >= XERABORA_BADGE_CHUNKS) {
        g_badge_pass++;
        g_badge_next = g_badge_pass < BADGE_PASSES ? 0 : -1;
        if (g_badge_next < 0)
            g_badge_again = time(NULL) + 30;
    }
}

/* ---- Web API commands: print and exit. They prove the Web API layer on
   its own, before any of it reaches the page. */

static int web_ready(void)
{
    char user[128] = "", token[256] = "", key[128] = "";

    if (!config_load_credentials(user, sizeof(user), token, sizeof(token))) {
        log_error("no saved login; run xerabora once and log in first");
        return 0;
    }
    if (!config_load_apikey(key, sizeof(key))) {
        log_error("no Web API key saved; get it from your RA profile settings "
                  "(the Keys section) and pass --api-key KEY once");
        return 0;
    }
    raweb_set_credentials(user, key);
    return 1;
}

static int cmd_library(void)
{
    struct raweb_profile me;
    struct raweb_game *games;
    int n, i;

    if (!web_ready())
        return 1;

    if (raweb_profile(&me)) {
        printf("%s -- %u points", me.user, me.points);
        if (me.rank > 0)
            printf(", rank %u of %u", me.rank, me.total_ranked);
        printf("\n");
        if (me.rich_presence[0] != '\0')
            printf("now: %s\n", me.rich_presence);
        printf("\n");
    }

    games = calloc(500, sizeof(*games));
    if (games == NULL)
        return 1;

    n = raweb_completion_progress(games, 500);
    printf("%d games with achievements\n\n", n);
    for (i = 0; i < n; i++) {
        printf("%6u  %-46.46s %-16.16s %3u/%-3u%s\n",
               games[i].id, games[i].title, games[i].console,
               games[i].awarded, games[i].achievements,
               games[i].mastered ? "  mastered" : "");
    }

    free(games);
    return n > 0 ? 0 : 1;
}

static int cmd_game(unsigned game_id)
{
    struct raweb_game_progress info;
    struct raweb_achievement *rows;
    unsigned *ids, *secs;
    int n, m, i, j;

    if (!web_ready())
        return 1;

    rows = calloc(600, sizeof(*rows));
    ids = calloc(600, sizeof(*ids));
    secs = calloc(600, sizeof(*secs));
    if (rows == NULL || ids == NULL || secs == NULL) {
        free(rows);
        free(ids);
        free(secs);
        return 1;
    }

    n = raweb_game_progress(game_id, &info, rows, 600);
    if (n == 0) {
        log_error("nothing came back for game %u", game_id);
        free(rows);
        free(ids);
        free(secs);
        return 1;
    }

    printf("%s (%s) -- %u of %u unlocked\n\n",
           info.title, info.console, info.awarded, info.achievements_total);

    /* The median unlock time is the order players get them in,
       which is the one worth sorting "what to chase next" by. */
    m = raweb_median_times(game_id, ids, secs, 600);

    for (i = 0; i < n; i++) {
        unsigned median = 0;

        for (j = 0; j < m; j++)
            if (ids[j] == rows[i].id) {
                median = secs[j];
                break;
            }

        printf("%3u %-40.40s %3u pts  %-12.12s %-9s",
               rows[i].display_order, rows[i].title, rows[i].points,
               rows[i].type, rows[i].date_earned[0] ? "unlocked" : "locked");
        if (median > 0)
            printf("  median %uh%02u", median / 3600, (median % 3600) / 60);
        printf("\n");
    }

    free(rows);
    free(ids);
    free(secs);
    return 0;
}

static int cmd_leaderboards(unsigned game_id)
{
    struct raweb_leaderboard *rows;
    int n, i;

    if (!web_ready())
        return 1;

    rows = calloc(300, sizeof(*rows));
    if (rows == NULL)
        return 1;

    n = raweb_game_leaderboards(game_id, rows, 300);
    printf("%d leaderboards\n\n", n);
    for (i = 0; i < n; i++) {
        printf("%7u %-44.44s %-10.10s %s", rows[i].id, rows[i].title,
               rows[i].format, rows[i].lower_is_better ? "asc " : "desc");
        if (rows[i].top_user[0] != '\0')
            printf("  top %s by %s", rows[i].top_score, rows[i].top_user);
        printf("\n");
    }

    free(rows);
    return n > 0 ? 0 : 1;
}

static int cmd_recent(void)
{
    struct raweb_unlock *rows;
    int n, i;

    if (!web_ready())
        return 1;

    rows = calloc(100, sizeof(*rows));
    if (rows == NULL)
        return 1;

    /* A week back: enough to see something on an account that is not
       played daily. */
    n = raweb_recent_unlocks(60 * 24 * 7, rows, 100);
    printf("%d unlocks in the last week\n\n", n);
    for (i = 0; i < n; i++)
        printf("%-20.20s %-36.36s %3u pts  %s%s\n", rows[i].date, rows[i].title,
               rows[i].points, rows[i].game_title, rows[i].hardcore ? "  [hardcore]" : "");

    free(rows);
    return 0;
}

static int cmd_hashes(unsigned game_id)
{
    char (*md5)[33];
    char (*names)[96];
    int n, i;

    if (!web_ready())
        return 1;

    md5 = calloc(64, sizeof(*md5));
    names = calloc(64, sizeof(*names));
    if (md5 == NULL || names == NULL) {
        free(md5);
        free(names);
        return 1;
    }

    n = raweb_game_hashes(game_id, md5, names, 64);
    printf("%d image%s known to RetroAchievements\n\n", n, n == 1 ? "" : "s");
    for (i = 0; i < n; i++)
        printf("%s  %s\n", md5[i], names[i]);

    free(md5);
    free(names);
    return n > 0 ? 0 : 1;
}

/* Four startup lines: each thing working, or the one sentence on how to
   make it work. Detail lives in the log file. */
static void startup_summary(int ui_ok, int ui_port, int signed_in, const char *user,
                            int have_key, int console_ok, int console_port)
{
    printf("\n  xerabora %s\n\n", XERABORA_VERSION);

    if (ui_ok)
        printf("  interface   http://127.0.0.1:%d/  (opening in your browser)\n", ui_port);
    else
        printf("  interface   off\n");

    if (signed_in)
        printf("  account     %s\n", user);
    else
        printf("  account     not signed in -- unlocks are off.\n"
               "              sign in on the page's SETTINGS tab\n");

    if (have_key)
        printf("  web api     key saved\n");
    else
        printf("  web api     no key -- your library and leaderboards stay empty.\n"
               "              get one at retroachievements.org/settings (Keys)\n"
               "              and paste it on the page's SETTINGS tab\n");

    if (console_ok)
        printf("  console     listening on UDP %d\n", console_port);
    else
        printf("  console     UDP %d is busy -- another xerabora is already running.\n"
               "              close the other one, or start this one with --port %d\n",
               console_port, console_port + 1);

    printf("\n");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *arg_user = NULL, *arg_password = NULL;
    int port = XERABORA_DEFAULT_PORT, sounds = 1;
    int ui_port = XERABORA_UI_PORT, ui_wanted = 1;
    sock_t ui = SOCK_INVALID;
    int i, want_console = 0, ui_only = 0;

    /* Window-subsystem build: a double click opens the page and no console.
       Attach to the terminal this was started from so text commands answer
       there; --console opens one regardless. */
    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "--console") == 0)
            want_console = 1;
    platform_attach_console(want_console);
    platform_console_init();
    rc_client_t *client;
    sock_t sock;
    struct sockaddr_in addr;
    int rcvbuf = 1 << 20;
    char cur_serial[16] = "";
    int game_up = 0, seen_pkts = 0, said_first = 0, said_stale = 0;
    unsigned char pkt[4096];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            arg_user = argv[++i];
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            arg_password = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            char tmp[64], *eq;

            snprintf(tmp, sizeof(tmp), "%s", argv[++i]);
            eq = strchr(tmp, '=');
            if (eq != NULL) {
                *eq = '\0';
                console_remember_game(tmp, eq + 1);
            }
        } else if (strcmp(argv[i], "--test-unlock") == 0) {
            g_test_unlock = 1;
        } else if (strcmp(argv[i], "--badge") == 0 && i + 1 < argc) {
            badge_load(argv[++i]);
        } else if (strcmp(argv[i], "--trace") == 0) {
            log_set_trace(1);
        } else if (strcmp(argv[i], "--ui-port") == 0 && i + 1 < argc) {
            ui_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--obs") == 0 && i + 1 < argc) {
            webui_set_obs_dir(argv[++i]);
        } else if (strcmp(argv[i], "--no-ui") == 0) {
            ui_wanted = 0;
        } else if (strcmp(argv[i], "--ui-only") == 0) {
            ui_only = 1; /* runs after the loop, so flag order is free */
        } else if (strcmp(argv[i], "--ui-file") == 0 && i + 1 < argc) {
            webui_set_ui_file(argv[++i]);
        } else if (strcmp(argv[i], "--no-sound") == 0) {
            sounds = 0;
        } else if (strcmp(argv[i], "--api-key") == 0 && i + 1 < argc) {
            if (config_save_apikey(argv[++i]) != 0) {
                printf("could not save the Web API key\n");
                return 1;
            }
            printf("Web API key saved\n");
            return 0;
        } else if (strcmp(argv[i], "--library") == 0) {
            if (http_init() != 0)
                return 1;
            return cmd_library();
        } else if (strcmp(argv[i], "--achievements") == 0 && i + 1 < argc) {
            if (http_init() != 0)
                return 1;
            return cmd_game((unsigned)strtoul(argv[++i], NULL, 10));
        } else if (strcmp(argv[i], "--leaderboards") == 0 && i + 1 < argc) {
            if (http_init() != 0)
                return 1;
            return cmd_leaderboards((unsigned)strtoul(argv[++i], NULL, 10));
        } else if (strcmp(argv[i], "--recent") == 0) {
            if (http_init() != 0)
                return 1;
            return cmd_recent();
        } else if (strcmp(argv[i], "--hashes") == 0 && i + 1 < argc) {
            if (http_init() != 0)
                return 1;
            return cmd_hashes((unsigned)strtoul(argv[++i], NULL, 10));
        } else if (strcmp(argv[i], "--window") == 0) {
#ifdef XERABORA_GUI
            if (http_init() != 0)
                return 1;
            web_ready(); /* the window works without a key, with less in it */
            return ui_run();
#else
            printf("this build has no window; build with `make windows-gui`\n");
            return 2;
#endif
        } else if (strcmp(argv[i], "--logout") == 0) {
            config_forget_credentials();
            printf("saved login removed\n");
            return 0;
        } else if (strcmp(argv[i], "--console") == 0) {
            /* Already handled before the loop. */
        } else {
            usage();
            return strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }

    if (ui_only) {
        /* The page, with no login and no console: what the design
           work needs, and a way to check the server on its own. */
        sock_t only;

        if (platform_net_init() != 0 || http_init() != 0)
            return 1;
        /* The page shows the account's library and games, so the
           Web API credentials matter even with no console and no
           login to the game server. */
        web_ready();
        {
            char k[128];

            webui_set_webapi(config_load_apikey(k, sizeof(k)));
        }
        only = webui_start(XERABORA_UI_PORT);
        if (only == SOCK_INVALID)
            return 1;
        webui_open_browser(webui_port());
        platform_on_stop(&g_stop);
        while (!g_stop && !webui_quit_requested()) {
            if (platform_wait_readable(only, 500) == 1)
                webui_serve(only, NULL);
        }
        webui_stop(only);
        return 0;
    }

    if (port <= 0 || port > 65535)
        port = XERABORA_DEFAULT_PORT;

    setvbuf(stdout, NULL, _IOLBF, 0);
    platform_console_init();

    /* A copy of everything into the config directory: when the console
       window is not the thing the user is looking at, this is where
       the answer to "it did not work" lives. */
    {
        char dir[512], path[600];

        if (platform_config_dir(dir, sizeof(dir)) == 0) {
#ifdef _WIN32
            snprintf(path, sizeof(path), "%s\\xerabora.log", dir);
#else
            snprintf(path, sizeof(path), "%s/xerabora.log", dir);
#endif
            log_to_file(path);
        }
    }

    log_detail("xerabora %s", XERABORA_VERSION);

    if (platform_net_init() != 0 || http_init() != 0) {
        log_error("network initialisation failed");
        return 1;
    }

    client = ra_create();
    if (client == NULL) {
        log_error("rc_client_create failed");
        return 1;
    }

    /* The interface comes up before the login on purpose: a rejected token
       used to end the program here, which looked like a program that does
       not open. */
    if (ui_wanted) {
        ui = webui_start(ui_port);
        if (ui != SOCK_INVALID) {
            ui_port = webui_port();
            webui_open_browser(ui_port);
        }
    }

    if (login(client, arg_user, arg_password)) {
        const rc_client_user_t *me = rc_client_get_user_info(client);

        webui_set_login(1, me != NULL ? me->display_name : "");
    } else {
        webui_set_login(0, "");
    }

    /* The Web API key is what the library and game screens run on, and
       it is independent of the login above. */
    {
        char wu[128] = "", wt[256] = "", wk[128] = "";

        if (config_load_apikey(wk, sizeof(wk))) {
            if (!config_load_credentials(wu, sizeof(wu), wt, sizeof(wt)) && arg_user != NULL)
                snprintf(wu, sizeof(wu), "%s", arg_user);
            raweb_set_credentials(wu, wk);
            webui_set_webapi(1);
        } else {
            log_detail("no Web API key saved");
            webui_set_webapi(0);
        }
    }

    console_load_games();

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (sock == SOCK_INVALID || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        /* Almost always another copy still running. The console half is off
           for this run, said plainly; the interface and the library keep
           working. */
        log_detail("bind on UDP %d failed: %s", port, platform_sock_error());
        if (sock != SOCK_INVALID) {
            sock_close(sock);
            sock = SOCK_INVALID;
        }

        /* Almost always another copy. A double click then means "show me
           the client": open the running copy's page and leave. */
        {
            int other = find_running_ui(ui != SOCK_INVALID ? webui_port() : 0);

            if (other > 0) {
                log_info("xerabora is already running; opening its page and leaving");
                webui_open_browser(other);
                if (ui != SOCK_INVALID)
                    webui_stop(ui);
                ra_destroy(client);
                return 0;
            }
        }
        webui_set_console("port busy", 0);

        /* With no interface either there is nothing left to do, so say
           why in plain words before going: this is the message someone
           reads after a window closed on them. */
        if (ui == SOCK_INVALID) {
            printf("\n  xerabora cannot start.\n\n"
                   "  UDP port %d is already in use, so the console has no way to\n"
                   "  reach this copy. Another xerabora is almost certainly still\n"
                   "  running -- close it, or start this one with --port %d.\n\n",
                   port, port + 1);
            fflush(stdout);
            ra_destroy(client);
            return 1;
        }
    }

    /* Server requests block this loop for a moment; a large receive
       buffer keeps the frames that arrive meanwhile. */
    if (sock != SOCK_INVALID) {
        int on = 1;

        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&rcvbuf, sizeof(rcvbuf));
        /* Pushes to the console also go to the subnet broadcast: in play
           the console stops answering ARP and unicasts to it die on the PC. */
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
    }

    sound_init(sounds);
    discord_init();
    platform_on_stop(&g_stop);

    {
        char key_probe[128] = "";
        const rc_client_user_t *me = rc_client_get_user_info(client);

        startup_summary(ui != SOCK_INVALID, ui_port,
                        me != NULL && me->display_name != NULL, me != NULL ? me->display_name : "",
                        config_load_apikey(key_probe, sizeof(key_probe)),
                        sock != SOCK_INVALID, port);
    }
    if (sock != SOCK_INVALID)
        log_detail("listening on UDP port %d", port);

    while (!g_stop && !webui_quit_requested()) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        const char *head = (const char *)pkt;
        char serial[16];
        const char *hash;
        int n, served, ready;

        ready = platform_wait_readable2(sock, ui, 1000);
        if (ready < 0) {
            platform_sleep_ms(1000);
            continue;
        }
        if (ready & 2) {
            webui_serve(ui, client);
            if (!(ready & 1))
                continue;
        }
        if (ready == 0) {
            link_idle(1000);
            rc_client_idle(client); /* retries of queued unlocks */
            discord_tick();
            test_unlock_tick();
            badge_tick();
            continue;
        }
        test_unlock_tick();
        badge_tick();

        n = (int)recvfrom(sock, (char *)pkt, sizeof(pkt) - 1, 0, (struct sockaddr *)&from, &fromlen);
        if (n <= 0)
            continue;
        pkt[n] = '\0';

        served = console_serve(sock, head, (size_t)n, &from, client);
        if (served == 2) {
            link_discovered();
            /* Discovery comes from a game that has just started. Hit
               counts and deltas from the previous run must not carry
               over, the same as an emulator reset. */
            if (game_up) {
                rc_client_reset(client);
                snapshot_reset();
                said_first = 0;
            }
        }
        if (served)
            continue;

        link_seen(inet_ntoa(from.sin_addr));
        console_learn(sock, &from);
        webui_note_packet();

        /* No set loaded, but the console is talking: let the page show
           a live console anyway, at a walking pace. When a game is up
           the per-snapshot push below takes over. */
        if (!game_up) {
            static time_t last_idle_push;
            time_t now = time(NULL);

            if (now != last_idle_push) {
                last_idle_push = now;
                webui_push(client);
            }
        }

        if (seen_pkts < 3)
            log_trace("packet %d: %.160s", seen_pkts, head);
        seen_pkts++;

        if (!snapshot_serial(head, serial, sizeof(serial)))
            continue;

        /* (Re)load when the game changes, and also when an image check
           from the menu loaded another game in between. */
        hash = console_hash_for(serial);
        if (strcmp(serial, cur_serial) != 0 ||
            (hash != NULL && strcmp(hash, ra_loaded_hash()) != 0)) {
            snprintf(cur_serial, sizeof(cur_serial), "%s", serial);
            log_info("console started %s", serial);
            webui_set_game(serial, console_hash_for(serial), NULL);
            discord_set(serial, "on a real PlayStation 2", NULL);
            webui_write_obs(client);
            snapshot_reset();
            said_first = 0;
            said_stale = 0;

            if (hash == NULL) {
                log_warn("no image hash known for %s: choose 'RA: check game support' "
                         "in the game's menu, or pass --game %s=HASH", serial, serial);
                game_up = 0;
                webui_set_status("no-hash");
            } else {
                game_up = ra_load_game(client, hash) && watchlist_build(client);
                webui_set_status(game_up ? "active" : "telemetry-only");
            }
            if (!game_up)
                log_warn("receiving telemetry only, no achievements are tracked");
        }

        if (!game_up)
            continue;

        if (!snapshot_feed(head, (size_t)n)) {
            if (snapshot_stale() && !said_stale) {
                said_stale = 1;
                log_warn("the console's watch list does not match the current achievement set; "
                         "run 'RA: check game support' again");
                webui_set_status("stale");
            }
            continue;
        }

        if (!said_first) {
            const struct snapshot_stats *st = snapshot_stats();

            said_first = 1;
            log_info("first complete snapshot: %u parts, %u bytes, %u addresses",
                     st->parts, st->bytes, st->count);
            ra_log_summary(client);
            /* Only now is the console provably listening on the socket the
               notice goes to; on discovery it is still in the game's boot. */
            badge_start();
            if (g_test_unlock)
                g_test_unlock_at = time(NULL) + 30;
            {
                char title[64] = "";
                unsigned total = 0, unlocked = 0, unsupported = 0;
                char detail[96];

                if (ra_game_summary(client, title, sizeof(title), &total, &unlocked, &unsupported)) {
                    webui_set_game(cur_serial, NULL, title);
                    snprintf(detail, sizeof(detail), "%u of %u achievements", unlocked, total);
                    discord_set(title, detail, NULL);
                }
            }
        }

        rc_client_do_frame(client);

        {
            const struct snapshot_stats *st = snapshot_stats();

            webui_set_stats(st->frames, st->gaps, st->dupes, st->torn,
                            st->parts, st->bytes, st->count);
            /* One push per snapshot: the page moves with the game. */
            webui_push(client);
            if ((st->frames % 3600) == 0)
                log_info("%lu snapshots | %lu skipped by console | %lu repeated | %lu incomplete | console rx fifo %u (max %u)",
                         st->frames, st->gaps, st->dupes, st->torn, st->rxq, st->rxq_max);
        }
    }

    /* Give queued server requests (an unlock that failed to send) a
       moment before exiting. */
    log_info("stopping");
    for (i = 0; i < 20; i++) {
        rc_client_idle(client);
        platform_sleep_ms(100);
    }

    discord_shutdown();
    webui_stop(ui);
    if (sock != SOCK_INVALID)
        sock_close(sock);
    ra_destroy(client);
    http_shutdown();
    platform_net_shutdown();
    return 0;
}
