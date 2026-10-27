#include "SampleSlicer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool SampleSlicer::load(const char *path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    WavFile::Info newInfo;
    bool ok = WavFile::readInfo(f, newInfo);
    if (!ok || newInfo.numChannels == 0) {
        f.close();
        return false;
    }

    info = newInfo;
    snprintf(sourcePath, sizeof(sourcePath), "%s", path);

    const uint32_t bytesPerSample = info.bitsPerSample / 8;
    const uint32_t bytesPerFrame = info.numChannels * (bytesPerSample ? bytesPerSample : 2);
    totalSamples_ = info.dataBytes / bytesPerFrame;

    // Single sequential pass building a min/max-style peak overview
    // (we only track abs-max per bucket, which is enough for a waveform
    // silhouette on a 170px-tall screen).
    memset(peakData, 0, sizeof(peakData));
    uint16_t bucketPeak[kPeakBuckets] = {0};

    // Use large 4KB chunk buffer (2048 frames) for 32x faster SD reading
    constexpr int kBufFrames = 2048;
    static uint8_t chunkBuf[kBufFrames * 32];

    // Strided sampling for large files so waveform building finishes in < 3ms
    uint32_t step = 1;
    if (totalSamples_ > 131072) {
        step = totalSamples_ / 131072;
    }

    f.seek(info.dataOffset);
    uint32_t frameIndex = 0;

    while (frameIndex < totalSamples_) {
        int framesToRead = kBufFrames;
        if (frameIndex + framesToRead > totalSamples_) framesToRead = totalSamples_ - frameIndex;
        int bytesToRead = framesToRead * bytesPerFrame;
        int got = f.read(chunkBuf, bytesToRead);
        int gotFrames = got / (int)bytesPerFrame;
        if (gotFrames <= 0) break;

        for (int i = 0; i < gotFrames; i += step) {
            const uint8_t *samplePtr = chunkBuf + i * bytesPerFrame;
            int16_t sample = WavFile::decodeSampleTo16(samplePtr, info.audioFormat, info.bitsPerSample);
            uint16_t mag = (uint16_t)abs((int)sample);
            uint32_t bucket = (uint64_t)(frameIndex + i) * kPeakBuckets / totalSamples_;
            if (bucket >= kPeakBuckets) bucket = kPeakBuckets - 1;
            if (mag > bucketPeak[bucket]) bucketPeak[bucket] = mag;
        }
        frameIndex += gotFrames;
    }
    f.close();

    for (int b = 0; b < kPeakBuckets; b++) {
        peakData[b] = (uint8_t)(bucketPeak[b] >> 7); // 0..32767 -> 0..255
    }

    numSlices = NS1_NUM_PADS;
    redistribute();
    return true;
}

void SampleSlicer::redistribute() {
    if (totalSamples_ == 0 || numSlices == 0) return;
    for (uint8_t i = 0; i < numSlices; i++) {
        sliceStart[i] = (uint64_t)i * totalSamples_ / numSlices;
        sliceEnd[i] = (uint64_t)(i + 1) * totalSamples_ / numSlices;
        if (sliceEnd[i] > 0) sliceEnd[i]--;
    }
}

void SampleSlicer::setSliceCount(uint8_t n) {
    if (n < 1) n = 1;
    if (n > NS1_NUM_PADS) n = NS1_NUM_PADS;
    numSlices = n;
    redistribute();
}

void SampleSlicer::nudgeStart(uint8_t i, int32_t deltaSamples) {
    if (i >= numSlices || totalSamples_ == 0) return;
    int64_t v = (int64_t)sliceStart[i] + deltaSamples;
    int64_t minBound = (i > 0) ? (int64_t)sliceStart[i - 1] + 101 : 0;
    int64_t maxBound = (int64_t)sliceEnd[i] - 100;

    if (v < minBound) v = minBound;
    if (v > maxBound) v = maxBound;

    sliceStart[i] = (uint32_t)v;
    if (i > 0) {
        sliceEnd[i - 1] = (sliceStart[i] > 0) ? sliceStart[i] - 1 : 0;
    }
}

