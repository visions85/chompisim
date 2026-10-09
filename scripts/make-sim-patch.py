#!/usr/bin/env python3
"""Generate the simulator patch for a CHOMPI firmware's chompi_main.cpp.

The patch wraps a handful of lines in #ifdef CHOMPI_SIM so the same file still
builds unchanged for the hardware:
  * includes chompi_sim_hooks.h
  * renames main() to chompi_firmware_main() and lets the main loop exit
  * makes ZeroSDRAM() a no-op (host globals are zero-initialised)
  * turns explicit SDRAM / DTCM section attributes into libDaisy's macros,
    which the simulator defines as empty

usage: make-sim-patch.py <path/to/chompi_main.cpp> <out.patch>
"""
import re, sys

src_path, out_path = sys.argv[1], sys.argv[2]
orig = open(src_path).read()
s = orig
log = []

def sub1(pattern, repl, what, flags=0, required=True):
    global s
    new, n = re.subn(pattern, repl, s, count=1, flags=flags)
    if n == 0 and required:
        sys.exit(f"pattern for '{what}' not found in {src_path}")
    if n:
        log.append(what)
    s = new

# 1. hooks header after the leading block of #include lines
m = None
for m in re.finditer(r'^#include "[^"]+"\n', s, flags=re.M):
    pass
if not m:
    sys.exit("no #include found")
s = s[:m.end()] + '#ifdef CHOMPI_SIM\n#include "chompi_sim_hooks.h"\n#endif\n' + s[m.end():]
log.append("hooks include")

# 2. the firmware's own DTCM macro
sub1(r'^#define DSY_DTCMRAM_BSS __attribute__\(\(section\("\.dtcmram_bss"\)\)\)\n',
     '#ifdef CHOMPI_SIM\n#define DSY_DTCMRAM_BSS /* host build: ordinary memory */\n#else\n'
     '#define DSY_DTCMRAM_BSS __attribute__((section(".dtcmram_bss")))\n#endif\n',
     "DSY_DTCMRAM_BSS define", flags=re.M, required=False)

# 3. explicit section attributes -> libDaisy macros (same attribute on hardware)
n_sdram = len(re.findall(r'__attribute__\(\(section\("\.sdram_bss"\)\)\)', s))
s = s.replace('__attribute__((section(".sdram_bss")))', 'DSY_SDRAM_BSS')
n_dtcm = len(re.findall(r'(?<!define DSY_DTCMRAM_BSS )__attribute__\(\(section\("\.dtcmram_bss"\)\)\)', s))
s = re.sub(r'(?<!define DSY_DTCMRAM_BSS )__attribute__\(\(section\("\.dtcmram_bss"\)\)\)', 'DTCM_MEM_SECTION', s)
if n_sdram or n_dtcm:
    log.append(f"section attributes ({n_sdram} sdram, {n_dtcm} dtcm)")

# 4. ZeroSDRAM body
sub1(r'(void ZeroSDRAM\(\)\n\{\n)(.*?)(\n\})',
     r'\1#ifndef CHOMPI_SIM /* host globals are zero-initialised; there is no SDRAM bank */\n\2\n#endif\3',
     "ZeroSDRAM", flags=re.S, required=False)

# 5. main()
sub1(r'^int main\(void\)\n\{', '#ifdef CHOMPI_SIM\nint chompi_firmware_main(void) /* run on the simulator\'s firmware thread */\n#else\nint main(void)\n#endif\n{',
     "main rename", flags=re.M)

# 6. variables initialised with the SDRAM base address (TEMPO's sample_ram_start):
#    on the host they point at the simulator's SDRAM stand-in instead
lines = s.split("\n")
out = []
redirected = 0
in_zero = False
for ln in lines:
    if ln.startswith("void ZeroSDRAM()"):
        in_zero = True
    elif in_zero and ln.startswith("}"):
        in_zero = False
    if (not in_zero) and re.search(r'\(\s*void\s*\*\s*\)\s*0x[cC]0000000\b', ln) and "=" in ln:
        host = re.sub(r'\(\s*void\s*\*\s*\)\s*0x[cC]0000000\b', 'chompi_sim_sdram() /* host stand-in for the SDRAM bank */', ln)
        out += ["#ifdef CHOMPI_SIM", host, "#else", ln, "#endif"]
        redirected += 1
    else:
        out.append(ln)
s = "\n".join(out)
if redirected:
    log.append(f"SDRAM base redirected ({redirected})")

# 7. the main loop
sub1(r'^(\s*)while ?\((1|true)\)\n(\s*)\{\n(\s*)MainLoop\(nullptr\);\n(\s*)\}\n\}',
     r'#ifdef CHOMPI_SIM\n\1while (chompi_sim_running())\n#else\n\1while (1)\n#endif\n\3{\n\4MainLoop(nullptr);\n\5}\n#ifdef CHOMPI_SIM\n\1return 0;\n#endif\n}',
     "main loop", flags=re.M)

# use the system diff so a missing newline at end of file is encoded correctly
import os, subprocess, tempfile
with tempfile.TemporaryDirectory() as td:
    os.makedirs(f"{td}/a"); os.makedirs(f"{td}/b")
    open(f"{td}/a/chompi_main.cpp", "w").write(orig)
    open(f"{td}/b/chompi_main.cpp", "w").write(s)
    r = subprocess.run(["diff", "-u", "--label", "a/chompi_main.cpp", "--label", "b/chompi_main.cpp",
                        "a/chompi_main.cpp", "b/chompi_main.cpp"], cwd=td, capture_output=True, text=True)
    if r.returncode not in (0, 1):
        sys.exit(r.stderr)
    open(out_path, "w").write(r.stdout)
print(f"{out_path}: " + ", ".join(log))
