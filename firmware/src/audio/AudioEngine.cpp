#include "AudioEngine.h"
#include <math.h>
#include <stdio.h>

namespace {
// Waveshaper LUT size — must be a size AudioEffectWaveshaper accepts
// (a power of two works reliably across library versions).
constexpr int kShaperTableSize = 129;
} // namespace

void AudioEngine::begin() {
    AudioMemory(NS1_AUDIO_MEMORY_BLOCKS);

    // enable() returns false if its first I2C write to the codec fails —
    // i.e. the SGTL5000 isn't responding at all (wiring/power/address
    // issue on the audio shield). Everything downstream (volume, line
    // levels, playback) still "succeeds" in software even if this failed,
    // so it would otherwise look identical to a silent-but-triggering pad.
    bool codecOk = sgtl5000.enable();
    Serial.printf("NS-1: SGTL5000 codec %s\n", codecOk ? "OK" : "NOT RESPONDING (check audio shield wiring/seating)");

    sgtl5000.inputSelect(AUDIO_INPUT_LINEIN);
    sgtl5000.lineInLevel(5);
    sgtl5000.lineOutLevel(24);
    sgtl5000.volume(0.7f);
    sgtl5000.eqSelect(1); // 5-Band Graphic EQ
    sgtl5000.eqBands(0.0f, +0.15f, +0.35f, +0.20f, 0.0f); // Pure warm mid-range boost over original mix!

    recorder.begin(recQueue);

    for (int g = 0; g < 4; g++) {
        for (int ch = 0; ch < 4; ch++) voiceFold[g].gain(ch, 0.5f);
    }
    for (int ch = 0; ch < 4; ch++) voiceGroupMix.gain(ch, 0.707f);

    lineInFold.gain(0, 0.707f);
    lineInFold.gain(1, 0.707f);
    usbInFold.gain(0, 0.707f);
    usbInFold.gain(1, 0.707f);

    masterMix.gain(0, 1.0f); // voices — always on
    masterMix.gain(1, 0.0f); // line-in monitor — off by default
    masterMix.gain(2, 0.0f); // usb-in monitor — off by default
    masterMix.gain(3, 0.0f); // spare

    for (int ch = 0; ch < 4; ch++) recordSourceMix.gain(ch, 0.0f);

    fxOutputMix.gain(0, 1.0f); // main audio path
    fxOutputMix.gain(1, 0.0f); // noise path (vinyl crackle / tape hiss)

    noiseGen.amplitude(0.0f); // off by default
    noiseFilter.frequency(3500.0f); // bandpass/lowpass shaping for vintage noise
    noiseFilter.resonance(0.707f);

    setCompressionAmount(0.3f);
    setGritAmount(0.0f);
    setSaturationAmount(0.0f);
    setClipAmount(0.0f);
    setFilterCutoff(0.0f);
    setVinylCrackleAmount(0.0f);
    setTapeHissAmount(0.0f);
    setWowFlutterAmount(0.0f);
    setMasterVolume(0.7f);
}

void AudioEngine::update() {
    recorder.update();

    uint32_t now = millis();

    // 1. Tape Wow & Flutter pitch drift
    if (wowFlutterAmount_ > 0.001f) {
        float wowPhase = (float)(now % 1800) / 1800.0f * 6.283185f;
        float flutterPhase = (float)(now % 310) / 310.0f * 6.283185f;

        int16_t driftCents = (int16_t)roundf((sinf(wowPhase) * 14.0f + sinf(flutterPhase) * 4.0f) * wowFlutterAmount_);

        for (int i = 0; i < NS1_NUM_VOICES; i++) {
            Voice &v = voices[i];
            if (v.padIndex >= 0 && v.player.isPlaying()) {
                v.player.setPitchCents(driftCents);
            }
        }
    }

    // 2. Vinyl Dust Crackle Pops
    if (vinylCrackleAmount_ > 0.001f) {
        static uint32_t lastPopMillis = 0;
        if (now - lastPopMillis > (uint32_t)(40 + random(0, 250))) {
            lastPopMillis = now;
            float popVol = ((float)random(10, 100) / 100.0f) * vinylCrackleAmount_ * 0.12f;
            fxOutputMix.gain(1, tapeHissAmount_ * 0.06f + popVol);
        } else if (now - lastPopMillis > 12) {
            fxOutputMix.gain(1, tapeHissAmount_ * 0.06f);
        }
    }

    for (int i = 0; i < NS1_NUM_VOICES; i++) {
        Voice &v = voices[i];
        if (v.padIndex >= 0 && !v.player.isPlaying()) {
            v.padIndex = -1;
        }
    }
}

int AudioEngine::findFreeVoice() const {
    for (int i = 0; i < NS1_NUM_VOICES; i++) {
        if (!voices[i].player.isPlaying() || voices[i].padIndex < 0) return i;
    }
    return -1;
}

