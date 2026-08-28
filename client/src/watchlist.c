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

/* Achievements that read through a pointer chain cannot unlock on this
   console: it reads a fixed list of addresses, so the dereferenced
   value is 0 on every frame. They are NOT disabled. The rcheevos
   maintainers' guidance (RA forum, 28.08.2026): the runtime follows
   pointers whatever they hold and expects a failed read to return 0;
   since nearly all logic watches values change, a permanent 0 does not
   trigger anything, it just never fires. Disabling them would only hide
   that from the user. So they stay active, and their number is counted
   here and reported as "unsupported" in the notice and the log. */
static int g_indirect = 0;

/* Not every modified memref is a pointer chain. rcheevos also builds
   them for arithmetic between direct reads -- AddSource/SubSource
   chains, combining conditions, prev(x)+prev(y) -- and those the
   console serves fine, since every underlying read is a plain address
   already on the watch list. Only RC_OPERATOR_INDIRECT_READ, anywhere
   up the chain, needs a dereference the console cannot do.

   Transformers: The Game showed the difference: 5 of 76 achievements
   use pointers, all 75 were being disabled. */
static int memref_is_indirect(const rc_memref_t *m)
{
    const rc_modified_memref_t *mm;

    if (m == NULL || m->value.memref_type != RC_MEMREF_TYPE_MODIFIED_MEMREF)
        return 0;

    mm = (const rc_modified_memref_t *)m;
    if (mm->modifier_type == RC_OPERATOR_INDIRECT_READ)
        return 1;

    if (rc_operand_is_memref(&mm->parent) && memref_is_indirect(mm->parent.value.memref))
        return 1;
    if (rc_operand_is_memref(&mm->modifier) && memref_is_indirect(mm->modifier.value.memref))
        return 1;

    return 0;
}

static void count_indirect(rc_client_t *client)
{
    rc_client_game_info_t *game = client->game;
    rc_client_subset_info_t *subset;

    g_indirect = 0;

    for (subset = game->subsets; subset != NULL; subset = subset->next) {
        rc_client_achievement_info_t *a = subset->achievements;
        rc_client_achievement_info_t *a_end = a + subset->public_.num_achievements;

        for (; a < a_end; a++) {
            rc_modified_memref_list_t *ml;
            int hit = 0;

            if (a->trigger == NULL)
                continue;

            for (ml = &game->runtime.memrefs->modified_memrefs; ml != NULL && !hit; ml = ml->next) {
                uint16_t k;

                for (k = 0; k < ml->count; k++) {
                    rc_memref_t *memref = &ml->items[k].memref;

                    if (memref_is_indirect(memref) && rc_trigger_contains_memref(a->trigger, memref)) {
                        log_trace("achievement %u reads through a pointer at %06X; it cannot unlock here", a->public_.id, memref->address);
                        hit = 1;
                        break;
                    }
                }
            }

            g_indirect += hit;
        }
    }

    if (g_indirect > 0) {
        log_info("%d achievement%s read through pointers the console cannot follow; they stay active but will not unlock",
                 g_indirect, g_indirect == 1 ? "" : "s");
    }
}

int watchlist_indirect_count(void)
{
    return g_indirect;
}

/* Pointer chains are not read: the console does only direct reads. See
   count_indirect().

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

    count_indirect(client);
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
