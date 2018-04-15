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
SRCS_TARGET_xmega65	= mega65.c sdcard.c uart_monitor.c hypervisor.c m65_snapshot.c memory_mapper.c io_mapper.c vic4.c ethernet65.c input_devices.c
SRCS_COMMON_xmega65	= emutools.c cpu65.c cia6526.c emutools_hid.c sid.c f011_core.c f018_core.c c64_kbd_mapping.c emutools_config.c emutools_snapshot.c emutools_files.c emutools_umon.c ethertap.c
CONFIG_CFLAGS_TARGET_xmega65	= sdl2|math|socketapi
CONFIG_LDFLAGS_TARGET_xmega65	= sdl2|math|socketapi

include ../../build/Makefile.common

character_wom_initialized.c:
	rm -f $@ $(@).tmp
	../../build/m65-import-charset < ../../../mega65-core/src/vhdl/charrom.vhdl > $(@).tmp
	mv $(@).tmp $@
