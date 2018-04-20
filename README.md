# X-Emulators ~ "Xemu"

[![Build Status](https://api.travis-ci.org/lgblgblgb/xemu.svg?branch=master)](https://travis-ci.org/lgblgblgb/xemu)
[![Gitter](https://badges.gitter.im/lgblgblgb/xemu.svg)](https://gitter.im/lgblgblgb/xemu)
[![Download](https://api.bintray.com/packages/lgblgblgb/generic/xemu/images/download.svg)](https://bintray.com/lgblgblgb/generic/xemu/_latestVersion)

Emulators running on Linux/Unix/Windows/OSX of various (mainly 8 bit) machines,
including the Commodore LCD and Commodore 65 (and also some Mega65) as well.

Written by (C)2016-2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
Source repository: https://github.com/lgblgblgb/xemu

Xemu also contains code wasn't written by me. Please read this page to get to
know more, also with my greeings for any kind of help from others:
https://github.com/lgblgblgb/xemu/wiki/Copyright

Xemu is licensed under the terms of GNU/GPL v2, for more information please
read file LICENSE. You can find the source on github, see above.

Note: there is *nothing* too much common in these machines. The only reason
that I emulate these within a single project, that I can easily re-use some
of the components needed, that's all! Also, the list of emulated machines is
simply the preference of myself, what I would like to emulate, nothing special
about my selection.

This file is only some place holder :) For some sane documentation, please **visit
the wiki section of the project, here**:

https://github.com/lgblgblgb/xemu/wiki

## Quick start (using binary)

For probably seriously outdated pre-compiled binaries for MacOS, Win32, Win64,
(and some deb package) please follow this link:

https://bintray.com/lgblgblgb/generic/xemu/current_version

Maybe more up-to-date site of mine for binaries (but no MacOS here) with experimental
windows installer and Ubuntu DEB package:

http://xemu.lgb.hu/download.php

## Quick start (from source)

For more information: https://github.com/lgblgblgb/xemu/wiki/Source

### Install software for compilation

#### Example for Ubuntu Linux

    sudo apt update
    sudo apt install git build-essential libsdl2-dev libgtk-3-dev libreadline-dev

#### Example for MacOS

Assuming development Apple development components and `homebrew` is already installed on
your Mac: https://brew.sh/

    brew update
    brew install sdl2 wget git

### Clone source respository

    git clone https://github.com/lgblgblgb/xemu.git
    cd xemu

### Download needed ROM images, etc

    make roms

### Compilation

    make

Optionally, to create binary DEBian .deb package for Ubuntu/Debian Linux,
result will be built in build/bin (which can be installed with `dpkg -i`,
followed by a `sudo xemu-download-data` which will download the data
files as well, you can then execute emulators like `xemu-xmega65`):

    make deb

To compile only (`make` in the top level directory will compile all of the
targets automatically) a given emulator (let's say mega65):

    cd targets
    ls -l
    cd mega65
    make
    cd ../..

Here, command `ls -l` is only for get a list of available targets (ie.
the emulators included in the Xemu project).

Note: in case of MacOS, you may get tons of warning of invalid options
during the compilation. This is harmless, and it's the sign of using
CLANG for real with the command name `gcc`. To avoid them, instead of
using plain `make`, use this: `make ARCH=osx`.

### Run the binary

    ls -l build/bin/

to get a list of compiled binaries, like `xmega65.native` or `xc65.native`

Run one of them, like (the Commodore LCD emulator in this case):

    build/bin/xclcd.native

### Windows

For building binary (exe) for Windows, you still need a UNIX-like environment
(in theory WSL - Windows Subsystem for Linux - should be enough)  for the
compilation, with cross-compiler and SDL2 MinGW cross platform suite installed.
Then you can say the following for 32 bit or 64 bit build process (in general,
32 bit version should be avoided on any OS - for performance reasons as well):

    make ARCH=win32
    make ARCH=win64

In build/bin you'll find files like *.win32 and *.win64, they are `exe` files
for real, you can rename and copy them to a Windows box to be able to run
them using Windows only (you also need the specific SDL2.dll though).
