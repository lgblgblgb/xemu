# X-Emulators ~ "Xemu"

[![Build Status](https://api.travis-ci.org/lgblgblgb/xemu.svg?branch=master)](https://travis-ci.org/lgblgblgb/xemu)
[![Gitter](https://badges.gitter.im/lgblgblgb/xemu.svg)](https://gitter.im/lgblgblgb/xemu)
[![Download](https://api.bintray.com/packages/lgblgblgb/generic/xemu/images/download.svg)](https://bintray.com/lgblgblgb/generic/xemu/_latestVersion)

Emulators running on Linux/Unix/Windows/OSX of various (mainly 8 bit) machines,
including the Commodore LCD and Commodore 65 (and also some Mega65) as well.

Written by (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

## Quick start on Ubuntu Linux
```sh
sudo apt update
sudo apt install git build-essential libsdl2-dev libgtk-3-dev libreadline-dev
git clone https://github.com/lgblgblgb/xemu.git
cd xemu/
# Download ROMs:
make roms
# Compile:
make
# Run:
build/bin/xmega65.native
```
For other platforms and more details, see the [Wiki](https://github.com/lgblgblgb/xemu/wiki/Source).
