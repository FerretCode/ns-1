#include "Display.h"
#include "WString.h"
#include <SD.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <stdio.h>
#include <string.h>

namespace {
const uint16_t kBg         = 0x10A2; // Slate Charcoal Dark Mode
const uint16_t kCardBg     = 0x1CF0; // Dark Graphite Card
const uint16_t kCardBorder = 0x3D15; // Mid Graphite Frame
const uint16_t kFg         = 0xFFFF; // Pure White
const uint16_t kDim       = 0x7BEF; // Muted Silver
const uint16_t kAccent     = 0x07FF; // Electric Cyan
const uint16_t kAccentAlt  = 0xF81F; // Hot Coral Magenta
const uint16_t kGold       = 0xFDE0; // Amber Gold

const uint16_t kComp = 0x3DFF; // Electric Cyan-Blue
const uint16_t kGrit = 0xFDE0; // Amber Gold
const uint16_t kSat  = 0xFB20; // Neon Orange
const uint16_t kClip = 0xF81F; // Hot Coral Pink
} // namespace

void Display::begin() {
    tft.init(NS1_TFT_WIDTH, NS1_TFT_HEIGHT);
    tft.setRotation(NS1_TFT_ROTATION);
    tft.setSPISpeed(40000000); // 40MHz SPI
    tft.fillScreen(kBg);
    tft.setTextWrap(false);
}

void Display::drawHeader(const char *title) {
    tft.fillRect(0, 0, tft.width(), 20, kBg);
    tft.fillRoundRect(4, 2, tft.width() - 8, 16, 4, kCardBg);
    tft.drawRoundRect(4, 2, tft.width() - 8, 16, 4, kAccent);

    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(kAccent);
    tft.setCursor(10, 14);
    tft.print(title);
    tft.setFont(); // Reset to standard font
}

void Display::drawFxBar(int x, int y, int w, int h, float value,
                        const char *label, uint16_t color) {
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;

    tft.drawRoundRect(x, y, w, h, 4, kCardBorder);
    int fillH = (int)(value * (h - 2));
    tft.fillRect(x + 1, y + 1, w - 2, h - 2 - fillH, kCardBg); // clear empty part
    if (fillH > 0) {
        tft.fillRect(x + 1, y + h - 1 - fillH, w - 2, fillH, color);
    }

    tft.setTextSize(1);
    tft.setTextColor(kFg);
    tft.setCursor(x + 2, y + h + 3);
    tft.print(label);
}

void Display::drawTextLine(int x, int y, int w, int h, uint16_t bg) {
    tft.fillRect(x, y, w, h, bg);
}

void Display::drawPlayScreen(bool fullRedraw, const char *bankName,
                             const bool padHighlight[NS1_NUM_PADS],
                             const bool padAssigned[NS1_NUM_PADS],
                             const char *const padNames[NS1_NUM_PADS],
                             float comp, float grit, float sat, float clip,
                             float volume) {
    if (fullRedraw) {
        tft.fillScreen(kBg);
    }

    // Header badge
    tft.fillRect(0, 0, 140, 20, kBg);
    tft.fillRoundRect(4, 2, 130, 16, 4, kCardBg);
    tft.drawRoundRect(4, 2, 130, 16, 4, kAccent);

    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(kAccent);
    tft.setCursor(10, 14);
    tft.print(bankName);
    tft.setFont(); // Reset to standard font

    // Top-right hint
    tft.setTextSize(1);
    tft.setTextColor(kDim);
    tft.setCursor(tft.width() - 88, 5);
    tft.print("SHIFT+PAD=EDIT");

    // 4x4 Pad Grid
    drawPadGrid(padHighlight, padAssigned, padNames);

    // Performance FX Bars
    const int cell = 26;
    const int gridX = 6;
    const int barW = 24;
    const int barH = 88;
    const int barY = 24;
    const int barsX = gridX + 4 * (cell + 3) + 8;
    drawFxBar(barsX + 0 * 36, barY, barW, barH, comp, "CMP", kComp);
    drawFxBar(barsX + 1 * 36, barY, barW, barH, grit, "GRT", kGrit);
    drawFxBar(barsX + 2 * 36, barY, barW, barH, sat, "SAT", kSat);
    drawFxBar(barsX + 3 * 36, barY, barW, barH, clip, "CLP", kClip);

    // Master volume bottom strip
    tft.setTextSize(1);
    tft.setTextColor(kFg);
    tft.setCursor(gridX, tft.height() - 14);
    tft.print("VOL");
    int volBarX = gridX + 26;
    int volBarW = tft.width() - volBarX - 6;
    tft.drawRoundRect(volBarX, tft.height() - 16, volBarW, 10, 3, kCardBorder);
    int volFillW = (int)(volume * (volBarW - 2));
    if (volFillW > 0) {
        tft.fillRoundRect(volBarX + 1, tft.height() - 15, volFillW, 8, 2, kAccent);
    }
    tft.fillRect(volBarX + 1 + volFillW, tft.height() - 15,
                 (volBarW - 2) - volFillW, 8, kBg);
}

