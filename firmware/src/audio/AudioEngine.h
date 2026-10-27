#pragma once

#include "AudioPlayArray.h"
#include "effects/AudioEffectCompressor.h"
#include "effects/AudioEffectHardClip.h"
#include "WavRecorder.h"
#include "../config/Config.h"

// Owns the entire audio graph: an 8-voice SD-streaming sample player pool
// (voice-stolen across 16 pads), a mono FX bus (compressor -> grit ->
// saturation -> hard clip), and a recorder that can capture line-in, USB-in,
// or the FX bus itself ("resampling").
//
// Signal path (deliberately summed to MONO after the voice mixer — this is
// an intentional choice, not a limitation: it's authentic to the
// SP-1200/MPC-style lo-fi character the brief asks for, and it means the
// whole FX chain only has to exist once. The mono signal is duplicated to
// both output channels for the codec and USB. See firmware/README.md for
// the full rationale and how to extend to true stereo later.
//
//   8x AudioPlaySdWav ---+
//                        +-> fold to mono -> masterMix --> compressor -> bitcrusher (grit)
//   line-in (monitor)  --+                       ^              -> waveshaper (saturation)
//   usb-in  (monitor)  --+                       |              -> hardClip
//                                                 |                    |
//                              recordSourceMix <-------- line-in / usb-in / FX tail
//                                     |                             |
//                                 recQueue                  lineOut + usbOut
//                                     |
//                                WavRecorder -> SD
class AudioEngine {
public:
    enum class RecordSource { LineIn, UsbIn, Resample };

    void begin();
    void update(); // call every loop() iteration

    // --- Pad playback -------------------------------------------------
    void triggerPad(uint8_t padIndex, const int16_t *data, uint32_t length, bool loop, int8_t chokeGroup, uint8_t volume = 100, uint16_t startTenThousand = 0, uint16_t endTenThousand = 10000, bool startNearEnd = false);
    void updatePadTrim(uint8_t padIndex, uint32_t length, uint16_t startTenThousand, uint16_t endTenThousand, bool jumpNearEnd = false);
    void stopPad(uint8_t padIndex);
    void stopAllVoices();
    bool isPadPlaying(uint8_t padIndex) const;
    void setPadVolume(uint8_t padIndex, uint8_t volume);

    void setPadLooping(uint8_t padIndex, bool loop);

    // --- FX (all inputs normalized 0..1, driven by pots / encoders) ----
    enum class OutputRoute { Both, UsbOnly };
    void setOutputRoute(OutputRoute route);
    OutputRoute outputRoute() const { return outputRoute_; }

    void setCompressionAmount(float v); // BUS FX 1 — VCA Compressor
    void setGritAmount(float v);        // BUS FX 2 — Bit + Sample-Rate Reduction
    void setSaturationAmount(float v);  // BUS FX 3 — Waveshaper Drive
    void setClipAmount(float v);        // BUS FX 4 — Hard Clip Drive/Ceiling
    void setFilterCutoff(float v);      // BUS FX 5 — Warm Lowpass Filter Cutoff

    // --- Performance Texture / Warmth FX (FX Screen) -------------------
    void setVinylCrackleAmount(float v); // Vinyl dust pops & needle clicks
    void setTapeHissAmount(float v);     // Vintage cassette noise floor
    void setWowFlutterAmount(float v);   // Tape pitch wobble & drift

    float filterCutoff() const { return filterCutoff_; }
    float vinylCrackleAmount() const { return vinylCrackleAmount_; }
    float tapeHissAmount() const { return tapeHissAmount_; }
    float wowFlutterAmount() const { return wowFlutterAmount_; }

    void setMasterVolume(float v);      // slider — headphone + line-out level

    void setLineInMonitor(bool on);
    void setUsbInMonitor(bool on);

    // --- Sampling / resampling -----------------------------------------
    bool startRecording(const char *path, RecordSource src);
    bool stopRecording();
    bool isRecording() const { return recorder.isRecording(); }
    float recordedSeconds() const { return recorder.recordedSeconds(); }

    float audioMemoryUsagePercent() const {
        return 100.0f * (float)AudioMemoryUsageMax() / (float)NS1_AUDIO_MEMORY_BLOCKS;
    }

private:
    struct Voice {
        AudioPlayArray player;
        int8_t padIndex = -1;
        int8_t chokeGroup = -1;
        bool looping = false;
        uint8_t volume = 100;
        uint32_t triggerOrder = 0;
    };