int AudioEngine::findStealVoice() const {
    int oldest = 0;
    uint32_t oldestOrder = voices[0].triggerOrder;
    for (int i = 1; i < NS1_NUM_VOICES; i++) {
        if (voices[i].triggerOrder < oldestOrder) {
            oldestOrder = voices[i].triggerOrder;
            oldest = i;
        }
    }
    return oldest;
}

void AudioEngine::triggerPad(uint8_t padIndex, const int16_t *data, uint32_t length, bool loop, int8_t chokeGroup, uint8_t volume, uint16_t startTenThousand, uint16_t endTenThousand, bool startNearEnd) {
    static uint32_t triggerCounter = 0;

    if (!data || length == 0) return;

    if (chokeGroup >= 0) {
        for (int i = 0; i < NS1_NUM_VOICES; i++) {
            if (voices[i].padIndex >= 0 && voices[i].chokeGroup == chokeGroup) {
                voices[i].player.stop();
                voices[i].padIndex = -1;
            }
        }
    }

    int idx = findFreeVoice();
    if (idx < 0) idx = findStealVoice();

    Voice &v = voices[idx];
    v.player.stop();
    v.padIndex = padIndex;
    v.chokeGroup = chokeGroup;
    v.looping = loop;
    v.volume = volume;
    v.triggerOrder = ++triggerCounter;

    int group = idx / 2;
    int ch = (idx % 2) * 2;
    float gain = (volume / 100.0f) * 0.25f;
    voiceFold[group].gain(ch, gain);
    voiceFold[group].gain(ch + 1, gain);

    uint32_t startFrame = (uint32_t)((uint64_t)length * startTenThousand / 10000);
    uint32_t endFrame = (uint32_t)((uint64_t)length * endTenThousand / 10000);
    if (endFrame <= startFrame) endFrame = startFrame + 1;
    if (endFrame > length) endFrame = length;

    uint32_t initialPos = startFrame;
    if (startNearEnd) {
        uint32_t preRoll = 66150; // 1.5s pre-roll
        if (endFrame > startFrame + preRoll) {
            initialPos = endFrame - preRoll;
        }
    }

    v.player.play(data, length, startFrame, endFrame, loop, initialPos);
}

void AudioEngine::updatePadTrim(uint8_t padIndex, uint32_t length, uint16_t startTenThousand, uint16_t endTenThousand, bool jumpNearEnd) {
    if (length == 0) return;
    uint32_t startFrame = (uint32_t)((uint64_t)length * startTenThousand / 10000);
    uint32_t endFrame = (uint32_t)((uint64_t)length * endTenThousand / 10000);
    if (endFrame <= startFrame) endFrame = startFrame + 1;
    if (endFrame > length) endFrame = length;

    for (int i = 0; i < NS1_NUM_VOICES; i++) {
        if (voices[i].padIndex == (int8_t)padIndex) {
            voices[i].player.setTrim(startFrame, endFrame, jumpNearEnd);
        }
    }
}

void AudioEngine::stopPad(uint8_t padIndex) {
    for (int i = 0; i < NS1_NUM_VOICES; i++) {
        if (voices[i].padIndex == (int8_t)padIndex) {
            voices[i].player.stop();
            voices[i].padIndex = -1;
        }
    }
}

void AudioEngine::setPadLooping(uint8_t padIndex, bool loop) {
    for (int i = 0; i < NS1_NUM_VOICES; i++) {
        if (voices[i].padIndex == (int8_t)padIndex) {
            voices[i].looping = loop;
        }
    }
}

void AudioEngine::setPadVolume(uint8_t padIndex, uint8_t volume) {
    for (int i = 0; i < NS1_NUM_VOICES; i++) {
        if (voices[i].padIndex == (int8_t)padIndex) {
            voices[i].volume = volume;
            int group = i / 2;
            int ch = (i % 2) * 2;
            float gain = (volume / 100.0f) * 0.5f;
            voiceFold[group].gain(ch, gain);
            voiceFold[group].gain(ch + 1, gain);
        }
    }
}

void AudioEngine::stopAllVoices() {
    for (int i = 0; i < NS1_NUM_VOICES; i++) {
        voices[i].player.stop();
        voices[i].padIndex = -1;
    }
}

bool AudioEngine::isPadPlaying(uint8_t padIndex) const {
    for (int i = 0; i < NS1_NUM_VOICES; i++) {
        if (voices[i].padIndex == (int8_t)padIndex && voices[i].player.isPlaying()) return true;
    }
    return false;
}

void AudioEngine::setCompressionAmount(float v) {
    compressor.amount(v);
}

