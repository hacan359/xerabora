/*
  Telemetry packets: header parsing and reassembly of a snapshot that
  the console split across several packets.
*/
#ifndef XERABORA_SNAPSHOT_H
#define XERABORA_SNAPSHOT_H

#include <stddef.h>

struct snapshot_stats
{
    unsigned long frames;   /* complete snapshots delivered */
    unsigned long gaps;     /* snapshot numbers never seen */
    unsigned long dupes;    /* repeated snapshot numbers */
    unsigned long torn;     /* snapshots that never completed */
    unsigned int parts;     /* parts per snapshot, from the last packet */
    unsigned int bytes;     /* value bytes in the last complete snapshot */
    unsigned int count;     /* watch list entries reported by the console */
    unsigned int rxq;       /* console's receive FIFO depth, last packet */
    unsigned int rxq_max;   /* and the highest seen this session */

    /* The console's link counters from the packet header; zero when
       the console does not send them. */
    unsigned int send_us;        /* last send time, us */
    unsigned int send_max_us;    /* longest send time, us */
    unsigned int send_fail;      /* failed sends */
    unsigned int send_deferred;  /* sends put off while the network chip was busy */
    int send_err;                /* last error code */
    char link[8];                /* link speed and duplex, e.g. "100F" */
    unsigned int ee_skipped;     /* frames the console skipped */
    unsigned int console_torn;   /* broken copies the console sent again */
    unsigned int read_cycles;    /* time the console spent reading, in ticks */
    unsigned int frame_cycles;   /* length of a frame, in ticks */
};

/* Extracts the game serial from a packet header. Returns 1 on success. */
int snapshot_serial(const char *pkt, char *out, size_t out_size);

/* Forget partial state, e.g. when the game changes. */
void snapshot_reset(void);

/* Feeds one telemetry packet. Writes values into the buffer from
   watchlist_values(). Returns 1 when a complete snapshot is ready. */
int snapshot_feed(const char *pkt, size_t len);

/* 1 when the console's list (entry count or size) differs from the
   watch list built here; such packets are dropped. */
int snapshot_stale(void);

const struct snapshot_stats *snapshot_stats(void);

#endif
