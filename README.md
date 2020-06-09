# X-Emulators ~ "Xemu"

[![Build Status](https://api.travis-ci.org/lgblgblgb/xemu.svg?branch=master)](https://travis-ci.org/lgblgblgb/xemu)
[![Gitter](https://badges.gitter.im/lgblgblgb/xemu.svg)](https://gitter.im/lgblgblgb/xemu)
[![Download](https://img.shields.io/badge/download-master-%236060FF)](https://github.com/lgblgblgb/xemu-binaries/blob/master/README.md)
[![License: GPL-2.0](https://img.shields.io/github/license/lgblgblgb/xemu.svg)](./LICENSE)
[![Contributors](https://img.shields.io/github/contributors/lgblgblgb/xemu.svg)](https://github.com/lgblgblgb/xemu/graphs/contributors)
[![GitHub last commit (branch)](https://img.shields.io/github/last-commit/lgblgblgb/xemu/dev)](https://github.com/lgblgblgb/xemu/tree/dev)

Emulators running on Linux/Unix/Windows/OSX of various (mainly 8 bit) machines,
including the Commodore LCD and Commodore 65 and MEGA65 as well.

Written by (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
Source repository: https://github.com/lgblgblgb/xemu

Xemu also contains code wasn't written by me. Please read this page to get to
know more, also with my greeings for any kind of help from others:
https://github.com/lgblgblgb/xemu/wiki/Copyright

Xemu is licensed under the terms of GNU/GPL v2, for more information please
read file LICENSE. You can find the source on github, see above.

Note: there is no serious logic about the set of emulated machines by the
Xemu project. The only reason that I emulate these within a single project,
that I can easily re-use some of the components needed, that's all! Also, the
list of emulated machines is simply the preference of myself, what I would
like to emulate, nothing special about my selection.

This file is only some place holder :) For some sane documentation, please **visit
the wiki section of the project, here**:

https://github.com/lgblgblgb/xemu/wiki

## Most quick start (Xemu running in your web browser!)

Just visit this page:

http://xemu-dist.lgb.hu/dist/xemu-sample.html

Note: this kind of demonstration is **limited** (or **even broken**!), often
not in pair with the native client for your OS (a "binary"), which - at the
other hand - requires more work: installation, configuration, etc ... Also,
currenty there can be problems with some emulators in this form (especially
MEGA65).

## Quick start (using pre-built binaries)

**Plese note, that binaries on some older site of mine (especially on bintray.com,
and xemu-dist.lgb.hu) can be OUTDATED. Currently to download up-to-date,
ready-to-use Xemu binaries, you should go here:**

https://github.lgb.hu/xemu/

Or, if you'are more an insider-oriented to see various builds from branches,
additional information, etc, you can go here (the same as above, but instead
of direct links, a more in-depth view of the distribution repository):

https://github.com/lgblgblgb/xemu-binaries/blob/master/README.md

### Windows

You can find 32 and 64 bit windows based installers for Windows. No need to
say, highly experimental (as I am not a windows user at all ...). You can
also use the ZIP'ed versions, which are simply the needed exe + dll files,
without any installer. Please carefully read the download page, about these,
especially about the NSIS-related problem which causes **false positive**
detections as **trojan/virus** by many antivirus software, unfortunately.
If you don't believe me: https://nsis.sourceforge.io/NSIS_False_Positives

Also, you can find ZIP archives on that page, contains only the executable
files and the needed `DLL`, without any installer. This may also solve the
false virus detection ...

### Linux

On Linux, you can try the provided DEB pacakge to install, if you run Ubuntu
(may work on other DEB based distriutions as well). There is an RPM package
provided too, however that is simply a converted version of the DEB pakcage
(with `alien`) so it may or may not work for you.

Work in progress to provide other - less distribution dependent - ways to
install Xemu on Linux (other than compiling yourself), maybe in the form for
`flatpak`, `AppImage` or something like that (though I really don't like
`Snappy` ...) in the future.

### MacOS

On MacOS, you want to use the MacOS build, of course. You can download a ZIP
file, with a binary and a .dylib, they must be in the same directory!

You can also download DMG, however the maturity of my DMG is considered "low"
currently.

## Quick start (from source)

For more information: https://github.com/lgblgblgb/xemu/wiki/Source

### Install software for compilation

#### Example for Ubuntu Linux

    sudo apt update
    sudo apt install git build-essential libsdl2-dev libgtk-3-dev libreadline-dev

#### Example for MacOS

Assuming Apple development components and `homebrew` is already installed on your
Mac: https://brew.sh/

    brew update
    brew install sdl2 wget git

Xemu should build with command:

    make

#### Raspberry Pi

If you use some Linux distributions (probably Raspbian) on your Raspberry Pi,
it should behave more or less the same way as a Linux distribution on your PC,
so nothing special here.

However. If you plan to run Xemu without X11, you need a special build of SDL2
must be done by yourself first. This is because, SDL2 provides a way to run
SDL2 applications on Raspberry without X11 ("RPI" architecture), however the
standard Raspbian - as far as I know - provides only SDL2 library with X11
support (though I can be wrong, contant me, if you know more on this topic).

I've - of course - tested this and it worked, however it's hard to provide a
binary build this way currently, and needs more "manual work" to compile Xemu,
and even SDL2 before.

#### Bare metal?

I received the idea to be able to run Xemu as a "bare-metal" project on
Raspberry Pi for example. "Bare metal" means that it does not run an OS but
uses the hardware directly, ie, Xemu needs to be booted instead of an OS.
Surely, it's a kind of exciting route for me, but unfortunately I don't have
too much time to play with this idea :( One idea, that there're frameworks
for these purposes. The other: some of the emulations within the Xemu
project (ie MEGA65) is very "pricy" to emulate, and may not run smooth
(real-time) even on the newest Raspberry Pi 4 ... However exactly this
problem can drive me to have more optimizations :)

### Clone source respository

    git clone https://github.com/lgblgblgb/xemu.git
    cd xemu

### Compilation

    make

Optionally, to create binary DEBian .deb package for Ubuntu/Debian Linux,
result will be built in build/bin (which can be installed with `dpkg -i`,
followed by a `sudo xemu-download-data` which will download the data
files as well, you can then execute emulators like `xemu-xmega65`):

    make deb

To compile only (`make` in the top level directory will compile all of the
targets automatically) a given emulator (let's say MEGA65):

    cd targets
    ls -l
    cd mega65
    make
    cd ../..

Here, command `ls -l` is only for get a list of available targets (ie.
the emulators included in the Xemu project).

### Run the binary

    ls -l build/bin/

to get a list of compiled binaries, like `xmega65.native` or `xc65.native`.
You may want to copy those files to `/usr/local/bin` or such. Surely you
can drop the .native ending. I also like to rename (done this way in the
DEB package) to have names like `xemu-xmega65` and such to avoid "collusion"
with other emulators in case of emulators with short names (probably).

Run one of them, like (the Commodore LCD emulator in this case):

    build/bin/xclcd.native

### Windows

For building binary (exe) for Windows, you still need a UNIX-like environment
(in theory WSL - Windows Subsystem for Linux - should be enough)  for the
compilation, with cross-compiler and SDL2 MinGW cross platform suite installed.

#### MSYS2 native build

Note: this is probably the easier method for a Windows user, however this is
_not the method we use to build official_ binaries for Windows.

An easy way to build xemu under Windows is to use the `MSYS2` package which
includes a full `MinGW` compiler, associated headers, tools , and a nice package
management utility for easy installation of required components: `Pacman`,
popular in Arch Linux based distributions.

The following steps are based on a Windows 10 x64 system.

* Download the executable installer in https://www.msys2.org for x86-64
  architecture.
* Install on the default location.
* Execute the MSYS2 MinGW 64bit system prompt at the end of the installation,
  or via your Start Menu.

At the command prompt, ensure you have the latest repositories by doing:

    pacman -Syu

Restart the prompt if needed, and finish installing remaining packages with:

    pacman -Su

Now we can install the GCC compiler and required packages to build xemu with
one command executed:

    pacman -S make mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 

Build the native Windows executables by issuing:

    make

You can find the executables, with `.native` extension, in the `build/bin`
directory. Surely, you can (and maybe you want) rename files to have extension
`.exe` instead.

#### Alternative method (Cross-compilation)

Note: this is the _official method we use to build official binaries for Windows_.

For Ubuntu (and probably other DEB based distros, this also includes of course
WSL if Ubuntu is used as the guest) you can install mingw by:

    apt-get install binutils-mingw-w64-i686 binutils-mingw-w64-x86-64 gcc-mingw-w64-i686 gcc-mingw-w64-x86-64

Then, you need to install mingw-specific SDL2 development suite, download,
modify its Makefile probably, install it, create compatibility symlinks for
Xemu ... If you trust into the version Xemu uses, probably it's better to
do the task with a single command. It needs you to be at the top directory
of the downloaded or cloned Xemu repository. You may want to run this as root,
or at least you need to have `sudo` capabilities. The command:

    build/install-cross-win-mingw-sdl-on-linux.sh /usr/local/bin

This command will install stuffs in `/usr/local/cross-tools` (regardless of
the parameter!) and create symlinks in `/usr/local/bin`. If it's not in your
`PATH` by default, you may need to put it, or use different argument than
`/usr/local/bin`.

Then you can say the following for 32 bit or 64 bit build process (in general,
32 bit version should be avoided on any OS - for performance reasons as well):

    make ARCH=win32
    make ARCH=win64

In `build/bin` you'll find files like `*.win32` and `*.win64`, they are `exe`
files for real, you can rename and copy them to a Windows box to be able to run
them using Windows only (you also need the specific `SDL2.dll` though - those
can be found at the same directory where the given - 32 or 64 bit -
`sdl2-config` script is from the development SDL2 stuff you've installed).
