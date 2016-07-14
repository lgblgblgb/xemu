# X-Emulators

[![Join the chat at https://gitter.im/lgblgblgb/xemu](https://badges.gitter.im/lgblgblgb/xemu.svg)](https://gitter.im/lgblgblgb/xemu?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

Emulations running on Linux/Unix/Windows of various (mainly 8 bit) machines,
including the Commodore LCD and Commodore 65 as well.

WARNING: there is *nothing* too much common in these machines. The only reason
that I emulate these within a single project, that I can easily re-use some
of the components needed, that's all!

For some story line: http://cubed-borka.blogspot.hu/2016/05/my-commodore-65-and-commodore-lcd.html

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

It was some years ago, and though a native emulator is developed (not made as a
public release) it was buggy, it used quite questionable SDL screen "updating"
solution etc. Now it's a fresh start, with reusing the CPU and VIA emulation parts
only.

This piece of code is about writing a native emulator version which can be run
at least on Windows and Linux (or Unix-like machines, Raspberry PI is included)
with the help of the SDL2 library. Note, that in theory, it would be easy to
port to other targets because of using SDL.

As I wanted to emulate other machines as well, I renamed the project from XCLCD
(CLCD = Commodore LCD) to "X-Emu".

## Basic usage of the emulator

Currently no data storage is implemented ... You basically have only two hot-keys
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

## About SDL rendering - please help me :)

Let me explain one thing: if you're more in SDL2 internals than me, please contact
me. I feel quite lost (with my "more serious" emulator too, Xep128, also on github)
about the "ideal" solution to render an average 8 bit computer's screen. That is
usually we have 256 or less (16 in case of VIC-20, and only two in case of Commodore
LCD) colours. That is, an indexed method would be ideal. I've tried to use that and
it seems it's unstable, and even not usable on some platforms. At the end, I've found
myself in the situation, that I should render RGBA pixels as DWORDs as a streaming
texture. It seems to be quite OK. However, just a quick peek on other emulatorts
using ("only" or "also") like FUSE (ZX Spectrum emulator) or Hatari (Atari emulator)
it seems they use SDL surfaces. Honestly, I can't even see the difference between
these notions and the precise impact ... Using textures seems to have acceptable
performance even in full screen/scaled mode, even on a quite "weak" hardware as
Raspberry Pi model 1 is! So maybe it's not even needed to lamenting a lot on this
issue, however I'm still curious the scene at the background behind this topic.
Also, using DWORDs by pixel seems to be overkill, but it's possible that today's
computer are not so optimized in 8 bit writes anyway during updating the pixel
buffer / texture, so using DWORDs by pixel cannot be a big problem. Also I am not
so sure what is the "ideal" format to be used (for best performance, for me it
seems `SDL_PIXELFORMAT_ARGB8888` is, with constant 0xFF for the alpha channel - but
do not forget that solution should be platform independent somewhat ...).

What I think: my solution can't be *so* bad, as it works (testes) at least on Linux,
Windows, and Raspberry Pi, with quite acceptable performace. However being as
a "maximalist" I always have the thoughts that it can be better, maybe :)

emutools.c is aimed to be a simple "layer" for an average emulator-like project,
so I've tried to put everything unrelated to the given emulated machine there.
Including the SDL stuffs, of course.

Please don't hesitate to contact me, if you have an constructive opinion. Thanks!

## The missing bits

Still many work is left. Honestly, I feel, it would be better to be included in
VICE, however I am lame to even understand *some* of the VICE internals ... But
VICE has already quite exact 65C02 (well, I guess it has that too, not only
6502, hmm ...), VIA, disk drive ... whatever emulation. Just it would be needed
to be "wire" things together.

For both emulators, the major problem is the lack of any storage solution, or
at least the ability to load programs.

Of course audio emulation would be nice as well (LCD had *some* audio, also
the VIC-20, for sure).

About further problems with VIC-20 (please note: VIC-20 is currently not the
major character of the show, much better emulators exists for that nice little
machine!).

## Commodore 65 emulator??

Yes. Or kind of ... Quite limited currently though. For me, it worked to the
level for BASIC prompt, and even for typing commands, or switching to C64
mode (GO64), or you can try write BASIC programs (NOTE: ROM 911001.bin seems
not to work properly, but 910111.bin does).

What is emulated and what is not:

* 65CE02 "core" instructions, hopefully more-or-less correctly emulated.
  65CE02 emulation is done by extending/modifying a 65C02 emulator (of mine)
  since 65CE02 is basically an extended 65C02, with only "some" differences
  (oh, yes, "some" ... well).

* 4502 "core" extensions over 65CE02, ie MAP opcode, and special meaning
  of NOP (as EOM)

* Though in theory 65CE02 does not use the old NMOS behaviour of RMW
  opcodes (ie, write twice), it introduces incompatibilties with C64 software.
  As it seems, Mega65 already uses the "old behaviour" for more compatibility,
  I try to follow this path as well.

* Memory management: CPU "MAP"/"EOM" stuff, VIC3 register $30 ROM select
  and 2K colour SRAM mode, "C64-style" "CPU port". REC is not emulated.

* DMAgic COPY/FILL should work, MIX/SWAP is only guessing. Modulo is not
  supported (and not so much information is available how it should work
  anyway, as with case of MIX).

* 128K ROM / 128K RAM. Optionally (enabled by default) 512K RAM emulated in
  the upper region, though not in a REC compatible way. This even can cause
  problems, I guess.

* FDC emulation may be used to *read* D81 disk images, but still there are bugs!
  Currently, the FDC part is the most ugly in source. SWAP bit is not emulated,
  which can cause even data corruptions ("fortunately" since everything is
  read-only, there is no chance to corrupt the disk image content itself).

* VIC3 code is horrible, it merely emulates full frames in one rendering
  step only. Many things are not emulated: MCM/ECM for classic VIC2 modes,
  no border, no DAT, no hardware character attributes, no V400
  and interlace, and no H1280 mode. Though bitplane modes, programmable
  palette, 40/80 column text mode, and classic VIC2 hires mode should work.
  K2 demo is able to show the "busty woman" already with my emulator.

  Sprites are emulated in a *very* lame and limited way. Multicolour
  sprites are not supported, and even the "under the screen" priority
  is not handled. Sprites in bitplane mode are incorrect now. Also, the
  "render a frame in once" scenario makes sprite multiplexing or any other
  tricks impossible. Sprite emulation is mainly included only for some
  simple tasks, ie "mouse pointer" in certain softwares, and for similar
  usages.

* There is *some* SID emulation, but it's horrible, believe me :)
  Two SIDs are emulated with the help of SID emulation core from
  'webSID' (see sid.c for more details). However the major problem is,
  that SID "audio rendering" is only done currently, if audo buffer
  is needed to fill. Moreover, the exact timing is nowehere in
  my implementation - I guess - compared to the original. Forgive me,
  I'm the lamest in the audio realm ...

* No emulation for the UART, REC (Ram Expansion Controller)

* CIA emulation (for real, part of 4510 and not even full CIAs from
  some aspects) is quite lame. Though it seems enough for keyboard and
  periodic IRQ generating in C64 mode at least

* Keyboard is not fully implemented (not the extra keys like TAB - it seems
  it's handled by - surprisingly - by the UART part ...), and
  some mapping problems (ie, position based mapping for US host machine layout,
  some keys are still missing, I was lazy ...).

Compatibility:

* First "milestone" is already here, it can "boot" to BASIC, though :) Now, it
  also does its work with "DIR" command, if D81 image is given.

* C65 standard boot is OK

* C64 mode can be used ("GO64")

* GEOS kernal can be booted, the "chessboard" background can be seen

* K2 demo is able to show the "busty woman" (320x200 256 colours, 8 bitplanes
  mode)

* IFF demos has problems currently, I am not sure what is the problem
  yet (though there is a modified version on an FTP site - saying it should
  work and fixed - it is able to show at least the "King Tut" picture)

Basic usage of the emulator:

* You need to give the disk image (should be D81 in format!) name as the
  parameter of the executable, if you want disk access.

* Press F11 to toggle between window/full screen mode

* Press F10 to RESET.

* Press F9 to exit

That's about all - currently :)

## What is that "Commodore GEOS" emulator?!

It's a very primitive Commodore 64 emulator without many features and with
the intent to have special support to run *OWN* GEOS version interacts with
the emulator in a special way. It's a help/debug tool for some GEOS
related projects. Probably you don't ever need this emulator currently,
and only useful for myself :) And no, please don't ask the "special GEOS
version" ...

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

