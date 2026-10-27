# NS-1 firmware

Teensy 4.1 + Audio Shield Rev D firmware for the NS-1 — a lo-fi-first,
resample-first hardware sampler for tape music / soulhop: 16-pad chopping,
heavy compression, grit, saturation and hard clipping, and sampling over
line-in, and USB-C.

This pin map and wiring below were **reverse-derived from the actual KiCad
schematic** (`ns-1.kicad_sch`) via a netlist export, not guessed — see
"Regenerating the pin map" if you respin the board.

## Building

Requires [PlatformIO](https://platformio.org/) (CLI or the VS Code
extension).

```sh
cd firmware
pio run                 # build
pio run -t upload       # build + flash over USB-C
pio device monitor       # serial debug log
```

Builds clean with `pio run` (PlatformIO Teensy platform 5.2.0, Audio
library 1.3). One gotcha if you're retargeting this: this platform version
does **not** honor `board_build.usb_type` — the USB personality (MIDI +
Audio + Serial) has to be selected as a raw `-D` in `build_flags` instead
(see the comment there). Without it, `AudioInputUSB`/`AudioOutputUSB`
silently don't exist and the build fails on the `AudioConnection` array.

## Debugging: no audio / pads seem unresponsive

Everything of interest is logged to `Serial` at 115200 baud — run
`pio device monitor` (or open a serial terminal) while poking at the
hardware and the log narrows down which of the three independent failure
points you're hitting:

- **Is the pad matrix detecting presses at all?** Every physical press/
  release prints `NS-1: pad <n> down` / `... up` directly from
  `PadMatrix::update()`, before any other logic runs. If nothing prints
  while you press pads (but other buttons like shift work fine), see
  "Every pad fails identically but shift works" below before assuming a
  wiring fault — there's a specific, confirmed cause for exactly that
  pattern on this board.
- **Is the SD card / bank loading correctly?** `NS-1: SD card init failed`
  prints on boot if `SD.begin()` fails outright. Separately, once booted,
  `NS-1: ready. bank="..." pads assigned=N/16` reports how many pads the
  loaded bank actually found files for — if this is `0/16` after loading a
  bank you expect to be full, the files/`BANK.CFG` don't match up (check
  filenames are exactly `PAD00.WAV`..`PAD15.WAV`, case included).
- **Is the codec responding?** `NS-1: SGTL5000 codec OK` / `NOT RESPONDING`
  prints once in `AudioEngine::begin()`, based on whether the very first
  I2C write to the chip succeeded. `NOT RESPONDING` points at the audio
  shield itself — reseat it on the Teensy's headers, check it's fully
  seated (a half-inserted shield is the classic cause), and check the
  board's I2C/I2S traces to pins 7/8/18/19/20/21/23.
