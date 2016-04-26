# X-VIC20

Just a quick experiment to write a _VERY_ inaccurate and ugly, unfinished
Commodore VIC-20 emulator using SDL2 library. Main purpose: allow to test
some theories of mine on a more common machine first about writing emulator,
than about the Commodore LCD ... This emulator can be also treated as test
bed for Xep128 (also on github, under my name) which is a more serious try
to emulate a Z80 based computer (Enterprise-128).

Written by (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

It should run at least on Linux (including hopefully Raspberry PI too,
since it's also Linux based by default, but you need to install SDL2.0.4
at least to allow to run without X11 actually! You may need to compile
and install SDL2.0.4 first! See the Rapsberry Pi note later in this
README).

The code itself should work on Windows too, compiled with Mingw suite,
but I haven't tested that (I have no Windows), however it should be
able to be done, just you need the mentioned Mingw, and also SDL2
headers, dll, etc. Then it can be compiled for Win32 on a UNIX-like
OS with cross-compilation.

## Usage:

Currently only two hot-keys are implemented:

* F11 toggles fullscreen / window mode
* F9 quits

You really want to redirect stdout and stderr to /dev/null, otherwise
you will be flooded with messages on the terminal, which also slows
the emulator down a lot!

## Problems with the emulator

Keyboard is not even emulated yet, just random keys are assigned for
testing, keys 0 - 9, but it won't produce the right result :) THis
is by intent, and for testing purpose.

Also, border is not emulated, neither of any VIC-I registers ... Only
the hard-wired config for the VIC-20 defaults by the kernal ...

Sound is not emulated.

Any storage device (IEC bus, floppy drive, tape) is not implemented.

CPU is basically a 65C02 now (from my Commodore LCD emulator project)
which is incorrect, since VIC-20 is 6502 based. Thus, illegal opcodes
won't work!

Also VIA emulation is unfinished and inaccurate, also used from
my LCD project, where I need VIAs only for purposes what the LCD
computer use them for ...

It's just a quick try to write a VIC-20 emulator without too much
work. Honestly, I even don't know VIC-20 too much and I am not sure
about many things I should know for a decent emulator.

## ROM images

ROM images should be get from somewhere, and must be placed
into directory "rom". Note, that if it's OK for you, Makefile can do
it (if wget is installed on your computer) with simply issuing the
following command:

make roms

## Raspberry PI notes

according to my experiments -falign-functions=16 -falign-loops=16 really
helps on Rapsberry-Pi at least.

For more information from me (it also contains tips for SDL compilation
on the Raspberry PI itself):

https://github.com/lgblgblgb/xep128/wiki/Raspberry-Pi

Then you need to run xvic20 directly from a text console, not in X11!

