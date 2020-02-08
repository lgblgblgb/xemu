/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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
#include "mega65.h"
#include "xemu/emutools_files.h"
#include "xemu/d81access.h"
#include "sdcard.h"


//#if defined(CONFIG_DROPFILE_CALLBACK) || defined(XEMU_GUI)

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


static void ui_native_os_file_browser ( void )
{
	xemuexec_open_native_file_browser(sdl_pref_dir);
}


#ifdef BASIC_TEXT_SUPPORT
#include "xemu/basic_text.h"
#include "memory_mapper.h"

static void ui_save_basic_as_text ( void )
{
	Uint8 *start = main_ram + 0x2001;
	Uint8 *end = main_ram + 0x4000;
	Uint8 *buffer;
	int size = xemu_basic_to_text_malloc(&buffer, 1000000, start, 0x2001, end, 0, 0);
	if (size < 0)
		return;
	if (size == 0) {
		INFO_WINDOW("BASIC memory is empty.");
		free(buffer);
		return;
	}
	printf("%s", buffer);
	FILE *f = fopen("/tmp/prgout.txt", "wb");
	if (f) {
		fwrite(buffer, size, 1, f);
		fclose(f);
	}
	size = SDL_SetClipboardText((const char *)buffer);
	free(buffer);
	if (size)
		ERROR_WINDOW("Cannot set clipboard: %s", SDL_GetError());
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
	{ "Reset M65",  		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_mega65 },
	{ "Attach D81",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_attach_d81_by_browsing },
#ifdef BASIC_TEXT_SUPPORT
	{ "Save BASIC as text",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_save_basic_as_text },
#endif
	{ "Browse system folder",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_native_os_file_browser },
#ifdef _WIN32
	{ "System console", XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK, xemugui_cb_sysconsole, NULL },
#endif
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
