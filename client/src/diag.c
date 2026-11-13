#include "diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "log.h"
#include "platform.h"
#include "protocol.h"
#include "ra.h"
#include "snapshot.h"
#include "version.h"
#include "watchlist.h"

/* The parsed conditions and the memref pool have no public accessor. */
#include "rc_client_internal.h"
#include "rc_internal.h"
#include "rc_runtime_types.h"

/* ---- The recording ---------------------------------------------------- */

/* A minute at 60 snapshots a second, or as many as fit in the budget
   when the snapshot is large. */
#define DIAG_FRAMES    3600
#define DIAG_MAX_BYTES (16u * 1024u * 1024u)

struct diag_frame
{
    unsigned int seq;
    time_t at;
};

static struct diag_frame *g_frames;
static unsigned char *g_data;
static unsigned int g_cap;   /* frames the buffers hold */
static unsigned int g_bytes; /* bytes per frame */
static unsigned int g_head;  /* next slot to write */
static unsigned int g_used;

void diag_reset(void)
{
    g_head = 0;
    g_used = 0;
}

void diag_record(unsigned int seq, unsigned int bytes)
{
    if (bytes == 0 || bytes > RA_SNAP_MAX_BYTES)
        return;

    if (bytes != g_bytes || g_frames == NULL) {
        unsigned int cap = DIAG_MAX_BYTES / bytes;

        if (cap > DIAG_FRAMES)
            cap = DIAG_FRAMES;
        free(g_frames);
        free(g_data);
        g_frames = malloc(cap * sizeof(*g_frames));
        g_data = malloc((size_t)cap * bytes);
        if (g_frames == NULL || g_data == NULL) {
            free(g_frames);
            free(g_data);
            g_frames = NULL;
            g_data = NULL;
            g_bytes = 0;
            return;
        }
        g_cap = cap;
        g_bytes = bytes;
        diag_reset();
    }

    g_frames[g_head].seq = seq;
    g_frames[g_head].at = time(NULL);
    memcpy(g_data + (size_t)g_head * g_bytes, watchlist_values(), g_bytes);
    g_head = (g_head + 1) % g_cap;
    if (g_used < g_cap)
        g_used++;
}

/* Frame k of the recording, 0 the oldest. */
static unsigned int frame_slot(unsigned int k)
{
    return (g_head + g_cap - g_used + k) % g_cap;
}

static uint32_t frame_value(unsigned int k, unsigned int offset, unsigned int size)
{
    const unsigned char *p = g_data + (size_t)frame_slot(k) * g_bytes + offset;
    uint32_t v = 0;
    unsigned int i;

    for (i = 0; i < size && i < 4; i++)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}

/* ---- Where reports go -------------------------------------------------- */

static char g_dir[600];

const char *diag_dir(void)
{
    char base[512];

    if (g_dir[0] != '\0')
        return g_dir;
    if (platform_config_dir(base, sizeof(base)) != 0)
        return "";
#ifdef _WIN32
    snprintf(g_dir, sizeof(g_dir), "%s\\reports", base);
    _mkdir(g_dir);
#else
    snprintf(g_dir, sizeof(g_dir), "%s/reports", base);
    mkdir(g_dir, 0700);
#endif
    return g_dir;
}

static void report_path(char *out, size_t size, const char *kind)
{
    time_t now = time(NULL);
    const struct tm *tm = localtime(&now);
    char stamp[32] = "";

    if (tm != NULL)
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm);
#ifdef _WIN32
    snprintf(out, size, "%s\\%s-%s.txt", diag_dir(), kind, stamp);
#else
    snprintf(out, size, "%s/%s-%s.txt", diag_dir(), kind, stamp);
#endif
}

/* ---- Printing conditions ------------------------------------------------ */

static const char *size_name(uint8_t size)
{
    switch (size) {
        case RC_MEMSIZE_8_BITS:  return "8-bit";
        case RC_MEMSIZE_16_BITS: return "16-bit";
        case RC_MEMSIZE_24_BITS: return "24-bit";
        case RC_MEMSIZE_32_BITS: return "32-bit";
        case RC_MEMSIZE_LOW:     return "low4";
        case RC_MEMSIZE_HIGH:    return "high4";
        case RC_MEMSIZE_BIT_0:   return "bit0";
        case RC_MEMSIZE_BIT_1:   return "bit1";
        case RC_MEMSIZE_BIT_2:   return "bit2";
        case RC_MEMSIZE_BIT_3:   return "bit3";
        case RC_MEMSIZE_BIT_4:   return "bit4";
        case RC_MEMSIZE_BIT_5:   return "bit5";
        case RC_MEMSIZE_BIT_6:   return "bit6";
        case RC_MEMSIZE_BIT_7:   return "bit7";
        case RC_MEMSIZE_BITCOUNT: return "bitcount";
        case RC_MEMSIZE_FLOAT:   return "float";
        default:                 return "other";
    }
}

