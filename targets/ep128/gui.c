/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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
#	define XEP128_HWND sdl_wminfo.info.win.window
#elif defined(XEP128_GTK)
#	include <gtk/gtk.h>
#endif

#include "xep128.h"
#include "gui.h"
#define XEP128_NEED_SDL_WMINFO
#include "screen.h"
#include "fileio.h"

#if defined(_WIN32) || defined (XEP128_GTK)
#define XEP128_GUI_C
#endif


#ifdef XEP128_GUI_C
static void store_dir_from_file_selection ( char *store_dir, const char *filename, int dialog_mode )
{
	if (store_dir && (dialog_mode & XEPGUI_FSEL_FLAG_STORE_DIR)) {
		if ((dialog_mode & 0xFF) == XEPGUI_FSEL_DIRECTORY)
			strcpy(store_dir, filename);
		else {
			char *p = strrchr(filename, DIRSEP[0]);
			if (p) {
				memcpy(store_dir, filename, p - filename + 1);
				store_dir[p - filename + 1] = '\0';
			}
		}
	}
}
#endif



#ifdef _WIN32

/* ---------------------------------------- Windows STUFFS based on Win32 native APIs ---------------------------------------- */


int xepgui_init ( void )
{
	return 0;
}


void xepgui_iteration ( void )
{
}


int xepgui_file_selector ( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size )
{
	int res;
	OPENFILENAME ofn;		// common dialog box structure
	ZeroMemory(&ofn, sizeof ofn);
	ofn.lStructSize = sizeof ofn;
	ofn.hwndOwner = 0; // XEP128_HWND;
	ofn.lpstrFile = selected;
	*selected = '\0';	// sets to zero, since it seems windows dialog handler also used this as input?
	ofn.nMaxFile = path_max_size;
	ofn.lpstrFilter = NULL; // "All\0*.*\0Text\0*.TXT\0";
	ofn.nFilterIndex = 0;	// 1
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = default_dir ? default_dir : NULL;
	ofn.lpstrTitle = dialog_title;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
	res = !GetOpenFileName(&ofn);
	if (res) {
		int err = CommDlgExtendedError();
		*selected = '\0';
		DEBUG("GUI: file selector (Windows) error code: %04Xh for HWND owner %p" NL, err, ofn.hwndOwner);
		if (err)
			ERROR_WINDOW("Windows CommDlgExtendedError: %04Xh for HWND owner %p", err, ofn.hwndOwner);
	} else
		store_dir_from_file_selection(default_dir, selected, dialog_mode);
	xepgui_iteration();
	sdl_burn_events();
	return res;
}


#elif defined(XEP128_GTK)
/* ---------------------------------------- LINUX/UNIX STUFFS based on GTK ---------------------------------------- */


int xepgui_init ( void )
{
	if (!gtk_init_check(NULL, NULL)) {
		ERROR_WINDOW("Cannot initialize GTK");
		return 1;
	}
	xepgui_iteration();	// consume possible peding (if any?) GTK stuffs after initialization - maybe not needed at all?
	return 0;
}


void xepgui_iteration ( void )
{
	while (gtk_events_pending())
		gtk_main_iteration();
}


int xepgui_file_selector ( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size )
{
	GtkWidget *dialog;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	gint res;
	dialog = gtk_file_chooser_dialog_new(dialog_title,
		NULL, // parent window!
		action,
		"_Cancel",
		GTK_RESPONSE_CANCEL,
		"_Open",
		GTK_RESPONSE_ACCEPT,
		NULL
	);
	if (default_dir)
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), default_dir);
	*selected = '\0';
	res = gtk_dialog_run(GTK_DIALOG(dialog));
	if (res == GTK_RESPONSE_ACCEPT) {
		char *filename;
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		if (strlen(filename) < path_max_size) {
			strcpy(selected, filename);
			store_dir_from_file_selection(default_dir, filename, dialog_mode);
		} else
			res = GTK_RESPONSE_CANCEL;
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
	xepgui_iteration();
	sdl_burn_events();
	return res != GTK_RESPONSE_ACCEPT;
}
#else
/* ----------------------------- NO GUI IS AVAILABLE ---------------------- */

int xepgui_init ( void ) {
	return 0;
}

void xepgui_iteration ( void ) {
}

int xepgui_file_selector ( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size ) {
	return 1;
}

#endif
