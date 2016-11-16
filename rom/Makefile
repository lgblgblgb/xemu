## XEmu: collection of simple emulators
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

WGET		= wget
URL_LIST	= rom-fetch-list.txt
#OUR_ROMS	= clcd-u104-parasite.rom vic20-emulator-tool.rom
OUR_ROMS	= vic20-emulator-tool.rom
#FETCH_ROMS	= vic20-basic.rom vic20-kernal.rom vic20-chargen.rom clcd-u105.rom clcd-u102.rom clcd-u104.rom clcd-u103.rom c65-system.rom c64-chargen.rom c64-kernal.rom c64-basic.rom primo-b64.rom
FETCH_ROMS	= $(shell awk '{ print $$1 }' $(URL_LIST))

all: roms $(OUR_ROMS)

clean:
	rm -f *.o

distclean:
	$(MAKE) clean
	rm -f $(FETCH_ROMS)

kickup.cdata: KICKUP.M65
	../build/bin2values.py $< $@

roms: $(FETCH_ROMS)

%.rom: %.asm
	cl65 -t none --cpu 65c02 -o $@ $<

%.rom:
	rm -f $@
	$(WGET) -O $@ `awk '$$1 == "$@" { print $$2 ; exit }' $(URL_LIST)` || { rm -f $@ ; false; }

.PHONY: all clean distclean roms kickup.cdata
