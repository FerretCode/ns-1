#include "WavRecorder.h"

namespace {
EXTMEM uint8_t psramRecordBuf[WavRecorder::kMaxRecordBytes];
} // namespace

bool WavRecorder::begin(AudioRecordQueue &q) {
    queue = &q;
    return true;
}

bool WavRecorder::start(const char *path) {
    if (!queue) return false;
    if (recording) stop();

    snprintf(targetPath, sizeof(targetPath), "%s", path);
    dataBytes = 0;
    queue->clear();
    queue->begin();
    recording = true;
    return true;
}

void WavRecorder::update() {
    if (!queue || !recording) return;
    while (queue->available() > 0) {
        uint8_t *buf = (uint8_t *)queue->readBuffer();
        if (buf) {
            if (dataBytes + 256 <= kMaxRecordBytes) {
                memcpy(psramRecordBuf + dataBytes, buf, 256);
                dataBytes += 256;
            }
        }
        queue->freeBuffer();
    }
}

bool WavRecorder::stop() {
    if (!recording) return false;
    queue->end();
    // Drain remaining queue blocks into PSRAM
    update();
    queue->clear();
    recording = false;

    if (dataBytes == 0) return false;

    int16_t *samples = (int16_t *)psramRecordBuf;
    uint32_t sampleCount = dataBytes / 2;

    // 1. Auto-trim leading silence (silence threshold = 300 PCM count, ~-40dBFS)
    uint32_t firstNonSilent = 0;
    constexpr int16_t kSilenceThreshold = 300;
    while (firstNonSilent < sampleCount) {
        int16_t val = samples[firstNonSilent];
        if (val > kSilenceThreshold || val < -kSilenceThreshold) {
            break;
        }
        firstNonSilent++;
    }

    // Keep up to 32 pre-attack samples (~0.7ms) for clickless attack start
    uint32_t startSample = 0;
    if (firstNonSilent > 32) {
        startSample = firstNonSilent - 32;
    }

    if (startSample < sampleCount) {
        samples += startSample;
        sampleCount -= startSample;
        dataBytes = sampleCount * 2;
    }

    // 2. Auto-peak normalization to ~92% / -0.7 dBFS (target peak 30000)
    int32_t maxPeak = 0;
    for (uint32_t i = 0; i < sampleCount; i++) {
        int32_t val = abs((int32_t)samples[i]);
        if (val > maxPeak) maxPeak = val;
    }

    if (maxPeak > 500 && maxPeak < 30000) {
        float normGain = 30000.0f / (float)maxPeak;
        for (uint32_t i = 0; i < sampleCount; i++) {
            int32_t scaled = (int32_t)roundf((float)samples[i] * normGain);
            if (scaled > 32767) scaled = 32767;
            if (scaled < -32768) scaled = -32768;
            samples[i] = (int16_t)scaled;
        }
    }

    // 3. Micro fade-in/fade-out at boundaries to eliminate start/end clicks
    for (uint32_t i = 0; i < 64 && i < sampleCount; i++) {
        samples[i] = (int16_t)((int32_t)samples[i] * (int32_t)i / 64);
    }
    if (sampleCount > 128) {
        for (uint32_t i = sampleCount - 128; i < sampleCount; i++) {
            uint32_t remain = sampleCount - 1 - i;
            samples[i] = (int16_t)((int32_t)samples[i] * (int32_t)remain / 128);
        }
    }

    if (SD.exists(targetPath)) SD.remove(targetPath);
    File f = SD.open(targetPath, FILE_WRITE);
    if (!f) return false;

    WavFile::writeHeader(f, NS1_SAMPLE_RATE, 1, 16, dataBytes);

    // Chunked PSRAM -> SD file write pass (32KB chunks with yield to service watchdog & system)
    const uint32_t kChunkSize = 32768; // 32 KB chunks
    uint32_t written = 0;
    uint8_t *rawBytePtr = (uint8_t *)samples;
    while (written < dataBytes) {
        uint32_t bytesToWrite = dataBytes - written;
        if (bytesToWrite > kChunkSize) bytesToWrite = kChunkSize;

        size_t n = f.write(rawBytePtr + written, bytesToWrite);
        if (n == 0) break;
        written += n;
        yield();
    }

    f.close();
    return (written == dataBytes);
}
