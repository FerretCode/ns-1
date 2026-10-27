#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// TFT (Adafruit 1.9" 170x320 ST7789) — 11-pin header, hardware SPI0
// ---------------------------------------------------------------------------
#define PIN_TFT_CS 10
#define PIN_TFT_DC 9
#define PIN_TFT_RST 22

// ---------------------------------------------------------------------------
// 16 pads — Cherry MX switches, wired as a diode-isolated 4x4 matrix
// ---------------------------------------------------------------------------
static const uint8_t PIN_MATRIX_COLS[4] = {2, 3, 4, 5};
static const uint8_t PIN_MATRIX_ROWS[4] = {24, 25, 26, 27};

// ---------------------------------------------------------------------------
// Rotary encoders (quadrature + push button), left to right on the panel
// ---------------------------------------------------------------------------
#define PIN_ENC1_A 0
#define PIN_ENC1_B 1
#define PIN_ENC1_SW 6

#define PIN_ENC2_A 28
#define PIN_ENC2_B 29
#define PIN_ENC2_SW 30

#define PIN_ENC3_A 31
#define PIN_ENC3_B 32
#define PIN_ENC3_SW 33

// ---------------------------------------------------------------------------
// Pots (FX control) — analog, RC-filtered on board
// ---------------------------------------------------------------------------
#define PIN_POT1 A0 // 14
#define PIN_POT2 A1 // 15
#define PIN_POT3 A2 // 16
#define PIN_POT4 A3 // 17

// ---------------------------------------------------------------------------
// Shift button + master volume slider
// ---------------------------------------------------------------------------
#define PIN_SHIFT_BTN 40 // A16, active LOW, external pull-up on board
#define PIN_SLIDER 41    // A17, RC-filtered analog
