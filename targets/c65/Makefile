## Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
## Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

TARGET		= c65
PRG_TARGET	= xc65
EMU_DESCRIPTION	= Commodore 65

CFLAGS_TARGET_xc65	=
SRCS_TARGET_xc65	= configdb.c commodore_65.c vic3.c c65_snapshot.c ui.c inject.c dma65.c
SRCS_COMMON_xc65	= emutools.c cpu65.c cia6526.c sid.c f011_core.c c64_kbd_mapping.c emutools_hid.c cbmhostfs.c emutools_config.c emutools_snapshot.c emutools_files.c d81access.c emutools_gui.c lodepng.c basic_text.c
CFLAGS_TARGET_xc65	= $(SDL2_CFLAGS) $(MATH_CFLAGS) $(SOCKET_CFLAGS) $(XEMUGUI_CFLAGS)
LDFLAGS_TARGET_xc65	= $(SDL2_LIBS) $(MATH_LIBS) $(SOCKET_LIBS) $(XEMUGUI_LIBS)
LDFLAGS_TARGET_xc65_ON_html = -s STACK_SIZE=655360 --preload-file=$$HOME/.local/share/xemu-lgb/c65/c65-system.rom@/files/c65-system.rom

include ../../build/Makefile.common
