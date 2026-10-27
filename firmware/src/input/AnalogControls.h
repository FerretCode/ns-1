#pragma once

#include "../config/Config.h"
#include "../config/Pins.h"
#include <Arduino.h>

class AnalogControls {
  public:
    void begin();
    void update();

    float pot(uint8_t index) const {
        return potSmoothed[index];
    } // 0..3, normalized 0..1

    float slider() const { return sliderSmoothed; } // normalized 0..1

    bool shiftDown() const { return shiftState; }
    bool shiftJustPressed();
    bool shiftJustReleased();

  private:
    const uint8_t potPins[4] = {PIN_POT1, PIN_POT2, PIN_POT3, PIN_POT4};
    float potSmoothed[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float sliderSmoothed = 0.0f;

    bool shiftState = false;
    bool shiftPendingPress = false;
    bool shiftPendingRelease = false;
    uint32_t shiftLastChangeMicros = 0;
};
