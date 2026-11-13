/* Diagnostics: records the last minute of snapshots and writes reports
   from it. Test mode arms every achievement, sends nothing to
   RetroAchievements and reports each trigger. */
#ifndef XERABORA_DIAG_H
#define XERABORA_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "rc_client.h"

/* Drops the recording: another game, or another watch list. */
void diag_reset(void);

/* Stores the snapshot now in the watch list's value buffer. */
void diag_record(unsigned int seq, unsigned int bytes);

/* Test mode. The page asks for a change; the main loop takes the
   request when rc_client is free and reloads the set. */
int diag_test_mode(void);
void diag_request_test_mode(int on);
int diag_take_request(int *on);
void diag_set_test_mode(rc_client_t *client, int on);

/* Appends a triggered achievement to the test session's file. */
void diag_report_unlock(rc_client_t *client, uint32_t id);

/* Writes every active achievement and the whole recording to a new
   file. Returns 0 and the path on success. */
int diag_save(rc_client_t *client, char *path, size_t size);

/* The folder reports go to, "" when it cannot be created. */
const char *diag_dir(void);

#endif
