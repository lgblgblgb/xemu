## XEmu: collection of simple emulators
##
## Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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
SHELL		= sh
AWK		= awk
CL65		= cl65
URL_LIST	= rom-fetch-list.txt
OUR_ROMS	= vic20-emulator-tool.rom c65-minimal-rom.rom clcd-u104-parasite.rom mega65-xemu-stub-rom.rom
OUR_INCS	= c65-minimal-rom.cdata mega65-xemu-stub-rom.cdata

all:	$(OUR_ROMS) $(OUR_INCS)

clean:
	rm -f *.o

distclean:
	$(MAKE) clean
	rm -f `$(AWK) 'NF == 3 && $$1 ~ /^[a-zA-Z0-9]/ { print $$1 }' < $(URL_LIST)`

%.rom: %.asm Makefile
	@echo "<<< Crafting $@ >>>"
	$(CL65) -t none -o $@ $<
	rm -f $(<:.asm=.o)

%.cdata: %.rom Makefile
	@echo "<<< Crafting $@ >>>"
	cat $< | xxd -i > $@

fetchroms:
	$(AWK) 'NF == 3 && $$1 ~ /^[a-zA-Z0-9]/ { print "test -s " $$1 " || { rm -f " $$1 ".tmp && $(WGET) -O " $$1 ".tmp " $$2 " && mv " $$1 ".tmp " $$1 "; } || exit $$?" }' < $(URL_LIST) | $(SHELL)

.PHONY: all clean distclean fetchroms
