#pragma once

#include "../config/Config.h"
#include "../config/Pins.h"
#include <Arduino.h>

// Scans the 4x4 diode-isolated pad matrix. Pad index = row*4 + col, which reads
// left-to-right, top-to- bottom on the panel (confirmed against the PCB's
// pick-and-place coordinates — row/col numbering matches the physical grid
// exactly).
class PadMatrix {
  public:
    void begin();

    void update();

    bool isDown(uint8_t padIndex) const { return down[padIndex]; }

    bool justPressed(uint8_t padIndex);
    bool justReleased(uint8_t padIndex);

  private:
    bool down[NS1_NUM_PADS] = {false};
    bool pendingPress[NS1_NUM_PADS] = {false};
    bool pendingRelease[NS1_NUM_PADS] = {false};
    uint32_t lastChangeMicros[NS1_NUM_PADS] = {0};
};
