#include "PadMatrix.h"

void PadMatrix::begin() {
    for (int r = 0; r < 4; r++) {
        pinMode(PIN_MATRIX_ROWS[r], OUTPUT);
        digitalWrite(PIN_MATRIX_ROWS[r], HIGH);
    }

    for (int c = 0; c < 4; c++) {
        pinMode(PIN_MATRIX_COLS[c], INPUT_PULLUP);
    }
}

void PadMatrix::update() {
    for (int r = 0; r < 4; r++) {
        digitalWrite(PIN_MATRIX_ROWS[r], LOW);
        delayMicroseconds(50); // let the driven row settle through the diodes

        for (int c = 0; c < 4; c++) {
            uint8_t idx = r * 4 + c;
            bool pressed = digitalRead(PIN_MATRIX_COLS[c]) == LOW;

            if (pressed != down[idx]) {
                uint32_t now = micros();

                if (now - lastChangeMicros[idx] >= NS1_PAD_DEBOUNCE_US) {
                    down[idx] = pressed;
                    lastChangeMicros[idx] = now;

                    if (pressed)
                        pendingPress[idx] = true;
                    else
                        pendingRelease[idx] = true;

                    Serial.printf("NS-1: pad %d %s\n", idx,
                                  pressed ? "down" : "up");
                }
            }
        }

        digitalWrite(PIN_MATRIX_ROWS[r], HIGH);
    }
}

bool PadMatrix::justPressed(uint8_t padIndex) {
    if (!pendingPress[padIndex])
        return false;

    pendingPress[padIndex] = false;

    return true;
}

bool PadMatrix::justReleased(uint8_t padIndex) {
    if (!pendingRelease[padIndex])
        return false;

    pendingRelease[padIndex] = false;

    return true;
}
