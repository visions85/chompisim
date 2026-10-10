#!/usr/bin/env python3
"""Makes a card folder for the GRAIN firmware.

By default it fills the fourteen sounds with the factory TAPE samples (bank A
of the cubbi instrument, 48 kHz 16-bit stereo) from the CHOMPI checkout, in
order, and writes the marker file the simulator's launcher reads the firmware
name from. Any 48 kHz 16-bit mono or stereo WAV works; drop your own in.

    scripts/make-grain-card.py DIR [--tape CARD] [--instrument cubbi|jammi] [--bank a|b|c] [--synthetic]

--tape       the TAPE card folder (default: third_party/CHOMPI/firmware/card-profiles/tape-2.0,
             fetched by scripts/fetch-firmware.sh)
--synthetic  four synthetic sounds (a pluck, a voice, rain, a bell) instead of the samples;
             also what you get when no TAPE card is found"""
import math, os, random, struct, sys, wave, shutil, glob, re
RATE = 48000

def write_wav(path, frames, channels=1):
    w = wave.open(path, 'wb')
    w.setnchannels(channels); w.setsampwidth(2); w.setframerate(RATE)
    data = bytearray()
    for f in frames:
        if channels == 1:
            data += struct.pack('<h', int(max(-1.0, min(1.0, f)) * 32767))
        else:
            data += struct.pack('<hh', int(max(-1.0, min(1.0, f[0])) * 32767), int(max(-1.0, min(1.0, f[1])) * 32767))
    w.writeframes(bytes(data)); w.close()

def pluck(seconds=2.0, hz=130.8128):
    """A plucked string: harmonics that decay faster the higher they are."""
    out = []
    for i in range(int(seconds * RATE)):
        t = i / RATE
        s = 0.0
        for h in range(1, 9):
            s += math.sin(2 * math.pi * hz * h * t + 0.3 * h) / h * math.exp(-t * (1.2 + 0.9 * h))
        out.append(0.5 * s * min(1.0, t / 0.004))
    return out

def voice(seconds=2.5, hz=110.0):
    """A pulse train through two moving formants: vowel-like."""
    out = []
    y1 = y2 = 0.0
    for i in range(int(seconds * RATE)):
        t = i / RATE
        f1 = 400 + 350 * math.sin(2 * math.pi * 0.4 * t)
        f2 = 1200 + 800 * math.sin(2 * math.pi * 0.27 * t + 1.0)
        pulse = 1.0 if (t * hz) % 1.0 < 0.08 else -0.08
        a1 = 2 * math.pi * f1 / RATE; a2 = 2 * math.pi * f2 / RATE
        y1 += a1 * (pulse - y1); y1 += a1 * (pulse - y1)
        y2 += a2 * (pulse - y2)
        env = min(1.0, t / 0.05) * min(1.0, (seconds - t) / 0.3)
        out.append(0.6 * (y1 + 0.5 * y2) * env)
    return out

def texture(seconds=3.0):
    """Rain-like stereo noise bursts, filtered differently left and right."""
    random.seed(7)
    out = []
    l = r = 0.0
    burst = 0.0
    for i in range(int(seconds * RATE)):
        if random.random() < 0.0008:
            burst = 1.0
        burst *= 0.9995
        n = (random.random() * 2 - 1) * (0.15 + burst)
        l += 0.12 * (n - l)
        r += 0.05 * (n - r)
        out.append((0.8 * l, 0.8 * r))
    return out

def bell(seconds=3.0, hz=261.6256):
    """Inharmonic partials with long decays."""
    ratios = [1.0, 2.76, 5.4, 8.93, 13.34]
    out = []
    for i in range(int(seconds * RATE)):
        t = i / RATE
        s = 0.0
        for k, rt in enumerate(ratios):
            s += math.sin(2 * math.pi * hz * rt * t) * math.exp(-t * (0.8 + 1.1 * k)) / (1 + k)
        out.append(0.45 * s * min(1.0, t / 0.002))
    return out

def arg(name, default=None):
    if name in sys.argv:
        i = sys.argv.index(name)
        if i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    return default

def main():
    if len(sys.argv) < 2 or sys.argv[1].startswith("--"):
        print(__doc__); sys.exit(2)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    tape = arg("--tape", os.path.join(root, "third_party", "CHOMPI", "firmware", "card-profiles", "tape-2.0"))
    instrument = arg("--instrument", "cubbi")
    bank = arg("--bank", "a")
    synthetic = "--synthetic" in sys.argv
    n = 0
    if not synthetic:
        files = glob.glob(os.path.join(tape, "%s_%s*.wav" % (instrument, bank)))
        files = [f for f in files if "_double" not in f]
        files.sort(key=lambda f: int(re.search(r"(\d+)\.wav$", f).group(1)))
        if not files:
            print("no TAPE samples at %s (run scripts/fetch-firmware.sh tape, or pass --tape CARD); "
                  "making the synthetic sounds instead" % tape)
            synthetic = True
        for f in files[:14]:
            n += 1
            shutil.copy(f, os.path.join(out, "%02d_%s" % (n, os.path.basename(f))))
    if synthetic:
        write_wav(os.path.join(out, "01_pluck_c3.wav"), pluck())
        write_wav(os.path.join(out, "02_voice.wav"), voice())
        write_wav(os.path.join(out, "03_rain.wav"), texture(), channels=2)
        write_wav(os.path.join(out, "04_bell_c4.wav"), bell())
        n = 4
    with open(os.path.join(out, "CHOMPI_GRAIN.txt"), "w") as f:
        f.write("A card for the GRAIN firmware: the simulator's launcher reads the firmware name off this file.\n")
    print("wrote %d sounds to %s%s" % (n, out, "" if synthetic else " (TAPE %s bank %s)" % (instrument, bank)))

if __name__ == "__main__":
    main()