void Display::drawPadEditScreen(bool fullRedraw, uint8_t padIndex,
                                bool assigned, bool loop, bool gate, bool roll, int8_t choke,
                                int16_t pendingCents, uint16_t pendingStart,
                                uint16_t pendingEnd, uint8_t pendingVolume,
                                uint8_t zoomLevel, bool focusStart,
                                const uint8_t *peaks, int peakCount) {
    if (fullRedraw) {
        tft.fillScreen(kBg);
        char title[24];
        snprintf(title, sizeof(title), "PAD %02d EDIT", padIndex);
        drawHeader(title);

        tft.setTextSize(1);
        tft.setTextColor(assigned ? kAccent : kDim);
        tft.setCursor(8, 25);
        tft.print(assigned ? "[ASSIGNED]" : "[EMPTY]");

        tft.setTextColor(kDim);
        tft.setCursor(110, 25);
        tft.print("SHIFT+E1: IMPORT");

        tft.setCursor(8, tft.height() - 14);
        tft.print("E2:ZOOM | E3:ROLL | S+E2:MODE | S+E3:CHOKE");
    }

    tft.setTextSize(1);
    tft.setTextColor(kFg);

    // Line 1: Pitch & Volume
    drawTextLine(8, 38, tft.width() - 16, 9, kBg);
    tft.setCursor(8, 38);
    tft.print("ENC1: PITCH=");
    tft.print(pendingCents / 100);
    tft.print("st | SHIFT+E1: VOL=");
    tft.print(pendingVolume);
    tft.print("%");

    // Line 2: Start % (Formatted as XX.XX%)
    drawTextLine(8, 51, tft.width() - 16, 9, kBg);
    tft.setCursor(8, 51);
    tft.print("START: ");
    tft.print(pendingStart / 100);
    tft.print(".");
    if ((pendingStart % 100) < 10) tft.print("0");
    tft.print(pendingStart % 100);
    tft.print("%");

    // Line 3: End % (Formatted as XX.XX%)
    drawTextLine(8, 64, tft.width() - 16, 9, kBg);
    tft.setCursor(8, 64);
    tft.print("END:   ");
    tft.print(pendingEnd / 100);
    tft.print(".");
    if ((pendingEnd % 100) < 10) tft.print("0");
    tft.print(pendingEnd % 100);
    tft.print("%");

    // Waveform Silhouette & Zoom Box
    int waveX = 8;
    int waveY = 77;
    int waveW = tft.width() - 16; // 154 px wide
    int waveH = 40;               // 40 px tall
    int midY = waveY + waveH / 2;

    tft.fillRect(waveX, waveY, waveW, waveH, kCardBg);
    tft.drawRoundRect(waveX, waveY, waveW, waveH, 4, kCardBorder);

    // Zoom window calculations (1x = 100%, 5x = 20%, 25x = 4%)
    uint16_t windowRange = 10000 / zoomLevel;
    uint16_t focusPos = focusStart ? pendingStart : pendingEnd;
    int32_t winStart = (int32_t)focusPos - windowRange / 2;
    int32_t winEnd = winStart + windowRange;
    if (winStart < 0) { winEnd -= winStart; winStart = 0; }
    if (winEnd > 10000) { winStart -= (winEnd - 10000); winEnd = 10000; }
    if (winStart < 0) winStart = 0;

    int startPx = waveX + 1 + (int)((int32_t)(pendingStart - winStart) * (waveW - 2) / (winEnd - winStart));
    int endPx = waveX + 1 + (int)((int32_t)(pendingEnd - winStart) * (waveW - 2) / (winEnd - winStart));

    // Dark Cyan tint for active trim selection
    int fillL = startPx < waveX + 1 ? waveX + 1 : startPx;
    int fillR = endPx > waveX + waveW - 1 ? waveX + waveW - 1 : endPx;
    if (fillR > fillL) {
        tft.fillRect(fillL, waveY + 1, fillR - fillL, waveH - 2, 0x0A4A);
    }

    if (peaks && peakCount > 0) {
        for (int i = 0; i < (waveW - 2); i++) {
            int px = waveX + 1 + i;
            uint32_t posTenThousand = winStart + (uint32_t)i * (winEnd - winStart) / (waveW - 2);
            int srcIdx = (int)(posTenThousand * peakCount / 10000);
            if (srcIdx < 0) srcIdx = 0;
            if (srcIdx >= peakCount) srcIdx = peakCount - 1;

            int amp = (peaks[srcIdx] * (waveH / 2 - 2)) / 255;
            bool active = (posTenThousand >= pendingStart && posTenThousand <= pendingEnd);
            uint16_t waveColor = active ? kAccent : kDim;
            tft.drawFastVLine(px, midY - amp, amp * 2 + 1, waveColor);
        }
    }

    // Start (White) & End (Magenta) Vertical Markers
    if (startPx >= waveX + 1 && startPx <= waveX + waveW - 2) {
        tft.drawFastVLine(startPx, waveY + 1, waveH - 2, kFg);
    }
    if (endPx >= waveX + 1 && endPx <= waveX + waveW - 2) {
        tft.drawFastVLine(endPx, waveY + 1, waveH - 2, kAccentAlt);
    }

    // Zoom & Roll Badges in top right of waveform box
    tft.setTextSize(1);
    if (roll) {
        tft.setTextColor(kAccentAlt);
        tft.setCursor(waveX + waveW - 70, waveY + 3);
        tft.print("[ROLL]");
    }
    tft.setTextColor(kAccent);
    tft.setCursor(waveX + waveW - 28, waveY + 3);
    tft.print(zoomLevel);
    tft.print("x");

    // Line 4: Play Mode (SHOT / GATE / LOOP) & Choke
    drawTextLine(8, 123, tft.width() - 16, 9, kBg);
    tft.setCursor(8, 123);
    tft.print("PLAY: ");
    if (gate) {
        tft.print("GATE");
    } else if (loop) {
        tft.print("LOOP");
    } else {
        tft.print("SHOT");
    }
    tft.print(" | CHOKE: ");
    if (choke < 0)
        tft.print("NONE");
    else
        tft.print(choke);
}