void AudioEngine::setGritAmount(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    // 16 bits / 44.1kHz (clean) down to 4 bits / 4kHz (crushed).
    uint16_t bits = (uint16_t)roundf(16.0f - v * 12.0f);
    uint32_t rate = (uint32_t)roundf(44100.0f - v * (44100.0f - 4000.0f));
    bitcrusher.bits(bits);
    bitcrusher.sampleRate(rate);
}

void AudioEngine::setSaturationAmount(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    static float table[kShaperTableSize];
    const float drive = 1.0f + v * 14.0f; // Original 1x .. 15x aggressive tape drive
    const float norm = tanhf(drive);

    for (int i = 0; i < kShaperTableSize; i++) {
        const float x = 2.0f * ((float)i / (float)(kShaperTableSize - 1)) - 1.0f;
        table[i] = tanhf(drive * x) / norm;
    }
    waveshaper.shape(table, kShaperTableSize);
}

void AudioEngine::setClipAmount(float v) {
    if (v <= 0.001f) {
        hardClip.drive(1.0f);
        hardClip.ceiling(1.0f);
        return;
    }
    if (v > 1.0f) v = 1.0f;
    hardClip.drive(1.0f + v * 7.0f); // Original 1x .. 8x aggressive clip drive
    hardClip.ceiling(1.0f - v * 0.35f);
}

void AudioEngine::setFilterCutoff(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    filterCutoff_ = v;

    if (v <= 0.001f) {
        masterFilter.frequency(12000.0f);
        masterFilter.resonance(0.707f);
        return;
    }

    float freq = 12000.0f * powf(300.0f / 12000.0f, v);
    if (freq > 12000.0f) freq = 12000.0f;
    if (freq < 300.0f) freq = 300.0f;
    masterFilter.frequency(freq);
    masterFilter.resonance(0.707f);
}

void AudioEngine::setVinylCrackleAmount(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    vinylCrackleAmount_ = v;

    if (vinylCrackleAmount_ > 0.001f || tapeHissAmount_ > 0.001f) {
        noiseGen.amplitude(0.3f);
    } else {
        noiseGen.amplitude(0.0f);
        fxOutputMix.gain(1, 0.0f);
    }
}

void AudioEngine::setTapeHissAmount(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    tapeHissAmount_ = v;

    if (vinylCrackleAmount_ > 0.001f || tapeHissAmount_ > 0.001f) {
        noiseGen.amplitude(0.3f);
        fxOutputMix.gain(1, tapeHissAmount_ * 0.06f);
    } else {
        noiseGen.amplitude(0.0f);
        fxOutputMix.gain(1, 0.0f);
    }
}

void AudioEngine::setWowFlutterAmount(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    wowFlutterAmount_ = v;
}

void AudioEngine::setOutputRoute(OutputRoute route) {
    outputRoute_ = route;
    if (outputRoute_ == OutputRoute::UsbOnly) {
        sgtl5000.volume(0.0f);
        sgtl5000.lineOutLevel(31);
        sgtl5000.muteHeadphone();
        sgtl5000.muteLineout();
    } else {
        sgtl5000.unmuteHeadphone();
        sgtl5000.unmuteLineout();
    }
}

void AudioEngine::setMasterVolume(float v) {
    if (v < 0.01f || outputRoute_ == OutputRoute::UsbOnly) {
        sgtl5000.volume(0.0f);
        sgtl5000.lineOutLevel(31);
        sgtl5000.muteHeadphone();
        sgtl5000.muteLineout();
        return;
    }

    sgtl5000.unmuteHeadphone();
    sgtl5000.unmuteLineout();

    // Logarithmic volume curve (v * v) matching human ear response
    float logVol = v * v;
    sgtl5000.volume(logVol);

    // lineOutLevel: 31 (quietest) .. 13 (loudest)
    uint8_t level = (uint8_t)roundf(31.0f - logVol * 18.0f);
    sgtl5000.lineOutLevel(level);
}

void AudioEngine::setLineInMonitor(bool on) {
    masterMix.gain(1, on ? 1.0f : 0.0f);
}

void AudioEngine::setUsbInMonitor(bool on) {
    masterMix.gain(2, on ? 1.0f : 0.0f);
}

bool AudioEngine::startRecording(const char *path, RecordSource src) {
    // 1.0f (0dBFS) unity gain alignment — 100% identical to live playback with 0 clipping
    recordSourceMix.gain(0, src == RecordSource::LineIn ? 1.000f : 0.0f);
    recordSourceMix.gain(1, src == RecordSource::UsbIn ? 1.000f : 0.0f);
    recordSourceMix.gain(2, src == RecordSource::Resample ? 1.000f : 0.0f);
    return recorder.start(path);
}

bool AudioEngine::stopRecording() {
    bool ok = recorder.stop();
    recordSourceMix.gain(0, 0.0f);
    recordSourceMix.gain(1, 0.0f);
    recordSourceMix.gain(2, 0.0f);
    return ok;
}
