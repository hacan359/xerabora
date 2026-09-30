/*
  Shared by the Android backends: the JVM and the calls into Java.
*/
#ifndef XERABORA_ANDROID_GLUE_H
#define XERABORA_ANDROID_GLUE_H

#include <jni.h>
#include <stddef.h>

/* A JNIEnv for the calling thread. The identification worker is a plain
   pthread; it is attached for the call and detached again, and
   *attached tells glue_env_release which case it was. NULL when the
   thread cannot be attached. */
JNIEnv *glue_env(int *attached);
void glue_env_release(int attached);

/* Each backend resolves its Java class in JNI_OnLoad: FindClass from an
   attached native thread only sees system classes. */
void http_android_onload(JNIEnv *env);
void sound_android_onload(JNIEnv *env);

/* Native.onUiReady(port): the page is up. */
void glue_ui_ready(int port);

/* What the status notification shows (status.c). Strings are UTF-8 and
   never NULL. */
struct glue_status {
    int connected;     /* telemetry is arriving */
    int port_busy;     /* UDP 18194 is taken by another program */
    int logged_in;
    const char *serial;
    const char *status; /* "", no-hash, identifying, telemetry-only, active, stale */
    unsigned game_id;   /* 0 until the console's set is loaded */
    const char *title;
    const char *image;  /* the game's icon URL */
    const char *presence;
    unsigned unlocked, total;
    unsigned points_unlocked, points_total;
};

/* Native.onStatus / onUnlock / onDiscovery / onCheck. */
void glue_status(const struct glue_status *s);
void glue_unlock(unsigned id, const char *title, const char *description, const char *image,
                 unsigned points, unsigned unlocked, unsigned total);
void glue_discovery(const char *console_ip);
/* The answer to the console's "RA: check game support". */
void glue_check(int ok, const char *title, unsigned total, unsigned unlocked, unsigned unsupported,
                const char *reason);

/* Every datagram the client sends (net_guard.c): status.c picks the
   console's RAA1 answers out of them. */
void status_datagram(const void *buf, size_t len);

#endif
