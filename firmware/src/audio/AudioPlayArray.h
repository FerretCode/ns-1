#pragma once

#include <Arduino.h>
#include <AudioStream.h>

class AudioPlayArray : public AudioStream {
public:
    AudioPlayArray() : AudioStream(0, NULL) {}

    void play(const int16_t *data, uint32_t length, uint32_t startFrame, uint32_t endFrame, bool loop, uint32_t initialPlayPos = 0) {
        sampleData = data;
        sampleLength = length;
        sampleStartPos = startFrame < length ? startFrame : 0;
        sampleEndPos = (endFrame > sampleStartPos && endFrame <= length) ? endFrame : length;
        uint32_t startP = (initialPlayPos >= sampleStartPos && initialPlayPos < sampleEndPos) ? initialPlayPos : sampleStartPos;
        samplePosFloat = (float)startP;
        looping = loop;
        playing = true;
    }

    void setPitchCents(int16_t cents) {
        pitchCents = cents;
        pitchStep = powf(2.0f, (float)cents / 1200.0f);
    }

    void setTrim(uint32_t startFrame, uint32_t endFrame, bool jumpNearEnd = false) {
        if (!sampleData || sampleLength == 0) return;
        sampleStartPos = startFrame < sampleLength ? startFrame : 0;
        sampleEndPos = (endFrame > sampleStartPos && endFrame <= sampleLength) ? endFrame : sampleLength;
        uint32_t curPos = (uint32_t)samplePosFloat;
        if (jumpNearEnd) {
            uint32_t preRoll = 66150; // 1.5s pre-roll before loop end
            if (sampleEndPos > sampleStartPos + preRoll) {
                samplePosFloat = (float)(sampleEndPos - preRoll);
            } else {
                samplePosFloat = (float)sampleStartPos;
            }
        } else if (curPos < sampleStartPos || curPos >= sampleEndPos) {
            samplePosFloat = (float)sampleStartPos;
        }
    }

    void stop() {
        playing = false;
        sampleData = nullptr;
        sampleLength = 0;
        sampleStartPos = 0;
        sampleEndPos = 0;
        samplePosFloat = 0.0f;
    }

    bool isPlaying() const { return playing; }

    virtual void update(void) override {
        if (!playing || !sampleData || sampleLength == 0) return;

        audio_block_t *block = allocate();
        if (!block) return;

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            uint32_t intPos = (uint32_t)samplePosFloat;
            if (intPos >= sampleEndPos || intPos >= sampleLength) {
                if (looping) {
                    samplePosFloat = (float)sampleStartPos;
                    intPos = sampleStartPos;
                } else {
                    playing = false;
                    for (; i < AUDIO_BLOCK_SAMPLES; i++) {
                        block->data[i] = 0;
                    }
                    break;
                }
            }
            block->data[i] = sampleData[intPos];
            samplePosFloat += pitchStep;
        }

        transmit(block);
        release(block);
    }

private:
    const int16_t *sampleData = nullptr;
    uint32_t sampleLength = 0;
    volatile uint32_t sampleStartPos = 0;
    volatile uint32_t sampleEndPos = 0;
    volatile float samplePosFloat = 0.0f;
    volatile int16_t pitchCents = 0;
    volatile float pitchStep = 1.0f;
    volatile bool looping = false;
    volatile bool playing = false;
};
