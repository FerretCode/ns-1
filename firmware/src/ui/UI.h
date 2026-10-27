#pragma once

#include "../audio/AudioEngine.h"
#include "../input/AnalogControls.h"
#include "../input/Encoders.h"
#include "../input/PadMatrix.h"
#include "../storage/BankManager.h"
#include "../storage/SampleSlicer.h"
#include "Display.h"
#include "WString.h"
#include <Arduino.h>
#include <cstdint>

//   PLAY   pads trigger/stop, SHIFT+pad edits a pad, ENC1 turn changes
//          the bank, ENC1 press opens the bank browser, ENC2 press arms
//          sampling. Pots/slider (compression, grit, saturation, clip,
//          volume) are always active regardless of the current screen.
//   EDIT   ENC1 pitch, ENC2 press toggles loop, ENC3 choke group; press
//          the pad again to leave. SHIFT + ENC1 press opens IMPORT to
//          load a WAV file from NS1_DIR_SAMPLES onto this pad.
//   IMPORT ENC1 turn selects a file from NS1_DIR_SAMPLES, ENC1 press
//          assigns it to the pad IMPORT was opened for and returns to
//          EDIT; ENC3 press cancels back to EDIT without changes.
//   SAMPLE ENC2 turn picks line-in/USB-in/resample, ENC2 press starts/
//          stops recording (stopping jumps straight into CHOP). ENC3
//          press toggles live monitoring of line-in/USB-in (not
//          available for resample — that's always audible already).
//   CHOP   ENC1 selects a slice, ENC2/ENC3 nudge its start/end, ENC1
//          press commits all slices to the bank as pad samples.
//   FILE   ENC1 browses banks, press loads one; ENC2 press creates a new
//          (auto-named) bank.
class UI {
  public:
    void begin(AudioEngine &engineRef, BankManager &bankRef, PadMatrix &padsRef,
               Encoders &encodersRef, AnalogControls &analogRef,
               Display &displayRef);
    void update();

  private:
    enum class Screen { PLAY, PAD_EDIT, FX, SAMPLE, CHOP, FILE, IMPORT, ERROR, PAD_COPY, PAD_DELETE, RESAMPLE };
    Screen screen = Screen::PLAY;
    Screen lastScreen = Screen::PLAY;

    AudioEngine *engine = nullptr;
    BankManager *bank = nullptr;
    PadMatrix *pads = nullptr;
    Encoders *encoders = nullptr;
    AnalogControls *analog = nullptr;
    Display *display = nullptr;

    // PAD_EDIT
    uint8_t editPad = 0;
    int16_t pendingPitchCents = 0;
    uint16_t pendingStartTrim = 0;   // 0..10000 (0.00% .. 99.99%)
    uint16_t pendingEndTrim = 10000; // 1..10000 (0.01% .. 100.00%)
    uint8_t pendingVolume = 100;
    uint8_t zoomLevel = 1;           // 1x, 5x, 25x
    bool focusStart = true;          // true = focus start marker, false = focus end marker
    bool rollActive = false;         // SP-404 style continuous loop auditioning
    int8_t pendingChoke = -1;
    SampleSlicer editSlicer;

    // SAMPLE
    AudioEngine::RecordSource sampleSource = AudioEngine::RecordSource::LineIn;
    char currentTakePath[96] = {0};
    bool monitorOn = false; // ENC3 press. Live-listen to the input source
                            // (line-in/USB-in) without recording

    // CHOP
    SampleSlicer slicer;
    uint8_t chopSelectedSlice = 0;

    // FILE
    static constexpr int kMaxBanks = 256;
    String bankNameList[kMaxBanks];
    int bankListCount = 0;
    int bankSelectedIndex = 0;
    int bankScrollOffset = 0;

    // IMPORT
    static constexpr int kMaxImportFiles = 256;
    uint8_t importPad = 0; // which pad IMPORT was opened for (from PAD_EDIT)
    String importFileList[kMaxImportFiles];
    int importFileCount = 0;
    int importSelectedIndex = 0;
    int importScrollOffset = 0;
    bool previewPending = false;
    uint32_t previewTriggerMillis = 0;
    char previewPendingPath[128] = {0};

    // ERROR
    String *error;

    uint32_t lastDrawMillis = 0;

    bool needFullRedraw = true;

    static constexpr uint32_t kPadFlashMillis = 150;
    uint32_t padFlashUntil[NS1_NUM_PADS] = {0};
    uint8_t lastPad = 0;

    void applyFxFromPots();
    void applySampleMonitor();
    void previewImportSample(const char *srcPath);
    void previewChopSlice(uint8_t sliceIdx);
    void toggleAllPadsChoke();

    // PAD_COPY & PAD_DELETE
    uint8_t copyStep = 0; // 0 = select source, 1 = select dest
    uint8_t copySrcPad = 0;
    int copyDestBankIndex = 0;

    void updatePlay();
    void updatePadEdit();
    void updateFx();
    void updateSample();
    void updateResample();
    void updateChop();
    void updateFile();
    void updateImport();
    void updateError();
    void updatePadCopy();
    void updatePadDelete();

    void enterScreen(Screen screen);
    void enterPlay();
    void enterPadEdit(uint8_t padIndex);
    void enterFx();
    void enterSample();
    void enterResample();
    void enterChop(const char *takePath);
    void enterFile();
    void enterImport(uint8_t padIndex);
    void enterError(String *error);
    void enterPadCopy();
    void enterPadDelete();

    void render();
};
