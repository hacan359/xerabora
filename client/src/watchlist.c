#include "watchlist.h"

#include <string.h>

#include "log.h"
#include "protocol.h"

/* Internal rcheevos headers: the memref pool of the loaded game is the
   only place that knows which addresses the achievement set reads, and
   rcheevos has no public accessor for it. */
#include "rc_client_internal.h"
#include "rc_internal.h"
#include "rc_runtime_types.h"

static unsigned int g_watch[RA_WATCH_MAX];
static unsigned int g_offset[RA_WATCH_MAX]; /* byte offset of each value in a snapshot */
static int g_count = 0;
static int g_bytes = 0;

static unsigned char g_values[RA_SNAP_MAX_BYTES];
static int g_have_values = 0;

/* Bytes the console must read for a memref of this size. Matches what
   the console derives from the packed list entry. */
static int memsize_bytes(uint8_t size)
{
    switch (size) {
        case RC_MEMSIZE_8_BITS:
        case RC_MEMSIZE_BIT_0:
        case RC_MEMSIZE_BIT_1:
        case RC_MEMSIZE_BIT_2:
        case RC_MEMSIZE_BIT_3:
        case RC_MEMSIZE_BIT_4:
        case RC_MEMSIZE_BIT_5:
        case RC_MEMSIZE_BIT_6:
        case RC_MEMSIZE_BIT_7:
        case RC_MEMSIZE_LOW:
        case RC_MEMSIZE_HIGH:
        case RC_MEMSIZE_BITCOUNT:
            return 1;
        case RC_MEMSIZE_16_BITS:
        case RC_MEMSIZE_16_BITS_BE:
            return 2;
        default:
            return 4;
    }
}

/* Achievements and leaderboards that read through a pointer chain
   (modified memrefs) are disabled here. The console does only direct
   reads, and rcheevos does not disable indirect reads on its own: an
   unreadable dereferenced address evaluates as zero, which could unlock
   an achievement whose condition happens to match zero. */
static void disable_indirect(rc_client_t *client)
{
    rc_client_game_info_t *game = client->game;
    rc_modified_memref_list_t *ml;
    int disabled = 0;

    for (ml = &game->runtime.memrefs->modified_memrefs; ml != NULL; ml = ml->next) {
        uint16_t k;

        for (k = 0; k < ml->count; k++) {
            rc_memref_t *memref = &ml->items[k].memref;
            rc_client_subset_info_t *subset;

            for (subset = game->subsets; subset != NULL; subset = subset->next) {
                rc_client_achievement_info_t *a = subset->achievements;
                rc_client_achievement_info_t *a_end = a + subset->public_.num_achievements;
                rc_client_leaderboard_info_t *l = subset->leaderboards;
                rc_client_leaderboard_info_t *l_end = l + subset->public_.num_leaderboards;

                for (; a < a_end; a++) {
                    if (a->public_.state == RC_CLIENT_ACHIEVEMENT_STATE_DISABLED || a->trigger == NULL)
                        continue;
                    if (rc_trigger_contains_memref(a->trigger, memref)) {
                        a->public_.state = RC_CLIENT_ACHIEVEMENT_STATE_DISABLED;
                        a->public_.bucket = RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED;
                        a->trigger->state = RC_TRIGGER_STATE_DISABLED;
                        log_trace("disabled achievement %u: pointer chain at %06X", a->public_.id, memref->address);
                        disabled++;
                    }
                }

                for (; l < l_end; l++) {
                    if (l->public_.state == RC_CLIENT_LEADERBOARD_STATE_DISABLED || l->lboard == NULL)
                        continue;
                    if (rc_trigger_contains_memref(&l->lboard->start, memref) ||
                        rc_trigger_contains_memref(&l->lboard->cancel, memref) ||
                        rc_trigger_contains_memref(&l->lboard->submit, memref) ||
                        rc_value_contains_memref(&l->lboard->value, memref)) {
                        l->public_.state = RC_CLIENT_LEADERBOARD_STATE_DISABLED;
                        l->lboard->state = RC_LBOARD_STATE_DISABLED;
                    }
                }
            }
        }
    }

    if (disabled > 0) {
        rc_client_update_active_achievements(game);
        rc_client_update_active_leaderboards(game);
        log_info("%d achievement%s disabled: pointer chains are not supported by the console",
                 disabled, disabled == 1 ? "" : "s");
    }
}