static const char *oper_name(uint8_t oper)
{
    switch (oper) {
        case RC_OPERATOR_EQ:   return "==";
        case RC_OPERATOR_LT:   return "<";
        case RC_OPERATOR_LE:   return "<=";
        case RC_OPERATOR_GT:   return ">";
        case RC_OPERATOR_GE:   return ">=";
        case RC_OPERATOR_NE:   return "!=";
        case RC_OPERATOR_MULT: return "*";
        case RC_OPERATOR_DIV:  return "/";
        case RC_OPERATOR_AND:  return "&";
        case RC_OPERATOR_XOR:  return "^";
        case RC_OPERATOR_MOD:  return "%";
        case RC_OPERATOR_ADD:  return "+";
        case RC_OPERATOR_SUB:  return "-";
        case RC_OPERATOR_ADD_ACCUMULATOR: return "+";
        case RC_OPERATOR_SUB_ACCUMULATOR: return "-";
        default:               return "";
    }
}

static const char *cond_name(uint8_t type)
{
    switch (type) {
        case RC_CONDITION_PAUSE_IF:      return "PauseIf";
        case RC_CONDITION_RESET_IF:      return "ResetIf";
        case RC_CONDITION_MEASURED_IF:   return "MeasuredIf";
        case RC_CONDITION_TRIGGER:       return "Trigger";
        case RC_CONDITION_MEASURED:      return "Measured";
        case RC_CONDITION_ADD_SOURCE:    return "AddSource";
        case RC_CONDITION_SUB_SOURCE:    return "SubSource";
        case RC_CONDITION_ADD_ADDRESS:   return "AddAddress";
        case RC_CONDITION_REMEMBER:      return "Remember";
        case RC_CONDITION_ADD_HITS:      return "AddHits";
        case RC_CONDITION_SUB_HITS:      return "SubHits";
        case RC_CONDITION_RESET_NEXT_IF: return "ResetNextIf";
        case RC_CONDITION_AND_NEXT:      return "AndNext";
        case RC_CONDITION_OR_NEXT:       return "OrNext";
        default:                         return "";
    }
}

static const char *state_name(uint8_t state)
{
    switch (state) {
        case RC_TRIGGER_STATE_INACTIVE:  return "inactive";
        case RC_TRIGGER_STATE_WAITING:   return "waiting";
        case RC_TRIGGER_STATE_ACTIVE:    return "active";
        case RC_TRIGGER_STATE_PAUSED:    return "paused";
        case RC_TRIGGER_STATE_RESET:     return "reset";
        case RC_TRIGGER_STATE_TRIGGERED: return "triggered";
        case RC_TRIGGER_STATE_PRIMED:    return "primed";
        default:                         return "disabled";
    }
}

static void print_memref(FILE *f, const rc_memref_t *m);

/* One side of a modified memref: a memref or a constant. */
static void print_part(FILE *f, const rc_operand_t *op)
{
    if (rc_operand_is_memref(op))
        print_memref(f, op->value.memref);
    else
        fprintf(f, "0x%X", (unsigned)op->value.num);
}

/* The address a memref stands for, as the set wrote it: a plain
   address, [parent + offset] for a pointer step, (a op b) for
   arithmetic. */
