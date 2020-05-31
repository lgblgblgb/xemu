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
#include "ui.h"
#include "xemu/emutools_gui.h"
#include "xemu/emutools_files.h"
#include "xemu/d81access.h"
#include "commodore_65.h"


//#if defined(CONFIG_DROPFILE_CALLBACK) || defined(XEMU_GUI)

#if 0
static int attach_d81 ( const char *fn )
{
	if (fd_mounted) {
		if (mount_external_d81(fn, 0)) {
			ERROR_WINDOW("Mount failed for some reason.");
			return 1;
		} else {
			DEBUGPRINT("UI: file seems to be mounted successfully as D81: %s" NL, fn);
			return 0;
		}
	} else {
		ERROR_WINDOW("Cannot mount external D81, since Mega65 was not instructed to mount any FD access yet.");
		return 1;
	}
}
#endif


static int attach_d81 ( const char *fn )
{
	if (fn && *fn)
		return d81access_attach_fsobj(fn, D81ACCESS_IMG | D81ACCESS_PRG | D81ACCESS_DIR | D81ACCESS_AUTOCLOSE);
	return -1;
}


// end of #if defined(CONFIG_DROPFILE_CALLBACK) || defined(XEMU_GUI_C)
//#endif


#ifdef CONFIG_DROPFILE_CALLBACK
void emu_dropfile_callback ( const char *fn )
{
	DEBUGGUI("UI: drop event, file: %s" NL, fn);
	if (ARE_YOU_SURE("Shall I try to mount the dropped file as D81 for you?"))
		attach_d81(fn);
}
#endif


#if 1
static void ui_attach_d81_by_browsing ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select D81 to attach",
		dir,
		fnbuf,
		sizeof fnbuf
	))
		attach_d81(fnbuf);
	else
		DEBUGPRINT("UI: file selection for D81 mount was cancalled." NL);
}
#endif


#if 0
static void ui_native_os_file_browser ( void )
{
	xemuexec_open_native_file_browser(sdl_pref_dir);
}
#endif


static const struct menu_st menu_display[] = {
	{ "Fullscreen",    XEMUGUI_MENUID_CALLABLE, xemugui_cb_windowsize, (void*)0 },
	{ "Window - 100%", XEMUGUI_MENUID_CALLABLE, xemugui_cb_windowsize, (void*)1 },
	{ "Window - 200%", XEMUGUI_MENUID_CALLABLE, xemugui_cb_windowsize, (void*)2 },
	{ NULL }
};


static const struct menu_st menu_main[] = {
	{ "Display",			XEMUGUI_MENUID_SUBMENU,		menu_display, NULL },
	{ "Reset C65",  		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, c65_reset_asked },
	{ "Attach D81",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_attach_d81_by_browsing },
//	{ "Browse system folder",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_native_os_file_browser },
#ifdef XEMU_ARCH_WIN
	{ "System console", XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK, xemugui_cb_sysconsole, NULL },
#endif
	{ "About", XEMUGUI_MENUID_CALLABLE, xemugui_cb_about_window, NULL },
	{ "Quit", XEMUGUI_MENUID_CALLABLE, xemugui_cb_call_quit_if_sure, NULL },
	{ NULL }
};


void ui_enter ( void )
{
	DEBUGGUI("UI: handler has been called." NL);
	if (xemugui_popup(menu_main)) {
		DEBUGPRINT("UI: oops, POPUP does not worked :(" NL);
	}
}