void Display::drawFxScreen(bool fullRedraw, float vinyl, float hiss, float wow,
                          float filter, float volume) {
    if (fullRedraw) {
        tft.fillScreen(kBg);
        drawHeader("TEXTURE & WARMTH FX");

        tft.setTextSize(1);
        tft.setTextColor(kDim);
        tft.setCursor(4, tft.height() - 14);
        tft.print("E1:VINYL | E2:HISS | E3:WOW | E1+S:FLTR");
    }

    const int barW = 44;
    const int barH = 88;
    const int barY = 24;
    const int startX = 12;
    const int stepX = 56;

    const uint16_t kVinylColor = ST77XX_ORANGE;
    const uint16_t kHissColor  = ST77XX_CYAN;
    const uint16_t kWowColor   = ST77XX_MAGENTA;
    const uint16_t kFltrColor  = ST77XX_YELLOW;

    drawFxBar(startX + 0 * stepX, barY, barW, barH, vinyl, "VINYL", kVinylColor);
    drawFxBar(startX + 1 * stepX, barY, barW, barH, hiss, "HISS", kHissColor);
    drawFxBar(startX + 2 * stepX, barY, barW, barH, wow, "WOW", kWowColor);
    drawFxBar(startX + 3 * stepX, barY, barW, barH, filter, "FLTR", kFltrColor);
}

