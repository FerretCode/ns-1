#!/usr/bin/env python3
"""Generates a 16-pad lo-fi hip-hop / soulhop drum kit for the NS-1.

Pure stdlib (wave + math + random), no numpy — synthesizes each hit
procedurally (sine sweeps, filtered noise, soft saturation) rather than
shipping sample audio. Output mirrors the SD card layout exactly:

    <outdir>/BANKS/DEMO/BANK.CFG
    <outdir>/BANKS/DEMO/PAD00.WAV .. PAD15.WAV

Copy the BANKS/ folder onto the SD card's root (merge with any existing
/BANKS directory) and pick DEMO from the FILE screen (ENC1 press from
PLAY) to load it. See firmware/README.md, "Loading samples onto pads".

Usage:
    python3 generate_demo_kit.py [outdir]   # defaults to ./demo_bank_output
"""
import math
import random
import struct
import sys
import wave
from pathlib import Path

SR = 44100


# --- small DSP toolkit -------------------------------------------------

def noise(n, seed=None):
    rng = random.Random(seed)
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def one_pole_lowpass(sig, cutoff_hz):
    a = math.exp(-2.0 * math.pi * cutoff_hz / SR)
    y = 0.0
    out = [0.0] * len(sig)
    for i, x in enumerate(sig):
        y = (1 - a) * x + a * y
        out[i] = y
    return out


def one_pole_highpass(sig, cutoff_hz):
    lp = one_pole_lowpass(sig, cutoff_hz)
    return [s - l for s, l in zip(sig, lp)]


def exp_decay(n, tau_seconds, start=1.0):
    return [start * math.exp(-i / (tau_seconds * SR)) for i in range(n)]


def sine_sweep(n, f_start, f_end, curve=3.0):
    out = [0.0] * n
    phase = 0.0
    for i in range(n):
        t = i / max(1, n - 1)
        f = f_end + (f_start - f_end) * ((1 - t) ** curve)
        phase += 2.0 * math.pi * f / SR
        out[i] = math.sin(phase)
    return out


def soft_clip(sig, drive=1.0):
    return [math.tanh(x * drive) for x in sig]


def mix(*sigs):
    n = max(len(s) for s in sigs)
    out = [0.0] * n
    for s in sigs:
        for i, x in enumerate(s):
            out[i] += x
    return out


def gain(sig, g):
    return [x * g for x in sig]


def pad_to(sig, n):
    if len(sig) >= n:
        return sig[:n]
    return sig + [0.0] * (n - len(sig))


def normalize(sig, peak=0.92):
    m = max((abs(x) for x in sig), default=0.0)
    if m < 1e-9:
        return sig
    k = peak / m
    return [x * k for x in sig]


def seconds(n):
    return int(n * SR)


# --- drum voices ---------------------------------------------------------
# Deliberately dirty/lo-fi: soft-clip saturation on nearly everything, dull
# low-passed transients rather than crisp modern ones — SP-1200/tape aesthetic
# to match the rest of the engine's FX chain (see AudioEngine's compressor ->
# bitcrusher -> waveshaper -> hard-clip chain in the firmware itself).

def kick(seed=1):
    n = seconds(0.45)
    body = sine_sweep(n, 150, 45, curve=4.0)
    body = [b * e for b, e in zip(body, exp_decay(n, 0.09))]
    click = gain(noise(seconds(0.006), seed), 0.6)
    click = pad_to(click, n)
    sig = mix(gain(body, 1.0), click)
    sig = soft_clip(sig, drive=2.2)
    sig = one_pole_lowpass(sig, 4500)
    return normalize(sig)


def kick_sub(seed=2):
    n = seconds(0.6)
    body = sine_sweep(n, 110, 38, curve=5.0)
    body = [b * e for b, e in zip(body, exp_decay(n, 0.14))]
    sig = soft_clip(body, drive=2.8)
    sig = one_pole_lowpass(sig, 2500)
    return normalize(sig)