void SampleSlicer::nudgeEnd(uint8_t i, int32_t deltaSamples) {
    if (i >= numSlices || totalSamples_ == 0) return;
    int64_t v = (int64_t)sliceEnd[i] + deltaSamples;
    int64_t minBound = (int64_t)sliceStart[i] + 100;
    int64_t maxBound = (i + 1 < numSlices) ? (int64_t)sliceEnd[i + 1] - 101 : (int64_t)totalSamples_ - 1;

    if (v < minBound) v = minBound;
    if (v > maxBound) v = maxBound;

    sliceEnd[i] = (uint32_t)v;
    if (i + 1 < numSlices) {
        sliceStart[i + 1] = sliceEnd[i] + 1;
    }
}

uint32_t SampleSlicer::decodeSliceToMemory(uint8_t sliceIdx, int16_t *outBuf, uint32_t maxSamples) {
    if (!outBuf || sliceIdx >= numSlices || maxSamples < 2) return 0;
    if (sliceEnd[sliceIdx] <= sliceStart[sliceIdx]) return 0;

    File src = SD.open(sourcePath, FILE_READ);
    if (!src) return 0;

    const uint32_t bytesPerSample = info.bitsPerSample / 8;
    const uint32_t bytesPerFrame = info.numChannels * (bytesPerSample ? bytesPerSample : 2);

    uint32_t frameCount = sliceEnd[sliceIdx] - sliceStart[sliceIdx] + 1;
    if (frameCount > maxSamples) frameCount = maxSamples;
    if (frameCount < 2) {
        src.close();
        return 0;
    }

    uint32_t byteStart = info.dataOffset + sliceStart[sliceIdx] * bytesPerFrame;
    src.seek(byteStart);

    constexpr int kFrames = 512;
    static uint8_t rawBuf[kFrames * 32];

    uint32_t decodedCount = 0;
    uint32_t framesLeft = frameCount;

    while (framesLeft > 0) {
        int framesThisChunk = framesLeft < (uint32_t)kFrames ? (int)framesLeft : kFrames;
        int got = src.read(rawBuf, framesThisChunk * bytesPerFrame);
        int gotFrames = got / (int)bytesPerFrame;
        if (gotFrames <= 0) break;

        for (int f = 0; f < gotFrames; f++) {
            const uint8_t *framePtr = rawBuf + f * bytesPerFrame;
            int32_t sum = 0;
            for (uint16_t ch = 0; ch < info.numChannels; ch++) {
                const uint8_t *samplePtr = framePtr + ch * bytesPerSample;
                sum += WavFile::decodeSampleTo16(samplePtr, info.audioFormat, info.bitsPerSample);
            }
            int16_t sample = (int16_t)(sum / (int32_t)info.numChannels);

            // Boundary micro fade-in/out for smooth auditioning
            if (decodedCount < 64) {
                sample = (int16_t)(sample * (int32_t)decodedCount / 64);
            }
            if (frameCount > 128 && decodedCount >= frameCount - 128) {
                uint32_t remain = frameCount - 1 - decodedCount;
                sample = (int16_t)(sample * (int32_t)remain / 128);
            }

            outBuf[decodedCount++] = sample;
        }
        framesLeft -= gotFrames;
    }

    src.close();
    return decodedCount;
}

