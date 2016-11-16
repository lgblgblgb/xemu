# CBM-HostFS test programs

This directory contains various tests about the 'HostFS'. HostFS
is intended to be a way to interact files on your host OS file system,
which means the file system of the OS, which runs the emulator
itself. It's an Xemu specific solution. Currently, only C65 emulator
implements it (not the M65).

You will need `CC65` suite installed (possible with the new, 4510 CPU target capable version!), and being in the PATH, also
the cbmconvert, and GNU `make` utility. Then, you need only say `make` to compile things in this directory.

## wedge.asm -> wedge (inside `cbmhostfs.d81` after `make`)

A little'n'stupid test solution. You should LOAD/RUN in C64 mode.
After that, LOAD vector is patched (at $330) for device number 7,
so you can try to LOAD things from device 7, which would mean the
HostFS with Xemu, ie the file system of your OS runs the emulator.

**NOTE**: do not try to `LOAD` and/or `RUN` wedge more times, it will crash your "C64"!

**NOTE**: it installs to `$C000`, if a program overwrites it, it will cause a crash on next `LOAD`.

**NOTE**: only `LOAD` vector is patched currently, even not `VERIFY`, `SAVE` or any other routine

**NOTE**: the `LOAD` routine does not call usual ROM functions currently to display *SEARCHING FOR* and *LOADING* which looks odd, and also may create other problems ...

## cbmhostfs.c -> cbmhostfs (inside `cbmhostfs.d81` after `make`)

A litte test program written in C. It does NOT needs the wedge above! It tries to use the HostFS feature on the low-level way.