static void print_memref(FILE *f, const rc_memref_t *m)
{
    const rc_modified_memref_t *mm;

    if (m->value.memref_type != RC_MEMREF_TYPE_MODIFIED_MEMREF) {
        fprintf(f, "0x%08X", (unsigned)m->address);
        return;
    }

    mm = (const rc_modified_memref_t *)m;
    if (mm->modifier_type == RC_OPERATOR_SUB_PARENT) {
        fputc('(', f);
        print_part(f, &mm->modifier);
        fprintf(f, " - ");
        print_part(f, &mm->parent);
        fputc(')', f);
        return;
    }
    fputc(mm->modifier_type == RC_OPERATOR_INDIRECT_READ ? '[' : '(', f);
    if (rc_operand_is_memref(&mm->parent))
        print_memref(f, mm->parent.value.memref);
    else
        fprintf(f, "0x%X", (unsigned)mm->parent.value.num);
    fprintf(f, " %s ", mm->modifier_type == RC_OPERATOR_INDIRECT_READ ? "+" : oper_name(mm->modifier_type));
    if (rc_operand_is_memref(&mm->modifier))
        print_memref(f, mm->modifier.value.memref);
    else
        fprintf(f, "0x%X", (unsigned)mm->modifier.value.num);
    fputc(mm->modifier_type == RC_OPERATOR_INDIRECT_READ ? ']' : ')', f);
}

static void print_value(FILE *f, uint8_t size, uint32_t raw)
{
    if (size == RC_MEMSIZE_FLOAT) {
        float v;

        memcpy(&v, &raw, sizeof(v));
        fprintf(f, "%g", (double)v);
    } else {
        fprintf(f, "0x%X", (unsigned)raw);
    }
}

static void print_operand(FILE *f, const rc_operand_t *op)
{
    const rc_memref_t *m;
    int at, is_node;

    switch (op->type) {
        case RC_OPERAND_CONST:
            fprintf(f, "0x%X", (unsigned)op->value.num);
            return;
        case RC_OPERAND_FP:
            fprintf(f, "%g", op->value.dbl);
            return;
        case RC_OPERAND_RECALL:
            fprintf(f, "{recall}");
            return;
        case RC_OPERAND_DELTA:    fprintf(f, "delta "); break;
        case RC_OPERAND_PRIOR:    fprintf(f, "prior "); break;
        case RC_OPERAND_BCD:      fprintf(f, "bcd "); break;
        case RC_OPERAND_INVERTED: fprintf(f, "inverted "); break;
        default: break;
    }

    if (!rc_operand_is_memref(op) || op->value.memref == NULL) {
        fprintf(f, "?");
        return;
    }

    m = op->value.memref;
    fprintf(f, "%s ", size_name(op->size));
    print_memref(f, m);
    fprintf(f, " {now ");
    print_value(f, op->size, m->value.value);
    fprintf(f, ", prior ");
    print_value(f, op->size, m->value.prior);
    fprintf(f, "%s", m->value.changed ? ", changed" : "");
    at = watchlist_locate(m, &is_node);
    if (at < 0 && m->value.memref_type == RC_MEMREF_TYPE_MODIFIED_MEMREF &&
        ((const rc_modified_memref_t *)m)->modifier_type == RC_OPERATOR_INDIRECT_READ)
        fprintf(f, ", NOT READ BY THE CONSOLE");
    fprintf(f, "}");
}

static void print_condset(FILE *f, const rc_condset_t *cs, const char *label)
{
    const rc_condition_t *c;
    int n = 1;

    fprintf(f, "  %s\n", label);
    for (c = cs != NULL ? cs->conditions : NULL; c != NULL; c = c->next, n++) {
        fprintf(f, "    %2d %-11s ", n, cond_name(c->type));
        print_operand(f, &c->operand1);
        if (c->oper != RC_OPERATOR_NONE) {
            fprintf(f, " %s ", oper_name(c->oper));
            print_operand(f, &c->operand2);
        }
        fprintf(f, "   hits %u/%u, %s\n", (unsigned)c->current_hits, (unsigned)c->required_hits,
                c->is_true ? "true" : "false");
    }
}

/* ---- History ------------------------------------------------------------ */

/* Which entries and nodes a report covers. */
static unsigned char g_want_entry[RA_WATCH_MAX];
static unsigned char g_want_node[RA_NODE_MAX];

static void want_memref(const rc_memref_t *m)
{
    const rc_modified_memref_t *mm;
    int at, is_node;

    if (m == NULL)
        return;
    at = watchlist_locate(m, &is_node);
    if (at >= 0 && is_node && at < RA_NODE_MAX)
        g_want_node[at] = 1;
    else if (at >= 0 && !is_node && at < RA_WATCH_MAX)
        g_want_entry[at] = 1;

    if (m->value.memref_type != RC_MEMREF_TYPE_MODIFIED_MEMREF)
        return;
    mm = (const rc_modified_memref_t *)m;
    if (rc_operand_is_memref(&mm->parent))
        want_memref(mm->parent.value.memref);
    if (rc_operand_is_memref(&mm->modifier))
        want_memref(mm->modifier.value.memref);
}

