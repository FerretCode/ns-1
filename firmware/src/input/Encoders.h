#pragma once

#include "../config/Config.h"
#include "../config/Pins.h"
#include <Arduino.h>
#include <Encoder.h>

#define NS1_NUM_ENCODERS 3

class Encoders {
  public:
    void begin();
    void update();

    // accumulated detents since the last call (signed, consumes the count).
    int32_t delta(uint8_t index);

    bool buttonDown(uint8_t index) const { return btnDown[index]; }
    bool buttonJustPressed(uint8_t index);
    bool buttonJustReleased(uint8_t index);

  private:
    Encoder enc0{PIN_ENC1_A, PIN_ENC1_B};
    Encoder enc1{PIN_ENC2_A, PIN_ENC2_B};
    Encoder enc2{PIN_ENC3_A, PIN_ENC3_B};
    Encoder *enc[NS1_NUM_ENCODERS] = {&enc0, &enc1, &enc2};

    const uint8_t btnPins[NS1_NUM_ENCODERS] = {PIN_ENC1_SW, PIN_ENC2_SW,
                                               PIN_ENC3_SW};

    long lastRaw[NS1_NUM_ENCODERS] = {0};
    long rawRemainder[NS1_NUM_ENCODERS] = {0};
    int32_t deltaAccum[NS1_NUM_ENCODERS] = {0};

    bool btnDown[NS1_NUM_ENCODERS] = {false};
    bool btnPendingPress[NS1_NUM_ENCODERS] = {false};
    bool btnPendingRelease[NS1_NUM_ENCODERS] = {false};
    uint32_t btnLastChangeMicros[NS1_NUM_ENCODERS] = {0};
};
