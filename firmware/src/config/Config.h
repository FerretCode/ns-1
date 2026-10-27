#pragma once

// ---------------------------------------------------------------------------
// General
// ---------------------------------------------------------------------------
#define NS1_SAMPLE_RATE 44100
#define NS1_NUM_PADS 16
#define NS1_NUM_VOICES 8 // simultaneous playing samples (voice-stealing)

// Block count, each block is 128 samples of int16.
#define NS1_AUDIO_MEMORY_BLOCKS 256

// ---------------------------------------------------------------------------
// TFT
// ---------------------------------------------------------------------------
#define NS1_TFT_WIDTH 170
#define NS1_TFT_HEIGHT 320
#define NS1_TFT_ROTATION 3 // landscape

// ---------------------------------------------------------------------------
// Storage layout (microSD)
// ---------------------------------------------------------------------------
#define NS1_DIR_BANKS "/BANKS"
#define NS1_DIR_REC "/REC"
#define NS1_DIR_SAMPLES "/SAMPLES" // drop WAV files here from a computer; browsable on-device from PAD_EDIT (SHIFT + ENC1 press)
#define NS1_BANK_CFG_NAME "BANK.CFG"

// ---------------------------------------------------------------------------
// Input timing
// ---------------------------------------------------------------------------
#define NS1_PAD_DEBOUNCE_US 3000
#define NS1_BUTTON_DEBOUNCE_US 15000
#define NS1_POT_SMOOTH_ALPHA 0.15f // EMA coefficient, 0..1 (higher = snappier)
#define NS1_UI_FPS 30
