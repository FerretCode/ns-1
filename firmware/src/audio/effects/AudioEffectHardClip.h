#pragma once

#include <Audio.h>

// Hard digital clipper: pre-gains the signal ("drive"), then clamps it to
// +/-ceiling. This is the last stage before the codec — it's what gives
// drums the crushed, over-driven edge the brief asks for. Unlike the
// waveshaper-based saturation stage upstream, this clips with a hard corner
// rather than a soft knee, on purpose.
class AudioEffectHardClip : public AudioStream {
public:
    AudioEffectHardClip() : AudioStream(1, inputQueueArray) {}

    // 1.0 = unity, higher pushes harder into the ceiling.
    void drive(float d) {
        if (d < 0.0f) d = 0.0f;
        if (d > 16.0f) d = 16.0f;
        driveGain = d;
    }

    // Clip ceiling as a fraction of full scale (0..1).
    void ceiling(float c) {
        if (c < 0.05f) c = 0.05f;
        if (c > 1.0f) c = 1.0f;
        clipCeiling = c;
    }

    virtual void update(void) override;

private:
    audio_block_t *inputQueueArray[1];
    volatile float driveGain = 1.0f;
    volatile float clipCeiling = 0.95f;
};
