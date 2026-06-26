# ns-1

Lo-fi first hardware sampler

# Overview

- Teensy 4.1 based
- 16 Cherry MX switch pads
- 1/4" line in/out
- 3.5mm headphone jack
- 3 rotary encoders for sample editing
- 4 potentiometers for FX control
- Slider for master track volume control
- MicroSD card for loading and exporting samples
- Per-pad looping
- Resampling
- Sampling over USB-C

## Architecture

- The core audio processing functionality is provided by the Teensy 4.1 and its accompanying audio shield (Rev. D)
- Adafruit 1.9" TFT display over SPI
- Line in/out, headphone jack is handled by the audio shield, with volume control in firmware
