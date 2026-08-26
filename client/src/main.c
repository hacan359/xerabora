/*
  ps2ra: RetroAchievements client for a real PlayStation 2.

  The console runs a fork of Open PS2 Loader that reads game memory
  every frame and streams snapshots over UDP. This program receives
  them, feeds rcheevos, and lets rc_client talk to the RetroAchievements
  server: identification, the achievement set, unlocks.

  Memory is exposed to rcheevos sparsely: only the addresses in the
  watch list exist. Achievements that read anything else (pointer
  chains) are disabled after the game loads.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "console.h"
#include "http.h"
#include "log.h"
#include "platform.h"
#include "protocol.h"
#include "ra.h"
#include "snapshot.h"
#include "sound.h"
#include "version.h"
#include "watchlist.h"

static void usage(void)
{
    printf("ps2ra %s - RetroAchievements client for PlayStation 2 hardware\n"
           "\n"
           "usage: ps2ra [options]\n"
           "  --user NAME        RetroAchievements username\n"
           "  --password PASS    password; prompted for when omitted. Visible to other\n"
           "                     processes; scripts should set PS2RA_PASSWORD instead\n"
           "  --logout           forget the saved login and exit\n"
           "  --port N           UDP port to listen on (default %d)\n"
           "  --game SERIAL=HASH remember an image hash for a game serial\n"
           "  --no-sound         no notification sounds\n"
           "  --trace            log every server request and rcheevos message\n"
           "  --help             this text\n"
           "\n"
           "On the first run ps2ra asks for your username and password. Only the\n"
           "login token is saved, in your user profile.\n"
           "Sounds: connect.wav, disconnect.wav, achievement.wav in the 'sounds'\n"
           "folder next to the saved login replace the built-in ones.\n",
           PS2RA_VERSION, PS2RA_DEFAULT_PORT);
}

static int prompt(const char *what, char *out, size_t size)
{
    printf("%s: ", what);
    fflush(stdout);
    if (fgets(out, (int)size, stdin) == NULL)
        return -1;
    out[strcspn(out, "\r\n")] = '\0';
    return out[0] ? 0 : -1;
}

/* Saved token first. When the server rejects it, ask for the username
   and password and save the token the server returns. When the server
   cannot be reached, say so and stop: a password would not help. */
static int login(rc_client_t *client, const char *arg_user, const char *arg_password)
{
    char user[128] = "", token[256] = "", password[256] = "";
    int rc;

    if (arg_user == NULL && config_load_credentials(user, sizeof(user), token, sizeof(token))) {
        log_info("logging in as %s with the saved token", user);
        rc = ra_login_with_token(client, user, token);
        if (rc == RC_OK)
            return 1;
        if (rc != RC_INVALID_CREDENTIALS && rc != RC_EXPIRED_TOKEN && rc != RC_ACCESS_DENIED) {
            log_error("RetroAchievements could not be reached; check the network and try again");
            return 0;
        }
        log_warn("saved token rejected, please log in again");
    }

    if (arg_user != NULL)
        snprintf(user, sizeof(user), "%s", arg_user);
    else if (prompt("RetroAchievements username", user, sizeof(user)) != 0)
        return 0;

    if (arg_password == NULL)
        arg_password = getenv("PS2RA_PASSWORD");

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

static void link_seen(void)
{
    g_link_silent_ms = 0;
    if (!g_link_up) {
        g_link_up = 1;
        log_info("console connected");
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
        sound_play(SOUND_DISCONNECT);
    }
}

/* Set by Ctrl-C; the loop ends and queued unlocks get a chance to go out. */
static volatile int g_stop = 0;

int main(int argc, char **argv)
{
    const char *arg_user = NULL, *arg_password = NULL;
    int port = PS2RA_DEFAULT_PORT, sounds = 1;
    int i;
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
        } else if (strcmp(argv[i], "--trace") == 0) {
            log_set_trace(1);
        } else if (strcmp(argv[i], "--no-sound") == 0) {
            sounds = 0;
        } else if (strcmp(argv[i], "--logout") == 0) {
            config_forget_credentials();
            printf("saved login removed\n");
            return 0;
        } else {
            usage();
            return strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }

    if (port <= 0 || port > 65535)
        port = PS2RA_DEFAULT_PORT;

    setvbuf(stdout, NULL, _IOLBF, 0);
    platform_console_init();
    log_info("ps2ra %s", PS2RA_VERSION);

    if (platform_net_init() != 0 || http_init() != 0) {
        log_error("network initialisation failed");
        return 1;
    }

    client = ra_create();
    if (client == NULL) {
        log_error("rc_client_create failed");
        return 1;
    }

    if (!login(client, arg_user, arg_password)) {
        ra_destroy(client);
        return 1;
    }

    console_load_games();

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (sock == SOCK_INVALID || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("cannot listen on UDP port %d: %s", port, platform_sock_error());
        ra_destroy(client);
        return 1;
    }

    /* Server requests block this loop for a moment; a large receive
       buffer keeps the frames that arrive meanwhile. */
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&rcvbuf, sizeof(rcvbuf));

    sound_init(sounds);
    platform_on_stop(&g_stop);
    log_info("listening on UDP port %d, waiting for the console", port);

    while (!g_stop) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        const char *head = (const char *)pkt;
        char serial[16];
        const char *hash;
        int n, served, ready;

        ready = platform_wait_readable(sock, 1000);
        if (ready < 0) {
            platform_sleep_ms(1000);
            continue;
        }
        if (ready == 0) {
            link_idle(1000);
            rc_client_idle(client); /* retries of queued unlocks */
            continue;
        }

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

        link_seen();

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
            snapshot_reset();
            said_first = 0;
            said_stale = 0;

            if (hash == NULL) {
                log_warn("no image hash known for %s: choose 'RA: check game support' "
                         "in the game's menu, or pass --game %s=HASH", serial, serial);
                game_up = 0;
            } else {
                game_up = ra_load_game(client, hash) && watchlist_build(client);
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
            }
            continue;
        }

        if (!said_first) {
            const struct snapshot_stats *st = snapshot_stats();

            said_first = 1;
            log_info("first complete snapshot: %u parts, %u bytes, %u addresses",
                     st->parts, st->bytes, st->count);
            ra_log_summary(client);
        }

        rc_client_do_frame(client);

        {
            const struct snapshot_stats *st = snapshot_stats();

            if ((st->frames % 3600) == 0)
                log_info("%lu snapshots | %lu skipped by console | %lu repeated | %lu incomplete",
                         st->frames, st->gaps, st->dupes, st->torn);
        }
    }

    /* Give queued server requests (an unlock that failed to send) a
       moment before exiting. */
    log_info("stopping");
    for (i = 0; i < 20; i++) {
        rc_client_idle(client);
        platform_sleep_ms(100);
    }

    sock_close(sock);
    ra_destroy(client);
    http_shutdown();
    platform_net_shutdown();
    return 0;
}
