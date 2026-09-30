/*
  sound.h through Android's SoundPool (SoundBridge). Same files as the
  desktop build: <config>/sounds/connect.wav, disconnect.wav or
  achievement.wav replace the embedded defaults.
*/
#include "sound.h"
#include "glue.h"
#include "log.h"
#include "platform.h"
#include "sounds_data.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define BRIDGE_CLASS "io/github/hacan359/xerabora/SoundBridge"

static const char *const g_names[SOUND_COUNT] = {"connect", "disconnect", "achievement"};

static jclass g_bridge;
static jmethodID g_load;
static jmethodID g_play;
static int g_enabled = 0;

void sound_android_onload(JNIEnv *env)
{
    jclass c = (*env)->FindClass(env, BRIDGE_CLASS);

    if (c == NULL)
        return;
    g_bridge = (*env)->NewGlobalRef(env, c);
    (*env)->DeleteLocalRef(env, c);
    g_load = (*env)->GetStaticMethodID(env, g_bridge, "load", "(ILjava/lang/String;)V");
    g_play = (*env)->GetStaticMethodID(env, g_bridge, "play", "(I)V");
}

static void bridge_load(int slot, const char *path)
{
    int attached;
    JNIEnv *env = glue_env(&attached);

    if (env != NULL && g_load != NULL && (*env)->PushLocalFrame(env, 4) == 0) {
        (*env)->CallStaticVoidMethod(env, g_bridge, g_load, (jint)slot, (*env)->NewStringUTF(env, path));
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
    }
    glue_env_release(attached);
}

void sound_init(int enabled)
{
    const unsigned char *data[SOUND_COUNT] = {sound_connect_wav, sound_disconnect_wav, sound_achievement_wav};
    const unsigned int len[SOUND_COUNT] = {sound_connect_wav_len, sound_disconnect_wav_len,
                                           sound_achievement_wav_len};
    char dir[512], path[600];
    struct stat st;
    int i;

    g_enabled = enabled;
    if (!enabled || platform_config_dir(dir, sizeof(dir)) != 0)
        return;
    strncat(dir, "/sounds", sizeof(dir) - strlen(dir) - 1);
    mkdir(dir, 0700);

    for (i = 0; i < SOUND_COUNT; i++) {
        snprintf(path, sizeof(path), "%s/%s.wav", dir, g_names[i]);
        if (stat(path, &st) == 0) {
            log_trace("sound %s: using %s", g_names[i], path);
        } else {
            FILE *f;

            snprintf(path, sizeof(path), "%s/default-%s.wav", dir, g_names[i]);
            f = fopen(path, "wb");
            if (f == NULL)
                continue;
            fwrite(data[i], 1, len[i], f);
            fclose(f);
        }
        bridge_load(i, path);
    }
}

void sound_play(enum sound_id id)
{
    int attached;
    JNIEnv *env;

    if (!g_enabled || (int)id < 0 || id >= SOUND_COUNT || g_play == NULL)
        return;
    env = glue_env(&attached);
    if (env != NULL) {
        (*env)->CallStaticVoidMethod(env, g_bridge, g_play, (jint)id);
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
    }
    glue_env_release(attached);
}
