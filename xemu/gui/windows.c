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
#include <SDL_syswm.h>

static struct {
	int num_of_hmenus;
	int num_of_items;
	HMENU hmenus[XEMUGUI_MAX_SUBMENUS];
	const struct menu_st *items[XEMUGUI_MAX_ITEMS];
	HWND win_hwnd;
	int problem;
} xemuwinmenu;


static int xemuwingui_init ( void )
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo(sdl_win, &info);
	xemuwinmenu.win_hwnd = info.info.win.window;
	is_xemugui_ok = 1;
	xemuwinmenu.num_of_hmenus = 0;
	xemuwinmenu.num_of_items = 0;
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
	ofn.hwndOwner = xemuwinmenu.win_hwnd; // FIXME: it should be this way, though it seems sometimes works better with the value 0 ...
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




static HMENU _wingui_recursive_menu_builder ( const struct menu_st desc[] )
{
	HMENU menu = CreatePopupMenu();
	if (!menu) {
		ERROR_WINDOW("CreatePopupMenu() failed in menu builder!");
		goto PROBLEM;
	}
	if (xemuwinmenu.num_of_hmenus >= XEMUGUI_MAX_SUBMENUS)
		FATAL("GUI: too many submenus!");
	xemuwinmenu.hmenus[xemuwinmenu.num_of_hmenus++] = menu;
	int radio_begin = xemuwinmenu.num_of_items;
	int radio_active = xemuwinmenu.num_of_items; // radio active is a kinda odd name, but never mind ...
	for (int a = 0; desc[a].name; a++) {
		if (!desc[a].handler || !desc[a].name) {
			DEBUGPRINT("GUI: invalid meny entry found, skipping it" NL);
			continue;
		}
		if (xemuwinmenu.num_of_items >= XEMUGUI_MAX_ITEMS)
			FATAL("GUI: too many items in menu builder!");
		int ret = 1, type = desc[a].type;
		switch (type & 0xFF) {
			case XEMUGUI_MENUID_SUBMENU: {
				HMENU submenu = _wingui_recursive_menu_builder(desc[a].handler);	// that's a prime example for using recursion :)
				if (!submenu)
					goto PROBLEM;
				ret = AppendMenu(menu, MF_POPUP, (UINT_PTR)submenu, desc[a].name);
				}
				break;
			case XEMUGUI_MENUID_CALLABLE:
				if ((type & XEMUGUI_MENUFLAG_QUERYBACK)) {
					DEBUGGUI("GUI: query-back for \"%s\"" NL, desc[a].name);
					((xemugui_callback_t)(desc[a].handler))(&desc[a], &type);
				}
				xemuwinmenu.items[xemuwinmenu.num_of_items] = &desc[a];
				// Note the +1 for ID. That is because some stange Windows sting:
				// TrackPopupMenu() with TPM_RETURNCMD returns zero as error/no-selection ...
				// So we want to being with '1' that's why the PRE-incrementation instead of the POST
				ret = AppendMenu(menu, MF_STRING, ++xemuwinmenu.num_of_items, desc[a].name);
				break;
			default:
				break;
		}
		if (!ret) {
			ERROR_WINDOW("AppendMenu() failed in menu builder!");
			goto PROBLEM;
		}
		if ((type & XEMUGUI_MENUFLAG_CHECKED))
			CheckMenuItem(menu, xemuwinmenu.num_of_items, MF_CHECKED);
		if ((type & XEMUGUI_MENUFLAG_SEPARATOR))
			AppendMenu(menu, MF_SEPARATOR, 0, NULL);
		if ((type & XEMUGUI_MENUFLAG_BEGIN_RADIO)) {
			radio_begin = xemuwinmenu.num_of_items;
			radio_active = xemuwinmenu.num_of_items;
		}
		if ((type & XEMUGUI_MENUFLAG_ACTIVE_RADIO))
			radio_active = xemuwinmenu.num_of_items;
		if ((type & XEMUGUI_MENUFLAG_END_RADIO)) {
			CheckMenuRadioItem(menu, radio_begin, xemuwinmenu.num_of_items, radio_active, MF_BYCOMMAND);
			radio_begin = xemuwinmenu.num_of_items;
			radio_active = xemuwinmenu.num_of_items;
		}
	}
	return menu;
PROBLEM:
	xemuwinmenu.problem = 1;
	return NULL;
}


static void _wingui_destroy_menu ( void )
{
	while (xemuwinmenu.num_of_hmenus > 0) {
		int ret = DestroyMenu(xemuwinmenu.hmenus[--xemuwinmenu.num_of_hmenus]);
		DEBUGGUI("GUI: destroyed menu at %p, retval = %d" NL, xemuwinmenu.hmenus[xemuwinmenu.num_of_hmenus], ret);
	}
	xemuwinmenu.num_of_hmenus = 0;
	xemuwinmenu.num_of_items = 0;
}


static HMENU _wingui_create_popup_menu ( const struct menu_st desc[] )
{
	_wingui_destroy_menu();
	xemuwinmenu.problem = 0;
	HMENU menu = _wingui_recursive_menu_builder(desc);
	if (!menu || xemuwinmenu.problem) {
		_wingui_destroy_menu();
		return NULL;
	} else
		return menu;
}


static inline void _wingui_getmousecoords ( POINT *point )
{
	int x, y;
	SDL_GetMouseState(&x, &y);
	point->x = x;	// we must play with these, since Windows uses point struct members type long (or WTF and why ...), rather than int ...
	point->y = y;
}


static void _wingui_callback ( const struct menu_st *item )
{
	DEBUGGUI("Return value = %s" NL, item->name);
	if ((item->type & 0xFF) == XEMUGUI_MENUID_CALLABLE && item->handler)
		((xemugui_callback_t)(item->handler))(item, NULL);
}


// Compared to the GTK version, it seems Windows is unable to do async GUI so it's a blocking implementation :(
static int xemuwingui_popup ( const struct menu_st desc[] )
{
	DEBUGGUI("GUI: WIN: popup!" NL);
	HMENU menu = _wingui_create_popup_menu(desc);
	if (!menu)
		return 1;
	int num_of_items = xemuwinmenu.num_of_items;
	POINT point;
	_wingui_getmousecoords(&point);
	if (!ClientToScreen(xemuwinmenu.win_hwnd, &point)) {
		// ClientToScreen returns with non-zero if it's OK!!!!
		_wingui_destroy_menu();
		ERROR_WINDOW("ClientToScreen returned with zero");
		return 1;
	}
	int n = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, xemuwinmenu.win_hwnd, NULL);	// TPM_RETURNCMD causes to return with the selected ID (ie the 3rd param of AppendMenu)
	xemu_drop_events();
	DEBUGGUI("Returned: items=%d RETVAL=%d" NL, num_of_items, n);
	// again, do not forget that IDs are from one in windows, since zero means non-selection or error!
	if (n > 0 && n <= num_of_items) {
		const struct menu_st *item = xemuwinmenu.items[n - 1];
		_wingui_destroy_menu();
		_wingui_callback(item);
	} else
		_wingui_destroy_menu();
	xemu_drop_events();
	return 0;
}




static const struct xemugui_descriptor_st xemuwingui_descriptor = {
	"windows",					// name
	"Windows API based Xemu UI implementation",	// desc
	xemuwingui_init,
	NULL,						// shutdown (we don't need shutdown for windows?)
	NULL,						// iteration (we don't need iteration for windows?)
	xemuwingui_file_selector,
	xemuwingui_popup
};
