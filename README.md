# X-Emulators ~ "Xemu"

[![Build Status](https://api.travis-ci.org/lgblgblgb/xemu.svg?branch=master)](https://travis-ci.org/lgblgblgb/xemu)
[![Gitter](https://badges.gitter.im/lgblgblgb/xemu.svg)](https://gitter.im/lgblgblgb/xemu)
[![Download](https://api.bintray.com/packages/lgblgblgb/generic/xemu/images/download.svg)](http://xemu-dist.lgb.hu/)

Emulators running on Linux/Unix/Windows/OSX of various (mainly 8 bit) machines,
including the Commodore LCD and Commodore 65 (and also some Mega65) as well.

Written by (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

## Most quick start (Xemu running in your web browser!)

Just visit this page, and use the "in-browser demonstration" link:

http://xemu-dist.lgb.hu/

Nore: this kind of demonstration is limited, often not in pair with the native
client for your OS (a "binary"), which - at the other hand - requires more
work: installation, configuration, etc ...

## Quick start (using pre-built binaries)

I'm trying to provide some sane ways to install/use Xemu for people don't like
to compile from source. Visit this page:

http://xemu-dist.lgb.hu/

### Windows

You can find 32 and 64 bit windows based installers for Windows. No need to say,
highly experimental (as I am not a windows user at all ...). You can also use
the ZIP'ed versions, which are simply the needed exe + dll files, without any
installer. Please carefully read the download page, about these, especially
about the NSIS-related problem which causes false positive detections as
*trojan/virus* by many antivirus software, unfortunately.

Also, you can find ZIP archives on that page, contains only the executable files
and the needed DLL, without any installer.

*HELP WANTED*: if you are an advanced Windows user, and want to help, contant me.
I would need tester, and somebody who knows Windows quite well, including being
able to suggest better installation and other solutions ...

### Linux

On Linux, you can try the provided DEB pacakge to install, if you run Ubuntu (may
work on other DEB based distriutions as well). There is an RPM package provided
too, however that is simply a converted version of the DEB pakcage (with "alien")
so it may or may not work for you.

Work in progress to provide other - less distribution dependent - ways to install
Xemu on Linux (other than compiling yourself), maybe in the form for flatpak,
AppImage or something like that (though I really hate Snappy ...) in the future.

### Where is the MacOS binary build?

Sorry, there is no MacOS build this time, since I don't find any sane way to
compile for MacOS legally without buying a Mac. That's a kind of lame from
Apple btw, since I want to help Apple users to have more software, but I
can't accept that I am forced to buy something to be able to do so (for
Windows, I can use mingw cross-compiler from Linux - basically without
any material from Microsoft I had to buy and/or use, there is no such a way
for MacOS, or even if there is, it needs Xcode components, etc, so it cannot
be totally free / legal ...).

Also I have no idea how installers work on MacOS, how to create one, etc ...
One thing is clear: Xemu should work on MacOS, if you compile it yourself.

*HELP WANTED* Surely, if you have any suggestions, feel free to contact me,
I've never even used MacOS, even less than Windows. I should find a way
to make Xemu MacOS builds without me needing to pay for Apple (to buy
a Mac) or to be illegal (using "Hackintosh"). Also, I don't know anything
about MacOS-specific installation methods preferred. Also, I soon would need
to have some GUI components, MacOS - again - is missing from my knowledge
totally.

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

#### Raspberry Pi

If you use some Linux distributions (probably Raspbian) on your Raspberry Pi, it should
behave more or less the same way as a Linux distribution on your PC, so nothing special
here.

However. If you plan to run Xemu without X11, you need a special build of SDL2 must be
done by yourself first. This is because, SDL2 provides a way to run SDL applications
on Raspberry without X11 ("RPI" architecture), however the standard Raspbian - as far
as I know - provides only SDL2 library with X support (though I can be wrong, contant
me, if you know more on this topic).

I've - of course - tested this and it worked, however it's hard to provide a binary
build this way currently, and needs more "manual work" to compile Xemu, and even SDL2
before.

#### Bare metal?

I received the idea to be able to run Xemu as a "bare-metal" project on Raspberry Pi
for example. "Bare metal" means that it does not run an OS but uses the hardware directly,
ie, Xemu needs to be booted instead of an OS. Surely, it's a kind of exciting route for
me, but unfortunately I don't have too much time to play with this idea :(

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

#### MSYS2 native build

An easy way to build xemu under Windows is to use the  MSYS2 package which includes
a full MinGW compiler, associated headers, tools , and a nice package management utility
for easy installation of required components: Pacman, popular in ArchLinux-based distros.

The following steps are based on a Windows-10 x64 system.

* Download the executable installer in https://www.msys2.org for x86-64 architecture.
* Install on the default location.
* Execute the MSYS2 MinGW 64bit system prompt at installation end, or by looking into your Start Menu.
* In the command prompt, ensure you have the latest repositories by doing:

```
pacman -Syu
```
Restart the prompt if needed, and finish installing remaining packages with:

```
pacman -Su
```

Now we can install the GCC compiler and required packages to build xemu with one command call:

```
pacman -S base-devel mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 
```

Build the native Windows executables by issuing:
```
make ARCH=nativewin
```



#### Alternative method (Cross-compilation)

For Ubuntu (and probably other DEB based distros, this also includes of course
WSL if Ubuntu is used as the guest) you can install mingw by:

    apt-get install binutils-mingw-w64-i686 binutils-mingw-w64-x86-64 gcc-mingw-w64-i686 gcc-mingw-w64-x86-64

Then, you need to install mingw-specific SDL2 development suite. Visit SDL site
at https://libsdl.org/ and use the Download / SDL 2.0 link. You will need
the development libraries (as a form of some .tar.gz) for Windows, the MinGW
version. Extract that into some directory. You'll find a Makefile inside,
you should utilize it by saying:

    make cross

After that, one thing is still missing: Xemu's build system won't find the
sdl2-config scripts. Thus, you need to symlink them into a directory which
is in your PATH, let's say /usr/local/bin in our example. Let's assume,
SDL2 above has been already installed at prefix /usr/local. Then, the commands
you need:

    ln -s /usr/local/i686-w64-mingw32/bin/sdl2-config /usr/local/bin/i686-w64-mingw32-sdl2-config
    ln -s /usr/local/x86_64-w64-mingw32/bin/sdl2-config /usr/local/bin/x86_64-w64-mingw32-sdl2-config

Then you can say the following for 32 bit or 64 bit build process (in general,
32 bit version should be avoided on any OS - for performance reasons as well):

    make ARCH=win32
    make ARCH=win64

In build/bin you'll find files like *.win32 and *.win64, they are `exe` files
for real, you can rename and copy them to a Windows box to be able to run
them using Windows only (you also need the specific SDL2.dll though - those can
be found at the same directory where the given - 32 or 64 bit - sdl2-config
script is from the development SDL2 stuff you've installed).
