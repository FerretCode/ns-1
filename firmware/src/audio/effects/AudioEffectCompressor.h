#pragma once

#include <Audio.h>
#include <math.h>

// Feedforward peak-envelope compressor, tuned to be able to get aggressively
// "squashed" — this is the glue that gives the NS-1 its tape/lo-fi feel
// sitting ahead of the grit/saturation/clip stages, not a transparent
// mastering compressor.
//
// A single amount() macro (driven by a pot, 0..1) walks threshold, ratio and
// makeup gain together: at 0 it's basically transparent, at 1 it's a hard,
// fast-limiting squash. This is deliberately a rough, ear-tuned mapping
// rather than a calibrated model — if you want independent control, add
// threshold()/ratio()/makeup() setters and wire more pots to them.
class AudioEffectCompressor : public AudioStream {
public:
    AudioEffectCompressor() : AudioStream(1, inputQueueArray) {
        setTimes(0.005f, 0.120f); // 5ms attack, 120ms release — original huge punchy character
    }

    // 0.0 (transparent) .. 1.0 (heavily squashed)
    void amount(float a) {
        if (a <= 0.001f) {
            thresholdLin = 1.0f;
            ratio = 1.0f;
            makeup = 1.0f;
            return;
        }
        if (a > 1.0f) a = 1.0f;
        // Threshold: 0.95 (engages lightly) down to 0.08 (squashed).
        thresholdLin = 0.95f - a * 0.87f;
        // Ratio: 1.5:1 up to 16:1.
        ratio = 1.5f + a * 14.5f;
        // Makeup gain: up to 2.0x massive volume push
        makeup = 1.0f + a * 0.85f;
        if (makeup > 2.0f) makeup = 2.0f;
    }

    void setTimes(float attackSeconds, float releaseSeconds) {
        attackCoeff = expf(-1.0f / (NS1_COMP_SR * attackSeconds));
        releaseCoeff = expf(-1.0f / (NS1_COMP_SR * releaseSeconds));
    }

    virtual void update(void) override;

private:
    static constexpr float NS1_COMP_SR = 44100.0f;

    audio_block_t *inputQueueArray[1];

    volatile float thresholdLin = 0.9f;
    volatile float ratio = 2.0f;
    volatile float makeup = 1.0f;
    float attackCoeff;
    float releaseCoeff;

    float envelope = 0.0f; // linear peak envelope, persists across blocks
};