bool SampleSlicer::commitToBank(BankManager &bank) {
    File src = SD.open(sourcePath, FILE_READ);
    if (!src) return false;

    // Sanitize slice boundaries: strictly monotonic and non-overlapping
    for (uint8_t i = 0; i < numSlices; i++) {
        if (i == 0) {
            if (sliceStart[0] >= totalSamples_) sliceStart[0] = 0;
        } else {
            if (sliceStart[i] <= sliceStart[i - 1] + 100) {
                sliceStart[i] = sliceStart[i - 1] + 101;
            }
            sliceEnd[i - 1] = sliceStart[i] - 1;
        }
        if (sliceEnd[i] <= sliceStart[i] + 100) {
            sliceEnd[i] = sliceStart[i] + 100;
        }
    }
    if (numSlices > 0 && sliceEnd[numSlices - 1] >= totalSamples_) {
        sliceEnd[numSlices - 1] = totalSamples_ - 1;
    }

    const uint32_t bytesPerSample = info.bitsPerSample / 8;
    const uint32_t bytesPerFrame = info.numChannels * (bytesPerSample ? bytesPerSample : 2);
    const int kFrames = 128;
    uint8_t rawBuf[kFrames * 32];
    int16_t outBuf[kFrames];     // mono out

    bool allOk = true;

    for (uint8_t i = 0; i < numSlices; i++) {
        uint32_t frameCount = sliceEnd[i] - sliceStart[i] + 1;
        uint32_t byteStart = info.dataOffset + sliceStart[i] * bytesPerFrame;
        uint32_t outBytes = frameCount * 2; // mono 16-bit

        snprintf(bank.pad(i).filename, sizeof(bank.pad(i).filename), "PAD%02d.WAV", i);
        snprintf(bank.pad(i).name, sizeof(bank.pad(i).name), "SLICE%02d", i);

        char destPath[96];
        bank.padFilePath(i, destPath, sizeof(destPath));
        if (SD.exists(destPath)) SD.remove(destPath);

        char origPath[96];
        bank.padOrigFilePath(i, origPath, sizeof(origPath));
        if (SD.exists(origPath)) SD.remove(origPath);

        File dst = SD.open(destPath, FILE_WRITE);
        if (!dst) {
            allOk = false;
            continue;
        }
        WavFile::writeHeader(dst, NS1_SAMPLE_RATE, 1, 16, outBytes);

        src.seek(byteStart);
        uint32_t framesLeft = frameCount;
        while (framesLeft > 0) {
            int framesThisChunk = framesLeft < (uint32_t)kFrames ? (int)framesLeft : kFrames;
            int got = src.read(rawBuf, framesThisChunk * bytesPerFrame);
            int gotFrames = got / (int)bytesPerFrame;
            if (gotFrames <= 0) break;

            for (int f = 0; f < gotFrames; f++) {
                const uint8_t *framePtr = rawBuf + f * bytesPerFrame;
                int32_t sum = 0;
                for (uint16_t ch = 0; ch < info.numChannels; ch++) {
                    const uint8_t *samplePtr = framePtr + ch * bytesPerSample;
                    sum += WavFile::decodeSampleTo16(samplePtr, info.audioFormat, info.bitsPerSample);
                }
                int16_t sample = (int16_t)(sum / (int32_t)info.numChannels);

                uint32_t currentFrame = frameCount - framesLeft + f;
                if (currentFrame < 64) {
                    sample = (int16_t)(sample * (int32_t)currentFrame / 64);
                }
                if (frameCount > 128 && currentFrame >= frameCount - 128) {
                    uint32_t remain = frameCount - 1 - currentFrame;
                    sample = (int16_t)(sample * (int32_t)remain / 128);
                }

                outBuf[f] = sample;
            }
            dst.write((const uint8_t *)outBuf, gotFrames * 2);
            framesLeft -= gotFrames;
        }

        dst.close();

        bank.pad(i).assigned = true;
        bank.pad(i).startTrim = 0;
        bank.pad(i).endTrim = 10000;
        bank.pad(i).pitchCents = 0;
        bank.pad(i).volume = 100;
        bank.pad(i).gate = false;
        bank.pad(i).loop = false;
    }

    // Unassign unused pads beyond numSlices in the current bank
    for (uint8_t i = numSlices; i < NS1_NUM_PADS; i++) {
        snprintf(bank.pad(i).filename, sizeof(bank.pad(i).filename), "PAD%02d.WAV", i);
        bank.pad(i).name[0] = '\0';
        bank.pad(i).assigned = false;

        char destPath[96], origPath[96];
        bank.padFilePath(i, destPath, sizeof(destPath));
        bank.padOrigFilePath(i, origPath, sizeof(origPath));
        if (SD.exists(destPath)) SD.remove(destPath);
        if (SD.exists(origPath)) SD.remove(origPath);
    }

    src.close();
    bank.saveBank();
    bank.reloadAllPsramSamples();
    return allOk;
}
