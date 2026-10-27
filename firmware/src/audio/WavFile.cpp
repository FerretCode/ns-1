#include "WavFile.h"

namespace {

void writeU32LE(File &f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v), (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    f.write(b, 4);
}

void writeU16LE(File &f, uint16_t v) {
    uint8_t b[2] = {(uint8_t)(v), (uint8_t)(v >> 8)};
    f.write(b, 2);
}

uint32_t readU32LE(File &f) {
    uint8_t b[4];
    if (f.read(b, 4) != 4) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint16_t readU16LE(File &f) {
    uint8_t b[2];
    if (f.read(b, 2) != 2) return 0;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

bool matchTag(const uint8_t *b, const char *tag) {
    return b[0] == tag[0] && b[1] == tag[1] && b[2] == tag[2] && b[3] == tag[3];
}

} // namespace

namespace WavFile {

bool writeHeader(File &f, uint32_t sampleRate, uint16_t numChannels,
                  uint16_t bitsPerSample, uint32_t dataBytes) {
    const uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
    const uint16_t blockAlign = numChannels * (bitsPerSample / 8);

    f.seek(0);
    f.write((const uint8_t *)"RIFF", 4);
    writeU32LE(f, 36 + dataBytes);
    f.write((const uint8_t *)"WAVE", 4);

    f.write((const uint8_t *)"fmt ", 4);
    writeU32LE(f, 16); // PCM fmt chunk size
    writeU16LE(f, 1);  // PCM
    writeU16LE(f, numChannels);
    writeU32LE(f, sampleRate);
    writeU32LE(f, byteRate);
    writeU16LE(f, blockAlign);
    writeU16LE(f, bitsPerSample);

    f.write((const uint8_t *)"data", 4);
    writeU32LE(f, dataBytes);

    return true; // header is exactly 44 bytes; PCM data follows
}

bool patchDataSize(File &f, uint32_t dataBytes) {
    if (!f) return false;
    f.seek(4);
    writeU32LE(f, 36 + dataBytes);
    f.seek(40);
    writeU32LE(f, dataBytes);
    return true;
}

bool resample(const char *srcPath, const char *dstPath, float speedRatio, uint32_t dstSampleRate) {
    if (speedRatio <= 0.0f) return false;

    File src = SD.open(srcPath, FILE_READ);
    if (!src) return false;

    Info info;
    if (!readInfo(src, info) || info.bitsPerSample != 16 || info.numChannels != 1) {
        src.close();
        return false;
    }

    uint32_t srcFrames = info.dataBytes / 2;
    if (srcFrames < 2) {
        src.close();
        return false;
    }

    uint32_t dstFrames = (uint32_t)(((float)srcFrames / speedRatio) + 0.5f);
    if (dstFrames < 1) dstFrames = 1;

    if (SD.exists(dstPath)) SD.remove(dstPath);
    File dst = SD.open(dstPath, FILE_WRITE);
    if (!dst) {
        src.close();
        return false;
    }
    writeHeader(dst, dstSampleRate, 1, 16, dstFrames * 2);

    // Sliding window of source samples so we're not seeking the SD card on
    // every single output frame — refilled only when interpolation walks
    // past its end. speedRatio here is always within a couple octaves
    // (see BankManager's clamp), so this covers many output frames per
    // fill even at the extremes.
    const int32_t kWindowFrames = 512;
    int16_t window[kWindowFrames];
    int32_t windowStart = -1;
    int32_t windowLen = 0;

    for (uint32_t i = 0; i < dstFrames; i++) {
        float srcPos = (float)i * speedRatio;
        int32_t i0 = (int32_t)srcPos;
        float frac = srcPos - (float)i0;
        int32_t i1 = i0 + 1;
        if (i0 >= (int32_t)srcFrames) i0 = srcFrames - 1;
        if (i1 >= (int32_t)srcFrames) i1 = srcFrames - 1;

        if (windowStart < 0 || i1 >= windowStart + windowLen) {
            windowStart = i0;
            src.seek(info.dataOffset + (uint32_t)windowStart * 2);
            int32_t want = kWindowFrames;
            if (windowStart + want > (int32_t)srcFrames) want = srcFrames - windowStart;
            windowLen = src.read((uint8_t *)window, want * 2) / 2;
        }

        int16_t s0 = window[i0 - windowStart];
        int16_t s1 = window[i1 - windowStart];
        int16_t outSample = (int16_t)((float)s0 + ((float)s1 - (float)s0) * frac);
        if (i < 64) {
            outSample = (int16_t)(outSample * (int32_t)i / 64);
        } else if (dstFrames > 128 && i >= dstFrames - 128) {
            uint32_t remain = dstFrames - 1 - i;
            outSample = (int16_t)(outSample * (int32_t)remain / 128);
        }
        dst.write((const uint8_t *)&outSample, 2);
    }

    dst.close();
    src.close();
    return true;
}

bool cropAndResample(const char *srcPath, const char *dstPath, float speedRatio,
                    uint32_t dstSampleRate, uint16_t startTenThousand, uint16_t endTenThousand) {
    if (speedRatio <= 0.0f) return false;
    if (startTenThousand >= endTenThousand) return false;

    File src = SD.open(srcPath, FILE_READ);
    if (!src) return false;

    Info info;
    if (!readInfo(src, info) || info.bitsPerSample != 16 || info.numChannels != 1) {
        src.close();
        return false;
    }

    uint32_t totalSrcFrames = info.dataBytes / 2;
    if (totalSrcFrames < 2) {
        src.close();
        return false;
    }

    uint32_t startFrame = (uint32_t)((uint64_t)totalSrcFrames * startTenThousand / 10000);
    uint32_t endFrame = (uint32_t)((uint64_t)totalSrcFrames * endTenThousand / 10000);
    if (endFrame <= startFrame) endFrame = startFrame + 1;
    if (endFrame > totalSrcFrames) endFrame = totalSrcFrames;

    uint32_t srcFrames = endFrame - startFrame;
    if (srcFrames < 2) {
        src.close();
        return false;
    }

    uint32_t dstFrames = (uint32_t)(((float)srcFrames / speedRatio) + 0.5f);
    if (dstFrames < 1) dstFrames = 1;

    if (SD.exists(dstPath)) SD.remove(dstPath);
    File dst = SD.open(dstPath, FILE_WRITE);
    if (!dst) {
        src.close();
        return false;
    }
    writeHeader(dst, dstSampleRate, 1, 16, dstFrames * 2);

    const int32_t kWindowFrames = 512;
    int16_t window[kWindowFrames];
    int32_t windowStart = -1;
    int32_t windowLen = 0;

    for (uint32_t i = 0; i < dstFrames; i++) {
        float srcPos = (float)i * speedRatio;
        int32_t i0 = (int32_t)srcPos;
        float frac = srcPos - (float)i0;
        int32_t i1 = i0 + 1;
        if (i0 >= (int32_t)srcFrames) i0 = srcFrames - 1;
        if (i1 >= (int32_t)srcFrames) i1 = srcFrames - 1;

        if (windowStart < 0 || i0 < windowStart || i1 >= windowStart + windowLen) {
            windowStart = i0;
            src.seek(info.dataOffset + (uint32_t)(startFrame + windowStart) * 2);
            int32_t want = kWindowFrames;
            if (windowStart + want > (int32_t)srcFrames) want = srcFrames - windowStart;
            windowLen = src.read((uint8_t *)window, want * 2) / 2;
        }

        int16_t s0 = window[i0 - windowStart];
        int16_t s1 = window[i1 - windowStart];
        int16_t outSample = (int16_t)((float)s0 + ((float)s1 - (float)s0) * frac);
        if (i < 64) {
            outSample = (int16_t)(outSample * (int32_t)i / 64);
        } else if (dstFrames > 128 && i >= dstFrames - 128) {
            uint32_t remain = dstFrames - 1 - i;
            outSample = (int16_t)(outSample * (int32_t)remain / 128);
        }
        dst.write((const uint8_t *)&outSample, 2);
    }

    dst.close();
    src.close();
    return true;
}

int16_t decodeSampleTo16(const uint8_t *ptr, uint16_t audioFormat, uint16_t bitsPerSample) {
    if (audioFormat == 3) { // 32-bit IEEE float
        float fval;
        memcpy(&fval, ptr, 4);
        if (fval > 1.0f) fval = 1.0f;
        if (fval < -1.0f) fval = -1.0f;
        return (int16_t)(fval * 32767.0f);
    }
    // Integer PCM (audioFormat == 1)
    switch (bitsPerSample) {
    case 8: {
        // 8-bit unsigned PCM in WAV (0..255, center 128)
        int sample = (int)ptr[0] - 128;
        return (int16_t)(sample << 8);
    }
    case 16: {
        // 16-bit signed LE
        return (int16_t)(ptr[0] | (ptr[1] << 8));
    }
    case 24: {
        // 24-bit signed LE
        int32_t val = (int32_t)((uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16));
        if (val & 0x800000) val |= 0xFF000000;
        return (int16_t)(val >> 8);
    }
    case 32: {
        // 32-bit signed LE
        int32_t val = (int32_t)((uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24));
        return (int16_t)(val >> 16);
    }
    default:
        return 0;
    }
}

bool readInfo(File &f, Info &info) {
    f.seek(0);
    uint8_t tag[4];
    if (f.read(tag, 4) != 4 || !matchTag(tag, "RIFF")) return false;
    readU32LE(f); // riff size, unused
    if (f.read(tag, 4) != 4 || !matchTag(tag, "WAVE")) return false;

    bool haveFmt = false;
    bool haveData = false;

    while (f.available()) {
        if (f.read(tag, 4) != 4) break;
        uint32_t chunkSize = readU32LE(f);

        if (matchTag(tag, "fmt ")) {
            uint32_t chunkStart = f.position();
            uint16_t formatTag = readU16LE(f);
            info.numChannels = readU16LE(f);
            info.sampleRate = readU32LE(f);
            readU32LE(f); // byte rate
            readU16LE(f); // block align
            info.bitsPerSample = readU16LE(f);

            info.audioFormat = formatTag;
            if (formatTag == 0xFFFE && chunkSize >= 40) {
                // WAVE_FORMAT_EXTENSIBLE: SubFormat GUID starts at chunkStart + 24
                f.seek(chunkStart + 24);
                info.audioFormat = readU16LE(f);
            }

            f.seek(chunkStart + chunkSize + (chunkSize & 1));
            haveFmt = true;
        } else if (matchTag(tag, "data")) {
            info.dataOffset = f.position();
            info.dataBytes = chunkSize;
            haveData = true;
            if (haveFmt) break; // if we already have fmt, stop reading
            f.seek(f.position() + chunkSize + (chunkSize & 1));
        } else {
            f.seek(f.position() + chunkSize + (chunkSize & 1));
        }
    }

    if (!haveFmt || !haveData) return false;

    // Validate format: audioFormat must be 1 (PCM) or 3 (Float)
    if (info.audioFormat != 1 && info.audioFormat != 3) return false;

    // Validate bitsPerSample
    if (info.audioFormat == 1) {
        if (info.bitsPerSample != 8 && info.bitsPerSample != 16 &&
            info.bitsPerSample != 24 && info.bitsPerSample != 32) {
            return false;
        }
    } else if (info.audioFormat == 3) {
        if (info.bitsPerSample != 32) {
            return false;
        }
    }

    if (info.numChannels == 0) return false;

    // Clamp dataBytes to actual remaining file size if needed
    uint32_t fSize = f.size();
    if (fSize > info.dataOffset) {
        uint32_t maxData = fSize - info.dataOffset;
        if (info.dataBytes > maxData) {
            info.dataBytes = maxData;
        }
    }

    return true;
}

bool convertTo16BitMono(const char *srcPath, const char *dstPath, float maxSeconds) {
    File src = SD.open(srcPath, FILE_READ);
    if (!src) return false;

    Info info;
    if (!readInfo(src, info) || info.numChannels == 0 || info.dataBytes == 0) {
        src.close();
        return false;
    }

    // Fast path: if already 16-bit 44.1kHz mono PCM and full conversion not explicitly capped, no conversion needed!
    if (maxSeconds <= 0.0f && info.bitsPerSample == 16 && info.numChannels == 1 && info.sampleRate == 44100 && info.audioFormat == 1) {
        src.close();
        return true;
    }

    const uint32_t bytesPerSample = info.bitsPerSample / 8;
    const uint32_t bytesPerFrame = info.numChannels * (bytesPerSample ? bytesPerSample : 2);
    uint32_t totalFrames = info.dataBytes / bytesPerFrame;
    if (maxSeconds > 0.0f) {
        uint32_t maxF = (uint32_t)(44100.0f * maxSeconds);
        if (totalFrames > maxF) totalFrames = maxF;
    }
    const uint32_t outBytes = totalFrames * 2; // mono 16-bit

    if (SD.exists(dstPath)) SD.remove(dstPath);
    File dst = SD.open(dstPath, FILE_WRITE);
    if (!dst) {
        src.close();
        return false;
    }

    writeHeader(dst, 44100, 1, 16, outBytes);

    src.seek(info.dataOffset);
    const int kFrames = 128;
    uint8_t rawBuf[kFrames * 32];
    int16_t outBuf[kFrames];

    uint32_t framesLeft = totalFrames;
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
                sum += decodeSampleTo16(samplePtr, info.audioFormat, info.bitsPerSample);
            }
            int16_t sample = (int16_t)(sum / (int32_t)info.numChannels);

            uint32_t currentFrame = totalFrames - framesLeft + f;
            if (currentFrame < 64) {
                sample = (int16_t)(sample * (int32_t)currentFrame / 64);
            }
            if (totalFrames > 128 && currentFrame >= totalFrames - 128) {
                uint32_t remain = totalFrames - 1 - currentFrame;
                sample = (int16_t)(sample * (int32_t)remain / 128);
            }
            outBuf[f] = sample;
        }
        dst.write((const uint8_t *)outBuf, gotFrames * 2);
        framesLeft -= gotFrames;
    }

