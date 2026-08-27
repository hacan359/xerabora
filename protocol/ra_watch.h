/*
  The watch list: the memory addresses the console-side agent reads every
  frame, and the size limits of the snapshot built from it. See
  PROTOCOL.md for the wire format. Both sides keep a matching copy of
  this file.

  Why a list of addresses rather than fixed offsets: achievement
  addresses differ per game, so the client derives the list from the
  game's achievement set (via rcheevos) and sends it to the console once
  per image.

  Why individual values rather than memory ranges: a real set (NFS
  Underground 2) has 482 addresses spread over 192 KB in tight clusters.
  As contiguous ranges that is ~3152 bytes, three packets per frame; as
  individual values about 1350 bytes, two packets.

  Values in the snapshot follow watch-list order, so addresses are not
  sent over the wire: the client generated the list and knows it.
*/

#ifndef __RA_WATCH_H__
#define __RA_WATCH_H__

#define RA_WATCH_MAGIC   0x4C574152 /* "RAWL" in little-endian */
#define RA_WATCH_VERSION 1

/* Entry ceiling. NFS Underground 2 needs 482, X-Men 39. This leaves
   headroom over both, and at four bytes per entry the array stays at
   4 KB. */
#define RA_WATCH_MAX 1024

/* A snapshot is split across several UDP packets. One packet carries
   1472 bytes: 1500 MTU minus 20 IP minus 8 UDP, the limit without
   fragmentation.

   HEADER. Ceiling for the text header built in raudp.c (162 bytes with
   the current field table). raudp measures the real header length in
   ra_head_build() and derives the value bytes per packet from it, so
   this constant only sizes buffers; it must not be smaller than the
   real header. */
#define RA_SNAP_HEAD_BYTES 167

/* Bytes of values in one packet. */
#define RA_SNAP_CHUNK_BYTES (1472 - RA_SNAP_HEAD_BYTES)

/* Packets per snapshot.

   The measured send ceiling is 215 packets per second at 1472 bytes
   (see the raudp.c header). Four parts per frame at 60 fps would need
   240 and drop packets; three need 180 and leave headroom. */
#define RA_SNAP_PARTS 3

#define RA_SNAP_MAX_BYTES (RA_SNAP_CHUNK_BYTES * RA_SNAP_PARTS)

/* A watch list entry packs into one word: the address in the low 28
   bits (up to 256 MB of address space) and the read size in the high
   four bits. */
#define RA_WATCH_ADDR(e)          ((e)&0x0FFFFFFF)
#define RA_WATCH_SIZE(e)          ((e) >> 28)
#define RA_WATCH_PACK(addr, size) (((unsigned int)(size) << 28) | ((addr)&0x0FFFFFFF))

/* Watch list file header. count words follow. */
struct ra_watch_file
{
    unsigned int magic;   /* RA_WATCH_MAGIC */
    unsigned int version; /* RA_WATCH_VERSION */
    unsigned int count;   /* entries that follow */
    unsigned int bytes;   /* snapshot size in bytes: sum of the entry sizes */
};

#endif /* __RA_WATCH_H__ */
