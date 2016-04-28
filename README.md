# XcLcd

Commodore LCD (and Commodore VIC-20) emulator

(Note: about the Commodore VIC-20, see below)

Commodore LCD is a highly unknown portable LCD based device with battary power and
ability to "sleep" with SRAMs still powered. Unfortunately the project was stopped
before the production phase, only about 2-5 units known to exist in different state
of developing. Compared to the LCD, Commodore 65 is a highly known, well documented,
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
with the help of the SDL2 library.

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
* Border is not emulated
* Keyboard currently does not work
* Only the stock machine is emulated without any RAM expansion
* No sound is emulated


## Compilation on Linux / UNIX-like machine

## Compilation for Windows

## Compilation on Raspberry Pi


