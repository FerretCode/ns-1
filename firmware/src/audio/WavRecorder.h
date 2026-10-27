#pragma once

#include <Audio.h>
#include <SD.h>
#include "WavFile.h"
#include "../config/Config.h"

// Drains an AudioRecordQueue to a mono 16-bit/44.1kHz WAV file on SD.
// Used for both "sampling in" (line-in/USB source feeding the queue) and
// "resampling" (the post-FX master bus feeding the queue) — AudioEngine
// decides what's patched into the queue's input; this class only knows how
// to get bytes onto the card.
//
// update() must be called frequently from loop() (not an ISR) — it does the
// actual SD.write() calls, which are too slow to run in the audio ISR.
class WavRecorder {
public:
    static constexpr uint32_t kMaxRecordBytes = 5 * 1024 * 1024; // 5MB EXTMEM PSRAM buffer (56.8s mono 44.1kHz audio)

    bool begin(AudioRecordQueue &queue);

    // Starts recording into PSRAM buffer (zero SD card writes during recording).
    bool start(const char *path);

    // Call every loop() iteration while recording to drain queue buffers into PSRAM.
    void update();

    // Writes the recorded PSRAM buffer to SD file, finalizes WAV header, and closes file.
    bool stop();

    bool isRecording() const { return recording; }
    uint32_t recordedBytes() const { return dataBytes; }
    float recordedSeconds() const { return (float)(dataBytes / 2) / (float)NS1_SAMPLE_RATE; }

private:
    AudioRecordQueue *queue = nullptr;
    uint32_t dataBytes = 0;
    bool recording = false;
    char targetPath[96] = {0};
};