def snare(seed=3):
    n = seconds(0.28)
    tone = sine_sweep(n, 200, 170, curve=2.0)
    tone = [t * e for t, e in zip(tone, exp_decay(n, 0.05))]
    body_noise = noise(n, seed)
    body_noise = one_pole_highpass(body_noise, 900)
    body_noise = one_pole_lowpass(body_noise, 5500)
    body_noise = [x * e for x, e in zip(body_noise, exp_decay(n, 0.07))]
    sig = mix(gain(tone, 0.5), gain(body_noise, 1.0))
    sig = soft_clip(sig, drive=1.8)
    return normalize(sig)


def snare_tight(seed=4):
    n = seconds(0.16)
    tone = sine_sweep(n, 240, 190, curve=2.0)
    tone = [t * e for t, e in zip(tone, exp_decay(n, 0.03))]
    body_noise = noise(n, seed)
    body_noise = one_pole_highpass(body_noise, 1400)
    body_noise = [x * e for x, e in zip(body_noise, exp_decay(n, 0.035))]
    sig = mix(gain(tone, 0.4), gain(body_noise, 1.0))
    sig = soft_clip(sig, drive=2.0)
    return normalize(sig)


def clap(seed=5):
    n = seconds(0.35)
    out = [0.0] * n
    burst_starts = [0.0, 0.011, 0.022, 0.035]
    for k, start in enumerate(burst_starts):
        off = seconds(start)
        blen = seconds(0.05)
        b = noise(blen, seed + k)
        b = one_pole_highpass(b, 1000)
        b = one_pole_lowpass(b, 6000)
        tail = 0.15 if k == len(burst_starts) - 1 else 0.02
        b = [x * e for x, e in zip(b, exp_decay(blen, tail))]
        for i, x in enumerate(b):
            if off + i < n:
                out[off + i] += x
    sig = soft_clip(out, drive=1.6)
    return normalize(sig)


def rim(seed=6):
    n = seconds(0.09)
    ping = [math.sin(2 * math.pi * 850 * i / SR) for i in range(n)]
    ping = [p * e for p, e in zip(ping, exp_decay(n, 0.012))]
    click = noise(seconds(0.004), seed)
    click = pad_to(click, n)
    sig = mix(gain(ping, 0.7), gain(click, 0.8))
    sig = soft_clip(sig, drive=2.0)
    return normalize(sig)


def hat_closed(seed=7):
    n = seconds(0.06)
    sig = noise(n, seed)
    sig = one_pole_highpass(sig, 6500)
    sig = [x * e for x, e in zip(sig, exp_decay(n, 0.012))]
    return normalize(sig, peak=0.75)


def hat_open(seed=8):
    n = seconds(0.4)
    sig = noise(n, seed)
    sig = one_pole_highpass(sig, 6000)
    ring = mix(*[
        gain([math.sin(2 * math.pi * f * i / SR) for i in range(n)], 0.06)
        for f in (6300, 8100, 9700)
    ])
    sig = mix(sig, ring)
    sig = [x * e for x, e in zip(sig, exp_decay(n, 0.11))]
    return normalize(sig, peak=0.75)


def tom(f_start, f_end, seed):
    n = seconds(0.35)
    body = sine_sweep(n, f_start, f_end, curve=3.0)
    body = [b * e for b, e in zip(body, exp_decay(n, 0.11))]
    sig = soft_clip(body, drive=1.6)
    sig = one_pole_lowpass(sig, 5000)
    return normalize(sig)


def shaker(seed=9):
    n = seconds(0.22)
    sig = noise(n, seed)
    sig = one_pole_highpass(sig, 4000)
    lfo = [0.5 + 0.5 * math.sin(2 * math.pi * 9 * i / SR) for i in range(n)]
    sig = [x * l for x, l in zip(sig, lfo)]
    sig = [x * e for x, e in zip(sig, exp_decay(n, 0.06))]
    return normalize(sig, peak=0.7)


def perc_block(seed=10, freq=1400.0, decay=0.015):
    n = seconds(0.12)
    blip = [math.sin(2 * math.pi * freq * i / SR) for i in range(n)]
    blip = [b * e for b, e in zip(blip, exp_decay(n, decay))]
    click = noise(seconds(0.005), seed)
    click = pad_to(click, n)
    sig = mix(gain(blip, 0.6), gain(click, 0.5))
    sig = one_pole_bandish(sig)
    return normalize(sig)


def one_pole_bandish(sig):
    sig = one_pole_highpass(sig, 500)
    sig = one_pole_lowpass(sig, 4000)
    return sig


