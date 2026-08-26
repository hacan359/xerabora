#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static int g_trace = 0;

void log_set_trace(int on)
{
    g_trace = on;
}

int log_trace_enabled(void)
{
    return g_trace;
}

static void emit(const char *level, const char *fmt, va_list ap)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char stamp[16];

    strftime(stamp, sizeof(stamp), "%H:%M:%S", t);
    printf("%s %s", stamp, level);
    vprintf(fmt, ap);
    putchar('\n');
    fflush(stdout);
}

void log_info(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    emit("", fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    emit("warning: ", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    emit("error: ", fmt, ap);
    va_end(ap);
}

void log_trace(const char *fmt, ...)
{
    va_list ap;

    if (!g_trace)
        return;
    va_start(ap, fmt);
    emit("  ", fmt, ap);
    va_end(ap);
}