static void want_trigger(const rc_trigger_t *t)
{
    const rc_condset_t *cs;
    const rc_condition_t *c;
    int alt = 0;

    for (cs = t->requirement; cs != NULL; cs = alt ? cs->next : t->alternative, alt = 1) {
        for (c = cs->conditions; c != NULL; c = c->next) {
            if (rc_operand_is_memref(&c->operand1))
                want_memref(c->operand1.value.memref);
            if (rc_operand_is_memref(&c->operand2))
                want_memref(c->operand2.value.memref);
        }
    }
}

/* One line per watched place: its value in the oldest frame, then every
   change with the snapshot number it arrived in. */
static void print_history(FILE *f)
{
    unsigned int nodes_at = (unsigned int)watchlist_bytes();
    int i, with_nodes = watchlist_have_nodes();
    unsigned int k;

    if (g_used == 0) {
        fprintf(f, "  no snapshots recorded\n");
        return;
    }

    fprintf(f, "  %u snapshots, #%u to #%u, %ld seconds\n", g_used, g_frames[frame_slot(0)].seq,
            g_frames[frame_slot(g_used - 1)].seq,
            (long)(g_frames[frame_slot(g_used - 1)].at - g_frames[frame_slot(0)].at));

    for (i = 0; i < watchlist_count() && i < RA_WATCH_MAX; i++) {
        unsigned int addr, size, off;
        uint32_t last;

        if (!g_want_entry[i])
            continue;
        watchlist_entry_info(i, &addr, &size, &off);
        if (off + size > g_bytes)
            continue;
        last = frame_value(0, off, size);
        fprintf(f, "  0x%08X/%u: %X", addr, size, (unsigned)last);
        for (k = 1; k < g_used; k++) {
            uint32_t v = frame_value(k, off, size);

            if (v != last)
                fprintf(f, "  #%u=%X", g_frames[frame_slot(k)].seq, (unsigned)v);
            last = v;
        }
        fputc('\n', f);
    }

    for (i = 0; with_nodes && i < watchlist_node_count() && i < RA_NODE_MAX; i++) {
        unsigned int off = nodes_at + (unsigned int)i * RA_NODE_PAIR_BYTES;
        uint32_t la, lv;

        if (!g_want_node[i] || off + RA_NODE_PAIR_BYTES > g_bytes)
            continue;
        la = frame_value(0, off, 4);
        lv = frame_value(0, off + 4, 4);
        fprintf(f, "  chain %d: at %08X %X", i, (unsigned)la, (unsigned)lv);
        for (k = 1; k < g_used; k++) {
            uint32_t a = frame_value(k, off, 4), v = frame_value(k, off + 4, 4);

            if (a != la)
                fprintf(f, "  #%u at %08X %X", g_frames[frame_slot(k)].seq, (unsigned)a, (unsigned)v);
            else if (v != lv)
                fprintf(f, "  #%u=%X", g_frames[frame_slot(k)].seq, (unsigned)v);
            la = a;
            lv = v;
        }
        fputc('\n', f);
    }
}

/* ---- Reports ------------------------------------------------------------ */

static void print_head(FILE *f, rc_client_t *client, const char *why)
{
    const rc_client_game_t *game = rc_client_get_game_info(client);
    const struct snapshot_stats *st = snapshot_stats();
    time_t now = time(NULL);
    char stamp[32] = "";
    const struct tm *tm = localtime(&now);

    if (tm != NULL)
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(f, "xerabora %s, %s\n", XERABORA_VERSION, stamp);
    fprintf(f, "%s\n", why);
    fprintf(f, "game: %s (id %u), image %s\n", game != NULL && game->title != NULL ? game->title : "?",
            game != NULL ? (unsigned)game->id : 0u, ra_loaded_hash());
    fprintf(f, "mode: %s\n", diag_test_mode() ? "test, nothing is sent to RetroAchievements" : "normal");
    fprintf(f, "watch list: %d addresses, %d bytes, %d chains, chains in snapshots: %s, %d achievements out of reach\n",
            watchlist_count(), watchlist_bytes(), watchlist_node_count(),
            watchlist_have_nodes() ? "yes" : "no", watchlist_indirect_count());
    fprintf(f, "snapshots: %lu received, %lu skipped by the console, %lu repeated, %lu incomplete, %u parts\n\n",
            st->frames, st->gaps, st->dupes, st->torn, st->parts);
}