    dst.close();
    src.close();
    return true;
}

uint32_t decodeToMemory(const char *srcPath, int16_t *outBuf, uint32_t maxSamples) {
    if (!outBuf || maxSamples < 2) return 0;

    File src = SD.open(srcPath, FILE_READ);
    if (!src) return 0;

    Info info;
    if (!readInfo(src, info) || info.numChannels == 0 || info.dataBytes == 0) {
        src.close();
        return 0;
    }

    const uint32_t bytesPerSample = info.bitsPerSample / 8;
    const uint32_t bytesPerFrame = info.numChannels * (bytesPerSample ? bytesPerSample : 2);
    uint32_t totalSrcFrames = info.dataBytes / bytesPerFrame;
    if (totalSrcFrames < 2) {
        src.close();
        return 0;
    }

    float speedRatio = info.sampleRate > 0 ? (float)info.sampleRate / 44100.0f : 1.0f;
    uint32_t dstFrames = (uint32_t)(((float)totalSrcFrames / speedRatio) + 0.5f);
    if (dstFrames > maxSamples) dstFrames = maxSamples;
    if (dstFrames == 0) {
        src.close();
        return 0;
    }

    constexpr int kFrames = 512;
    static int16_t pcmBuffer[kFrames];
    int32_t windowStart = -1;
    int32_t windowLen = 0;

    for (uint32_t i = 0; i < dstFrames; i++) {
        float srcPos = (float)i * speedRatio;
        int32_t i0 = (int32_t)srcPos;
        float frac = srcPos - (float)i0;
        int32_t i1 = i0 + 1;
        if (i0 >= (int32_t)totalSrcFrames) i0 = totalSrcFrames - 1;
        if (i1 >= (int32_t)totalSrcFrames) i1 = totalSrcFrames - 1;

        if (windowStart < 0 || i0 < windowStart || i1 >= windowStart + windowLen) {
            windowStart = i0;
            src.seek(info.dataOffset + (uint32_t)windowStart * bytesPerFrame);
            int32_t want = kFrames;
            if (windowStart + want > (int32_t)totalSrcFrames) want = totalSrcFrames - windowStart;

            static uint8_t rawBuf[kFrames * 32];
            int got = src.read(rawBuf, want * bytesPerFrame);
            windowLen = got / (int)bytesPerFrame;

            for (int f = 0; f < windowLen; f++) {
                const uint8_t *framePtr = rawBuf + f * bytesPerFrame;
                int32_t sum = 0;
                for (uint16_t ch = 0; ch < info.numChannels; ch++) {
                    const uint8_t *samplePtr = framePtr + ch * bytesPerSample;
                    sum += decodeSampleTo16(samplePtr, info.audioFormat, info.bitsPerSample);
                }
                pcmBuffer[f] = (int16_t)(sum / (int32_t)info.numChannels);
            }
        }

        int32_t idx0 = i0 - windowStart;
        int32_t idx1 = i1 - windowStart;
        if (idx0 < 0) idx0 = 0;
        if (idx0 >= windowLen) idx0 = windowLen - 1;
        if (idx1 < 0) idx1 = 0;
        if (idx1 >= windowLen) idx1 = windowLen - 1;

        int16_t s0 = pcmBuffer[idx0];
        int16_t s1 = pcmBuffer[idx1];
        int16_t sample = (int16_t)((float)s0 + ((float)s1 - (float)s0) * frac);

        // Micro boundary fades for clickless previewing
        if (i < 64) {
            sample = (int16_t)(sample * (int32_t)i / 64);
        }
        if (dstFrames > 128 && i >= dstFrames - 128) {
            uint32_t remain = dstFrames - 1 - i;
            sample = (int16_t)(sample * (int32_t)remain / 128);
        }

        outBuf[i] = sample;
    }

    src.close();
    return dstFrames;
}

} // namespace WavFile
