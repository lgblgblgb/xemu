/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2019-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include "xemu/emutools.h"
#include "xemu/emutools_gui.h"
#include <string.h>

int is_xemugui_ok = 0;


#if defined(XEMU_HAS_GTK3) || defined(XEMU_ARCH_MAC) || defined(XEMU_ARCH_WIN)
static void store_dir_from_file_selection ( char *store_dir, const char *filename, int dialog_mode )
{
	if (store_dir && (dialog_mode & XEMUGUI_FSEL_FLAG_STORE_DIR)) {
		if ((dialog_mode & 0xFF) == XEMUGUI_FSEL_DIRECTORY)
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

#include "xemu/gui/popular_user_funcs.c"

struct xemugui_descriptor_st {
	const char *name;
	const char *description;
	int	(*init)(void);
	void	(*shutdown)(void);
	int	(*iteration)(void);
	int	(*file_selector)( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size );
	int	(*popup)( const struct menu_st desc[] );
	int	(*info)(int sdl_class, const char *msg);
};

#if defined(XEMU_HAS_GTK3)
#	include "xemu/gui/gui_gtk.c"
#elif defined(XEMU_ARCH_MAC)
#	include "xemu/gui/gui_osx.c"
#elif defined(XEMU_ARCH_WIN)
#	include "xemu/gui/gui_win.c"
#endif
#include "xemu/gui/gui_nogui.c"
#include "xemu/gui/gui_osd.c"

static const struct xemugui_descriptor_st *current_gui = NULL;

static const struct xemugui_descriptor_st *xemugui_descriptor_list[] = {
#if defined(XEMU_ARCH_MAC)
	&xemuosxgui_descriptor,
#endif
#if defined (XEMU_ARCH_WIN)
	&xemuwingui_descriptor,
#endif
#if defined(XEMU_HAS_GTK3)
	&xemugtkgui_descriptor,
#endif
//	&xemuosdgui_descriptor,
	&xemunullgui_descriptor		// THIS MUST BE THE LAST ENTRY
};


int xemugui_init ( const char *name )
{
	char avail[256];	// "should be enough" (yeah, I know, the 640K ...) for some names ...
	avail[0] = 0;
	is_xemugui_ok = 0;
	if (name && name[0] == '\0')	// make sure that empty string means the same as NULL pointer passed
		name = NULL;
	for (int a = 0 ;; a++) {
		strcat(avail, " ");
		strcat(avail, xemugui_descriptor_list[a]->name);
		if (name && !strcasecmp(xemugui_descriptor_list[a]->name, name)) {
			current_gui = xemugui_descriptor_list[a];
			break;
		}
		if (xemugui_descriptor_list[a] == &xemunullgui_descriptor) {
			current_gui = xemugui_descriptor_list[0];
			if (name)
				ERROR_WINDOW(
					"Requested GUI (\"%s\") cannot be found, using \"%s\" instead.\nAvailable GUI implementations:%s",
					name, current_gui->name, avail
				);
			else
				DEBUGPRINT("GUI: no GUI was specified, using the first available one from this list:%s" NL, avail);
			break;
		}
	}
	DEBUGPRINT("GUI: using \"%s\" (%s)" NL, current_gui->name, current_gui->description);
	return current_gui->init ? current_gui->init() : 1;
}


void xemugui_shutdown ( void )
{
	if (current_gui && current_gui->shutdown)
		current_gui->shutdown();
}


int xemugui_iteration ( void )
{
	return (current_gui && current_gui->iteration) ? current_gui->iteration() : 0;
}


int xemugui_file_selector ( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size )
{
	if (current_gui && current_gui->file_selector)
		return current_gui->file_selector(dialog_mode, dialog_title, default_dir, selected, path_max_size);
	else
		return 1;
}


int xemugui_popup ( const struct menu_st desc[] )
{
	if (!current_gui) {
		ERROR_WINDOW("GUI hasn't been initialized yet, cannot pop menu.");
		return 1;
	}
	if (!current_gui->popup) {
#ifndef GUI_HAS_POPUP
		ERROR_WINDOW("GUI support is not compiled for this Xemu build for any GUI backend which can pop menu");
		return 1;
#endif
		if (current_gui == &xemunullgui_descriptor)
			ERROR_WINDOW("The 'none' GUI is used. It does not support popping menu");
		else
			ERROR_WINDOW("Current GUI backend (%s) does not support popping menu", current_gui->name);
		return 1;
	}
	return current_gui->popup(desc);
}


int xemugui_info ( int sdl_class, const char *msg )
{
	if (current_gui && current_gui->info)
		return current_gui->info(sdl_class, msg);
	else
		return 1;
}
