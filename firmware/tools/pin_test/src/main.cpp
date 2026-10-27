// Standalone hardware bring-up test — see platformio.ini for why this is a
// separate project from the main firmware. Pin numbers are copied from
// ../../../src/config/Pins.h; keep them in sync if that ever changes.
//
// Prints the live state of the whole 4x4 pad matrix, the slider's raw ADC
// reading, and the shift button, ~5x/second, forever. No debouncing, no
// smoothing, no libraries beyond core Arduino — this is meant to show
// exactly what the pins are doing at the electrical level, not to look
// like the real firmware.

#include <Arduino.h>

static const uint8_t COLS[4] = {2, 3, 4, 5};
static const uint8_t ROWS[4] = {24, 25, 26, 27};
static const uint8_t PIN_SLIDER = 41;
static const uint8_t PIN_SHIFT_BTN = 40;

void setup() {
    Serial.begin(115200);

    // Rows drive, columns sense — required by the as-built diode direction
    // (every pad diode is wired anode-toward-column, cathode-toward-row),
    // not a style choice. See PadMatrix.cpp for the full explanation.
    for (int r = 0; r < 4; r++) {
        pinMode(ROWS[r], OUTPUT);
        digitalWrite(ROWS[r], HIGH);
    }
    for (int c = 0; c < 4; c++) {
        pinMode(COLS[c], INPUT_PULLUP);
    }
    pinMode(PIN_SHIFT_BTN, INPUT_PULLUP);
    analogReadResolution(12);

    delay(1500); // give the host a moment to open the serial monitor
    Serial.println();
    Serial.println("NS-1 pin test — raw pad matrix / slider / shift button");
    Serial.println("Idle grid should read all dots. Press pads and watch");
    Serial.println("the matching cell flip to X. Move the slider and watch");
    Serial.println("its raw value track smoothly from ~0 to ~4095.");
    Serial.println();
}

void loop() {
    static uint32_t lastPrint = 0;
    uint32_t now = millis();
    if (now - lastPrint < 200) return;
    lastPrint = now;

    bool grid[4][4];
    for (int r = 0; r < 4; r++) {
        digitalWrite(ROWS[r], LOW);
        delayMicroseconds(50); // let the driven row settle through the diodes
        for (int c = 0; c < 4; c++) {
            grid[r][c] = (digitalRead(COLS[c]) == LOW);
        }
        digitalWrite(ROWS[r], HIGH);
    }

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            Serial.print(grid[r][c] ? 'X' : '.');
            Serial.print(' ');
        }
    }

    int sliderRaw = analogRead(PIN_SLIDER);
    bool shiftDown = digitalRead(PIN_SHIFT_BTN) == LOW;
    Serial.printf(" | slider raw=%4d (%.2f)  shift=%s\n", sliderRaw,
                  sliderRaw / 4095.0f, shiftDown ? "DOWN" : "up");
}
