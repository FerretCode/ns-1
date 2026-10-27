#include "UI.h"
#include <SD.h>
#include <stdio.h>
#include <stdlib.h>

void UI::begin(AudioEngine &engineRef, BankManager &bankRef, PadMatrix &padsRef,
               Encoders &encodersRef, AnalogControls &analogRef,
               Display &displayRef) {
    engine = &engineRef;
    bank = &bankRef;
    pads = &padsRef;
    encoders = &encodersRef;
    analog = &analogRef;
    display = &displayRef;
}

void UI::update() {
    pads->update();
    encoders->update();
    analog->update();

    applyFxFromPots();

    if (screen == Screen::IMPORT && previewPending && millis() >= previewTriggerMillis) {
        previewPending = false;
        previewImportSample(previewPendingPath);
    }

    switch (screen) {
    case Screen::PLAY:
        updatePlay();
        break;
    case Screen::PAD_EDIT:
        updatePadEdit();
        break;
    case Screen::FX:
        updateFx();
        break;
    case Screen::SAMPLE:
        updateSample();
        break;
    case Screen::CHOP:
        updateChop();
        break;
    case Screen::FILE:
        updateFile();
        break;
    case Screen::IMPORT:
        updateImport();
        break;
    case Screen::ERROR:
        updateError();
        break;
    case Screen::PAD_COPY:
        updatePadCopy();
        break;
    case Screen::PAD_DELETE:
        updatePadDelete();
        break;
    case Screen::RESAMPLE:
        updateResample();
        break;
    }

    uint32_t now = millis();
    if (now - lastDrawMillis >= (1000u / NS1_UI_FPS)) {
        lastDrawMillis = now;

        render();
    }
}

void UI::applyFxFromPots() {
    engine->setCompressionAmount(analog->pot(0));
    engine->setGritAmount(analog->pot(1));
    engine->setSaturationAmount(analog->pot(2));
    engine->setClipAmount(analog->pot(3));
    engine->setMasterVolume(analog->slider());
}

void UI::applySampleMonitor() {
    bool line = monitorOn && sampleSource == AudioEngine::RecordSource::LineIn;
    bool usb = monitorOn && sampleSource == AudioEngine::RecordSource::UsbIn;

    engine->setLineInMonitor(line);
    engine->setUsbInMonitor(usb);
}

// --- PLAY --------------------------------------------------------------

void UI::updatePlay() {
    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        if (pads->justPressed(i)) {
            padFlashUntil[i] = millis() + kPadFlashMillis;

            if (analog->shiftDown()) {
                enterPadEdit(i);
                return;
            }

            BankManager::PadConfig &pc = bank->pad(i);
            if (!pc.assigned) {
                continue;
            }

            if (pc.loop && engine->isPadPlaying(i)) {
                engine->stopPad(i); // second press of a looping pad stops it
            } else {
                const int16_t *data = bank->getPadSampleData(i);
                uint32_t len = bank->getPadSampleLength(i);
                engine->triggerPad(i, data, len, pc.loop, pc.chokeGroup, pc.volume);
            }
        }
        if (pads->justReleased(i)) {
            BankManager::PadConfig &pc = bank->pad(i);
            if (pc.assigned && pc.gate) {
                engine->stopPad(i);
            }
        }
    }

    if (encoders->buttonJustPressed(0)) {
        if (analog->shiftDown()) {
            enterPadDelete();
        } else {
            enterFile();
        }
        return;
    }

    if (encoders->buttonJustPressed(1)) {
        if (analog->shiftDown()) {
            enterPadCopy();
        } else {
            enterSample();
        }
        return;
    }

    if (encoders->buttonJustPressed(2)) {
        if (analog->shiftDown()) {
            toggleAllPadsChoke();
        } else {
            enterFx();
        }
        return;
    }

    int32_t bankDelta = encoders->delta(0);
    if (bankDelta != 0) {
        int n = bank->listBanks(bankNameList, kMaxBanks);
        bankListCount = n;

        if (n > 0) {
            int cur = 0;

            for (int i = 0; i < n; i++) {
                if (bankNameList[i].equals(bank->currentBankName()))
                    cur = i;
            }

            int next = (cur + (bankDelta > 0 ? 1 : -1) + n) % n;

            bank->loadBank(bankNameList[next].c_str());

            needFullRedraw = true;
        }
    }
}