Other reason: my "more serious" emulator (Xep128, also on github) is about
being just too complex to "play" with it for "quick modifications" to try
things out about the basics of writing emulators (ie using SDL, the timing
code, in the future: audio sync, etc). It seems it's better to do it here,
with more simple emulators, and using the results in the Xep128 project as
well.

About the CPU: since Commodore LCD uses software compatible CPU (exact type is
not very known) of 65C02, my experimental VIC-20 emulator also uses that!
That's a direct attack against poor VIC-20 :) as it used 6502! So, of course
things like illegal opcodes won't work using my emulator!

Full (?) list of stupidities of my VIC-20 emulator:

* 65C02 as the CPU, not 6502 (no illegal opcodes), this is by intent!
* Exact screen origins, timings are not exact
* Scanline based emulation, parameters can only change after a whole scanline
* No sound is emulated
* No tape, or serial IEC bus is emulated (floppy drive)
* NMI handling, RUN/STOP + RESTORE does not work
* Only "PAL" system is emulated
* VIA emulation is unfinished, and maybe quite inaccurate as well

Some extra features of Commodore VIC-20 emulator (compared to Commodore LCD):

Command line options:

@1 @8 @16 @24 @40

@1 @8 @16 @24 @40 -

@1 @8 @16 @24 @40 something.prg

