## Collection of *simple* emulators of some 8 bits machines using SDL2 library,
## including the Commodore LCD and Commodore 65 too.
##
## This is my Xep128 (Enterprise-128 emulator) being ported to the Xemu project.
## Still it's mainly do many things on its own, unlike any other targets in the
## Xemu projects. Though, the build system and Z80 emulation core (Z80ex) is
## from Xemu now!
##
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

TARGET		= ep128
PRG_TARGET	= xep128
EMU_DESCRIPTION	= Enterprise 128

SRCS_TARGET_xep128	= screen.c main.c cpu.c z180.c nick.c dave.c input.c exdos_wd.c sdext.c rtc.c printer.c zxemu.c primoemu.c emu_rom_interface.c epnet.c apu.c keyboard_mapping.c configuration.c roms.c console.c emu_monitor.c joystick.c fileio.c gui.c snapshot.c
SRCS_COMMON_xep128	= z80.c z80_dasm.c emutools_socketapi.c lodepng.c
CFLAGS_TARGET_xep128	= $(SDL2_CFLAGS) $(MATH_CFLAGS) $(SOCKET_CFLAGS) $(XEMUGUI_CFLAGS) $(READLINE_CFLAGS)
LDFLAGS_TARGET_xep128	= $(SDL2_LIBS) $(MATH_LIBS) $(SOCKET_LIBS) $(XEMUGUI_LIBS) $(READLINE_LIBS)

include ../../build/Makefile.common
