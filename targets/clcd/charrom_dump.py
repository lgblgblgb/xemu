#!/usr/bin/env python3

import sys

if len(sys.argv) != 2:
    sys.stderr.write("Bad usage.\n")
    sys.exit(1)
with open(sys.argv[1], "rb") as rom:
    rom = rom.read()
print("ROM {} loaded, {} bytes (${:X} chars).".format(sys.argv[1], len(rom), len(rom) >> 3))
if len(rom) & 7:
    sys.stderr.write("Error, ROM length is not 8 byte aligned.\n")
    sys.exit(1)
line = 0
for b in rom:
    if line & 7 == 0:
        ch = line >> 3
        print("--- Character ${:02X} ({})".format(ch, ch))
        for a1 in range(0, len(rom), 8):
            if rom[line:line+8] == rom[a1:a1+8] and ch != a1:
                print("  same as character ${0:02X} ({0})".format(a1 >> 3))
    print("  {}{}{}{}{}{}{}{}".format(
        "*" if b & 0x80 else ".",
        "*" if b & 0x40 else ".",
        "*" if b & 0x20 else ".",
        "*" if b & 0x10 else ".",
        "*" if b & 0x08 else ".",
        "*" if b & 0x04 else ".",
        "*" if b & 0x02 else ".",
        "*" if b & 0x01 else ".",
    ))
    line += 1



