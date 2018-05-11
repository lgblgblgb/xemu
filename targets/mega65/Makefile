## A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
## Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
## Copyright (C)2016-2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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
EMU_DESCRIPTION	= Mega 65

CFLAGS_TARGET_xmega65	=
SRCS_TARGET_xmega65	= mega65.c sdcard.c uart_monitor.c hypervisor.c m65_snapshot.c memory_mapper.c io_mapper.c vic4.c ethernet65.c input_devices.c memcontent.c
SRCS_COMMON_xmega65	= emutools.c cpu65.c cia6526.c emutools_hid.c sid.c f011_core.c f018_core.c c64_kbd_mapping.c emutools_config.c emutools_snapshot.c emutools_files.c emutools_umon.c ethertap.c
CONFIG_CFLAGS_TARGET_xmega65	= sdl2|math|socketapi
CONFIG_LDFLAGS_TARGET_xmega65	= sdl2|math|socketapi

include ../../build/Makefile.common

recreatememcontent:
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
	../../build/bin2values.py ../../../mega65-core/bin/KICKUP.M65 - >> memcontent.c.tmp
	echo "};" >> memcontent.c.tmp
	echo "Uint8 meminitdata_chrwom[MEMINITDATA_CHRWOM_SIZE] = {" >> memcontent.c.tmp
	gawk '/^[\t ]*[xX]"/ { $$0 = toupper($$0); gsub("X\"","0x",$$0); gsub("\"","",$$0); print "\t" $$0 }' < ../../../mega65-core/src/vhdl/charrom.vhdl >> memcontent.c.tmp
	echo "};" >> memcontent.c.tmp
	echo "Uint8 meminitdata_cramutils[MEMINITDATA_CRAMUTILS_SIZE] = {" >> memcontent.c.tmp
	../../build/bin2values.py ../../../mega65-core/bin/COLOURRAM.BIN - >> memcontent.c.tmp
	echo "};" >> memcontent.c.tmp
	echo "Uint8 meminitdata_banner[MEMINITDATA_BANNER_SIZE] = {" >> memcontent.c.tmp
	../../build/bin2values.py ../../../mega65-core/sdcard-files/BANNER.M65 - >> memcontent.c.tmp
	echo "};" >> memcontent.c.tmp
	mv memcontent.c.tmp memcontent.c