static int32_t calculatePadEditNudge(int32_t delta, bool shiftDown, uint32_t &lastClickMillis) {
    if (delta == 0) return 0;
    if (shiftDown) return delta * 1; // Fine mode: 1 unit (0.01%) per click

    uint32_t now = millis();
    uint32_t dt = now - lastClickMillis;
    lastClickMillis = now;

    int32_t absD = delta < 0 ? -delta : delta;
    int32_t scale = 100; // Base: 1.0% per click

    if (absD >= 8 || dt < 8) {
        scale = 2500; // Rapid spin: 25.0% per click
    } else if (absD >= 4 || dt < 20) {
        scale = 800;  // Fast turn: 8.0% per click
    } else if (absD >= 2 || dt < 50) {
        scale = 300;  // Medium turn: 3.0% per click
    }

    return delta * scale;
}

// --- PAD EDIT ------------------------------------------------------------

void UI::updatePadEdit() {
    static uint32_t lastStartTrimClickMillis = 0;
    static uint32_t lastEndTrimClickMillis = 0;

    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        if (pads->justPressed(i)) {
            if (rollActive) {
                rollActive = false;
                engine->stopPad(editPad);
            }
            if (i == editPad) {
                if (analog->shiftDown()) {
                    bank->exportPad(editPad);
                } else {
                    enterPlay();
                    return;
                }
            } else {
                enterPadEdit(i);
                return;
            }
        }
    }

    // ENC1 turn: Volume (or SHIFT + ENC1 turn: Pitch Cents)
    int32_t dEnc1 = encoders->delta(0);
    if (dEnc1 != 0) {
        if (!analog->shiftDown()) {
            int32_t v = (int32_t)pendingVolume + dEnc1 * 2;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            pendingVolume = (uint8_t)v;
            engine->setPadVolume(editPad, pendingVolume);
        } else {
            int32_t v = (int32_t)pendingPitchCents + dEnc1 * 100;
            if (v < -1200) v = -1200;
            if (v > 1200) v = 1200;
            pendingPitchCents = (int16_t)v;
        }
    }

    // ENC2 turn: Start Trim (Velocity-Accelerated: Coarse 1.0%, Fine 0.01% with SHIFT, Fast spin up to 25.0%)
    int32_t dStart = encoders->delta(1);
    if (dStart != 0) {
        focusStart = true;
        int32_t nudge = calculatePadEditNudge(dStart, analog->shiftDown(), lastStartTrimClickMillis);
        int32_t v = (int32_t)pendingStartTrim + nudge;
        if (v < 0) v = 0;
        if (v >= (int32_t)pendingEndTrim) v = (int32_t)pendingEndTrim - 1;
        pendingStartTrim = (uint16_t)v;

        if (rollActive) {
            engine->updatePadTrim(editPad, bank->getPadSampleLength(editPad), pendingStartTrim, pendingEndTrim, true);
        }
    }

    // ENC3 turn: End Trim (Velocity-Accelerated: Coarse 1.0%, Fine 0.01% with SHIFT, Fast spin up to 25.0%)
    int32_t dEnd = encoders->delta(2);
    if (dEnd != 0) {
        focusStart = false;
        int32_t nudge = calculatePadEditNudge(dEnd, analog->shiftDown(), lastEndTrimClickMillis);
        int32_t v = (int32_t)pendingEndTrim + nudge;
        if (v <= (int32_t)pendingStartTrim) v = (int32_t)pendingStartTrim + 1;
        if (v > 10000) v = 10000;
        pendingEndTrim = (uint16_t)v;

        if (rollActive) {
            engine->updatePadTrim(editPad, bank->getPadSampleLength(editPad), pendingStartTrim, pendingEndTrim, true);
        }
    }

    // ENC1 press: Commit & save pitch, trim, and volume edits (or SHIFT + ENC1 press: Open Import browser)
    if (encoders->buttonJustPressed(0)) {
        if (analog->shiftDown()) {
            if (rollActive) {
                rollActive = false;
                engine->stopPad(editPad);
            }
            enterImport(editPad);
            return;
        } else {
            if (rollActive) {
                rollActive = false;
                engine->stopPad(editPad);
            }
            bank->setVolume(editPad, pendingVolume);
            bank->applyPadProcess(editPad, pendingPitchCents, pendingStartTrim, pendingEndTrim);
        }
    }

    // ENC2 press: Zoom & Focus Toggle (or SHIFT + ENC2 press: Cycle Play Mode SHOT/GATE/LOOP)
    if (encoders->buttonJustPressed(1)) {
        if (analog->shiftDown()) {
            bool currentGate = bank->pad(editPad).gate;
            bool currentLoop = bank->pad(editPad).loop;
            if (!currentGate && !currentLoop) {
                bank->setGate(editPad, true);
                bank->setLoop(editPad, false);
            } else if (currentGate) {
                bank->setGate(editPad, false);
                bank->setLoop(editPad, true);
            } else {
                bank->setGate(editPad, false);
                bank->setLoop(editPad, false);
            }
            bank->saveBank();
        } else {
            focusStart = !focusStart;
            zoomLevel = zoomLevel == 1 ? 5 : (zoomLevel == 5 ? 25 : 1);
        }
    }

    // ENC3 press: Toggle ROLL audition (or SHIFT + ENC3 press: Cycle Choke Group -1..3)
    if (encoders->buttonJustPressed(2)) {
        if (analog->shiftDown()) {
            int8_t nextChoke = (bank->pad(editPad).chokeGroup + 2 + 5) % 5 - 1; // cycle -1..3
            bank->setChoke(editPad, nextChoke);
            bank->saveBank();
            pendingChoke = nextChoke;
        } else {
            rollActive = !rollActive;
            if (rollActive) {
                const int16_t *data = bank->getPadSampleData(editPad);
                uint32_t len = bank->getPadSampleLength(editPad);
                if (data && len > 0) {
                    engine->triggerPad(editPad, data, len, true, -1, pendingVolume, pendingStartTrim, pendingEndTrim, true);
                }
            } else {
                engine->stopPad(editPad);
            }
        }
    }
}

