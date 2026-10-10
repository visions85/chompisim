#!/usr/bin/env python3
"""Makes one card folder that holds several firmwares: the layout the
simulator's boot window prototypes (hold a white key while the window
boots to pick the firmware; see "One card, several firmwares" in the README).

    scripts/make-multi-card.py DIR [--profiles DIR] [--no-grain]

Layout of DIR, from the factory card profiles:
    CHOMPI_TAPEv2_0.bin, CHOMPI_WAVE_V1_0.bin, CHOMPI_TEMPOv1_0.bin   the firmware files, as on a real card
    CHOMPI_GRAIN.txt           marker for the simulator's own firmware (it has no binary)
    jammi_*.wav cubbi_*.wav options.json presets.json   TAPE, in the root: it is left unpatched
    WAVE/                      the wavetables and WAVE's settings
    TEMPO/Chromatic Slice Buffer, options.json, presets.json
    GRAIN/                     the GRAIN sounds (scripts/make-grain-card.py)

--profiles   the card-profiles folder of the CHOMPI checkout
             (default: third_party/CHOMPI/firmware/card-profiles, fetched by scripts/fetch-firmware.sh)
--no-grain   leave GRAIN out"""
import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))

def copy_into(src, dst, skip=()):
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        if name.startswith('.') or name in skip:
            continue
        s = os.path.join(src, name)
        if os.path.isdir(s):
            shutil.copytree(s, os.path.join(dst, name), dirs_exist_ok=True)
        else:
            shutil.copy2(s, os.path.join(dst, name))

def binaries(src):
    return [n for n in os.listdir(src) if n.lower().startswith('chompi') and n.lower().endswith(('.bin', '.txt'))]

def main():
    args = sys.argv[1:]
    if not args or args[0].startswith('-'):
        print(__doc__); sys.exit(2)
    out = args[0]
    profiles = os.path.join(HERE, '..', 'third_party', 'CHOMPI', 'firmware', 'card-profiles')
    grain = True
    i = 1
    while i < len(args):
        if args[i] == '--profiles' and i + 1 < len(args):
            profiles = args[i + 1]; i += 2
        elif args[i] == '--no-grain':
            grain = False; i += 1
        else:
            print(__doc__); sys.exit(2)
    cards = {}
    for fw in ('tape', 'wave', 'tempo'):
        hits = sorted(n for n in os.listdir(profiles) if n.lower().startswith(fw)) if os.path.isdir(profiles) else []
        if not hits:
            print(f'no {fw} card profile under {profiles}; run scripts/fetch-firmware.sh (or pass --profiles)')
            sys.exit(1)
        cards[fw] = os.path.join(profiles, hits[-1])
    os.makedirs(out, exist_ok=True)

    # TAPE reads the root of the card and has no room left for a folder patch: it goes in as is
    copy_into(cards['tape'], out)
    print(f"TAPE: {cards['tape']} -> {out}/ (root)")

    # WAVE and TEMPO read WAVE/ and TEMPO/ (firmware/patches/*/card_folder.patch); their binaries stay in the root
    for fw, folder in (('wave', 'WAVE'), ('tempo', 'TEMPO')):
        src = cards[fw]
        bins = binaries(src)
        copy_into(src, os.path.join(out, folder), skip=set(bins))
        for b in bins:
            shutil.copy2(os.path.join(src, b), os.path.join(out, b))
        print(f"{fw.upper()}: {src} -> {out}/{folder}/ and {', '.join(bins)}")

    if grain:
        subprocess.check_call([sys.executable, os.path.join(HERE, 'make-grain-card.py'), os.path.join(out, 'GRAIN'),
                               '--tape', cards['tape']])
        marker = os.path.join(out, 'GRAIN', 'CHOMPI_GRAIN.txt')
        if os.path.exists(marker):
            shutil.move(marker, os.path.join(out, 'CHOMPI_GRAIN.txt'))
        print(f"GRAIN: {out}/GRAIN/ and CHOMPI_GRAIN.txt")

    with open(os.path.join(out, 'README.txt'), 'w') as f:
        f.write("One card, several firmwares (chompi-sim prototype).\n"
                "TAPE's files are in the root; WAVE/, TEMPO/ and GRAIN/ hold the others' files;\n"
                "the firmware binaries and markers sit in the root. Hold a white key while the\n"
                "simulator boots to choose: 1 TAPE  2 WAVE  3 TEMPO  4 GRAIN; no key boots the last choice.\n")
    print(f"card ready: {out}  firmwares: {', '.join(sorted(n for n in os.listdir(out) if n.lower().startswith('chompi')))}")

if __name__ == '__main__':
    main()
