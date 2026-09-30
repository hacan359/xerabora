/*
  Entry point from Java: environment, stdout to logcat, then the
  client's own main().
*/
#include "glue.h"

#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TAG "xerabora"
#define NATIVE_CLASS "io/github/hacan359/xerabora/Native"

int xerabora_main(int argc, char **argv);

static JavaVM *g_vm;
static jclass g_native;
static jmethodID g_on_ui_ready;
static jmethodID g_on_status;
static jmethodID g_on_unlock;
static jmethodID g_on_discovery;
static jmethodID g_on_check;

JNIEnv *glue_env(int *attached)
{
    JNIEnv *env = NULL;

    *attached = 0;
    if (g_vm == NULL)
        return NULL;
    if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK)
        return env;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK)
        return NULL;
    *attached = 1;
    return env;
}

void glue_env_release(int attached)
{
    if (attached)
        (*g_vm)->DetachCurrentThread(g_vm);
}

void glue_ui_ready(int port)
{
    int attached;
    JNIEnv *env = glue_env(&attached);

    if (env == NULL)
        return;
    (*env)->CallStaticVoidMethod(env, g_native, g_on_ui_ready, (jint)port);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    glue_env_release(attached);
}

/* Text crosses as UTF-8 bytes, decoded in Java: NewStringUTF wants
   modified UTF-8, and achievement titles do carry emoji. */
static jbyteArray utf8(JNIEnv *env, const char *s)
{
    jsize n = (jsize)strlen(s);
    jbyteArray a = (*env)->NewByteArray(env, n);

    if (a != NULL)
        (*env)->SetByteArrayRegion(env, a, 0, n, (const jbyte *)s);
    return a;
}

void glue_status(const struct glue_status *s)
{
    int attached;
    JNIEnv *env = glue_env(&attached);

    if (env != NULL && g_on_status != NULL && (*env)->PushLocalFrame(env, 8) == 0) {
        jint flags = (s->connected ? 1 : 0) | (s->port_busy ? 2 : 0) | (s->logged_in ? 4 : 0);

        (*env)->CallStaticVoidMethod(env, g_native, g_on_status, flags, utf8(env, s->serial),
                                     utf8(env, s->status), (jint)s->game_id, utf8(env, s->title),
                                     utf8(env, s->image), utf8(env, s->presence), (jint)s->unlocked,
                                     (jint)s->total, (jint)s->points_unlocked, (jint)s->points_total);
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
    }
    glue_env_release(attached);
}

void glue_unlock(unsigned id, const char *title, const char *description, const char *image,
                 unsigned points, unsigned unlocked, unsigned total)
{
    int attached;
    JNIEnv *env = glue_env(&attached);

    if (env != NULL && g_on_unlock != NULL && (*env)->PushLocalFrame(env, 4) == 0) {
        (*env)->CallStaticVoidMethod(env, g_native, g_on_unlock, (jint)id, utf8(env, title),
                                     utf8(env, description), utf8(env, image), (jint)points,
                                     (jint)unlocked, (jint)total);
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
    }
    glue_env_release(attached);
}

void glue_discovery(const char *console_ip)
{
    int attached;
    JNIEnv *env = glue_env(&attached);

    if (env != NULL && g_on_discovery != NULL && (*env)->PushLocalFrame(env, 2) == 0) {
        (*env)->CallStaticVoidMethod(env, g_native, g_on_discovery, utf8(env, console_ip));
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
    }
    glue_env_release(attached);
}

void glue_check(int ok, const char *title, unsigned total, unsigned unlocked, unsigned unsupported,
                const char *reason)
{
    int attached;
    JNIEnv *env = glue_env(&attached);

    if (env != NULL && g_on_check != NULL && (*env)->PushLocalFrame(env, 4) == 0) {
        (*env)->CallStaticVoidMethod(env, g_native, g_on_check, (jint)ok, utf8(env, title), (jint)total,
                                     (jint)unlocked, (jint)unsupported, utf8(env, reason));
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
    }
    glue_env_release(attached);
}

/* ---- stdout and stderr to logcat ---------------------------------------- */

static int g_log_pipe[2];

