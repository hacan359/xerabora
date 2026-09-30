package io.github.hacan359.xerabora;

import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/** http_request() from http.h, called by http_android.c. */
final class HttpBridge {
    private static final String TAG = "xerabora";

    private HttpBridge() {
    }

    /**
     * One blocking GET, or POST when {@code post} is not null.
     *
     * @return the body with {@code status[0]} set, any status included;
     *         null when no response arrived
     */
    static byte[] request(String url, byte[] post, String contentType, String userAgent, int[] status) {
        HttpURLConnection c = null;

        try {
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setConnectTimeout(15000);
            c.setReadTimeout(30000);
            c.setInstanceFollowRedirects(true);
            c.setUseCaches(false);
            if (userAgent != null) {
                c.setRequestProperty("User-Agent", userAgent);
            }
            if (post != null) {
                c.setDoOutput(true);
                c.setRequestMethod("POST");
                if (contentType != null) {
                    c.setRequestProperty("Content-Type", contentType);
                }
                c.setFixedLengthStreamingMode(post.length);
                try (OutputStream out = c.getOutputStream()) {
                    out.write(post);
                }
            }

            int code = c.getResponseCode();
            InputStream in = code >= 400 ? c.getErrorStream() : c.getInputStream();
            byte[] body = in != null ? readAll(in) : new byte[0];
            status[0] = code;
            return body;
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "http " + e);
            return null;
        } finally {
            if (c != null) {
                c.disconnect();
            }
        }
    }

    static byte[] readAll(InputStream in) throws IOException {
        try (InputStream s = in) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = s.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return out.toByteArray();
        }
    }
}
