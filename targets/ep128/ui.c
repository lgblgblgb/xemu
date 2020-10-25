/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_files.h"
#include "xemu/emutools_hid.h"

#include "ui.h"

#include "enterprise128.h"
#include "nick.h"
#include "cpu.h"
#include "exdos_wd.h"


static void ui_hard_reset ( void )
{
	ep_reset();
}


#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
static void ui_screenshot ( void )
{
//	register_screenshot_request = 1;
	screenshot();
}
#endif

#if 0
static void ui_set_scale_filtering ( const struct menu_st *m, int *query )
{
	static char enabled[2] = "0";
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, (enabled[0] & 1));
	enabled[0] ^= 1;
	SDL_SetHint("SDL_HINT_RENDER_SCALE_QUALITY", enabled);
}
#endif

#ifdef CONFIG_EXDOS_SUPPORT
static void ui_attach_disk ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select floppy disk image to attach",
		dir,
		fnbuf,
		sizeof fnbuf
	))
		wd_attach_disk_image(fnbuf);
	else
		DEBUGPRINT("UI: file selection for floppy mount was cancalled." NL);
}
#endif


/**** MENU SYSTEM ****/


static const struct menu_st menu_display[] = {
	{ "Fullscreen",    		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)0 },
	{ "Window - 100%", 		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)1 },
	{ "Window - 200%", 		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_SEPARATOR,	xemugui_cb_windowsize, (void*)2 },
	{ "Enable mouse grab + emu",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_set_mouse_grab, NULL },
//	{ "Enable scale filtering",	XEMUGUI_MENUID_CALLABLE |
//					XEMUGUI_MENUFLAG_QUERYBACK,	ui_set_scale_filtering, NULL },
	{ NULL }
};
static const struct menu_st menu_debug[] = {
	{ "OSD key debugger",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_osd_key_debugger, NULL },
//	{ "Dump memory info file",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_dump_memory },
	{ "Browse system folder",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_native_os_prefdir_browser, NULL },
	{ NULL }
};
static const struct menu_st menu_reset[] = {
//	{ "Reset C65",  		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c65_mode },
//	{ "Reset into C64 mode",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c64_mode },
	{ "Reset",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ep_reset },
	{ "Hard reset",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_hard_reset },
	{ NULL }
};
static const struct menu_st menu_main[] = {
	{ "Display",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_display },
	{ "Reset", 	 		XEMUGUI_MENUID_SUBMENU,		NULL, menu_reset   },
	{ "Debug",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_debug   },
#ifdef CONFIG_EXDOS_SUPPORT
	{ "Attach floppy",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_attach_disk },
#endif
//	{ "Attach D81",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_attach_d81_by_browsing },
//	{ "Run PRG directly",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_run_prg_by_browsing },
#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
	{ "Screenshot",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_screenshot },
#endif
#ifdef XEMU_ARCH_WIN
	{ "System console",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_sysconsole, NULL },
#endif
	{ "About",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_about_window, NULL },
	{ "Quit",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_quit_if_sure, NULL },
	{ NULL }
};


void ui_enter ( void )
{
	DEBUGGUI("UI: handler has been called." NL);
	if (xemugui_popup(menu_main)) {
		DEBUGPRINT("UI: oops, POPUP does not worked :(" NL);
	}
}
