## Collection of *simple* emulators of some 8 bits machines using SDL2 library,
## including the Commodore LCD and Commodore 65 too.
##
## Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


TARGETS = c65 cvic20 clcd cgeos ep128 mega65 primo tvc
ARCHS	= native win32 win64 osx

ARCH = native

all:
	for t in $(TARGETS) ; do $(MAKE) -C targets/$$t || exit 1 ; done

all-arch:
	for t in $(TARGETS) ; do for a in $(ARCHS) ; do $(MAKE) -C targets/$$t ARCH=$$a || exit 1 ; done ; done

all-html:
	for t in $(TARGETS) ; do egrep -q '^#\s*define\s+CONFIG_EMSCRIPTEN_OK\s*$$' targets/$$t/xemu-target.h && make -C targets/$$t ARCH=html ; done

clean:
	for t in $(TARGETS) ; do $(MAKE) -C targets/$$t ARCH=$(ARCH) clean || exit 1 ; done

strip:
	for t in $(TARGETS) ; do $(MAKE) -C targets/$$t ARCH=$(ARCH) strip || exit 1 ; done

all-clean:
	for t in $(TARGETS) ; do for a in $(ARCHS) ; do $(MAKE) -C targets/$$t ARCH=$$a clean || exit 1 ; done ; done
	$(MAKE) -C rom clean
	$(MAKE) -C build/bin clean

distclean:
	$(MAKE) all-clean
	$(MAKE) -C rom distclean
	$(MAKE) -C build/bin/clean

dep:
	for t in $(TARGETS) ; do $(MAKE) -C targets/$$t dep || exit 1 ; done

all-dep:
	for t in $(TARGETS) ; do for a in $(ARCHS) ; do $(MAKE) -C targets/$$t ARCH=$$a dep || exit 1 ; done ; done

roms:
	$(MAKE) -C rom

deb:
	$(MAKE) all
	build/deb-build-simple.sh

nsi:
	rm -f build/bin/*.[dD][lL][lL] build/bin/*.[eE][xX][eE]
	for t in $(TARGETS) ; do for a in win32 win64 ; do $(MAKE) -C targets/$$t ARCH=$$a || exit 1 ; done ; done
	build/nsi-build.sh win32 `build/system-config win32 sdl2 dll`
	build/nsi-build.sh win64 `build/system-config win64 sdl2 dll`

publish:
	$(MAKE)
	$(MAKE) deb
	$(MAKE) nsi
	$(MAKE) -C build/bin dist
	@echo "*** You should not use this target, this is only for distributing binaries on the site of the author!"

.PHONY: all all-arch clean all-clean roms distclean dep all-dep deb nsi publish
