/*
  Requests from the console: discovery, image lookup, watch list chunks.
  Also the table mapping game serials to image hashes, persisted in the
  config directory so a client restart mid-game needs no new image check.
*/
#ifndef XERABORA_CONSOLE_H
#define XERABORA_CONSOLE_H

#include <stddef.h>

#include "platform.h"
#include "rc_client.h"

/* Loads the saved serial-to-hash table. */
void console_load_games(void);

/* Remember that <serial> runs the image with <hash>. */
void console_remember_game(const char *serial, const char *hash);
const char *console_hash_for(const char *serial);

/* Handles a request packet if it is one. Returns 0 when the packet is
   telemetry, 1 for a handled or ignored request, 2 for a discovery
   request (the console has just found this PC). */
int console_serve(sock_t sock, const char *pkt, size_t len,
                  const struct sockaddr_in *from, rc_client_t *client);

/* Tell the console an achievement unlocked, so it can show a notice over
   the game. Goes to the address discovery recorded; returns 0 when no
   console has been discovered yet. */
int console_notify_unlock(unsigned id, unsigned points);
int console_send_badge_chunk(const unsigned char *px, int idx);

/* Any packet from the console names its address: a client started while
   the game already runs sees no discovery, only telemetry. */
void console_learn(sock_t sock, const struct sockaddr_in *from);

#endif
