package io.github.hacan359.xerabora;

import android.media.AudioAttributes;
import android.media.SoundPool;

/** sound.h, called by sound_android.c: connect, disconnect, achievement. */
final class SoundBridge {
    private static final int SLOTS = 3;
    private static final int[] ids = new int[SLOTS];
    private static SoundPool pool;

    private SoundBridge() {
    }

    static synchronized void load(int slot, String path) {
        if (slot < 0 || slot >= SLOTS) {
            return;
        }
        if (pool == null) {
            pool = new SoundPool.Builder()
                    .setMaxStreams(2)
                    .setAudioAttributes(new AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_NOTIFICATION_EVENT)
                            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                            .build())
                    .build();
        }
        ids[slot] = pool.load(path, 1);
    }

    static synchronized void play(int slot) {
        if (pool != null && slot >= 0 && slot < SLOTS && ids[slot] != 0) {
            pool.play(ids[slot], 1f, 1f, 1, 0, 1f);
        }
    }
}