// --- FX (PERFORMANCE FX) -------------------------------------------------

void UI::updateFx() {
    // Encoder 1: Vinyl Crackle (or Filter Cutoff with SHIFT)
    int32_t dEnc1 = encoders->delta(0);
    if (dEnc1 != 0) {
        if (analog->shiftDown()) {
            float cur = engine->filterCutoff();
            float next = cur + (float)dEnc1 * 0.05f;
            if (next < 0.0f) next = 0.0f;
            if (next > 1.0f) next = 1.0f;
            engine->setFilterCutoff(next);
        } else {
            float cur = engine->vinylCrackleAmount();
            float next = cur + (float)dEnc1 * 0.05f;
            if (next < 0.0f) next = 0.0f;
            if (next > 1.0f) next = 1.0f;
            engine->setVinylCrackleAmount(next);
        }
        needFullRedraw = true;
    }

    // Encoder 2: Tape Hiss
    int32_t dEnc2 = encoders->delta(1);
    if (dEnc2 != 0) {
        float cur = engine->tapeHissAmount();
        float next = cur + (float)dEnc2 * 0.05f;
        if (next < 0.0f) next = 0.0f;
        if (next > 1.0f) next = 1.0f;
        engine->setTapeHissAmount(next);
        needFullRedraw = true;
    }

    // Encoder 3: Wow & Flutter
    int32_t dEnc3 = encoders->delta(2);
    if (dEnc3 != 0) {
        float cur = engine->wowFlutterAmount();
        float next = cur + (float)dEnc3 * 0.05f;
        if (next < 0.0f) next = 0.0f;
        if (next > 1.0f) next = 1.0f;
        engine->setWowFlutterAmount(next);
        needFullRedraw = true;
    }

    if (encoders->buttonJustPressed(2) || encoders->buttonJustPressed(0)) {
        enterPlay();
        return;
    }

    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        if (pads->justPressed(i)) {
            padFlashUntil[i] = millis() + kPadFlashMillis;
            BankManager::PadConfig &pc = bank->pad(i);
            if (pc.assigned) {
                const int16_t *data = bank->getPadSampleData(i);
                uint32_t len = bank->getPadSampleLength(i);
                engine->triggerPad(i, data, len, pc.loop, pc.chokeGroup, pc.volume);
            }
        }
    }
}

// --- SAMPLE --------------------------------------------------------------