void Display::drawSampleScreen(bool fullRedraw, const char *sourceName,
                               bool recording, float seconds,
                               bool monitorAvailable, bool monitorOn) {
    if (fullRedraw) {
        tft.fillScreen(kBg);
        drawHeader("SAMPLE / RESAMPLE");
        tft.setTextSize(1);
        tft.setTextColor(kFg);
        tft.setCursor(8, tft.height() - 14);
        tft.print("ENC1 press: cancel / back");
    }

    tft.setTextSize(1);
    tft.setTextColor(kFg);

    drawTextLine(8, 30, tft.width() - 16, 9, kBg);
    tft.setCursor(8, 30);
    tft.print("ENC2 turn: source = ");
    tft.print(sourceName);

    drawTextLine(8, 50, tft.width() - 16, 9, kBg);
    tft.setCursor(8, 50);
    tft.print("ENC2 press: ");
    tft.print(recording ? "STOP" : "START");
    tft.print(" recording");

    drawTextLine(8, 64, tft.width() - 16, 9, kBg);
    tft.setCursor(8, 64);
    if (monitorAvailable) {
        tft.setTextColor(monitorOn ? kAccent : kFg);
        tft.print("ENC3 press: MONITOR = ");
        tft.print(monitorOn ? "ON" : "OFF");
    } else {
        tft.setTextColor(kDim);
        tft.print("MONITOR: n/a for resample");
    }

    tft.setTextSize(1);
    tft.setTextColor(kFg);
    drawTextLine(8, 90, tft.width() - 16, 18, kBg);
    tft.setTextSize(2);
    tft.setTextColor(recording ? ST77XX_RED : kDim);
    tft.setCursor(8, 90);
    tft.print(recording ? "REC " : "IDLE ");
    tft.print(seconds, 1);
    tft.print("s");
}

void Display::drawResampleScreen(bool fullRedraw, bool recording, float seconds,
                                 const bool padAssigned[NS1_NUM_PADS],
                                 const char *const padNames[NS1_NUM_PADS],
                                 const bool padHighlight[NS1_NUM_PADS]) {
    if (fullRedraw) {
        tft.fillScreen(kBg);
        char title[32];
        if (recording) {
            snprintf(title, sizeof(title), "RESAMPLING REC %.1fs", seconds);
        } else {
            snprintf(title, sizeof(title), "RESAMPLE MODE");
        }
        drawHeader(title);
        tft.setTextSize(1);
        tft.setTextColor(kDim);
        tft.setCursor(4, tft.height() - 14);
        tft.print(recording ? "TAP PADS TO PLAY | E2: STOP REC" : "E2: START REC | E1: BACK");
    }

    drawPadGrid(padHighlight, padAssigned, padNames);
}

