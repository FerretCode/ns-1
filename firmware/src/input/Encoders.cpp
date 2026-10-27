#include "Encoders.h"

void Encoders::begin() {
    for (int i = 0; i < NS1_NUM_ENCODERS; i++) {
        pinMode(btnPins[i], INPUT_PULLUP);
    }
}

void Encoders::update() {
    for (int i = 0; i < NS1_NUM_ENCODERS; i++) {
        long pos = enc[i]->read();
        long diff = pos - lastRaw[i];
        lastRaw[i] = pos;

        if (diff != 0) {
            rawRemainder[i] += diff;
            int32_t detents = rawRemainder[i] / 4;
            rawRemainder[i] -= detents * 4;
            // Negated: the raw quadrature count's sign is whatever the
            // physical A/B pin wiring happens to produce, not a chosen
            // convention. On this board's encoders that came out backwards
            // from "turn right = increase" (confirmed against bank
            // scrolling), so flip it once here rather than in every screen
            // that reads delta().
            deltaAccum[i] -= detents;
        }

        bool pressed = digitalRead(btnPins[i]) == LOW;
        if (pressed != btnDown[i]) {
            uint32_t now = micros();

            if (now - btnLastChangeMicros[i] >= NS1_BUTTON_DEBOUNCE_US) {
                btnDown[i] = pressed;
                btnLastChangeMicros[i] = now;

                if (pressed)
                    btnPendingPress[i] = true;
                else
                    btnPendingRelease[i] = true;
            }
        }
    }
}

int32_t Encoders::delta(uint8_t index) {
    int32_t v = deltaAccum[index];
    deltaAccum[index] = 0;
    return v;
}

bool Encoders::buttonJustPressed(uint8_t index) {
    if (!btnPendingPress[index])
        return false;
    btnPendingPress[index] = false;
    return true;
}

bool Encoders::buttonJustReleased(uint8_t index) {
    if (!btnPendingRelease[index])
        return false;
    btnPendingRelease[index] = false;
    return true;
}