- **Is a specific pad's file actually opening?** `NS-1: failed to open
  <path> for playback` prints if `AudioPlaySdWav::play()` couldn't open
  that pad's file — distinguishes "this one pad's file is missing/corrupt"
  from a systemic problem.

On the UI side: a pad flashes orange for a minimum ~150ms on every press
(`UI::kPadFlashMillis`), even if the physical press was shorter than one
render tick — so on PLAY, a pad that never visibly flashes when pressed is
corroborating evidence for the matrix problem above, independent of the
serial log.

### Every pad fails identically but shift works — confirmed diode direction issue

This exact symptom (100% of pads dead, non-matrixed buttons like shift
fine) was hit and traced to a real cause, not a guess: exporting the
schematic's netlist (`kicad-cli sch export netlist --format kicadxml`)
shows every pad diode wired the same way —
`Col N -> switch -> diode anode | diode cathode -> Row N` — for all 16
diodes identically. That's the deciding detail: a *uniform* wiring choice
produces a *uniform* result, not "at least one would work by chance."
Whichever side is driven low during a scan must be the diode's cathode
side for current to ever flow when a switch closes (diodes only conduct
anode-to-cathode); with columns driven low and rows pulled high (the more
common layout, and what an earlier version of this firmware did), the
column/anode side sits at 0V and the row/cathode side sits at 3.3V —
backwards for every diode, all at once. Shift has no diode in its path at
all, so it was unaffected — which is exactly the reported pattern.

The fix that's in this tree now: `PadMatrix` drives **rows** low one at a
time and reads **columns** with `INPUT_PULLUP` (the reverse of the more
typical layout) — that puts the column/anode side high and the row/cathode
side low, which is correctly forward-biased. No rework needed; this is a
scan-direction fix, not a hardware fix. `tools/pin_test/` was updated the
same way, so you can confirm it electrically (raw `X`/`.` grid, no
firmware logic involved) before trusting the full firmware.

If you ever respin the board: either flip the diode footprint so the more
common cols-drive/rows-read scan works as originally written, or just keep
matching PadMatrix's rows-drive direction — both are valid, just pick one
and keep the schematic and firmware in agreement.

### If literally nothing shows in Serial at all — not even boot messages

That's a different problem from "the matrix isn't detecting presses" — it
means either the upload didn't actually take (check `pio run -t upload`
printed success, not just `pio run`), the monitor is on the wrong
port/baud, or the board isn't running. Before chasing matrix wiring,
confirm you see *something* — the `NS-1: SD card init...`/`SGTL5000
codec...`/`NS-1: ready...` lines all print unconditionally within the
first second after boot, with no button involved at all.

### Standalone hardware test (bypasses the whole firmware)

`tools/pin_test/` is a second, tiny, completely independent PlatformIO
project — no Audio library, no SD, no TFT, no Encoder lib, nothing that
could plausibly interfere with the pad matrix or slider pins. It prints
the raw state of all 16 matrix cells, the slider's raw ADC reading, and
the shift button, ~5x/second, forever:

```sh
cd firmware/tools/pin_test
pio run -t upload
pio device monitor
```

If pressing pads still shows nothing here, or the slider's raw ADC value
doesn't track its physical position going both up *and* down, that's
conclusive: it's the wiring/pins/switches themselves, not anything in the
layered main firmware (input smoothing, the audio engine, SGTL5000, none
of it is even compiled into this test). That directly narrows down the
volume-only-goes-up report too — the raw `slider raw=` number bypasses the
EMA smoothing and the SGTL5000 `lineOutLevel()`/`volume()` calls entirely,
so if *it* doesn't fall back down when you slide down, the pot/fader
itself (or its wiring — e.g. only two of its three terminals connected,
which behaves exactly like this: rises as you'd expect, then floats/sticks
instead of falling) is the actual problem, not firmware logic.

## Hardware overview

| Function | Teensy 4.1 pin(s) |
|---|---|
| Audio Shield Rev D (I2S/I2C) | 7, 8, 18, 19, 20, 21, 23 (fixed silicon pins, standard PJRC layout) |
| TFT (Adafruit 1.9" 170x320 ST7789) | CS=10, DC=9, RST=22, SCK=13, MOSI=11, MISO=12 (hw SPI0) |
| microSD | Teensy 4.1's built-in SDIO slot (`SD.begin(BUILTIN_SDCARD)`) — separate from the TFT's SPI bus |
| 16 pads (4x4 diode matrix) | rows 24,25,26,27 (driven) / cols 2,3,4,5 (read, `INPUT_PULLUP`) — see "Debugging" below for why it's rows-drive, not the more common cols-drive |
| Encoder 1 (A/B/SW) | 0, 1, 6 |
| Encoder 2 (A/B/SW) | 28, 29, 30 |
| Encoder 3 (A/B/SW) | 31, 32, 33 |
| Pots 1-4 | A0, A1, A2, A3 |
| Shift button | 40 (active low, board has its own pull-up) |
| Master volume slider | 41 (analog) |

Full detail and rationale lives in `src/config/Pins.h`.

The 16 pads are wired as a diode-isolated 4×4 matrix (one 1N4148 per
switch — that's what the 16 diodes in the BOM are for), not direct GPIO.
The electrical row/col grid lines up exactly with the physical layout on
the board (confirmed against the pick-and-place coordinates), so
`pad index = row * 4 + col` reads left-to-right, top-to-bottom.

### Regenerating the pin map

If you respin the board, re-derive `Pins.h` rather than hand-editing pin
numbers from memory:

```sh
kicad-cli sch export netlist --format kicadxml -o netlist.xml hardware/ns-1.kicad_sch
```

(the KiCad project lives under `hardware/` at the repo root, not next to
this firmware directory)

Then look at which nets connect to `U1` (the Teensy41 symbol) — each
node's `pinfunction` attribute encodes the actual Teensy pin number
(e.g. `24_A10_TX6_SCL2_16` means physical Teensy pin 24).

## Signal path

```
8x AudioPlaySdWav (pad voices) --\
                                   > fold L+R to mono -> masterMix -> compressor -> bitcrusher (grit)
line-in (optional monitor)      -/                         ^              -> waveshaper (saturation)
usb-in  (optional monitor)     -/                          |              -> hardClip
                                                            |                    |
                                   recordSourceMix <--- line-in / usb-in / FX tail
                                          |                                      |
                                      recQueue                            lineOut + usbOut
                                          |
                                     WavRecorder -> SD (WAV)
