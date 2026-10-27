#pragma once

#include <Arduino.h>
#include <SD.h>
#include "../audio/WavFile.h"
#include "BankManager.h"

// Implements the "chop" workflow: load a recorded take, view a peak
// waveform overview, define up to 16 slice points, and commit — which
// physically copies each slice's PCM range into its own PAD*.WAV file
// (folding to mono on the way if the source is stereo) and updates the
// bank. Slices become ordinary, zero-offset pad samples: no runtime
// seeking is needed to play them.
class SampleSlicer {
public:
    static constexpr int kPeakBuckets = 256;

    // Reads the WAV header, builds the peak overview, and resets to an
    // even 16-way split. Returns false if the file can't be read as PCM.
    bool load(const char *sourcePath);

    uint32_t totalSamples() const { return totalSamples_; }
    const uint8_t *peaks() const { return peakData; }

    uint8_t sliceCount() const { return numSlices; }
    void setSliceCount(uint8_t n); // 1..NS1_NUM_PADS, redistributes evenly

    uint32_t sliceStartSample(uint8_t i) const { return sliceStart[i]; }
    uint32_t sliceEndSample(uint8_t i) const { return sliceEnd[i]; }

    void nudgeStart(uint8_t i, int32_t deltaSamples);
    void nudgeEnd(uint8_t i, int32_t deltaSamples);

    // Decodes a specific slice's PCM samples into a memory buffer for instant auditioning
    uint32_t decodeSliceToMemory(uint8_t sliceIdx, int16_t *outBuf, uint32_t maxSamples);

    // Writes each slice to PAD00.WAV..PAD(n-1).WAV in the bank's directory
    // and persists the bank. Existing pads beyond numSlices are unassigned.
    bool commitToBank(BankManager &bank);

private:
    char sourcePath[96] = {0};
    WavFile::Info info;
    uint32_t totalSamples_ = 0;

    uint8_t numSlices = NS1_NUM_PADS;
    uint32_t sliceStart[NS1_NUM_PADS] = {0};
    uint32_t sliceEnd[NS1_NUM_PADS] = {0};

    uint8_t peakData[kPeakBuckets] = {0};

    void redistribute();
};
