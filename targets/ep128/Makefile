## Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
## Copyright (C)2016-2021,2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

TARGET		= ep128
PRG_TARGET	= xep128
EMU_DESCRIPTION	= Enterprise 128

SRCS_TARGET_xep128	= configdb.c enterprise128.c cpu.c z180.c nick.c dave.c input_devices.c exdos_wd.c sdext.c rtc.c printer.c zxemu.c primoemu.c emu_rom_interface.c epnet.c apu.c roms.c emu_monitor.c fileio.c snapshot.c ui.c
SRCS_COMMON_xep128	= emutools.c emutools_files.c emutools_config.c emutools_gui.c emutools_hid.c z80.c z80_dasm.c emutools_socketapi.c lodepng.c emutools_osk.c
CFLAGS_TARGET_xep128	= $(SDL2_CFLAGS) $(MATH_CFLAGS) $(SOCKET_CFLAGS) $(XEMUGUI_CFLAGS) $(READLINE_CFLAGS)
LDFLAGS_TARGET_xep128	= $(SDL2_LIBS) $(MATH_LIBS) $(SOCKET_LIBS) $(XEMUGUI_LIBS) $(READLINE_LIBS)

#LDFLAGS_TARGET_xep128_ON_html = -s STACK_SIZE=655360 --preload-file=$$HOME/.local/share/xemu-lgb/ep128/exos.rom@/files/exos.rom --preload-file=$$HOME/.local/share/xemu-lgb/ep128/sdcard.img@/files/sdcard.img
LDFLAGS_TARGET_xep128_ON_html = -s STACK_SIZE=655360 --preload-file=$$HOME/.local/share/xemu-lgb/ep128/exos.rom@/files/exos.rom

include ../../build/Makefile.common

web:
	$(MAKE) ARCH=html
	python3 ../../build/webserver.py 8000
	sleep .1
	xdg-open http://localhost:8000/$(PRG_TARGET).html
