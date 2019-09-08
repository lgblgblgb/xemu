/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifdef _WIN32
#	include <windows.h>
#elif defined(HAVE_GTK3)
#	include <gtk/gtk.h>
#else
#	warning "emutools_nativegui is being compiled in, but could not find backend (Windows or GTK3 libs)"
#endif

// FIXME: very ugly hack for ep128 emulator which uses its own implemtnation of things still ...
#ifndef DO_NOT_INCLUDE_EMUTOOLS
#include "xemu/emutools.h"
#endif

#include "xemu/emutools_nativegui.h"


int is_xemunativegui_ok = 0;


#ifdef XEMU_NATIVEGUI
static void store_dir_from_file_selection ( char *store_dir, const char *filename, int dialog_mode )
{
	if (store_dir && (dialog_mode & XEMUNATIVEGUI_FSEL_FLAG_STORE_DIR)) {
		if ((dialog_mode & 0xFF) == XEMUNATIVEGUI_FSEL_DIRECTORY)
			strcpy(store_dir, filename);
		else {
			char *p = strrchr(filename, DIRSEP_CHR);
			if (p) {
				memcpy(store_dir, filename, p - filename + 1);
				store_dir[p - filename + 1] = '\0';
			}
		}
	}
}
#endif



#ifdef _WIN32
#	include "xemu/gui/windows.c"
#elif defined(HAVE_GTK3)
#	include "xemu/gui/gtk.c"
#else
#	include "xemu/gui/nogui.c"
#endif
