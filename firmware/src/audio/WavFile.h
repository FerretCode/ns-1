#pragma once

#include <Arduino.h>
#include <SD.h>

// Minimal helpers for reading/writing canonical 16-bit PCM WAV files on SD.
// Used by WavRecorder (writing takes), SampleSlicer (chopping a take into
// per-pad files) and BankManager (the pitch/"tape speed" feature, which
// actually resamples the PCM data — see resample() below for why).
namespace WavFile {

struct Info {
    uint32_t sampleRate = 0;
    uint16_t numChannels = 0;
    uint16_t bitsPerSample = 0;
    uint16_t audioFormat = 1; // 1 = PCM, 3 = IEEE Float
    uint32_t dataOffset = 0; // byte offset in file where PCM samples start
    uint32_t dataBytes = 0;  // length of the data chunk
};

// Decodes a single sample from raw little-endian bytes according to audioFormat and bitsPerSample,
// returning a signed 16-bit PCM integer (-32768..32767).
int16_t decodeSampleTo16(const uint8_t *ptr, uint16_t audioFormat, uint16_t bitsPerSample);

// Writes a 44-byte canonical PCM header at the file's current position
// (expected to be offset 0). dataBytes may be 0 if not yet known — call
// patchDataSize() later once the real length is known (this is how
// streaming recorders work: write the header up front with placeholder
// sizes, stream PCM data, then patch the sizes in on close()).
bool writeHeader(File &f, uint32_t sampleRate, uint16_t numChannels,
                  uint16_t bitsPerSample, uint32_t dataBytes);

// Rewrites the RIFF chunk size and data chunk size fields once the real
// PCM byte count is known. Leaves the file position where it found it.
bool patchDataSize(File &f, uint32_t dataBytes);

// Reads srcPath (mono 16-bit PCM) and writes a resampled copy to dstPath
// (linear interpolation), declaring dstSampleRate in the output header.
// speedRatio is "source samples consumed per output sample":
//   - Pitch/"tape speed" shifting: dstSampleRate == the source's own rate,
//     speedRatio = 2^(cents/1200). >1 plays back faster/higher (fewer,
//     shorter output frames); <1 is slower/lower and longer. This is
//     deliberately real resampling of the PCM data, not a header trick:
//     AudioPlaySdWav's parse_format() only accepts an EXACT 44100/22050/
//     11025 Hz in the header (see play_sd_wav.cpp) and silently refuses to
//     play anything else, so simply rewriting the sample-rate field (an
//     earlier, broken version of this feature) made a pad go mute for
//     almost any pitch value instead of changing its pitch.
//   - True sample-rate conversion (e.g. importing a 48kHz file so it plays
//     at the right pitch/duration): dstSampleRate = NS1_SAMPLE_RATE,
//     speedRatio = sourceRate / (float)NS1_SAMPLE_RATE.
// dstPath may equal srcPath's usual "live" pad file, but must NOT be
// srcPath itself in the same call (reads and writes interleave). Returns
// false if srcPath isn't a readable mono 16-bit WAV.
bool resample(const char *srcPath, const char *dstPath, float speedRatio, uint32_t dstSampleRate);

// Crops srcPath between startTenThousand (0..9999) and endTenThousand (1..10000) and writes a resampled copy to dstPath.
bool cropAndResample(const char *srcPath, const char *dstPath, float speedRatio,
                    uint32_t dstSampleRate, uint16_t startTenThousand, uint16_t endTenThousand);

// Converts any WAV file (8/16/24/32-bit PCM or 32-bit float) into a 16-bit 44.1kHz mono WAV file.
// If maxSeconds > 0.0f, limits output to maxSeconds for fast preview decoding.
bool convertTo16BitMono(const char *srcPath, const char *dstPath, float maxSeconds = 0.0f);

// Decodes up to maxSamples from srcPath directly into a 16-bit PCM memory array for zero-SD-write memory playback.
uint32_t decodeToMemory(const char *srcPath, int16_t *outBuf, uint32_t maxSamples);

// Parses fmt/data chunks, skipping any other chunks in between. Returns
// false if the file isn't a readable PCM WAV.
bool readInfo(File &f, Info &info);

} // namespace WavFile
