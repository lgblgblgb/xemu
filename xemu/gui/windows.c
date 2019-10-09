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


/* ---------------------------------------- Windows STUFFS based on Win32 native APIs ---------------------------------------- */

#include <windows.h>


static int xemuwingui_init ( void )
{
	is_xemugui_ok = 1;
	return 0;
}


#if 0
static int xemuwingui_iteration ( void )
{
	return 0;
}
#endif

static int xemuwingui_file_selector ( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size )
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
		DEBUGGUI("GUI: file selector (Windows) error code: %04Xh for HWND owner %p" NL, err, ofn.hwndOwner);
		if (err)
			ERROR_WINDOW("Windows CommDlgExtendedError: %04Xh for HWND owner %p", err, ofn.hwndOwner);
	} else
		store_dir_from_file_selection(default_dir, selected, dialog_mode);
	//xemunativegui_iteration();
	xemu_drop_events();
	return res;
}


static const struct xemugui_descriptor_st xemuwingui_descriptor = {
	"windows",					// name
	"Windows API based Xemu UI implementation",	// desc
	xemuwingui_init,
	NULL,						// shutdown (we don't need shutdown for windows?)
	NULL,						// iteration (we don't need iteration for windows?)
	xemuwingui_file_selector,
	NULL						// popup FIXME: not implemented yet!
};
