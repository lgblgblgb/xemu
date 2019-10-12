/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
	DEBUGPRINT("UI: drop event, file: %s" NL, fn);
	if (ARE_YOU_SURE("Shall I try to mount the dropped file as D81 for you?"))
		attach_d81(fn);
}
#endif




static void attach_d81_by_browsing ( void )
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




static void do_menu ( int n )
{
	switch (n) {
		case 0:
			reset_mega65();
			break;
		case 1:
			if (ARE_YOU_SURE(NULL)) {
				SDL_Event ev;
				ev.type = SDL_QUIT;
				SDL_PushEvent(&ev);
				//exit(0);
			}
			break;
		case 2:
			xemu_set_full_screen(-1);
			break;
		case 3:
			xemuexec_open_native_file_browser(sdl_pref_dir);
			break;
		case 4:
			sysconsole_toggle(-1);
			break;
		case 5:
			attach_d81_by_browsing();
			break;
		default:
			break;
	}
}


static void menuitem_response( struct menu_st *m )
{
	do_menu((int)(uintptr_t)m->user_data);
}


static const struct menu_st menu_subsystem[] = {
	{ "ItemSub1", XEMUGUI_MENUID_CALLABLE, menuitem_response,"SUB1" },
	{ "ItemSub2", XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_BEGIN_RADIO, menuitem_response,"SUB2" },
	{ "ItemSub3", XEMUGUI_MENUID_CALLABLE, menuitem_response,"SUB1" },
	{ "ItemSub4", XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_END_RADIO, menuitem_response,"SUB2" },
	{ "ItemSub5", XEMUGUI_MENUID_CALLABLE, menuitem_response,"SUB1" },
	{ "ItemSub6", XEMUGUI_MENUID_CALLABLE, menuitem_response,"SUB2" },
	{ "ItemSub7", XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_SEPARATOR, menuitem_response,"SUB1" },
	{ "ItemSub8", XEMUGUI_MENUID_CALLABLE, menuitem_response,"SUB2" },
	{ NULL }
};

static const struct menu_st menu_system[] = {
	{ "Reset M65",  XEMUGUI_MENUID_CALLABLE, menuitem_response, (void*)0 },
	{ "Fullscreen", XEMUGUI_MENUID_CALLABLE, menuitem_response, (void*)2 },
	{ "Attach D81", XEMUGUI_MENUID_CALLABLE, menuitem_response, (void*)5 },
	{ "Browse dir", XEMUGUI_MENUID_CALLABLE, menuitem_response, (void*)3 },
#ifdef _WIN32
	{ "Console on/off", XEMUGUI_MENUID_CALLABLE, menuitem_response, (void*)4 },
#endif
	{ "Submenu-test", XEMUGUI_MENUID_SUBMENU, menu_subsystem, menu_subsystem },
	{ "Quit", XEMUGUI_MENUID_CALLABLE, menuitem_response, (void*)1 },
	{ NULL }
};


void ui_enter ( void )
{
	DEBUGPRINT("UI: handler has been called." NL);
	if (xemugui_popup(menu_system)) {
		DEBUGPRINT("UI: GUI POPUP seems to be not working, using a question window instead ..." NL);
		do_menu(QUESTION_WINDOW("Reset|Quit|Fullscr|Pref.dir|Console|Attach D81", "Xemu Quick Task Menu"));
	}
}