void UI::updateSample() {
    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        if (pads->justPressed(i)) {
            padFlashUntil[i] = millis() + kPadFlashMillis;
            BankManager::PadConfig &pc = bank->pad(i);
            if (pc.assigned) {
                const int16_t *data = bank->getPadSampleData(i);
                uint32_t len = bank->getPadSampleLength(i);
                engine->triggerPad(i, data, len, pc.loop, pc.chokeGroup, pc.volume);
            }
        }
    }

    if (encoders->buttonJustPressed(0)) {
        if (engine->isRecording())
            engine->stopRecording();

        monitorOn = false;
        applySampleMonitor();

        enterPlay();

        return;
    }

    int32_t d = encoders->delta(1);
    if (d != 0 && !engine->isRecording()) {
        int idx = (int)sampleSource;

        idx = (idx + (d > 0 ? 1 : -1) + 3) % 3;
        sampleSource = (AudioEngine::RecordSource)idx;

        applySampleMonitor();
        if (sampleSource == AudioEngine::RecordSource::Resample) {
            enterResample();
            return;
        }
    }

    if (encoders->buttonJustPressed(1)) {
        if (!engine->isRecording()) {
            bank->nextTakePath(currentTakePath, sizeof(currentTakePath));
            engine->startRecording(currentTakePath, sampleSource);
        } else {
            engine->stopRecording();

            monitorOn = false;

            applySampleMonitor();
            enterChop(currentTakePath);

            return;
        }
    }

    if (encoders->buttonJustPressed(2) &&
        sampleSource != AudioEngine::RecordSource::Resample) {
        monitorOn = !monitorOn;

        applySampleMonitor();
    }
}

void UI::updateResample() {
    int32_t d = encoders->delta(1);
    if (d != 0 && !engine->isRecording()) {
        int idx = (int)sampleSource;
        idx = (idx + (d > 0 ? 1 : -1) + 3) % 3;
        sampleSource = (AudioEngine::RecordSource)idx;
        applySampleMonitor();
        if (sampleSource != AudioEngine::RecordSource::Resample) {
            enterSample();
            return;
        }
    }

    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        if (pads->justPressed(i)) {
            padFlashUntil[i] = millis() + kPadFlashMillis;
            needFullRedraw = true;
            BankManager::PadConfig &pc = bank->pad(i);
            if (pc.assigned) {
                const int16_t *data = bank->getPadSampleData(i);
                uint32_t len = bank->getPadSampleLength(i);
                engine->triggerPad(i, data, len, pc.loop, pc.chokeGroup, pc.volume);
            }
        }
    }

    if (encoders->buttonJustPressed(1)) {
        if (!engine->isRecording()) {
            bank->nextTakePath(currentTakePath, sizeof(currentTakePath));
            engine->startRecording(currentTakePath, AudioEngine::RecordSource::Resample);
            needFullRedraw = true;
        } else {
            engine->stopRecording();
            monitorOn = false;
            applySampleMonitor();
            enterChop(currentTakePath);
            return;
        }
    }

    if (encoders->buttonJustPressed(0)) {
        if (engine->isRecording())
            engine->stopRecording();
        monitorOn = false;
        applySampleMonitor();
        enterPlay();
        return;
    }
}

static int32_t calculateAcceleratedNudge(int32_t delta, uint32_t &lastClickMillis) {
    if (delta == 0) return 0;
    uint32_t now = millis();
    uint32_t dt = now - lastClickMillis;
    lastClickMillis = now;

    int32_t absD = delta < 0 ? -delta : delta;
    int32_t scale = 150; // Base micro-precision: ~3.4ms per detent

    if (absD >= 8 || dt < 8) {
        scale = 45000; // Rapid spin: ~1.0 second per detent
    } else if (absD >= 4 || dt < 20) {
        scale = 12000; // Fast turn: ~270ms per detent
    } else if (absD >= 2 || dt < 50) {
        scale = 2000;  // Medium turn: ~45ms per detent
    }

    return delta * scale;
}

// --- CHOP ------------------------------------------------------------------

