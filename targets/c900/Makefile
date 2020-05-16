## Collection of *simple* emulators of some 8 bits machines using SDL2 library,
## including the Commodore LCD and Commodore 65 too. This is now a try to
## emulate the Z8001 based Commodore 900 :-O I said, "a try" ...
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

TARGET		= c900
PRG_TARGET	= xc900
EMU_DESCRIPTION	= Commodore 900

SRCS_TARGET_xc900	= commodore_900.c z8k1.c z8010.c
SRCS_COMMON_xc900	= emutools.c emutools_config.c emutools_files.c
CFLAGS_TARGET_xc900	= $(SDL2_CFLAGS) $(MATH_CFLAGS)
LDFLAGS_TARGET_xc900	= $(SDL2_LIBS) $(MATH_LIBS)

include ../../build/Makefile.common
