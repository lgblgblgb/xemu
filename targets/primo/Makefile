## Collection of *simple* emulators of some 8 bits machines using SDL2 library,
## including the Commodore LCD and Commodore 65 too.
##
## Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

TARGET		= primo
PRG_TARGET	= xprimo

CFLAGS_TARGET_xprimo	=
SRCS_TARGET_xprimo	= primo.c
SRCS_COMMON_xprimo	= emutools.c emutools_hid.c z80.c emutools_config.c
CONFIG_CFLAGS_TARGET_xprimo	= sdl2
CONFIG_LDFLAGS_TARGET_xprimo	= sdl2

include ../../build/Makefile.common
