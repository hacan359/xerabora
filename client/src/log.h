/*
  Timestamped console logging. Everything goes to stdout so that a
  redirect captures the whole session.
*/
#ifndef XERABORA_LOG_H
#define XERABORA_LOG_H

void log_set_trace(int on);
int log_trace_enabled(void);

void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_trace(const char *fmt, ...); /* only with --trace */

#endif
