#pragma once

#include "../config/Config.h"
#include "../config/Pins.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>

// Thin drawing layer over the ST7789 panel. Deliberately takes plain data
// (arrays/numbers/strings) rather than engine/bank types, so it doesn't
// need to know about AudioEngine/BankManager — UI.cpp is the only place
// that translates app state into these calls.
//
// `fullRedraw` must be true on the first call after a screen transition
// (clears the panel and draws static chrome: header, labels, hints) and
// false on every subsequent call while the same screen stays up (only the
// bounded, per-widget regions that can actually change get touched). See
// README "Display flicker / tearing" for why this matters.
class Display {
  public:
    // Scrollable list screens' (FILE, IMPORT) visible-row window. Note
    // tft.height() is 170 at runtime, not 320: setRotation() with an odd
    // value (landscape) swaps Adafruit_GFX's reported width()/height()
    // relative to the physical panel dims passed to init() — easy to get
    // backwards when doing layout math by hand.
    static constexpr int kMenuVisibleRows = 8;

    void begin();

    // padHighlight is "down OR recently pressed" (UI keeps a short flash
    // alive after release so even a fast tap is visible — see UI::render()).
    // padNames[i] may be nullptr/empty; truncated to fit the pad cell.
    void drawPlayScreen(bool fullRedraw, const char *bankName,
                        const bool padHighlight[NS1_NUM_PADS],
                        const bool padAssigned[NS1_NUM_PADS],
                        const char *const padNames[NS1_NUM_PADS], float comp,
                        float grit, float sat, float clip, float volume);

    void drawPadGrid(const bool highlight[NS1_NUM_PADS],
                     const bool padAssigned[NS1_NUM_PADS],
                     const char *const padNames[NS1_NUM_PADS]);

    void drawPadCopyScreen(bool fullRedraw, const char *bankName,
                           uint8_t step, uint8_t srcPad,
                           const char *targetBankName,
                           const bool padAssigned[NS1_NUM_PADS],
                           const char *const padNames[NS1_NUM_PADS]);

    void drawPadDeleteScreen(bool fullRedraw, const char *bankName,
                             const bool padAssigned[NS1_NUM_PADS],
                             const char *const padNames[NS1_NUM_PADS]);

    void drawPadEditScreen(bool fullRedraw, uint8_t padIndex, bool assigned,
                           bool loop, bool gate, bool roll, int8_t choke,
                           int16_t pendingCents, uint16_t pendingStart,
                           uint16_t pendingEnd, uint8_t pendingVolume,
                           uint8_t zoomLevel, bool focusStart,
                           const uint8_t *peaks, int peakCount);

    void drawFxScreen(bool fullRedraw, float vinyl, float hiss, float wow,
                      float filter, float volume);

    void drawSampleScreen(bool fullRedraw, const char *sourceName,
                          bool recording, float seconds, bool monitorAvailable,
                          bool monitorOn);

    void drawResampleScreen(bool fullRedraw, bool recording, float seconds,
                            const bool padAssigned[NS1_NUM_PADS],
                            const char *const padNames[NS1_NUM_PADS],
                            const bool padHighlight[NS1_NUM_PADS]);

    void drawChopScreen(bool fullRedraw, const uint8_t *peaks, int peakCount,
                        uint8_t numSlices, uint8_t selectedSlice,
                        const uint32_t *sliceStart, const uint32_t *sliceEnd,
                        uint32_t totalSamples);

    // Only [scrollOffset, scrollOffset+kMenuVisibleRows) is drawn — caller
    // (UI) keeps scrollOffset following selectedIndex so it's always
    // within that window.
    void drawFileScreen(bool fullRedraw, const String *bankNames, int count,
                        int selectedIndex, int scrollOffset,
                        const char *currentBank, bool usbOnlyRoute);

    // fileNames is the full list; only [scrollOffset, scrollOffset+visible)
    // is actually drawn — caller (UI) keeps scrollOffset following
    // selectedIndex so it's always in that window.
    void drawImportScreen(bool fullRedraw, uint8_t padIndex,
                          const String *fileNames, int count, int selectedIndex,
                          int scrollOffset);

    void drawErrorScreen(bool fullRedraw, const String *error);

  private:
    Adafruit_ST7789 tft{PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST};

    void drawHeader(const char *title);
    void drawFxBar(int x, int y, int w, int h, float value, const char *label,
                   uint16_t color);
    void drawTextLine(int x, int y, int w, int h, uint16_t bg);
};