The '@' options configures the memory expansions at the given kilobyte.
Of course you can specify less (or none) of them.

The '-' option causes to "boot" into monitor mode. The monitor is currently
unusable, only 'x' (exit) command works :)

If some filename is given (ie not start with '@' or '-') it tried to be
loaded and auto-started, as a BASIC program (or ML one, with BASIC 'stub').

No filename or '-' causes to boot into BASIC.

NOTE: currently no check about illegal .prg, which would exceed the available
configured RAM, or even overwriting ROM with bogus start address with the
two first bytes, etc. Also, it's possible that the program requires other
memory configuration, so the BASIC start is different, this is not handled
autmatically either ...

Functionality of auto-load (currently the only way to load a program ...)
requires the emu ROM to be usable, you can find it in the rom/ directory.

You can use numeric key pad arrow keys as the "joystick", "5" is the fire.
Optionally, you can use the right CTRL of keypad button '0' for fire too.

You can try to use a supported Game Controller / Joystick as well (I've
tested with XBox-360 controller). Currently, there is no mapping of controls,
which can be a *big* problem. Only axes 0/1 can be used as the joystick,
but it's hard to tell, which controls they are on *your* joystick (maybe even
vertical/horizontal is mixed ...), or the "hats" can be used as well. Every
buttons are used as the "fire" button.

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

Resulting binaries will have the "extension" of .native, but you can forgot
that of course, it's just for the ability to be able to build for multiple
architectures (ie for Windows it will have .win32).

## Compilation for Windows

To be able to compile emulators for Windows, you must have a Linux (or probably
other UNIX-like system) with suitable cross-compiler able to generate Windows
binaries. You also need the SDL2 (at least 2.0.2, 2.0.4 is recommended!) stuffs
installed, and arch/Makefile.win32 is possible modified for proper settings.

First, you need to select "architecture" to win32 with this command:

`make set-arch TO=win32`

Then you can say: `make`

The result will have the extension .win32, but actually it's an .exe :)

You can switch back to native architecture with this command:

`make set-arch TO=native`

## General compilation related notes

Link-time-optimalization (lto, see -flto gcc switch) seems to be kinda picky.
You may want to give the optimization flags at the linker stage to! Also:
go *not* mix -g switch (eg you want to debug the program with gdb) with -flto
according to the gcc manual, it can cause very odd problems! Basically you
should change DEBUG setting in Makefile from -flto to -g, if you want to debug.

Architecture specific (ie win32 or native) settings are in the arch/ directory!
Including the used C compiler path, flags, sdl2-config path, etc ...

If you add/remove files, include new header files, etc, you should say `make dep`
for the given architecture selected.

## Compilation for Raspberry Pi

You have two choices. You can compile it on the Raspberry Pi itself, then you need
to follow instructions given in the "Compilation on Linux / UNIX-like machine"
section.

You'll need SDL version 2.0.4 (or newer, if any ...), then you will be able to
run the emulators directly from text console, no X11 GUI is required at all :) Which
means lower resource usage, of course. I've not tested the stuff on Raspb.PI on
X11!

Also, you can use a cross-compiler on your desktop machine, if it's a Linux/UNIX
kind of machine. This option is under development though, and isn't made available!

Some notes:

According to my experiments -falign-functions=16 -falign-loops=16 really
helps on Rapsberry-Pi at least.

*Latest test: about 19% CPU usage on a good old Raspberry Pi model 1 for the
Commodore VIC-20 emulator. Not bad ... For the Commodore 65 emulator, it is not
fair to do any test yet, since the emulator does literally hundreds of log messages
by second which slows down the emulation a lot, even if that is redirected into
/dev/null (involves syscalls, etc).*

It's interesting to play with locked texture access compared to the non-locked
version. On my PC, the difference cannot even be seen too much. However it's
quite possible that on a lower performance hardware the difference can be seen
more. Basically, the locked method seems to be the suggested one, however I'm
still not sure, any feedback is welcome!

For more information from me (it also contains tips for SDL compilation
on the Raspberry PI itself):

https://github.com/lgblgblgb/xep128/wiki/Raspberry-Pi

Then you need to run xvic20 directly from a text console, not in X11!