/* Pointer chains (modified memrefs) are not read: the console does only
   direct reads. See disable_indirect().

   The order of entries here is the order of values in every snapshot;
   the console reads addresses in the order it received them. */
int watchlist_build(rc_client_t *client)
{
    rc_memrefs_t *pool;
    rc_memref_list_t *ml;
    int off = 0, n = 0;

    g_count = 0;
    g_bytes = 0;
    g_have_values = 0;

    if (client == NULL || client->game == NULL)
        return 0;

    pool = client->game->runtime.memrefs;
    if (pool == NULL) {
        log_warn("the achievement set has no memory references");
        return 0;
    }

    for (ml = &pool->memrefs; ml != NULL; ml = ml->next) {
        uint16_t k;

        for (k = 0; k < ml->count; k++) {
            rc_memref_t *m = &ml->items[k];
            int b = memsize_bytes(rc_memref_shared_size(m->value.size));

            if (n >= RA_WATCH_MAX) {
                log_warn("more than %d addresses, the watch list does not fit", RA_WATCH_MAX);
                return 0;
            }
            if (off + b > RA_SNAP_MAX_BYTES) {
                log_warn("snapshot exceeds %d bytes, it does not fit in a packet", RA_SNAP_MAX_BYTES);
                return 0;
            }

            g_watch[n] = RA_WATCH_PACK(m->address, (unsigned int)b);
            g_offset[n] = (unsigned int)off;
            off += b;
            n++;
        }
    }

    if (n == 0) {
        log_warn("the achievement set has no direct memory reads");
        return 0;
    }

    g_count = n;
    g_bytes = off;
    log_info("watch list: %d addresses, %d bytes per snapshot", n, off);

    disable_indirect(client);
    return 1;
}

int watchlist_count(void)
{
    return g_count;
}

int watchlist_bytes(void)
{
    return g_bytes;
}

int watchlist_serialize(unsigned char *out, size_t cap)
{
    struct ra_watch_file hdr;
    size_t need = sizeof(hdr) + (size_t)g_count * sizeof(unsigned int);
    int i;

    if (g_count == 0 || need > cap)
        return 0;

    hdr.magic = RA_WATCH_MAGIC;
    hdr.version = RA_WATCH_VERSION;
    hdr.count = (unsigned int)g_count;
    hdr.bytes = (unsigned int)g_bytes;
    memcpy(out, &hdr, sizeof(hdr));

    for (i = 0; i < g_count; i++)
        memcpy(out + sizeof(hdr) + (size_t)i * sizeof(unsigned int), &g_watch[i], sizeof(unsigned int));

    return (int)need;
}

unsigned char *watchlist_values(void)
{
    return g_values;
}

void watchlist_set_have_values(int have)
{
    g_have_values = have;
}

/* Must return exactly num_bytes or nothing. A short read of a direct
   memref makes rcheevos disable every achievement using it for the rest
   of the session.

   Before the first snapshot rcheevos probes each memref once to check
   that the address is readable. Answering "unreadable" would disable
   the achievements, so zeros are returned with a full count. */
uint32_t watchlist_read_memory(uint32_t address, uint8_t *buffer, uint32_t num_bytes,
                               rc_client_t *client)
{
    int i;

    (void)client;

    if (!g_have_values) {
        memset(buffer, 0, num_bytes);
        return num_bytes;
    }

    /* Linear search over a few hundred entries is cheap on a PC, and a
       sorted index would complicate the order mapping to the snapshot. */
    for (i = 0; i < g_count; i++) {
        unsigned int a = RA_WATCH_ADDR(g_watch[i]);
        unsigned int n = RA_WATCH_SIZE(g_watch[i]);

        if (address >= a && address + num_bytes <= a + n) {
            unsigned int off = g_offset[i] + (address - a);

            if (off + num_bytes > (unsigned int)g_bytes)
                return 0;

            memcpy(buffer, &g_values[off], num_bytes);
            return num_bytes;
        }
    }

    return 0;
}