void UI::updateChop() {
    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        if (pads->justPressed(i)) {
            if (analog->shiftDown()) {
                if (engine) engine->stopAllVoices();
                bank->importSample(i, currentTakePath);
                enterPlay();
                return;
            } else {
                if (i < slicer.sliceCount()) {
                    chopSelectedSlice = i;
                    previewChopSlice(i);
                }
                return;
            }
        }
    }

    int32_t dSel = encoders->delta(0);
    if (dSel != 0) {
        int n = slicer.sliceCount();
        int sel = ((int)chopSelectedSlice + dSel) % n;

        if (sel < 0)
            sel += n;

        chopSelectedSlice = (uint8_t)sel;
        previewChopSlice(chopSelectedSlice);
    }

    static uint32_t lastStartClickMillis = 0;
    static uint32_t lastEndClickMillis = 0;
    static uint32_t lastNudgeAuditionMillis = 0;

    int32_t dStart = encoders->delta(1);
    if (dStart != 0) {
        int32_t nudgeFrames = calculateAcceleratedNudge(dStart, lastStartClickMillis);
        slicer.nudgeStart(chopSelectedSlice, nudgeFrames);
        uint32_t now = millis();
        if (now - lastNudgeAuditionMillis >= 120) {
            lastNudgeAuditionMillis = now;
            previewChopSlice(chopSelectedSlice);
        }
    }

    int32_t dEnd = encoders->delta(2);
    if (dEnd != 0) {
        int32_t nudgeFrames = calculateAcceleratedNudge(dEnd, lastEndClickMillis);
        slicer.nudgeEnd(chopSelectedSlice, nudgeFrames);
        uint32_t now = millis();
        if (now - lastNudgeAuditionMillis >= 120) {
            lastNudgeAuditionMillis = now;
            previewChopSlice(chopSelectedSlice);
        }
    }

    if (encoders->buttonJustPressed(0)) {
        if (engine) engine->stopAllVoices();
        if (analog->shiftDown()) {
            bank->importSample(0, currentTakePath);
        } else {
            slicer.commitToBank(*bank);
        }
        enterPlay();
        return;
    }

    if (encoders->buttonJustPressed(1)) {
        static const uint8_t counts[] = {4, 8, 12, 16};
        uint8_t cur = slicer.sliceCount();

        int idx = 0;
        for (int i = 0; i < 4; i++)
            if (counts[i] == cur)
                idx = i;

        idx = (idx + 1) % 4;

        slicer.setSliceCount(counts[idx]);

        if (chopSelectedSlice >= slicer.sliceCount())
            chopSelectedSlice = slicer.sliceCount() - 1;

        previewChopSlice(chopSelectedSlice);
    }

    if (encoders->buttonJustPressed(2)) {
        enterPlay();
        return;
    }
}

void UI::previewImportSample(const char *srcPath) {
    if (engine) engine->stopAllVoices();
    EXTMEM static int16_t gPreviewBuf[NS1_SAMPLE_RATE];
    uint32_t count = WavFile::decodeToMemory(srcPath, gPreviewBuf, NS1_SAMPLE_RATE);
    if (count > 0) {
        engine->triggerPad(importPad, gPreviewBuf, count, false, -1);
    }
}

void UI::previewChopSlice(uint8_t sliceIdx) {
    if (engine) engine->stopAllVoices();
    EXTMEM static int16_t gSlicePreviewBuf[NS1_SAMPLE_RATE * 2];
    uint32_t count = slicer.decodeSliceToMemory(sliceIdx, gSlicePreviewBuf, NS1_SAMPLE_RATE * 2);
    if (count > 0) {
        engine->triggerPad(sliceIdx < NS1_NUM_PADS ? sliceIdx : 0, gSlicePreviewBuf, count, false, -1);
    }
}

void UI::toggleAllPadsChoke() {
    bool allChoked = true;
    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        if (bank->pad(i).chokeGroup != 0) {
            allChoked = false;
            break;
        }
    }
    int8_t targetGroup = allChoked ? -1 : 0;
    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        bank->setChoke(i, targetGroup);
    }
    bank->saveBank();
    needFullRedraw = true;
}

// --- FILE ------------------------------------------------------------------

void UI::updateFile() {
    int32_t d = encoders->delta(0);

    if (d != 0 && bankListCount > 0) {
        int sel = (bankSelectedIndex + d) % bankListCount;

        if (sel < 0)
            sel += bankListCount;

        bankSelectedIndex = sel;

        if (bankSelectedIndex < bankScrollOffset) {
            bankScrollOffset = bankSelectedIndex;
        } else if (bankSelectedIndex >=
                   bankScrollOffset + Display::kMenuVisibleRows) {
            bankScrollOffset =
                bankSelectedIndex - Display::kMenuVisibleRows + 1;
        }
    }

    if (encoders->buttonJustPressed(0)) {
        if (analog->shiftDown()) {
            AudioEngine::OutputRoute current = engine->outputRoute();
            AudioEngine::OutputRoute next = (current == AudioEngine::OutputRoute::Both) ? AudioEngine::OutputRoute::UsbOnly : AudioEngine::OutputRoute::Both;
            engine->setOutputRoute(next);
            needFullRedraw = true;
        } else {
            if (bankListCount > 0)
                bank->loadBank(bankNameList[bankSelectedIndex].c_str());

            enterPlay();
            return;
        }
    }

    if (encoders->buttonJustPressed(1)) {
        if (analog->shiftDown()) {
            if (bankListCount > 0) {
                bank->exportBank(bankNameList[bankSelectedIndex].c_str());
            }
        } else {
            char name[16];
            for (int n = 0; n < 100; n++) {
                snprintf(name, sizeof(name), "BANK%02d", n);

                char testDir[64];
                snprintf(testDir, sizeof(testDir), "%s/%s", NS1_DIR_BANKS, name);

                if (!SD.exists(testDir))
                    break;
            }

            bank->createBank(name);
            enterPlay();
            return;
        }
    }

    if (encoders->buttonJustPressed(2)) {
        enterPlay();
        return;
    }
}