void Display::drawChopScreen(bool fullRedraw, const uint8_t *peaks,
                             int peakCount, uint8_t numSlices,
                             uint8_t selectedSlice, const uint32_t *sliceStart,
                             const uint32_t *sliceEnd, uint32_t totalSamples) {
    const int waveX = 4;
    const int waveY = 24;
    const int waveW = tft.width() - 8;
    const int waveH = 70;
    const int midY = waveY + waveH / 2;

    if (fullRedraw) {
        tft.fillScreen(kBg);
        drawHeader("CHOP");
        tft.setTextSize(1);
        tft.setTextColor(kFg);
        tft.setCursor(4, waveY + waveH + 20);
        tft.print("E1: COMMIT SLICES | S+PAD: SAVE FULL");
        tft.setCursor(4, waveY + waveH + 32);
        tft.print("E2/E3: NUDGE | E1: COMMIT | E3: BACK");
    }

    tft.fillRect(waveX, waveY, waveW, waveH, kCardBg);
    tft.drawRoundRect(waveX, waveY, waveW, waveH, 4, kCardBorder);
    for (int i = 0; i < waveW && i < peakCount; i++) {
        int srcIdx = i * peakCount / waveW;
        int amp = (peaks[srcIdx] * (waveH / 2)) / 255;
        tft.drawFastVLine(waveX + i, midY - amp, amp * 2 + 1, kFg);
    }

    if (totalSamples > 0) {
        for (uint8_t s = 0; s < numSlices; s++) {
            int xStart =
                waveX + (int)((uint64_t)sliceStart[s] * waveW / totalSamples);
            int xEnd =
                waveX + (int)((uint64_t)sliceEnd[s] * waveW / totalSamples);
            uint16_t color = (s == selectedSlice) ? kAccent : kComp;
            tft.drawFastVLine(xStart, waveY, waveH, color);
            tft.drawFastVLine(xEnd, waveY, waveH, color);
        }
    }

    tft.setTextSize(1);
    tft.setTextColor(kFg);
    drawTextLine(4, waveY + waveH + 6, 120, 9, kBg);
    tft.setCursor(4, waveY + waveH + 6);
    tft.print("SLICE ");
    tft.print(selectedSlice + 1);
    tft.print("/");
    tft.print(numSlices);
}

void Display::drawFileScreen(bool fullRedraw, const String *bankNames,
                             int count, int selectedIndex, int scrollOffset,
                             const char *currentBank, bool usbOnlyRoute) {
    if (fullRedraw) {
        tft.fillScreen(kBg);
        drawHeader("BANKS & AUDIO OUT");
        tft.setTextSize(1);
        tft.setTextColor(kDim);
        tft.setCursor(4, tft.height() - 26);
        tft.print("E1: LOAD | E2: NEW | S+E1: ROUTE OUT");
        tft.setCursor(4, tft.height() - 14);
        tft.print("S+E2: EXPORT BANK | E3: BACK");
    }

    tft.setTextSize(1);
    drawTextLine(4, 22, tft.width() - 8, 12, kBg);
    tft.setCursor(4, 22);
    tft.setTextColor(usbOnlyRoute ? ST77XX_MAGENTA : kAccent);
    tft.print("ROUTE: ");
    tft.print(usbOnlyRoute ? "[USB ONLY (PC)]" : "[HEADPHONES + USB]");

    if (count == 0) {
        drawTextLine(8, 38, tft.width() - 16, 70, kBg);
        tft.setCursor(8, 40);
        tft.setTextColor(kAccentAlt);
        
        uint8_t errCode = SD.sdfs.sdErrorCode();
        uint8_t errData = SD.sdfs.sdErrorData();
        bool cardOk = SD.sdfs.card() != nullptr && errCode == 0;
        File rF = SD.open("/");
        bool rOk = (bool)rF;
        if (rF) rF.close();

        File bF = SD.open("/BANKS");
        bool bOk = (bool)bF;
        if (bF) bF.close();

        File sF = SD.open("/SAMPLES");
        bool sOk = (bool)sF;
        if (sF) sF.close();

        tft.print("SD DIAGNOSTIC:");
        tft.setCursor(8, 52);
        tft.setTextColor(kFg);
        if (cardOk) {
            tft.printf("Card:OK  Root:%s", rOk ? "OK" : "NO");
        } else {
            tft.printf("Card:NO (0x%02X/0x%02X)", errCode, errData);
        }
        tft.setCursor(8, 64);
        tft.printf("Banks:%s  Samples:%s", bOk ? "OK" : "NO", sOk ? "OK" : "NO");
        tft.setCursor(8, 76);
        tft.setTextColor(kDim);
        tft.print("Check SD seating / card");
        return;
    }

    const int rowH = 14;
    int y = 38;
    int shown = count - scrollOffset;
    if (shown > kMenuVisibleRows)
        shown = kMenuVisibleRows;
    if (shown < 0)
        shown = 0;

    for (int row = 0; row < kMenuVisibleRows; row++) {
        drawTextLine(8, y, tft.width() - 16, rowH - 2, kBg);
        if (row < shown) {
            int i = scrollOffset + row;
            bool sel = (i == selectedIndex);
            bool isCurrent = bankNames[i].equals(currentBank);

            tft.setTextColor(sel ? kAccent : kBg);
            tft.setCursor(8, y);
            tft.print(">");

            tft.setTextColor(sel ? kAccent : kFg);
            tft.setCursor(20, y);
            tft.print(bankNames[i]);
            if (isCurrent)
                tft.print(" *");
        }
        y += rowH;
    }
}