    int findFreeVoice() const;
    int findStealVoice() const;

    Voice voices[NS1_NUM_VOICES];

    AudioInputI2S  lineIn;
    AudioOutputI2S lineOut;
    AudioInputUSB  usbIn;
    AudioOutputUSB usbOut;

    AudioMixer4 voiceFold[4];   // folds voices to mono, x4 groups
    AudioMixer4 voiceGroupMix;  // sums the 4 fold groups -> all-voices mono
    AudioMixer4 lineInFold;     // line-in L+R -> mono
    AudioMixer4 usbInFold;      // usb-in L+R -> mono
    AudioMixer4 masterMix;      // voices + line-in monitor + usb-in monitor

    AudioFilterStateVariable subHighpass; // 35Hz sub-bass highpass filter
    AudioEffectCompressor compressor;
    AudioEffectBitcrusher bitcrusher; // "grit" — library-provided
    AudioEffectWaveshaper waveshaper; // "saturation" — library-provided, custom tanh LUT
    AudioEffectHardClip   hardClip;
    AudioFilterStateVariable masterFilter;

    OutputRoute outputRoute_ = OutputRoute::Both;

    AudioSynthNoiseWhite noiseGen;
    AudioFilterStateVariable noiseFilter;
    AudioMixer4 fxOutputMix;

    float filterCutoff_ = 0.0f;
    float vinylCrackleAmount_ = 0.0f;
    float tapeHissAmount_ = 0.0f;
    float wowFlutterAmount_ = 0.0f;

    AudioMixer4      recordSourceMix; // selects line-in / usb-in / FX-tail for recording
    AudioRecordQueue recQueue;

    AudioControlSGTL5000 sgtl5000;
    WavRecorder recorder;

    AudioConnection patchVoiceToFold[NS1_NUM_VOICES] = {
        {voices[0].player, 0, voiceFold[0], 0},
        {voices[1].player, 0, voiceFold[0], 2},
        {voices[2].player, 0, voiceFold[1], 0},
        {voices[3].player, 0, voiceFold[1], 2},
        {voices[4].player, 0, voiceFold[2], 0},
        {voices[5].player, 0, voiceFold[2], 2},
        {voices[6].player, 0, voiceFold[3], 0},
        {voices[7].player, 0, voiceFold[3], 2},
    };

    AudioConnection patchFoldToGroup[4] = {
        {voiceFold[0], 0, voiceGroupMix, 0},
        {voiceFold[1], 0, voiceGroupMix, 1},
        {voiceFold[2], 0, voiceGroupMix, 2},
        {voiceFold[3], 0, voiceGroupMix, 3},
    };

    AudioConnection patchLineInFold[2] = {
        {lineIn, 0, lineInFold, 0},
        {lineIn, 1, lineInFold, 1},
    };

    AudioConnection patchUsbInFold[2] = {
        {usbIn, 0, usbInFold, 0},
        {usbIn, 1, usbInFold, 1},
    };

    AudioConnection patchToMaster[3] = {
        {voiceGroupMix, 0, masterMix, 0},
        {lineInFold, 0, masterMix, 1},
        {usbInFold, 0, masterMix, 2},
    };

    AudioConnection patchFxChain[6] = {
        {masterMix, 0, compressor, 0},
        {compressor, 0, bitcrusher, 0},
        {bitcrusher, 0, waveshaper, 0},
        {waveshaper, 0, hardClip, 0},
        {hardClip, 0, masterFilter, 0},
        {masterFilter, 0, fxOutputMix, 0},
    };

    AudioConnection patchNoiseGen[2] = {
        {noiseGen, 0, noiseFilter, 0},
        {noiseFilter, 0, fxOutputMix, 1},
    };

    AudioConnection patchToOutputs[4] = {
        {fxOutputMix, 0, lineOut, 0},
        {fxOutputMix, 0, lineOut, 1},
        {fxOutputMix, 0, usbOut, 0},
        {fxOutputMix, 0, usbOut, 1},
    };

    AudioConnection patchToRecordMix[3] = {
        {lineInFold, 0, recordSourceMix, 0},
        {usbInFold, 0, recordSourceMix, 1},
        {fxOutputMix, 0, recordSourceMix, 2},
    };

    AudioConnection patchToQueue[1] = {
        {recordSourceMix, 0, recQueue, 0},
    };
};