// --- IMPORT ------------------------------------------------------------------

void UI::updateImport() {
    int32_t d = encoders->delta(0);
    if (d != 0 && importFileCount > 0) {
        int sel = (importSelectedIndex + d) % importFileCount;

        if (sel < 0)
            sel += importFileCount;

        importSelectedIndex = sel;

        if (importSelectedIndex < importScrollOffset) {
            importScrollOffset = importSelectedIndex;
        } else if (importSelectedIndex >=
                   importScrollOffset + Display::kMenuVisibleRows) {
            importScrollOffset =
                importSelectedIndex - Display::kMenuVisibleRows + 1;
        }

        // Schedule debounced audio preview (80ms pause) so rapid scrolling remains 60 FPS smooth
        snprintf(previewPendingPath, sizeof(previewPendingPath), "%s",
                 importFileList[importSelectedIndex].c_str());
        previewPending = true;
        previewTriggerMillis = millis() + 80;
    }

    if (encoders->buttonJustPressed(0)) {
        if (importFileCount == 0) {
            return;
        }

        bool result = bank->importSample(importPad, importFileList[importSelectedIndex].c_str());
        if (!result) {
            enterError(bank->getError());
        }

        enterPadEdit(importPad); // refreshes pending pitch/choke from the
                                 // (now-assigned) pad
        return;
    }

    if (encoders->buttonJustPressed(2)) {
        enterPadEdit(importPad);

        return;
    }
}

void UI::updateError() {
    uint32_t now = millis();
    if (now - lastDrawMillis >= (2000u)) {
        lastDrawMillis = now;

        enterScreen(screen);
    }
}

// --- transitions -----------------------------------------------------------

void UI::enterScreen(Screen screen) {
    switch (screen) {
    case Screen::PLAY:
        enterPlay();
        break;
    case Screen::PAD_EDIT:
        enterPadEdit(lastPad);
        break;
    case Screen::FX:
        enterFx();
        break;
    case Screen::SAMPLE:
        enterSample();
        break;
    case Screen::RESAMPLE:
        enterResample();
        break;
    case Screen::CHOP:
        updateChop();
        break;
    case Screen::FILE:
        updateFile();
        break;
    case Screen::IMPORT:
        updateImport();
        break;
    case Screen::ERROR:
        updateError();
        break;
    case Screen::PAD_COPY:
        enterPadCopy();
        break;
    case Screen::PAD_DELETE:
        enterPadDelete();
        break;
    }
}

void UI::enterPlay() {
    screen = Screen::PLAY;
    needFullRedraw = true;
}

void UI::enterPadEdit(uint8_t padIndex) {
    lastPad = padIndex;

    screen = Screen::PAD_EDIT;
    editPad = padIndex;
    pendingPitchCents = bank->pad(padIndex).pitchCents;
    pendingStartTrim = bank->pad(padIndex).startTrim;
    pendingEndTrim = bank->pad(padIndex).endTrim;
    pendingVolume = bank->pad(padIndex).volume;
    pendingChoke = bank->pad(padIndex).chokeGroup;
    zoomLevel = 1;
    focusStart = true;
    rollActive = false;

    char origPath[96], path[96];
    bank->padOrigFilePath(padIndex, origPath, sizeof(origPath));
    bank->padFilePath(padIndex, path, sizeof(path));
    if (SD.exists(origPath)) {
        editSlicer.load(origPath);
    } else if (SD.exists(path)) {
        editSlicer.load(path);
    }

    needFullRedraw = true;
}

void UI::enterFx() {
    screen = Screen::FX;
    needFullRedraw = true;
}