void Display::drawErrorScreen(bool fullRedraw, const String *error) {
    tft.fillScreen(kBg);

    tft.setTextSize(5);
    tft.setTextColor(kDim);
    tft.setCursor(4, tft.height() - 14);

    if (error != nullptr) {
        tft.println(*error);
    }
}

void Display::drawPadGrid(const bool highlight[NS1_NUM_PADS],
                         const bool padAssigned[NS1_NUM_PADS],
                         const char *const padNames[NS1_NUM_PADS]) {
    const int cell = 26;
    const int gridX = 6;
    const int gridY = 24;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int idx = r * 4 + c;
            int x = gridX + c * (cell + 3);
            int y = gridY + r * (cell + 3);

            if (highlight[idx]) {
                tft.fillRoundRect(x, y, cell, cell, 4, kAccent);
                tft.drawRoundRect(x, y, cell, cell, 4, kFg);
            } else {
                tft.fillRoundRect(x, y, cell, cell, 4, kCardBg);
                tft.drawRoundRect(x, y, cell, cell, 4, padAssigned[idx] ? kAccent : kCardBorder);
            }

            const char *name = padNames[idx];
            if (name && name[0]) {
                char buf[5];
                size_t len = strlen(name);
                if (len > 4) len = 4;
                memcpy(buf, name, len);
                buf[len] = '\0';

                tft.setTextSize(1);
                tft.setTextColor(highlight[idx] ? ST77XX_BLACK : (padAssigned[idx] ? kFg : kDim));
                tft.setCursor(x + 2, y + cell - 9);
                tft.print(buf);
            }
        }
    }
}

void Display::drawPadCopyScreen(bool fullRedraw, const char *bankName,
                                uint8_t step, uint8_t srcPad,
                                const char *targetBankName,
                                const bool padAssigned[NS1_NUM_PADS],
                                const char *const padNames[NS1_NUM_PADS]) {
    if (fullRedraw) {
        tft.fillScreen(kBg);
        char title[40];
        if (step == 0) {
            snprintf(title, sizeof(title), "COPY PAD: SELECT SOURCE");
        } else {
            snprintf(title, sizeof(title), "COPY P%02d -> [%s]", srcPad, targetBankName ? targetBankName : bankName);
        }
        drawHeader(title);

        tft.setTextSize(1);
        tft.setTextColor(kDim);
        tft.setCursor(8, tft.height() - 14);
        if (step == 0) {
            tft.print("TAP PAD TO SELECT | ENC2: CANCEL");
        } else {
            tft.print("E1:BANK | TAP DEST PAD | E2:CANCEL");
        }
    }

    bool highlight[NS1_NUM_PADS] = {false};
    if (step == 1 && srcPad < NS1_NUM_PADS && targetBankName && strcmp(targetBankName, bankName) == 0) {
        highlight[srcPad] = true;
    }
    drawPadGrid(highlight, padAssigned, padNames);
}

