/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "primoemu.h"
#include "emu_monitor.h"
#include "dave.h"
#include "configdb.h"


static void ui_hard_reset ( void )
{
	if (primo_on)
		primo_emulator_exit();
	ep_reset();
	ep_clear_ram();
}


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
	)) {
		wd_attach_disk_image(fnbuf);
	} else
		DEBUGPRINT("UI: file selection for floppy mount was cancelled." NL);
}
#endif


static void ui_cb_cpuclock ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, CPU_CLOCK == VOIDPTR_TO_INT(m->user_data));
	set_cpu_clock_with_osd(VOIDPTR_TO_INT(m->user_data));
}


static void ui_cb_monitor ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, monitor_check());
	if (monitor_check())
		monitor_stop();
	else
		monitor_start();
}


static void ui_cb_sound ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, is_audio_emulation_active());
	if (is_audio_emulation_active())
		audio_stop();
	else {
		audio_init(1);	// function itself will check if there was an init already, so not worrying here ...
		audio_start();
	}
}


static void ui_cb_render_scale_quality ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, VOIDPTR_TO_INT(m->user_data) == configdb.sdlrenderquality);
	char req_str[] = { VOIDPTR_TO_INT(m->user_data) + '0', 0 };
	SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, req_str, SDL_HINT_OVERRIDE);
	configdb.sdlrenderquality = VOIDPTR_TO_INT(m->user_data);
	register_new_texture_creation = 1;
}

/**** MENU SYSTEM ****/

static const struct menu_st menu_render_scale_quality[] = {
	{ "Nearest pixel sampling",     XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_render_scale_quality, (void*)0 },
	{ "Linear filtering",           XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_render_scale_quality, (void*)1 },
	{ "Anisotropic (Direct3D only)",XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_render_scale_quality, (void*)2 },
	{ NULL }
};
static const struct menu_st menu_display[] = {
	{ "Render scale quality",	XEMUGUI_MENUID_SUBMENU,		NULL, menu_render_scale_quality },
	{ "Fullscreen",    		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)0 },
	{ "Window - 100%", 		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)1 },
	{ "Window - 200%", 		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_SEPARATOR,	xemugui_cb_windowsize, (void*)2 },
	{ "Enable mouse grab + emu",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_set_mouse_grab, NULL },
	{ NULL }
};
static const struct menu_st menu_debug[] = {
	{ "OSD key debugger",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_osd_key_debugger, NULL },
	{ "Monitor prompt",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_monitor, NULL },
#ifdef HAVE_XEMU_EXEC_API
	{ "Browse system folder",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_native_os_prefdir_browser, NULL },
#endif
	{ NULL }
};
static const struct menu_st menu_reset[] = {
	{ "Reset",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ep_reset },
	{ "Hard reset",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_hard_reset },
	{ NULL }
};
static const struct menu_st menu_cpuclock[] = {
	{ "4.00 MHz",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_cpuclock, (void*)4000000 },
	{ "6.00 MHz",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_cpuclock, (void*)6000000 },
	{ "7.12 MHz",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_cpuclock, (void*)7120000 },
	{ "8.00 MHz",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_cpuclock, (void*)8000000 },
	{ "10.00 MHz",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_cpuclock, (void*)10000000 },
	{ NULL }
};
static const struct menu_st menu_main[] = {
	{ "Display",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_display },
	{ "CPU clock",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_cpuclock },
	{ "Reset", 	 		XEMUGUI_MENUID_SUBMENU,		NULL, menu_reset   },
	{ "Debug",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_debug   },
#ifdef CONFIG_EXDOS_SUPPORT
	{ "Attach floppy",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_attach_disk },
#endif
#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
	{ "Screenshot",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_set_integer_to_one, &register_screenshot_request },
#endif
	{ "Sound emulation",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_sound, NULL },
#ifdef XEMU_ARCH_WIN
	{ "System console",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_sysconsole, NULL },
#endif
#ifdef HAVE_XEMU_EXEC_API
	{ "Help (on-line)",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_web_help_main, NULL },
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