void UI::enterSample() {
    screen = Screen::SAMPLE;
    monitorOn = false;
    applySampleMonitor();
    needFullRedraw = true;
}

void UI::enterResample() {
    screen = Screen::RESAMPLE;
    sampleSource = AudioEngine::RecordSource::Resample;
    monitorOn = false;
    applySampleMonitor();
    needFullRedraw = true;
}

void UI::enterChop(const char *takePath) {
    screen = Screen::CHOP;
    slicer.load(takePath);
    chopSelectedSlice = 0;
    needFullRedraw = true;
}

void UI::enterFile() {
    screen = Screen::FILE;
    bankListCount = bank->listBanks(bankNameList, kMaxBanks);
    bankSelectedIndex = 0;

    for (int i = 0; i < bankListCount; i++) {
        if (bankNameList[i].equals(bank->currentBankName()))
            bankSelectedIndex = i;
    }

    bankScrollOffset = 0;

    if (bankSelectedIndex >= Display::kMenuVisibleRows) {
        bankScrollOffset = bankSelectedIndex - Display::kMenuVisibleRows + 1;
    }

    needFullRedraw = true;
}

void UI::enterPadCopy() {
    screen = Screen::PAD_COPY;
    copyStep = 0;
    copySrcPad = 0;
    bankListCount = bank->listBanks(bankNameList, kMaxBanks);
    copyDestBankIndex = 0;
    for (int i = 0; i < bankListCount; i++) {
        if (bankNameList[i].equals(bank->currentBankName())) {
            copyDestBankIndex = i;
            break;
        }
    }
    needFullRedraw = true;
}

void UI::enterPadDelete() {
    screen = Screen::PAD_DELETE;
    needFullRedraw = true;
}

void UI::updatePadCopy() {
    if (copyStep == 1 && bankListCount > 0) {
        int32_t dBank = encoders->delta(0);
        if (dBank != 0) {
            int sel = (copyDestBankIndex + dBank) % bankListCount;
            if (sel < 0) sel += bankListCount;
            copyDestBankIndex = sel;
            needFullRedraw = true;
        }
    }

    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        if (pads->justPressed(i)) {
            if (copyStep == 0) {
                if (bank->pad(i).assigned) {
                    copySrcPad = i;
                    copyStep = 1;
                    needFullRedraw = true;
                }
            } else {
                const char *targetBank = (bankListCount > 0 && copyDestBankIndex < bankListCount)
                    ? bankNameList[copyDestBankIndex].c_str() : bank->currentBankName();
                if (strcmp(targetBank, bank->currentBankName()) != 0 || i != copySrcPad) {
                    bank->copyPadToBank(copySrcPad, targetBank, i);
                    enterPlay();
                }
            }
            return;
        }
    }

    if (encoders->buttonJustPressed(1) || encoders->buttonJustPressed(0) || encoders->buttonJustPressed(2)) {
        enterPlay();
        return;
    }
}

void UI::updatePadDelete() {
    for (uint8_t i = 0; i < NS1_NUM_PADS; i++) {
        if (pads->justPressed(i)) {
            if (engine) engine->stopAllVoices();
            bank->deletePad(i);
            enterPlay();
            return;
        }
    }

    if (encoders->buttonJustPressed(0) || encoders->buttonJustPressed(1) || encoders->buttonJustPressed(2)) {
        enterPlay();
        return;
    }
}

void UI::enterImport(uint8_t padIndex) {
    screen = Screen::IMPORT;
    importPad = padIndex;
    importFileCount = bank->listSampleFiles(importFileList, kMaxImportFiles);
    importSelectedIndex = 0;
    importScrollOffset = 0;
    needFullRedraw = true;

    if (importFileCount > 0) {
        snprintf(previewPendingPath, sizeof(previewPendingPath), "%s",
                 importFileList[0].c_str());
        previewPending = true;
        previewTriggerMillis = millis();
    }
}

void UI::enterError(String *err) {
    lastScreen = screen;

    error = err;

    screen = Screen::ERROR;
}

// --- render ------------------------------------------------------------------

