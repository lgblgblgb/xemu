/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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
#include "ui.h"
#include "xemu/emutools_gui.h"
#include "xemu/emutools_files.h"
#include "xemu/d81access.h"
#include "xemu/emutools_hid.h"
#include "commodore_65.h"
#include "inject.h"
#include "vic3.h"
#include "xemu/f018_core.h"
#include "xemu/emutools_config.h"
#include "xemu/basic_text.h"


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
		DEBUGPRINT("UI: file selection for D81 mount was cancelled." NL);
}
#endif


static void ui_run_prg_by_browsing ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select PRG to directly load&run",
		dir,
		fnbuf,
		sizeof fnbuf
	)) {
		c65_reset();
		inject_register_prg(fnbuf, 0);
	} else
		DEBUGPRINT("UI: file selection for PRG injection was cancelled." NL);
}


static void ui_dump_memory ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_SAVE | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Dump memory content into file",
		dir,
		fnbuf,
		sizeof fnbuf
	)) {
		dump_memory(fnbuf);
	}
}

static void reset_into_c64_mode ( void )
{
	if (c65_reset_asked()) {
		// we need this, because autoboot disk image would bypass the "go to C64 mode" on 'Commodore key' feature
		// this call will deny disk access, and re-enable on the READY. state.
		inject_register_allow_disk_access();
		hid_set_autoreleased_key(0x75);
		KBD_PRESS_KEY(0x75);	// C= key is pressed for C64 mode
	}
}

static void reset_into_c65_mode ( void )
{
	if (c65_reset_asked()) {
		KBD_RELEASE_KEY(0x75);
	}
}

static void reset_into_c65_mode_noboot ( void )
{
	if (c65_reset_asked()) {
		inject_register_allow_disk_access();
		KBD_RELEASE_KEY(0x75);
	}
}

static void ui_cb_show_drive_led ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, show_drive_led);
	show_drive_led = !show_drive_led;
}

static void ui_emu_info ( void )
{
	char td_stat_str[XEMU_CPU_STAT_INFO_BUFFER_SIZE];
	xemu_get_timing_stat_string(td_stat_str, sizeof td_stat_str);
	char uname_str[100];
	xemu_get_uname_string(uname_str, sizeof uname_str);
	INFO_WINDOW(
		"DMA chip current revision: %d (F018 rev-%s)\n"
		"ROM version detected: %d%s\n"
		"Current ROM: %s\n"
		//"C64 'CPU' I/O port (low 3 bits): DDR=%d OUT=%d\n"
		"Current VIC I/O mode: %s\n"
		"\n"
		"Xemu host CPU usage so far: %s\n"
		"Xemu's host OS: %s"
		,
		dma_chip_revision, dma_chip_revision ? "B, new" : "A, old",
		rom_date, rom_date > 0 ? "" : " (unknown or bad ROM signature)",
		current_rom_filepath,
		//memory_get_cpu_io_port(0) & 7, memory_get_cpu_io_port(1) & 7,
		vic_new_mode ? "VIC-III" : "VIC-II",
		td_stat_str,
		uname_str
	);
}


// TODO: maybe we want to move these functions to somewhere else from this UI specific file ui.c
// It may can help to make ui.c xemucfg independent, btw.
static void load_and_use_rom ( const char *fn )
{
	if (c65_reset_asked()) {
		KBD_RELEASE_KEY(0x75);
		c65_load_rom(fn, xemucfg_get_num("dmarev"));
	} else
		ERROR_WINDOW("Reset has been disallowed, thus you've rejected to load and use the selected ROM");
}
static void ui_load_rom_default ( void )
{
	load_and_use_rom(DEFAULT_ROM_FILE);
}
static void ui_load_rom_specified ( void )
{
	load_and_use_rom(xemucfg_get_str("rom"));
}


static void ui_load_rom_by_browsing ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select ROM to attach",
		dir,
		fnbuf,
		sizeof fnbuf
	))
		DEBUGPRINT("UI: file selection for loading ROM was cancelled." NL);
	else
		load_and_use_rom(fnbuf);
}


static void ui_put_screen_text_into_paste_buffer ( void )
{
	char text[8192];
	char *result = xemu_cbm_screen_to_text(
		text,
		sizeof text,
		memory + ((vic3_registers[0x31] & 0x80) ? (vic3_registers[0x18] & 0xE0) << 6 : (vic3_registers[0x18] & 0xF0) << 6),	// pointer to screen RAM, try to audo-tected: FIXME: works only in bank0!
		(vic3_registers[0x31] & 0x80) ? 80 : 40,		// number of columns, try to auto-detect it
		25,						// number of rows
		(vic3_registers[0x18] & 2)			// lowercase font? try to auto-detect by checking selected address chargen addr, LSB
	);
	if (result == NULL)
		return;
	if (*result) {
		if (SDL_SetClipboardText(result))
			ERROR_WINDOW("Cannot insert text into the OS paste buffer: %s", SDL_GetError());
		else
			OSD(-1, -1, "Copied to OS paste buffer.");
	} else
		INFO_WINDOW("Screen is empty, nothing to capture.");
}


/**** MENU SYSTEM ****/


static const struct menu_st menu_display[] = {
	{ "Fullscreen",    		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)0 },
	{ "Window - 100%", 		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)1 },
	{ "Window - 200%", 		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_SEPARATOR,	xemugui_cb_windowsize, (void*)2 },
	{ "Enable mouse grab + emu",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_set_mouse_grab, NULL },
	{ "Show drive LED",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_show_drive_led, NULL },
#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
	{ "Screenshot",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_set_integer_to_one, &register_screenshot_request },
#endif
	{ "Screen to OS paste buffer",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_put_screen_text_into_paste_buffer },
	{ NULL }
};
static const struct menu_st menu_debug[] = {
	{ "OSD key debugger",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_osd_key_debugger, NULL },
	{ "Dump memory info file",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_dump_memory },
	{ "Browse system folder",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_native_os_prefdir_browser, NULL },
	{ NULL }
};
static const struct menu_st menu_reset[] = {
	{ "Reset C65",  		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c65_mode        },
	{ "Reset C65 without autoboot",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c65_mode_noboot },
	{ "Reset into C64 mode",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c64_mode        },
	{ NULL }
};
static const struct menu_st menu_rom[] = {
	{ "Load custom ROM",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_load_rom_by_browsing   },
	{ "Load CLI specified ROM",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_load_rom_specified     },
	{ "Load Xemu default ROM",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_load_rom_default       },
	{ NULL }
};
static const struct menu_st menu_main[] = {
	{ "Display",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_display },
	{ "Reset", 	 		XEMUGUI_MENUID_SUBMENU,		NULL, menu_reset   },
	{ "Debug",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_debug   },
	{ "ROM",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_rom     },
	{ "Attach D81",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_attach_d81_by_browsing },
	{ "Run PRG directly",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_run_prg_by_browsing    },
#ifdef XEMU_ARCH_WIN
	{ "System console",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_sysconsole, NULL },
#endif
	{ "Emulation state info",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_emu_info },
	{ "About",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_about_window, NULL },
#ifdef HAVE_XEMU_EXEC_API
	{ "Help (on-line)",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_web_help_main, NULL },
#endif
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