void Display::drawPadDeleteScreen(bool fullRedraw, const char *bankName,
                                  const bool padAssigned[NS1_NUM_PADS],
                                  const char *const padNames[NS1_NUM_PADS]) {
    if (fullRedraw) {
        tft.fillScreen(kBg);
        drawHeader("DELETE PAD: TAP TO CLEAR");

        tft.setTextSize(1);
        tft.setTextColor(kDim);
        tft.setCursor(8, tft.height() - 14);
        tft.print("TAP PAD TO DELETE | ENC1: EXIT");
    }

    bool highlight[NS1_NUM_PADS] = {false};
    drawPadGrid(highlight, padAssigned, padNames);
}

void Display::drawImportScreen(bool fullRedraw, uint8_t padIndex,
                               const String *fileNames, int count,
                               int selectedIndex, int scrollOffset) {
    if (fullRedraw) {
        tft.fillScreen(kBg);
        char title[24];
        snprintf(title, sizeof(title), "IMPORT -> PAD %02d", padIndex);
        drawHeader(title);
        tft.setTextSize(1);
        tft.setTextColor(kDim);
        tft.setCursor(4, tft.height() - 14);
        tft.print("ENC1 turn=select press=assign  ENC3 press=cancel");
    }

    tft.setTextSize(1);

    if (count == 0) {
        drawTextLine(8, 24, tft.width() - 16, 70, kBg);
        tft.setCursor(8, 24);
        tft.setTextColor(kAccentAlt);

        uint8_t errCode = SD.sdfs.sdErrorCode();
        uint8_t errData = SD.sdfs.sdErrorData();
        bool cardOk = SD.sdfs.card() != nullptr && errCode == 0;
        File rF = SD.open("/");
        bool rOk = (bool)rF;
        if (rF) rF.close();

        File sF = SD.open("/SAMPLES");
        bool sOk = (bool)sF;
        if (sF) sF.close();

        tft.print("SD IMPORT DIAG:");
        tft.setCursor(8, 36);
        tft.setTextColor(kFg);
        if (cardOk) {
            tft.printf("Card:OK  Root:%s", rOk ? "OK" : "NO");
        } else {
            tft.printf("Card:NO (0x%02X/0x%02X)", errCode, errData);
        }
        tft.setCursor(8, 48);
        tft.printf("Samples Dir: %s", sOk ? "OK" : "NO");
        return;
    }

    const int rowH = 14;
    int y = 22;
    int shown = count - scrollOffset;
    if (shown > kMenuVisibleRows)
        shown = kMenuVisibleRows;
    if (shown < 0)
        shown = 0;

    for (int row = 0; row < kMenuVisibleRows; row++) {
        drawTextLine(8, y, tft.width() - 16, rowH - 2, kBg);
        if (row < shown) {
            int i = scrollOffset + row;
            bool sel = (i == selectedIndex);

            tft.setTextColor(sel ? kAccent : kBg);
            tft.setCursor(8, y);
            tft.print(">");

            tft.setTextColor(sel ? kAccent : kFg);
            tft.setCursor(20, y);

            const char *dispName = fileNames[i].c_str();
            if (strncmp(dispName, "/SAMPLES/", 9) == 0) {
                dispName += 9;
            } else if (strncmp(dispName, "/REC/", 5) == 0) {
                dispName += 1;
            } else if (dispName[0] == '/') {
                dispName += 1;
            }
            tft.print(dispName);
        }
        y += rowH;
    }
}