void UI::render() {
    bool full = needFullRedraw;
    needFullRedraw = false;

    switch (screen) {
    case Screen::PLAY: {
        bool highlight[NS1_NUM_PADS];
        bool assigned[NS1_NUM_PADS];
        const char *names[NS1_NUM_PADS];

        uint32_t now = millis();
        for (int i = 0; i < NS1_NUM_PADS; i++) {
            highlight[i] = pads->isDown(i) || now < padFlashUntil[i];
            assigned[i] = bank->pad(i).assigned;
            names[i] = bank->pad(i).name;
        }

        display->drawPlayScreen(full, bank->currentBankName(), highlight,
                                assigned, names, analog->pot(0), analog->pot(1),
                                analog->pot(2), analog->pot(3),
                                analog->slider());

        break;
    }

    case Screen::PAD_EDIT: {
        const BankManager::PadConfig &pc = bank->pad(editPad);
        display->drawPadEditScreen(full, editPad, pc.assigned, pc.loop, pc.gate,
                                   rollActive, pendingChoke, pendingPitchCents,
                                   pendingStartTrim, pendingEndTrim,
                                   pendingVolume, zoomLevel, focusStart,
                                   editSlicer.peaks(),
                                   SampleSlicer::kPeakBuckets);

        break;
    }

    case Screen::FX: {
        display->drawFxScreen(full, engine->vinylCrackleAmount(),
                              engine->tapeHissAmount(),
                              engine->wowFlutterAmount(),
                              engine->filterCutoff(),
                              analog->slider());

        break;
    }

    case Screen::SAMPLE: {
        static const char *names[3] = {"LINE IN", "USB IN", "RESAMPLE"};

        bool monitorAvailable =
            sampleSource != AudioEngine::RecordSource::Resample;

        display->drawSampleScreen(
            full, names[(int)sampleSource], engine->isRecording(),
            engine->recordedSeconds(), monitorAvailable, monitorOn);

        break;
    }

    case Screen::RESAMPLE: {
        bool assigned[NS1_NUM_PADS];
        const char *padNamesBuf[NS1_NUM_PADS];
        bool flashHighlight[NS1_NUM_PADS];
        uint32_t now = millis();

        for (int i = 0; i < NS1_NUM_PADS; i++) {
            assigned[i] = bank->pad(i).assigned;
            padNamesBuf[i] = bank->pad(i).name;
            flashHighlight[i] = (now < padFlashUntil[i]);
        }

        display->drawResampleScreen(full, engine->isRecording(),
                                    engine->recordedSeconds(),
                                    assigned, padNamesBuf, flashHighlight);
        break;
    }

    case Screen::CHOP: {
        uint32_t starts[NS1_NUM_PADS];
        uint32_t ends[NS1_NUM_PADS];

        for (int i = 0; i < slicer.sliceCount(); i++) {
            starts[i] = slicer.sliceStartSample(i);
            ends[i] = slicer.sliceEndSample(i);
        }

        display->drawChopScreen(full, slicer.peaks(),
                                SampleSlicer::kPeakBuckets, slicer.sliceCount(),
                                chopSelectedSlice, starts, ends,
                                slicer.totalSamples());

        break;
    }

    case Screen::FILE: {
        display->drawFileScreen(full, bankNameList, bankListCount,
                                bankSelectedIndex, bankScrollOffset,
                                bank->currentBankName(),
                                engine->outputRoute() == AudioEngine::OutputRoute::UsbOnly);

        break;
    }

    case Screen::IMPORT: {
        display->drawImportScreen(full, importPad, importFileList,
                                  importFileCount, importSelectedIndex,
                                  importScrollOffset);

        break;
    }

    case Screen::ERROR: {
        display->drawErrorScreen(true, error);
        break;
    }

    case Screen::PAD_COPY: {
        bool assigned[NS1_NUM_PADS];
        const char *names[NS1_NUM_PADS];
        for (int i = 0; i < NS1_NUM_PADS; i++) {
            assigned[i] = bank->pad(i).assigned;
            names[i] = bank->pad(i).name;
        }
        const char *targetBank = (bankListCount > 0 && copyDestBankIndex < bankListCount)
            ? bankNameList[copyDestBankIndex].c_str() : bank->currentBankName();
        display->drawPadCopyScreen(full, bank->currentBankName(), copyStep,
                                   copySrcPad, targetBank, assigned, names);
        break;
    }

    case Screen::PAD_DELETE: {
        bool assigned[NS1_NUM_PADS];
        const char *names[NS1_NUM_PADS];
        for (int i = 0; i < NS1_NUM_PADS; i++) {
            assigned[i] = bank->pad(i).assigned;
            names[i] = bank->pad(i).name;
        }
        display->drawPadDeleteScreen(full, bank->currentBankName(), assigned, names);
        break;
    }
    }
}
