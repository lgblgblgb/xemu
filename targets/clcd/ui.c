/* Commodore LCD emulator (rewrite of my world's first working Commodore LCD emulator)
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Part of the Xemu project: https://github.com/lgblgblgb/xemu

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
#include "ui.h"
#include "inject.h"
#include "commodore_lcd.h"


int register_screenshot_request = 0;



static char last_used_ui_dir[PATH_MAX + 1] = "";

static void ui_load_basic_prg ( void )
{
	char fnbuf[PATH_MAX + 1];
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Load BASIC .PRG",
		last_used_ui_dir,
		fnbuf,
		sizeof fnbuf
	)) {
		if (prg_load_prepare_inject(fnbuf, BASIC_START)) {
			return;
		}
		prg_load_do_inject(BASIC_START);
	}
}

static void ui_save_basic_prg ( void )
{
	if (prg_save_prepare_store(BASIC_START))
		return;
	char fnbuf[PATH_MAX + 1];
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_SAVE | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Save BASIC .PRG",
		last_used_ui_dir,
		fnbuf,
		sizeof fnbuf
	)) {
		xemu_save_file(fnbuf, prg_inject.data, prg_inject.size, "Cannot save BASIC PRG");
	}
}

static void ui_dump_memory ( void )
{
	char fnbuf[PATH_MAX + 1];
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_SAVE | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Dump memory content into file",
		last_used_ui_dir,
		fnbuf,
		sizeof fnbuf
	)) {
		xemu_save_file(fnbuf, memory, get_ram_size(), "Cannot dump RAM content into file");
	}
}

static void ui_cb_clock_mhz ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, get_cpu_mhz() == VOIDPTR_TO_INT(m->user_data));
	set_cpu_mhz(VOIDPTR_TO_INT(m->user_data));
}

static void ui_cb_ramsize ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, (get_ram_size() >> 10) == VOIDPTR_TO_INT(m->user_data));
	if (VOIDPTR_TO_INT(m->user_data) != (get_ram_size() >> 10) && ARE_YOU_SURE("This will RESET your machine!", i_am_sure_override | ARE_YOU_SURE_DEFAULT_YES)) {
		ramsizechange_request(VOIDPTR_TO_INT(m->user_data) << 10);
	}
}

static void ui_cb_batterylow ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, powerstatus & 0x40);
	powerstatus ^= 0x40;
}

static void ui_reset ( void )
{
	reset_request(0);
}

static void ui_reset_scrub ( void )
{
	reset_request(1);
}


// FIXME: reorganize this mess in the menu!!



static const struct menu_st menu_display[] = {
	{ "Fullscreen",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)0 },
	{ "Window - 100%",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)1 },
	{ "Window - 200%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_SEPARATOR,	xemugui_cb_windowsize, (void*)2 },
        { NULL }
};
static const struct menu_st menu_clock[] = {
	{ "1MHz",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_clock_mhz, (void*)1 },
	{ "2MHz",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_clock_mhz, (void*)2 },
	{ "4MHz",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_clock_mhz, (void*)4 },
	{ NULL }
};
static const struct menu_st menu_ramsize[] = {
	{ " 32K",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_ramsize, (void*)32  },
	{ " 64K",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_ramsize, (void*)64  },
	{ " 96K",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_ramsize, (void*)96  },
	{ "128K",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_ramsize, (void*)128 },
	{ NULL }
};
static const struct menu_st menu_advanced[] = {
	{ "Simulate battery low",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_batterylow, NULL },
	{ "Power off!",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, poweroff_request },
	{ "Reset - no scrub",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_reset },
	{ "Reset - WITH scrub!!!",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_reset_scrub },
	{ NULL }
};
static const struct menu_st menu_main[] = {
	{ "Display",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_display },
	{ "CPU clock speed",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_clock },
	{ "RAM size",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_ramsize },
	{ "Advanced",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_advanced },
#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
	{ "Screenshot",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_set_integer_to_one, &register_screenshot_request },
#endif
	{ "Load BASIC program",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_load_basic_prg },
	{ "Save BASIC program",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_save_basic_prg },
	{ "Dump RAM into file",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_dump_memory },
#ifdef XEMU_ARCH_WIN
	{ "System console",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_sysconsole, NULL },
#endif
#ifdef HAVE_XEMU_EXEC_API
	{ "Browse system folder",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_native_os_prefdir_browser, NULL },
#endif
	//{ "Reset",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data_if_sure, clcd_reset },
	//{ "Force saving RAM content",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, backup_ram_content },
	{ "About",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_about_window, NULL },
#ifdef HAVE_XEMU_EXEC_API
	{ "Help (on-line)",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_web_help_main, NULL },
#endif
	{ "Quit",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_quit_if_sure, NULL },
	{ NULL }
};


int ui_enter_menu ( void )
{
	DEBUGGUI("UI: handler has been called." NL);
	const int ret = xemugui_popup(menu_main);
	if (ret)
		DEBUGPRINT("UI: oops, POPUP did not work :(" NL);
	return ret;
}


int ui_init ( const char *name )
{
	return xemugui_init(name);
}


int ui_iteration ( void )
{
	return xemugui_iteration();
}
