#include "AnalogControls.h"

void AnalogControls::begin() {
    analogReadResolution(12);
    pinMode(PIN_SHIFT_BTN, INPUT_PULLUP);
}

void AnalogControls::update() {
    for (int i = 0; i < 4; i++) {
        float norm = analogRead(potPins[i]) / 4095.0f;
        potSmoothed[i] += NS1_POT_SMOOTH_ALPHA * (norm - potSmoothed[i]);
    }

    float sliderRaw = analogRead(PIN_SLIDER) / 4095.0f;
    float sliderNorm = 0.0f;
    if (sliderRaw > 0.02f) {
        sliderNorm = (sliderRaw - 0.02f) / 0.96f;
        if (sliderNorm > 1.0f) sliderNorm = 1.0f;
    }
    sliderSmoothed += NS1_POT_SMOOTH_ALPHA * (sliderNorm - sliderSmoothed);

    bool pressed = digitalRead(PIN_SHIFT_BTN) == LOW;
    if (pressed != shiftState) {
        uint32_t now = micros();

        if (now - shiftLastChangeMicros >= NS1_BUTTON_DEBOUNCE_US) {
            shiftState = pressed;
            shiftLastChangeMicros = now;

            if (pressed)
                shiftPendingPress = true;
            else
                shiftPendingRelease = true;
        }
    }
}

bool AnalogControls::shiftJustPressed() {
    if (!shiftPendingPress)
        return false;

    shiftPendingPress = false;

    return true;
}

bool AnalogControls::shiftJustReleased() {
    if (!shiftPendingRelease)
        return false;

    shiftPendingRelease = false;

    return true;
}