def cymbal(n_seconds, seed, decay_tau):
    n = seconds(n_seconds)
    sig = noise(n, seed)
    sig = one_pole_highpass(sig, 5000)
    ring = mix(*[
        gain([math.sin(2 * math.pi * f * i / SR) for i in range(n)], 0.05)
        for f in (4200, 5600, 7300, 8900, 11200)
    ])
    sig = mix(sig, ring)
    sig = [x * e for x, e in zip(sig, exp_decay(n, decay_tau))]
    return normalize(sig, peak=0.7)


def vinyl_crackle(seed=13):
    n = seconds(2.0)
    base = noise(n, seed)
    base = one_pole_lowpass(base, 3500)
    base = one_pole_highpass(base, 40)
    base = gain(base, 0.05)
    rng = random.Random(seed + 100)
    pops = [0.0] * n
    i = 0
    while i < n:
        i += rng.randint(80, 900)
        if i >= n:
            break
        amp = rng.uniform(0.15, 0.5)
        pops[i] += amp * (1 if rng.random() > 0.5 else -1)
        if i + 1 < n:
            pops[i + 1] += amp * 0.4
    sig = mix(base, pops)
    return normalize(sig, peak=0.5)


# --- pad layout ------------------------------------------------------------
# index = row*4 + col, matching Pins.h's PadMatrix (row-major, top-left = 0).
PADS = [
    ("KICK",        kick),
    ("KICK SUB",    kick_sub),
    ("SNARE",       snare),
    ("SNARE TIGHT", snare_tight),
    ("CLAP",        clap),
    ("RIM",         rim),
    ("HAT CLOSED",  hat_closed),
    ("HAT OPEN",    hat_open),
    ("TOM LO",      lambda: tom(120, 70, 14)),
    ("TOM HI",      lambda: tom(220, 140, 15)),
    ("SHAKER",      shaker),
    ("WOODBLOCK",  perc_block),
    ("CRASH",       lambda: cymbal(1.6, 16, 0.55)),
    ("RIDE",        lambda: cymbal(0.8, 17, 0.28)),
    ("VINYL CRKL", vinyl_crackle),
    ("COWBELL", lambda: perc_block(seed=18, freq=800.0, decay=0.09)),
]

# Hi-hat choke group: opening the open-hat pad should cut a ringing closed-hat,
# same as a real drum machine/hardware hi-hat — demonstrates BankManager's
# chokeGroup feature. Indices are into PADS above.
HAT_CLOSED_IDX = 6
HAT_OPEN_IDX = 7
HAT_CHOKE_GROUP = 0


def write_wav_mono16(path, samples_float):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        ints = [max(-32768, min(32767, int(round(x * 32767)))) for x in samples_float]
        w.writeframes(struct.pack("<%dh" % len(ints), *ints))


def write_bank_cfg(path, names):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="\n") as f:
        for i in range(16):
            choke = HAT_CHOKE_GROUP if i in (HAT_CLOSED_IDX, HAT_OPEN_IDX) else -1
            # 6th field is the display name shown (truncated) on the pad in
            # the UI — PadConfig.filename is always PADnn.WAV, so it alone
            # wouldn't tell you which drum is on which pad.
            f.write(f"{i},PAD{i:02d}.WAV,0,{choke},0,{names[i]}\n")


def main():
    outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("demo_bank_output")
    bank_dir = outdir / "BANKS" / "DEMO"

    for label, _ in PADS:
        # PadConfig::name is char[13] (12 chars + null) on the firmware side.
        assert len(label) <= 12, f"pad name {label!r} too long for PadConfig::name[13]"

    names = []
    for i, (label, gen) in enumerate(PADS):
        sig = gen()
        write_wav_mono16(bank_dir / f"PAD{i:02d}.WAV", sig)
        names.append(label)
        print(f"  PAD{i:02d}.WAV  {label}")

    write_bank_cfg(bank_dir / "BANK.CFG", names)
    print(f"\nWrote demo kit to: {bank_dir}")
    print("Copy the BANKS/ folder onto the SD card root, then load bank "
          "\"DEMO\" from the FILE screen (ENC1 press from PLAY).")


if __name__ == "__main__":
    main()