```

**The engine sums everything to mono right after the voice mixer, on
purpose.** This isn't a shortcut — it's the classic SP-1200/MPC-60
"glue" character the brief asks for (tape music, soulhop, heavy
compression), and it means the whole FX chain (compressor → grit →
saturation → hard clip) only has to exist once instead of twice. The
mono signal is duplicated to both output channels for the codec and USB
audio. If you want true stereo later, `AudioEngine` is the only place
that needs to change — duplicate the FX chain and feed L/R separately
into it instead of folding to mono in `voiceFold[]`.

### The four FX pots

| Pot | Effect | What it actually does |
|---|---|---|
| 1 | Compression | Custom feedforward peak-envelope compressor (`AudioEffectCompressor`). One `amount()` macro walks threshold (0.9→0.06), ratio (2:1→20:1) and makeup gain together — transparent at 0, hard-limiting squash at 1. |
| 2 | Grit | `AudioEffectBitcrusher` (Teensy Audio Library) — bit depth 16→4 and sample rate 44.1kHz→4kHz, both driven by the same knob. |
| 3 | Saturation | `AudioEffectWaveshaper` (library) loaded with a `tanh(drive·x)` lookup table regenerated as the pot moves; drive goes 1x→15x. |
| 4 | Clip | Custom `AudioEffectHardClip` — pre-gain (1x→8x) into a hard ±ceiling clamp, ceiling tightens as the pot increases for a harder edge. |

The slider sets both the SGTL5000 headphone volume and line-out level
together, post-FX.

### Pitch: real resampling, not a header trick

An earlier version of this tried the classic "lie about the WAV header's
sample-rate field" trick (declare a different rate than the file actually
is, since `AudioPlaySdWav` always plays out at the system's fixed
44.1kHz). **That doesn't work on this Teensy Audio Library version** —
`AudioPlaySdWav::parse_format()` only accepts an *exact* 44100, 22050, or
11025 Hz in the header (see `play_sd_wav.cpp`) and silently refuses to
play anything else at all. `-1200` cents happens to compute to exactly
22050 (which is why that one value "worked"), but almost any other pitch
value made the pad go completely mute — not mistuned, silent.

The fix: pitch is real PCM resampling (linear interpolation,
`WavFile::resample()`) done once at commit time, always writing the pad's
file back declaring exactly `NS1_SAMPLE_RATE` (which `AudioPlaySdWav`
always accepts) with the actual waveform samples stretched or compressed
to realize the pitch/speed change — the audible effect (and the
duration-changes-with-pitch tape-style behavior) is the same as the
original trick was supposed to produce, it's just implemented for real
now. To avoid compounding interpolation loss if you nudge a pad's pitch
back and forth repeatedly, `BankManager::setPitchCents()` lazily snapshots
an untouched copy of the sample the first time a pad's pitch is ever
changed (`P<nn>ORIG.WAV`, 8.3-compliant name) and always resamples from
that pristine copy, never from an already-resampled file. Re-chopping a
pad slot (`SampleSlicer::commitToBank()`) invalidates any leftover
backup from whatever used to be on that slot.

The UI steps pitch in whole semitones (`+/-12`, one octave each way), not
raw cents — `PadConfig::pitchCents` still stores it as cents internally
(always a multiple of 100) so `BANK.CFG`'s format didn't need to change.

## Storage layout (microSD)

```
/BANKS/<name>/BANK.CFG       pad -> file / loop / choke / pitch / name, one line per pad
/BANKS/<name>/PAD00.WAV      mono 16-bit/44.1kHz, one file per pad slot (00-15) — the live, playable file
/BANKS/<name>/P00ORIG.WAV    lazily created on a pad's first pitch edit; pitch always resamples from here (see "Pitch" above)
...
/SAMPLES/*.WAV                drop files here from a computer to browse/import them on-device (PAD_EDIT -> SHIFT+ENC1 -> IMPORT)
/REC/TAKE0000.WAV            raw takes from sampling/resampling, before chopping
```

Each pad is its own standalone WAV file rather than one file with
offsets, because `AudioPlaySdWav` can't seek to an arbitrary start
point. Chopping a take physically copies each slice's PCM range into
its own pad file (folding stereo sources to mono on the way).

## Loading samples onto pads

Three ways to get audio onto a pad:

1. **Record + chop, on the device.** SAMPLE (line-in/USB-in/resample) ->
   stop recording -> CHOP -> ENC1 press commits every slice straight into
   the current bank's pad files. This is the normal workflow the UI is
   built around.
2. **Import an existing file, on the device.** Drop WAV files into
   `/SAMPLES` on the card from a computer, then from PLAY: SHIFT + pad ->
   PAD_EDIT -> SHIFT + ENC1 press -> IMPORT. ENC1 turn browses
   `/SAMPLES` (scrolls automatically past `Display::kMenuVisibleRows`
   entries), ENC1 press assigns the selected file to the pad you opened
   IMPORT from and returns to PAD_EDIT; ENC3 press cancels. This is the
   easiest option and the only one that doesn't require touching
   `BANK.CFG` by hand — see `BankManager::importSample()`. It folds
   stereo sources to mono and always resamples to exactly
   `NS1_SAMPLE_RATE`, so unlike option 3 below, the source file's own
   sample rate doesn't matter.
3. **Copy WAV files onto the SD card directly** into a bank folder,
   bypassing IMPORT entirely. A bank's pad->file mapping lives in
   `BANK.CFG`; `BankManager::loadBank()` auto-detects which pads are
   "assigned" by checking whether the file each line points to actually
   exists on disk. So to hand-load a bank:
   - Create the bank once on-device (FILE -> ENC2 press -> new `BANKnn`),
     which writes a `BANK.CFG` that already names `PAD00.WAV`..`PAD15.WAV`
     for every slot, or write that `BANK.CFG` yourself (see the format in
     `BankManager.cpp`'s `saveBank()` —
     `index,filename,loop,choke,pitch,name` per line; `name` is optional —
     older 5-field lines still parse — and is the (up to 12-char) label
     shown, truncated to ~4 characters, on the pad in the PLAY screen. Blank
     just shows no label.).
   - Drop **mono 16-bit PCM WAV files** at those exact filenames into
     `/BANKS/<name>/` on the card. Unlike IMPORT, this path does **not**
     resample anything — the sample rate declared in the header must be
     exactly 44100, 22050, or 11025 Hz or the pad will be completely silent
     (`AudioPlaySdWav::parse_format()` rejects anything else outright; see
     "Pitch" above for the full story). If you're not sure, use IMPORT
     instead — it handles arbitrary source rates for you.
   - (Re)load that bank on the device (FILE screen) or power-cycle; pad
     `assigned` state is recomputed from file existence at load time.

`tools/generate_demo_kit.py` in this directory uses exactly this second
path to install a test kit: it synthesizes a 16-pad lo-fi hip-hop/soulhop
drum kit (kick, sub kick, two snares, clap, rim, closed/open hats sharing a
choke group, two toms, shaker, woodblock, crash, ride, a vinyl-crackle
texture, cowbell) straight into `/BANKS/DEMO/` layout, stdlib-only (no
numpy):

```sh
cd firmware/tools
python3 generate_demo_kit.py ../demo_bank   # writes demo_bank/BANKS/DEMO/...
```

Copy the resulting `BANKS/` folder onto the SD card root (merging with any
existing `/BANKS` dir), then load bank `DEMO` from the FILE screen. A
pre-generated copy is checked in at `firmware/demo_bank/`.

## Controls

**Always live, any screen:** pots 1-4 (compression/grit/saturation/clip),
slider (master volume).

**PLAY** (default screen)
- Pad: trigger / retrigger a sample. A looping pad's second press stops it.
- SHIFT + pad: open PAD EDIT for that pad.
- ENC1 turn: switch bank. ENC1 press: open the bank browser (FILE).
- ENC2 press: open SAMPLE (arm sampling/resampling).

**PAD EDIT** (SHIFT + pad from PLAY)
- ENC1 turn: pitch, ±1 semitone/step, ±12 semitones range. ENC1 press:
  commit (resamples the pad's PCM data — see "Pitch" above).
- SHIFT + ENC1 press: open IMPORT to load a WAV file from `/SAMPLES` onto
  this pad.
- ENC2 press: toggle looping.
- ENC3 turn: choke group (none, 0-3). ENC3 press: commit.
- Press the same pad again to exit.

**IMPORT** (SHIFT + ENC1 press from PAD EDIT)
- ENC1 turn: select a file from `/SAMPLES` (scrolls automatically).
- ENC1 press: assign the selected file to the pad, return to PAD EDIT.
- ENC3 press: cancel, return to PAD EDIT without changes.

**SAMPLE** (ENC2 press from PLAY)
- ENC2 turn: source (line-in / USB-in / resample-the-output).
- ENC2 press: start recording; press again to stop, which jumps straight into CHOP with the take loaded.
- ENC3 press: toggle live monitoring of the selected source (line-in/USB-in
  only — resample is always audible already, since it's the live output).
  Useful for A/B'ing whether an input sounds bad live, at the source,
  versus only after it's been recorded and played back.
- ENC1 press: cancel / back.

**CHOP** (entered automatically after a recording stops)
- ENC1 turn: select a slice. ENC2 / ENC3 turn: nudge the selected slice's start / end.
- ENC1 press: commit all slices into the current bank's pads, return to PLAY.
- ENC2 press: cycle slice count (4/8/12/16). ENC3 press: cancel.

**FILE** (bank browser, ENC1 press from PLAY)
- ENC1 turn: select a bank. ENC1 press: load it, return to PLAY.
- ENC2 press: create a new bank (auto-named `BANKnn` — see limitations below).
- ENC3 press: back without changing banks.

## Display flicker / tearing

If the panel flickers with a visible diagonal seam, it was a full-panel
redraw problem, not a hardware fault, and should already be fixed in this
tree — worth knowing in case you extend `Display`/`UI` and reintroduce it.

**Root cause:** every `draw*Screen()` call used to open with
`tft.fillScreen()`, and `UI::render()` called that at 30fps unconditionally
— even when nothing on screen had changed. A full 170x320x16bpp clear is
108,800 bytes; at the Adafruit ST7789 library's default 16MHz SPI clock
that's ~55ms, longer than the entire 33ms frame budget. So each redraw was
still writing when the next one started, and the panel — which scans its
GRAM asynchronously, with no double buffer or vsync here — showed the
seam between old and new frame data as a diagonal tear, plus a flash/flicker
every tick as the whole screen strobed blank-then-redraw.

**Fix:** `Display`'s five `draw*Screen()` methods now take a leading
`fullRedraw` bool. `UI` sets it (via `needFullRedraw`) only when the visible
screen actually transitions (or, on PLAY, when the bank name changes via
ENC1 without a full screen change) — see `UI::render()` and the `enterX()`
methods in `UI.cpp`. `fillScreen()` and other fully-static chrome
(headers/hints) now only run on that flag; everything that's genuinely
live every frame (FX bars, pad states, the chop waveform box, dynamic text
lines) still redraws every tick, but through small bounded `fillRect()`
regions instead of the whole panel — see `Display::drawTextLine()` for the
pattern used to avoid ghosted/overlapping text on shrinking values (e.g.
pitch cents). `Display::begin()` also now sets the SPI clock explicitly to
40MHz (`tft.setSPISpeed()`) for extra headroom on whatever's still drawn
each frame; drop that if your specific panel/wiring shows noise or garbage
pixels — ST7789 modules vary in how much overclock past the 16MHz default
they tolerate.

## Known limitations / deliberate tradeoffs

These are documented choices made to keep the firmware honest about what
it actually does, not oversights:

- **Mono internal bus.** See "Signal path" above — authentic to the
  brief's lo-fi/tape aesthetic, and halves the FX chain's complexity.
- **Looping isn't sample-accurate/gapless.** A looping voice is
  retriggered from the top once `AudioPlaySdWav::isStopped()` reports
  true, which has a small gap (SD reopen + a few ms). True gapless
  looping needs a custom streaming WAV reader with seek, which is a
  reasonable follow-up but out of scope here.
- **8-voice polyphony shared across 16 pads**, voice-stealing
  oldest-triggered on overflow. This is an SD-bandwidth-driven limit —
  Teensy 4.1's built-in SDIO can sustain more than 8 streams on a fast
  card, so bump `NS1_NUM_VOICES` in `Config.h` and test if you want more,
  but recording (resampling) concurrently with heavy pad activity will
  contend for the same SD bus.
- **No on-screen keyboard.** New banks are auto-named (`BANK00`,
  `BANK01`, ...) rather than typed on the pads — renaming is a matter of
  editing the folder name on the SD card from a computer. A pad-based
  text entry screen would be a reasonable UI addition.
- **Pitch changes playback duration** (like real tape/sampler pitch,
  since it's realized as a genuine speed-change resample — see "Pitch"
  above) — this is intentional, not a bug.
- **The IMPORT file list doesn't recurse into subfolders** of
  `NS1_DIR_SAMPLES` — keep it a flat folder of WAV files. It's also
  capped at `UI::kMaxImportFiles` (32) entries scanned.

## Extending

- `AudioEngine` is the single place that owns the audio graph — add
  effects there, following the existing `AudioConnection` array pattern
  (declare new `AudioStream` objects before any connection that
  references them; C++ constructs class members in declaration order).
- `BankManager`'s `BANK.CFG` format is a plain CSV-ish text file — trivial
  to extend with more per-pad fields if you add per-pad gain/pan/etc.
- `UI` and `Display` are deliberately decoupled: `Display` only takes
  plain data, so new screens don't need to know about `BankManager` or
  `AudioEngine` types.