static void print_achievement(FILE *f, const rc_client_achievement_info_t *a)
{
    const rc_condset_t *cs;
    int n = 1;

    fprintf(f, "achievement %u \"%s\": %s\n", (unsigned)a->public_.id, a->public_.title,
            state_name(a->trigger->state));
    fprintf(f, "  %s\n", a->public_.description != NULL ? a->public_.description : "");
    print_condset(f, a->trigger->requirement, "core");
    for (cs = a->trigger->alternative; cs != NULL; cs = cs->next, n++) {
        char label[16];

        snprintf(label, sizeof(label), "alt %d", n);
        print_condset(f, cs, label);
    }
}

static const rc_client_achievement_info_t *find_achievement(const rc_client_t *client, uint32_t id)
{
    const rc_client_subset_info_t *subset;

    if (client == NULL || client->game == NULL)
        return NULL;
    for (subset = client->game->subsets; subset != NULL; subset = subset->next) {
        const rc_client_achievement_info_t *a = subset->achievements;
        const rc_client_achievement_info_t *a_end = a + subset->public_.num_achievements;

        for (; a < a_end; a++)
            if (a->public_.id == id)
                return a;
    }
    return NULL;
}

static char g_session[640];

void diag_report_unlock(rc_client_t *client, uint32_t id)
{
    const rc_client_achievement_info_t *a;
    FILE *f;

    if (!diag_test_mode() || g_session[0] == '\0')
        return;
    a = find_achievement(client, id);
    if (a == NULL || a->trigger == NULL)
        return;
    f = fopen(g_session, "a");
    if (f == NULL)
        return;

    print_head(f, client, "an achievement triggered");
    print_achievement(f, a);
    memset(g_want_entry, 0, sizeof(g_want_entry));
    memset(g_want_node, 0, sizeof(g_want_node));
    want_trigger(a->trigger);
    fprintf(f, "history of what it reads\n");
    print_history(f);
    fprintf(f, "\n----\n\n");
    fclose(f);
    log_info("test mode: \"%s\" written to %s", a->public_.title, g_session);
}

int diag_save(rc_client_t *client, char *path, size_t size)
{
    const rc_client_subset_info_t *subset;
    FILE *f;

    if (client == NULL || client->game == NULL || diag_dir()[0] == '\0')
        return -1;
    report_path(path, size, "report");
    f = fopen(path, "w");
    if (f == NULL)
        return -1;

    print_head(f, client, "saved from the page");
    for (subset = client->game->subsets; subset != NULL; subset = subset->next) {
        const rc_client_achievement_info_t *a = subset->achievements;
        const rc_client_achievement_info_t *a_end = a + subset->public_.num_achievements;

        for (; a < a_end; a++) {
            if (a->trigger == NULL || a->public_.state != RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE)
                continue;
            print_achievement(f, a);
            fputc('\n', f);
        }
    }
    memset(g_want_entry, 1, sizeof(g_want_entry));
    memset(g_want_node, 1, sizeof(g_want_node));
    fprintf(f, "history of every watched address\n");
    print_history(f);
    fclose(f);
    log_info("the last minute saved to %s", path);
    return 0;
}

/* ---- Test mode ---------------------------------------------------------- */

static int g_test;
static volatile int g_request = -1;

int diag_test_mode(void)
{
    return g_test;
}

void diag_request_test_mode(int on)
{
    g_request = on ? 1 : 0;
}

int diag_take_request(int *on)
{
    if (g_request < 0)
        return 0;
    *on = g_request;
    g_request = -1;
    return *on != g_test;
}

/* Turns test mode on or off and unloads the game; the next packet
   loads it again under the new mode. */
void diag_set_test_mode(rc_client_t *client, int on)
{
    g_test = on ? 1 : 0;
    ra_unload_game(client);
    rc_client_set_spectator_mode_enabled(client, g_test);
    rc_client_set_encore_mode_enabled(client, g_test);

    g_session[0] = '\0';
    if (g_test && diag_dir()[0] != '\0')
        report_path(g_session, sizeof(g_session), "test");
    log_info(g_test ? "test mode on: every achievement is active, nothing is sent to RetroAchievements"
                    : "test mode off");
}
