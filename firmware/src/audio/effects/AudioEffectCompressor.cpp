#include "AudioEffectCompressor.h"

void AudioEffectCompressor::update(void) {
    audio_block_t *block = receiveWritable(0);
    if (!block) return;

    const float thresh = thresholdLin;
    const float r = ratio;
    const float mk = makeup;
    const float atk = attackCoeff;
    const float rel = releaseCoeff;
    float env = envelope;

    const float invScale = 1.0f / 32768.0f;

    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        const float x = (float)block->data[i] * invScale;
        const float rectified = fabsf(x);

        // Peak envelope follower with independent attack/release.
        if (rectified > env) {
            env = atk * env + (1.0f - atk) * rectified;
        } else {
            env = rel * env + (1.0f - rel) * rectified;
        }

        float gain = 1.0f;
        if (env > thresh && env > 1.0e-6f) {
            // dB-domain gain reduction above threshold, converted back to
            // linear: gainReduced = (env/thresh)^(1/ratio - 1)
            gain = powf(env / thresh, (1.0f / r) - 1.0f);
        }

        float y = x * gain * mk;
        if (y > 0.95f || y < -0.95f) {
            y = tanhf(y);
        }

        block->data[i] = (int16_t)(y * 32767.0f);
    }

    envelope = env;

    transmit(block);
    release(block);
}
