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
#OUR_ROMS	= clcd-u104-parasite.rom vic20-emulator-tool.rom
OUR_ROMS	= vic20-emulator-tool.rom
FETCH_ROMS	= vic20-basic.rom vic20-kernal.rom vic20-chargen.rom clcd-u105.rom clcd-u102.rom clcd-u104.rom clcd-u103.rom c65-system.rom c64-chargen.rom c64-kernal.rom c64-basic.rom primo-b64.rom
MEGA65IMG	= mega65.img
MEGA65IMGSIZE	= 64 	# size in MBytes (MibiBytes ... heh)


all: roms $(OUR_ROMS)

clean:
	rm -f *.o 20150716ddr.bit

distclean:
	$(MAKE) clean
	rm -f $(FETCH_ROMS) $(MEGA65IMG)

kickup.cdata:
	./bin2values.py KICKUP.M65 $@

$(MEGA65IMG): Makefile ../build/create-sdcard-image c64-chargen.rom c65-system.rom
	@echo "*** SORRY, THIS IS NOT YET SUPPORTED"
	@false
	@echo "*** Building $(MEGA65IMG), a faked SD-card image for the Mega65 emulator"
	@echo "*** For image rebuild: delete $(MEGA65IMG) and re-issue command: make $(MEGA65IMG)"
	@echo "*** Note: various tools must be installed, watch the output for errors ..."
	@echo "*** 20150716ddr.bit is a FAKE file just to comply the rule of order of files on the SD-card"
	touch 20150716ddr.bit
	../build/create-sdcard-image $(MEGA65IMG) $(MEGA65IMGSIZE) 20150716ddr.bit c65-system.rom:MEGA65.ROM c64-chargen.rom:CHARROM.M65 +BOOTLOGO.M65 +KICKUP.M65 || { rm -f $(MEGA65IMG) 20150716ddr.bit ; false; }

roms: $(FETCH_ROMS)

vic20-basic.rom:
	$(WGET) -O $@ http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/vic20/basic.901486-01.bin
vic20-kernal.rom:
	$(WGET) -O $@ http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/vic20/kernal.901486-07.bin
vic20-chargen.rom:
	$(WGET) -O $@ http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/vic20/characters.901460-03.bin
clcd-u105.rom:
	$(WGET) -O $@ http://github.lgb.hu/xemu/files/SS-CALC-13apr-u105.bin
clcd-u102.rom:
	$(WGET) -O $@ http://github.lgb.hu/xemu/files/kizapr-u102.bin
clcd-u104.rom:
	$(WGET) -O $@ http://github.lgb.hu/xemu/files/sept-M-13apr-u104.bin
clcd-u103.rom:
	$(WGET) -O $@ http://github.lgb.hu/xemu/files/sizapr-u103.bin
c65-system.rom:
	$(WGET) -O $@ http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/c65/910111.bin
c64-chargen.rom:
	$(WGET) -O $@ http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/c64/characters.901225-01.bin
c64-kernal.rom:
	$(WGET) -O $@ http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/c64/kernal.901227-03.bin
c64-basic.rom:
	$(WGET) -O $@ http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/c64/basic.901226-01.bin
primo-b64.rom:
	$(WGET) -O $@ http://github.lgb.hu/xemu/files/pr_b64.rom

%.rom: %.asm
	cl65 -t none --cpu 65c02 -o $@ $<

.PHONY: all clean distclean roms kickup.cdata
