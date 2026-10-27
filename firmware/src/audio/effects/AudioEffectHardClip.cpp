#include "AudioEffectHardClip.h"

void AudioEffectHardClip::update(void) {
    audio_block_t *block = receiveWritable(0);
    if (!block) return;

    const float g = driveGain;
    const int32_t ceil16 = (int32_t)(clipCeiling * 32767.0f);

    if (g == 1.0f) {
        // Fast path: still need to clip even with unity drive.
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            int32_t s = block->data[i];
            if (s > ceil16) s = ceil16;
            else if (s < -ceil16) s = -ceil16;
            block->data[i] = (int16_t)s;
        }
    } else {
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            int32_t s = (int32_t)((float)block->data[i] * g);
            if (s > ceil16) s = ceil16;
            else if (s < -ceil16) s = -ceil16;
            block->data[i] = (int16_t)s;
        }
    }

    transmit(block);
    release(block);
}
