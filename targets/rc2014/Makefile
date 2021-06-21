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

TARGET		= rc2014
PRG_TARGET	= xrc2014
EMU_DESCRIPTION	= RC2014 SBCs

CFLAGS_TARGET_xrc2014	=
SRCS_TARGET_xrc2014	= rc2014.c hardware.c console.c uart.c fake_rom.c
SRCS_COMMON_xrc2014	= emutools.c emutools_hid.c z80.c z80_dasm.c emutools_config.c emutools_files.c
CFLAGS_TARGET_xrc2014	= $(SDL2_CFLAGS)
LDFLAGS_TARGET_xrc2014	= $(SDL2_LIBS)

include ../../build/Makefile.common
