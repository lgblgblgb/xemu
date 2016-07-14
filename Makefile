## A Commodore LCD emulator using SDL2 library.
## Also included a very simple and inaccurate Commodore VIC-20 emulator
##
## Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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


all:
	$(MAKE) do-all


ifneq ($(wildcard .arch),)
include .arch
else
ARCH    = native
endif

# Edit the architecture specific file, if you really need it, instead of this "main" one ...
include arch/Makefile.$(ARCH)

# Remove -flto (link time optimization) if you have problems
# If you want to compile with -g option, also *DELETE* the -flto, both of them together known to be problematic!
DEBUG	= 

CFLAGS_WITHOUT_DEBUG = $(CFLAGS_ARCH) -DCPU_TRAP=0xFC

CFLAGS	= $(DEBUG) $(CFLAGS_WITHOUT_DEBUG)
LDFLAGS	= $(DEBUG) $(LDFLAGS_ARCH)
PRG_V20	= xvic20.$(ARCH)
PRG_LCD = xclcd.$(ARCH)
PRG_C65 = xc65.$(ARCH)
PRG_ALL = $(PRG_V20) $(PRG_LCD) $(PRG_C65)
SRC_V20	= cpu65c02.c  via65c22.c emutools.c commodore_vic20.c vic6561.c $(SRCS_ARCH_V20)
SRC_LCD	= cpu65c02.c  via65c22.c emutools.c commodore_lcd.c $(SRCS_ARCH_LCD)
SRC_C65 = cpu65ce02.c cia6526.c  emutools.c commodore_65.c c65fdc.c vic3.c c65dma.c sid.c c65hid.c $(SRCS_ARCH_C65)
FILES	= LICENSE README.md Makefile $(SRCS) *.h rom/README
OBJ_V20	= $(SRC_V20:.c=.o)
OBJ_LCD = $(SRC_LCD:.c=.o)
OBJ_C65 = $(SRC_C65:.c=.o)
DIST	= xclcd-emus.tgz

do-all:
	$(MAKE) .depend.$(ARCH)
	$(MAKE) $(PRG_ALL)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

%.s: %.c
	$(CC) -S $(CFLAGS_WITHOUT_DEBUG) $< -o $@

$(PRG_V20): $(OBJ_V20)
	$(CC) -o $(PRG_V20) $(OBJ_V20) $(LDFLAGS)

$(PRG_LCD): $(OBJ_LCD)
	$(CC) -o $(PRG_LCD) $(OBJ_LCD) $(LDFLAGS)

$(PRG_C65): $(OBJ_C65)
	$(CC) -o $(PRG_C65) $(OBJ_C65) $(LDFLAGS)

set-arch:
	if [ x$(TO) = x ]; then echo "*** Must specify architecture with TO=..." ; false ; fi
	if [ x$(TO) = x$(ARCH) ]; then echo "*** Already this ($(ARCH)) architecture is set" ; false ; fi
	if [ ! -f arch/Makefile.$(TO) ]; then echo "*** This architecture ($(TO)) is not supported" ; false ; fi
	mkdir -p arch/objs.$(TO) arch/objs.$(ARCH)
	echo "ARCH=$(TO)" > .arch
	mv *.o arch/objs.$(ARCH)/ 2>/dev/null || true
	mv arch/objs.$(TO)/*.o . 2>/dev/null || true
	@echo "OK, architecture is set to $(TO) (from $(ARCH))."

$(DIST):
	tar cfvz $(DIST) $(FILES)

install: $(PRG_ALL) roms
	$(MAKE) strip
	mkdir -p $(BINDIR) $(DATADIR)
	cp $(PRG_ALL) $(BINDIR)/
	cp rom/vic20-*.rom $(DATADIR)/
	cp rom/clcd-*.rom $(DATADIR)/
	cp rom/c65-*.rom $(DATADIR)/
	cp rom/geos-*.rom $(DATADIR)/

dist:
	$(MAKE) $(DIST)

clean:
	rm -f $(PRG_ALL) $(OBJ_V20) $(OBJ_LCD) $(OBJ_C65) .depend.$(ARCH) $(DIST)
	$(MAKE) -C rom clean

distclean:
	$(MAKE) clean
	$(MAKE) -C rom distclean
	rm -f .arch
	rm -f arch/objs.*/*.o || true
	rmdir arch/objs.* 2>/dev/null || true

strip: $(PRG_ALL)
	strip $(PRG_V20) $(PRG_LCD) $(PRG_C65)

dep:
	rm -f .depend.$(ARCH)
	$(MAKE) .depend.$(ARCH)

.depend.$(ARCH):
	$(CC) -MM $(CFLAGS) $(SRC_V20) $(SRC_LCD) $(SRC_C65) > .depend.$(ARCH)

roms:
	$(MAKE) -C rom roms

.PHONY: clean all strip dep dist roms distclean install do-all set-arch

ifneq ($(wildcard .depend.$(ARCH)),)
include .depend.$(ARCH)
endif