static void *log_pump(void *arg)
{
    char buf[1024];
    size_t used = 0;
    ssize_t r;

    (void)arg;
    while ((r = read(g_log_pipe[0], buf + used, sizeof(buf) - 1 - used)) > 0) {
        char *start = buf, *nl;

        used += (size_t)r;
        buf[used] = '\0';
        while ((nl = memchr(start, '\n', used - (size_t)(start - buf))) != NULL) {
            *nl = '\0';
            if (nl > start)
                __android_log_write(ANDROID_LOG_INFO, TAG, start);
            start = nl + 1;
        }
        used -= (size_t)(start - buf);
        memmove(buf, start, used);
        /* A line longer than the buffer goes out in pieces. */
        if (used == sizeof(buf) - 1) {
            buf[used] = '\0';
            __android_log_write(ANDROID_LOG_INFO, TAG, buf);
            used = 0;
        }
    }
    return NULL;
}

static void redirect_output(void)
{
    pthread_t t;

    if (pipe(g_log_pipe) != 0)
        return;
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(g_log_pipe[1], STDOUT_FILENO);
    dup2(g_log_pipe[1], STDERR_FILENO);
    if (pthread_create(&t, NULL, log_pump, NULL) == 0)
        pthread_detach(t);
}

/* ---- JNI ------------------------------------------------------------------ */

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env;
    jclass c;

    (void)reserved;
    g_vm = vm;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;

    c = (*env)->FindClass(env, NATIVE_CLASS);
    if (c == NULL)
        return JNI_ERR;
    g_native = (*env)->NewGlobalRef(env, c);
    (*env)->DeleteLocalRef(env, c);
    g_on_ui_ready = (*env)->GetStaticMethodID(env, g_native, "onUiReady", "(I)V");
    g_on_status = (*env)->GetStaticMethodID(env, g_native, "onStatus", "(I[B[BI[B[B[BIIII)V");
    g_on_unlock = (*env)->GetStaticMethodID(env, g_native, "onUnlock", "(I[B[B[BIII)V");
    g_on_discovery = (*env)->GetStaticMethodID(env, g_native, "onDiscovery", "([B)V");
    g_on_check = (*env)->GetStaticMethodID(env, g_native, "onCheck", "(I[BIII[B)V");

    http_android_onload(env);
    sound_android_onload(env);
    if ((*env)->ExceptionCheck(env))
        return JNI_ERR;
    return JNI_VERSION_1_6;
}

/* Runs the client until it exits. home becomes $HOME, so the saved login
   and the log land in <home>/.config/xerabora. */
JNIEXPORT jint JNICALL
Java_io_github_hacan359_xerabora_Native_run(JNIEnv *env, jclass cls, jstring home, jstring tmp, jobjectArray args)
{
    const char *s;
    jsize n = args != NULL ? (*env)->GetArrayLength(env, args) : 0;
    char **argv = calloc((size_t)n + 2, sizeof(char *));
    jsize i;
    int rc;

    (void)cls;
    if (argv == NULL)
        return -1;

    redirect_output();

    s = (*env)->GetStringUTFChars(env, home, NULL);
    setenv("HOME", s, 1);
    unsetenv("XDG_CONFIG_HOME");
    (*env)->ReleaseStringUTFChars(env, home, s);
    s = (*env)->GetStringUTFChars(env, tmp, NULL);
    setenv("TMPDIR", s, 1);
    (*env)->ReleaseStringUTFChars(env, tmp, s);

    argv[0] = strdup("xerabora");
    for (i = 0; i < n; i++) {
        jstring a = (jstring)(*env)->GetObjectArrayElement(env, args, i);

        s = (*env)->GetStringUTFChars(env, a, NULL);
        argv[i + 1] = strdup(s);
        (*env)->ReleaseStringUTFChars(env, a, s);
        (*env)->DeleteLocalRef(env, a);
    }

    __android_log_print(ANDROID_LOG_INFO, TAG, "starting the client with %d argument(s)", (int)n);
    rc = xerabora_main((int)n + 1, argv);
    __android_log_print(ANDROID_LOG_INFO, TAG, "the client returned %d", rc);
    fflush(stdout);
    return rc;
}
