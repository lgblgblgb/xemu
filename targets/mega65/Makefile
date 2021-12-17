## A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
## Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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

TARGET		= mega65
PRG_TARGET	= xmega65
EMU_DESCRIPTION	= MEGA65

SRCS_TARGET_xmega65	= configdb.c mega65.c sdcard.c uart_monitor.c hypervisor.c m65_snapshot.c memory_mapper.c io_mapper.c vic4.c vic4_palette.c ethernet65.c input_devices.c memcontent.c ui.c fat32.c sdcontent.c audio65.c inject.c dma65.c rom.c
SRCS_COMMON_xmega65	= emutools.c cpu65.c cia6526.c emutools_hid.c sid.c f011_core.c c64_kbd_mapping.c emutools_config.c emutools_snapshot.c emutools_files.c emutools_umon.c emutools_socketapi.c ethertap.c d81access.c emutools_gui.c basic_text.c opl3.c lodepng.c
CFLAGS_TARGET_xmega65	= $(SDL2_CFLAGS) $(MATH_CFLAGS) $(SOCKET_CFLAGS) $(XEMUGUI_CFLAGS)
LDFLAGS_TARGET_xmega65	= $(SDL2_LIBS) $(MATH_LIBS) $(SOCKET_LIBS) $(XEMUGUI_LIBS)

MEGA65_CORE	= ../../../mega65-core
MEGA65_FILES	= bin/charrom.bin bin/HICKUP.M65 bin/COLOURRAM.BIN sdcard-files/BANNER.M65 sdcard-files/ONBOARD.M65 src/open-roms/bin/mega65.rom
MEGA65_FILES	+= sdcard-files/FREEZER.M65
MEGA65_FILES	+= sdcard-files/AUDIOMIX.M65 sdcard-files/C64THUMB.M65 sdcard-files/C65THUMB.M65 sdcard-files/ROMLOAD.M65 sdcard-files/SPRITED.M65

MEGA65_FP_FILES	= $(addprefix $(MEGA65_CORE)/,$(MEGA65_FILES))
MEGA65_LOCALDIR	= .sys-files-mega65-core

include ../../build/Makefile.common

# mega65-core has very tricky build system, it's very hard to be sure, that everything
# is up-to-date, this is the reason for this mess
recreatememcontent:
	@echo "*** Compiling needed files from mega65-core at $(MEGA65_CORE) ***"
	$(MAKE) -C $(MEGA65_CORE) clean
	for dir in src/mega65-freezemenu src/mega65-fdisk src/open-roms ; do $(MAKE) -C $(MEGA65_CORE)/$$dir clean || exit 1 ; done
	find $(MEGA65_CORE) -name "*.o" | xargs -n 100 rm -f
	find $(MEGA65_CORE) -name "*.M65" | xargs -n 100 rm -f
	mkdir -p $(MEGA65_CORE)/sdcard-files
	rm -f `echo $(MEGA65_FP_FILES) | fgrep -v mega65.rom`
	rm -f $(MEGA65_CORE)/sdcard-files/* $(MEGA65_CORE)/src/mega65-fdisk/*.prg $(MEGA65_CORE)/src/utilities/*.prg
	$(MAKE) -C $(MEGA65_CORE)/src/mega65-fdisk m65fdisk.prg
	$(MAKE) -C $(MEGA65_CORE)/src/mega65-freezemenu
	$(MAKE) -C $(MEGA65_CORE) freezer_files
	$(MAKE) -C $(MEGA65_CORE) $(MEGA65_FILES)
	mkdir -p $(MEGA65_LOCALDIR)
	rm -f $(MEGA65_LOCALDIR)/*
	cp $(MEGA65_FP_FILES) $(MEGA65_LOCALDIR)/
	@echo "*** Generating C files from mega65-core binary results ***"
	python3 $(TOPDIR)/build/m65-memcontent-generator.py memcontent.c memcontent.h $(MEGA65_FP_FILES)
