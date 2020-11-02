# Xep128 - An Enterprise-128 emulator

This is an on-going effort to try to integrate my (separeted) Xep128 project
into Xemu. Note: this emulation target in Xemu is highly different from others,
ie doing many things on its own. This will change slowly however.

Original (not so much maintained in the future) repository:

https://github.com/lgblgblgb/xep128

## Part of the original README:

Xep128 is an Enterprise-128 (a Z80 based, 8 bit computer) emulator (uses SDL2
and modified z80ex for Z80 emulation) with the main focus on emulating somewhat
"exotic" hardware additions. Currently it runs on Linux/UNIX (including the
Raspberry-Pi), Windows, OSX and also inside your web browser (with the help
of Emscripten compiler).

Written by (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

Xep128 main site: http://xep128.lgb.hu/
Source repository: http://github.com/lgblgblgb/xep128

Xep128 uses (modified, by me) Z80 emulator "Z80ex": https://sourceforge.net/projects/z80ex/
and lodePNG to write screenshots: http://lodev.org/lodepng/

Xep128 is licensed under the terms of GNU/GPL v2, for more information please
read file LICENSE. You can find the source on github, see above.

WARNING! Xep128 is in early alpha stage currently! It lacks many important
features (no/ugly sound, not so precise nick emulation, etc etc), and it's not
comfortable to use, there is only CLI/config file based configuration, etc.

Currently it's mainly for Linux and/or UNIX-like systems and Windows, however
since I don't use Windows, I can't test if it really works (cross compiled on
Linux).

Please note, that it's not the "best" Enterprise-128 emulator on the planet,
for that, you should use ep128emu project instead. Also, my emulator is not so
cycle exact for now, it does not emulate sound quite well (currently), it also lacks
debugger what ep128emu has. However it emulates some "more exotic" (not so much
traditional) hardware additions becomes (or becoming) popular among EP users
recently: mouse support, APU ("FPU"), SD card reader and soon limited wiznet
w5300 emulation (Ethernet connection with built-in TCP/IP support).
