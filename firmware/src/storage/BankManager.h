#pragma once

#include "../config/Config.h"
#include <Arduino.h>
#include <SD.h>
#include <cstddef>

class BankManager {
  public:
    struct PadConfig {
        bool assigned = false;
        bool loop = false;
        int8_t chokeGroup = -1; // -1 = none, 0..3 = choke group id
        int16_t pitchCents = 0;
        uint16_t startTrim = 0;     // 0..10000 (0.00% .. 99.99%)
        uint16_t endTrim = 10000;   // 1..10000 (0.01% .. 100.00%)
        uint8_t volume = 100;       // 0% .. 100% per-pad volume
        bool gate = false;          // false = one-shot, true = play while holding

        char filename[13] = {
            0};              // 8.3 name, e.g. "PAD00.WAV" — always PADnn.WAV
        char name[13] = {0}; // display label, e.g. "KICK"
    };

    void begin();

    bool loadBank(const char *name);
    bool saveBank() const;
    bool createBank(const char *name);

    const char *currentBankName() const { return bankName; }

    PadConfig &pad(uint8_t index) { return pads[index]; }
    const PadConfig &pad(uint8_t index) const { return pads[index]; }

    // full SD path for a pad's sample file (may not exist yet).
    void padFilePath(uint8_t index, char *out, size_t outSize) const;

    // full SD path for the pad's untouched-original backup
    void padOrigFilePath(uint8_t index, char *out, size_t outSize) const;

    // full SD path for a fresh, timestamp-free recording take.
    void nextTakePath(char *out, size_t outSize) const;

    void setLoop(uint8_t index, bool loop) { pads[index].loop = loop; }
    void setChoke(uint8_t index, int8_t group) { pads[index].chokeGroup = group; }
    void setVolume(uint8_t index, uint8_t volume) { pads[index].volume = volume; }
    void setGate(uint8_t index, bool gate) { pads[index].gate = gate; }

    const int16_t *getPadSampleData(uint8_t index) const;
    uint32_t getPadSampleLength(uint8_t index) const;
    void loadPadSampleToPsram(uint8_t index);
    void reloadAllPsramSamples();

    bool setPitchCents(uint8_t index, int16_t cents);
    bool applyPadProcess(uint8_t index, int16_t cents, uint16_t startTrim, uint16_t endTrim);

    bool ensureSdReady() const;

    int listBanks(String *outNames, int maxCount) const;

    int listSampleFiles(String *outNames, int maxCount) const;

    bool importSample(uint8_t index, const char *srcPath);

    bool copyPad(uint8_t srcIndex, uint8_t dstIndex);
    bool copyPadToBank(uint8_t srcIndex, const char *destBankName, uint8_t dstIndex);
    bool exportBank(const char *targetBankName = nullptr);
    bool exportPad(uint8_t index);
    bool deletePad(uint8_t index);

    String *getError();

  private:
    char bankName[32] = "INIT";
    PadConfig pads[NS1_NUM_PADS];
    uint32_t takeCounter = 0;
    String error;

    void bankDirPath(char *out, size_t outSize) const;
    void bankCfgPath(char *out, size_t outSize) const;
};
