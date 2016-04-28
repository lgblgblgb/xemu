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
DEBUG	= -flto

CFLAGS	= $(DEBUG) $(CFLAGS_ARCH)
LDFLAGS	= $(DEBUG) $(LDFLAGS_ARCH)
PRG_V20	= xvic20.$(ARCH)
PRG_LCD = xclcd.$(ARCH)
PRG_ALL = $(PRG_V20) $(PRG_LCD)
SRC_V20	= commodore_vic20.c cpu65c02.c via65c22.c $(SRCS_ARCH_V20)
SRC_LCD	= commodore_lcd.c cpu65c02.c via65c22.c $(SRCS_ARCH_LCD)
FILES	= LICENSE README.md Makefile $(SRCS) *.h rom/README
OBJ_V20	= $(SRC_V20:.c=.o)
OBJ_LCD = $(SRC_LCD:.c=.o)
DIST	= xclcd-emus.tgz

do-all:
	$(MAKE) .depend.$(ARCH)
	$(MAKE) $(PRG_ALL)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

$(PRG_V20): $(OBJ_V20)
	$(CC) -o $(PRG_V20) $(OBJ_V20) $(LDFLAGS)

$(PRG_LCD): $(OBJ_LCD)
	$(CC) -o $(PRG_LCD) $(OBJ_LCD) $(LDFLAGS)


set-arch:
	if [ x$(TO) = x ]; then echo "*** Must specify architecture with TO=..." ; false ; fi
	if [ x$(TO) = x$(ARCH) ]; then echo "*** Already this ($(ARCH)) architecture is set" ; false ; fi
	if [ ! -f arch/Makefile.$(TO) ]; then echo "*** This architecture ($(TO)) is not supported" ; false ; fi
	mkdir -p arch/objs.$(TO) arch/objs.$(ARCH)
	echo "ARCH = $(TO)" > .arch
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

dist:
	$(MAKE) $(DIST)

clean:
	rm -f $(PRG_ALL) $(OBJ_V20) $(OBJ_LCD) .depend.$(ARCH) $(DIST) .arch

distclean:
	$(MAKE) clean
	rm -f rom/*.rom
	rm -f arch/objs.*/*.o || true
	rmdir arch/objs.* 2>/dev/null || true

strip: $(PRG_ALL)
	strip $(PRG_V20) $(PRG_LCD)

dep:
	rm -f .depend.$(ARCH)
	$(MAKE) .depend.$(ARCH)

.depend.$(ARCH):
	$(CC) -MM $(CFLAGS) $(SRC_V20) $(SRC_LCD) > .depend.$(ARCH)

roms:
	test -s rom/vic20-basic.rom || wget -O rom/vic20-basic.rom http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/vic20/basic.901486-01.bin
	test -s rom/vic20-kernal.rom || wget -O rom/vic20-kernal.rom http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/vic20/kernal.901486-07.bin
	test -s rom/vic20-chargen.rom || wget -O rom/vic20-chargen.rom http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/vic20/characters.901460-03.bin

.PHONY: clean all strip dep dist roms distclean install do-all set-arch

ifneq ($(wildcard .depend.$(ARCH)),)
include .depend.$(ARCH)
endif

