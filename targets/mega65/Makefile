## A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
## Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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

TARGET		= mega65
PRG_TARGET	= xmega65
EMU_DESCRIPTION	= MEGA65

SRCS_TARGET_xmega65	= mega65.c sdcard.c uart_monitor.c hypervisor.c m65_snapshot.c memory_mapper.c io_mapper.c vic4.c vic4_palette.c ethernet65.c input_devices.c memcontent.c ui.c fat32.c sdcontent.c audio65.c inject.c
SRCS_COMMON_xmega65	= emutools.c cpu65.c cia6526.c emutools_hid.c sid.c f011_core.c f018_core.c c64_kbd_mapping.c emutools_config.c emutools_snapshot.c emutools_files.c emutools_umon.c ethertap.c d81access.c emutools_gui.c basic_text.c
CFLAGS_TARGET_xmega65	= $(SDL2_CFLAGS) $(MATH_CFLAGS) $(SOCKET_CFLAGS) $(XEMUGUI_CFLAGS)
LDFLAGS_TARGET_xmega65	= $(SDL2_LIBS) $(MATH_LIBS) $(SOCKET_LIBS) $(XEMUGUI_LIBS)

MEGA65_CORE = ../../../mega65-core

include ../../build/Makefile.common

recreatememcontent:
	@echo "*** Creating needed files from mega65-core ..."
	$(MAKE) -C $(MEGA65_CORE) bin/HICKUP.M65
	$(MAKE) -C $(MEGA65_CORE) bin/COLOURRAM.BIN
	$(MAKE) -C $(MEGA65_CORE) sdcard-files/BANNER.M65
	$(MAKE) -C $(MEGA65_CORE) sdcard-files/FREEZER.M65
	@echo "*** About creating our C file"
	rm -f memcontent.c.tmp
	echo "/* !THIS IS A GENERATED FILE! DO NOT EDIT!" >> memcontent.c.tmp
	echo " * Instead, say 'make recreatememcontent' to re-generate this file" >> memcontent.c.tmp
	echo " * Binary data from Mega65 project. Please note, that Mega65 is" >> memcontent.c.tmp
	echo " * an open source, GNU/GPL project, like Xemu. Thus, it's valid" >> memcontent.c.tmp
	echo " * to use binaries as it's from the compiled version of Mega65" >> memcontent.c.tmp
	echo " * which is available in source form at https://github.com/MEGA65/mega65-core */" >> memcontent.c.tmp
	echo '#include "xemu/emutools.h"' >> memcontent.c.tmp
	echo '#include "memcontent.h"' >> memcontent.c.tmp
	echo "Uint8 meminitdata_kickstart[MEMINITDATA_KICKSTART_SIZE] = {" >> memcontent.c.tmp
	../../build/bin2values.py $(MEGA65_CORE)/bin/HICKUP.M65 - >> memcontent.c.tmp
	echo "};" >> memcontent.c.tmp
	echo "Uint8 meminitdata_chrwom[MEMINITDATA_CHRWOM_SIZE] = {" >> memcontent.c.tmp
	gawk '/^[\t ]*[xX]"/ { $$0 = toupper($$0); gsub("X\"","0x",$$0); gsub("\"","",$$0); print "\t" $$0 }' < $(MEGA65_CORE)/src/vhdl/charrom.vhdl >> memcontent.c.tmp
	echo "};" >> memcontent.c.tmp
	echo "Uint8 meminitdata_cramutils[MEMINITDATA_CRAMUTILS_SIZE] = {" >> memcontent.c.tmp
	../../build/bin2values.py $(MEGA65_CORE)/bin/COLOURRAM.BIN - >> memcontent.c.tmp
	echo "};" >> memcontent.c.tmp
	echo "Uint8 meminitdata_banner[MEMINITDATA_BANNER_SIZE] = {" >> memcontent.c.tmp
	../../build/bin2values.py $(MEGA65_CORE)/sdcard-files/BANNER.M65 - >> memcontent.c.tmp
	echo "};" >> memcontent.c.tmp
	echo "Uint8 meminitdata_freezer[] = {" >> memcontent.c.tmp
	../../build/bin2values.py $(MEGA65_CORE)/sdcard-files/FREEZER.M65 - >> memcontent.c.tmp
	echo "};" >> memcontent.c.tmp
	echo "const int meminitdata_freezer_size = sizeof(meminitdata_freezer);" >> memcontent.c.tmp
	cp memcontent.c memcontent.c.BAK
	@echo "*** OK. Please rename memcontent.c.tmp to memcontent.c to really apply these changes, then re-compile the emulator ***"
