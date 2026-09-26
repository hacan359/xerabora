/*
  http.h through Java's HttpURLConnection (HttpBridge.request): TLS and
  the certificate store come from the system, as WinHTTP does for the
  Windows build, so no TLS library is bundled.
*/
#include "http.h"
#include "glue.h"

#include <stdlib.h>
#include <string.h>

#define BRIDGE_CLASS "io/github/hacan359/xerabora/HttpBridge"

static jclass g_bridge;
static jmethodID g_request;

void http_android_onload(JNIEnv *env)
{
    jclass c = (*env)->FindClass(env, BRIDGE_CLASS);

    if (c == NULL)
        return;
    g_bridge = (*env)->NewGlobalRef(env, c);
    (*env)->DeleteLocalRef(env, c);
    g_request = (*env)->GetStaticMethodID(env, g_bridge, "request",
                                          "(Ljava/lang/String;[BLjava/lang/String;Ljava/lang/String;[I)[B");
}

int http_init(void)
{
    return g_request != NULL ? 0 : -1;
}

void http_shutdown(void)
{
}

int http_request(const char *url, const char *post_data, const char *content_type,
                 const char *user_agent, struct http_response *out)
{
    int attached, rc = -1;
    JNIEnv *env = glue_env(&attached);

    memset(out, 0, sizeof(*out));
    out->status = -1;

    /* The client's main thread stays inside native code for its whole
       life, so every local reference made here is dropped with the frame. */
    if (env != NULL && g_request != NULL && (*env)->PushLocalFrame(env, 16) == 0) {
        jstring jurl = (*env)->NewStringUTF(env, url);
        jstring jtype = content_type != NULL ? (*env)->NewStringUTF(env, content_type) : NULL;
        jstring jagent = user_agent != NULL ? (*env)->NewStringUTF(env, user_agent) : NULL;
        jintArray jstatus = (*env)->NewIntArray(env, 1);
        jbyteArray jpost = NULL;
        jbyteArray body;

        if (post_data != NULL) {
            jsize n = (jsize)strlen(post_data);

            jpost = (*env)->NewByteArray(env, n);
            if (jpost != NULL)
                (*env)->SetByteArrayRegion(env, jpost, 0, n, (const jbyte *)post_data);
        }

        body = (jbyteArray)(*env)->CallStaticObjectMethod(env, g_bridge, g_request,
                                                          jurl, jpost, jtype, jagent, jstatus);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            body = NULL;
        }

        if (body != NULL) {
            jsize n = (*env)->GetArrayLength(env, body);
            jint status = -1;

            (*env)->GetIntArrayRegion(env, jstatus, 0, 1, &status);
            out->body = malloc((size_t)n + 1);
            if (out->body != NULL) {
                (*env)->GetByteArrayRegion(env, body, 0, n, (jbyte *)out->body);
                out->body[n] = '\0';
                out->length = (size_t)n;
                out->status = (int)status;
                rc = 0;
            }
        }
        (*env)->PopLocalFrame(env, NULL);
    }
    glue_env_release(attached);

    if (out->body == NULL) {
        out->body = calloc(1, 1);
        out->length = 0;
    }
    return rc;
}
