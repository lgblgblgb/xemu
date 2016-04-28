# X-cLCD

Commodore LCD (and Commodore VIC-20) emulator

(Note: about the Commodore VIC-20, see below)

Commodore LCD is a highly unknown portable LCD based device with battery power and
ability to "sleep" with SRAMs still powered. Unfortunately the project was stopped
before the production phase, only about 2-5 units known to exist in different state
of development. Compared to the LCD, Commodore 65 is a highly known, well documented,
easy to get and wide-spread machine :)

I spent lot of work to get know the basic (then detailed) information on this nice
machine, enough, to be able to write an emulator. Intially not even the basic facts
were available (what kind of CPU, clock frequency, etc). I've managed to get enough
information (from disassembly of the available ROM images, debugging my emulator,
and some cases by asking ex-Commodore ex-engineers). The result was a specification
and a JavaScript based emulator.

http://commodore-lcd.lgb.hu/

This piece of code is about writing a native emulator version which can be run
at least on Windows and Linux (or Unix-like machines, Raspberry PI is included)
with the help of the SDL2 library. Note, that in theory, it would be easy to
port to other targets because of using SDL.

## Basic usage of the emulator

Currently no data storage is implemented ... You basically have only two host-keys
defined, press F9 to exit, or F11 to toggle between windowed and full screen mode.
That's all.

Otherwise, you should be able to use your keyboard to "type" as it would be the
keyboard of the real machine. However, please note: keyboard layout is SDL scancode
based! That is, maybe it's nothing about the letters on your PC keyboard :)
especially if you use some national (and not US) keyboard layout. As there are tons
of layouts out there, it would be hard to do otherwise ... Though symblic mapping
can be a solution, but shift difference (ie, a key is shifted on PC, but not on the
emulated machine or vice versa) would cause problems. The only exception here is
Commodore VIC-20 shifted cursors. It should work without shift too (with the four
cursor keys), however exactly because the emulated shift, it's possible that shift
is not detected, as it's only "pressed" virtually at the same time as the key itself.

On Windows, you need SDL2.dll (included in the distributed package). You also need
the ROM images.

## Why the Commodore VIC-20 emulation is included?

Foreword: this VIC-20 emulator is incomplete, unfinished, and very incorrect,
even the CPU type is not what a real VIC-20 used! You really don't want to use
this emulator as your main VIC-20 experience :)

The reason of this emulator: it's a great test bed for Commodore LCD emulation.
Commodore VIC-20 is a much more known machine, and basically also uses the
VIAs as the Commodore LCD. Commodore VIC-20 is proven to be able to use serial
IEC bus, while I have/had (?) problems with that with Commodore LCD emulation.
Thus, if I can do it well on a Commodore VIC-20 emulation, I can say, I do it
well, so I can try to hook the IEC part to the Commodore LCD emulation. And so
on. Also, VIC-20 is a "simple" machine enough to emulate, compared eg to a
Commodore 64 (which would also need CIAs and not VIAs to be emulated).

About the CPU: since Commodore LCD uses software compatible CPU (exact type is
not very known) of 65C02, my experimental VIC-20 emulator also uses that!
That's a direct attack against poor VIC-20 :) as it used 6502! So, of course
things like illegal opcodes won't work using my emulator!

Full (?) list of stupidities of my VIC-20 emulator:

* 65C02 as the CPU, not 6502 (no illegal opcodes)
* VIC-I is not emulated at register level, only the default mode for KERNAL/BASIC
* No 16 pixel height character mode, no multicolor, no screen width setting
* Screen is rendered in one step, which is incorrect, of course
* Border is not emulated
* Keyboard currently does not work
* Only the stock machine is emulated without any RAM expansion
* No sound is emulated

## Compilation on Linux / UNIX-like machine

Note: I tested this on Linux only. You should have sdl2 _development_ libraries
installed (at least version 2.0.2 is needed, at least 2.0.4 is recommended!),
along with gcc, GNU make (probably BSD make won't work). Then the only thing you
need is saying: `make`

You also need the ROM images to be able to use the emulator. For that purpose,
download those yourself, or if it's OK to allow make to do the work, you can say:
`make roms`. This will download ROM images from the Net.

Finally, you can install emulators (by default in /usr/local/bin for binaries,
and /usr/local/share/xclcd/ for ROM files) with this command: `make install`.
You probably need root privileges to do so. Warning: it will also try to download
ROM images first! If you don't want this, place the proper ROM images into
directory rom/ before `make install`.

Note, if you switched to another architecture (eg see the Windows chapter) you
can switch back to native compilation with command:

`make set-arch TO=native`

## Compilation for Windows

To be able to compile emulators for Windows, you must have a Linux (or probably
other UNIX-like system) with suitable cross-compiler able to generate Windows
binaries. You also need the S

First, you need to select "architecture" to win32 with this command:

`make set-arch TO=win32`

Then you can say: `make`

The result will have the extension .win32, but actually it's an .exe :)

You can switch back to native architecture with this command:

`make set-arch TO=native`

## Compilation for Raspberry Pi

You have two choices. You can compile it on the Raspberry Pi itself, then you need
to follow instructions given in the "Compilation on Linux / UNIX-like machine"
section.

You'll need SDL version 2.0.4 (or newer, if any ...), then you will be able to
run the emulators directly from text console, no X11 GUI is required at all :) Which
means lower resource usage, of course. I've not tested the stuff on Raspb.PI on
X11!

Also, you can use a cross-compiler on your desktop machine, if it's a Linux/UNIX
kind of machine.

Some notes:

Aaccording to my experiments -falign-functions=16 -falign-loops=16 really
helps on Rapsberry-Pi at least.

For more information from me (it also contains tips for SDL compilation
on the Raspberry PI itself):

https://github.com/lgblgblgb/xep128/wiki/Raspberry-Pi

Then you need to run xvic20 directly from a text console, not in X11!

