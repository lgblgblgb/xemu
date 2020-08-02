/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
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
#include "sdcontent.h"
#include "xemu/emutools_hid.h"
#include "xemu/c64_kbd_mapping.h"
#include "inject.h"

#define		HELP_URL	"https://github.com/lgblgblgb/xemu/wiki/MEGA65-help"


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
		ERROR_WINDOW(
			"External D81 cannot be mounted, unless you have first setup the SD card image.\n"
			"Please use menu at 'SD-card -> Update files on SD image' to create MEGA65.D81,\n"
			"which can be overriden then to mount external D81 images for you"
		);
		return 1;
	}
}


// end of #if defined(CONFIG_DROPFILE_CALLBACK) || defined(XEMU_GUI_C)
//#endif


#ifdef CONFIG_DROPFILE_CALLBACK
void emu_dropfile_callback ( const char *fn )
{
	DEBUGGUI("UI: drop event, file: %s" NL, fn);
	if (ARE_YOU_SURE("Shall I try to mount the dropped file as D81 for you?", 0))
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
		reset_mega65();
		inject_register_prg(fnbuf, 0);
	} else
		DEBUGPRINT("UI: file selection for D81 mount was cancalled." NL);
}


static void ui_native_os_file_browser ( void )
{
	xemuexec_open_native_file_browser(sdl_pref_dir);
}


static void ui_online_help ( void )
{
	if (ARE_YOU_SURE("Shall I open a web browser instance with the help for you?", 0))
		xemuexec_open_native_file_browser(HELP_URL);
	else
		INFO_WINDOW("Help request was cancelled by you");
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

static void ui_format_sdcard ( void )
{
	if (ARE_YOU_SURE(
		"Formatting your SD-card image file will cause ALL your data,\n"
		"system files (etc!) to be lost, forever!\n"
		"Are you sure to continue this self-desctruction sequence? :)"
		,
		0
	)) {
		if (!sdcontent_handle(sdcard_get_size(), NULL, SDCONTENT_FORCE_FDISK))
			INFO_WINDOW("You SD-card file has been partitioned/formatted\nMEGA65 emulation is about to RESET now!");
	}
	reset_mega65();
}


static void ui_update_sdcard ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (!*dir)
		strcpy(dir, sdl_pref_dir);
	// Select ROM image
	if (xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select your ROM image",
		dir,
		fnbuf,
		sizeof fnbuf
	)) {
		WARNING_WINDOW("Cannot update: you haven't selected a ROM image");
		return;
	}
	// Load selected ROM image into memory, also checks the size!
	if (xemu_load_file(fnbuf, NULL, 0x20000, 0x20000, "Cannot begin image update, bad C65/M65 ROM image has been selected!") != 0x20000)
		return;
	// Copy file to the pref'dir (if not the same as the selected file)
	char fnbuf_target[PATH_MAX];
	strcpy(fnbuf_target, sdl_pref_dir);
	strcpy(fnbuf_target + strlen(sdl_pref_dir), MEGA65_ROM_NAME);
	if (strcmp(fnbuf_target, MEGA65_ROM_NAME)) {
		DEBUGPRINT("Backing up ROM image %s to %s" NL, fnbuf, fnbuf_target);
		if (xemu_save_file(
			fnbuf_target,
			xemu_load_buffer_p,
			0x20000,
			"Cannot save the selected ROM file for the updater"
		)) {
			free(xemu_load_buffer_p);
			xemu_load_buffer_p = NULL;
			return;
		}
	}
	// Generate character ROM from the ROM image
	Uint8 char_rom[CHAR_ROM_SIZE];
	memcpy(char_rom + 0x0000, xemu_load_buffer_p + 0xD000, 0x1000);
	memcpy(char_rom + 0x1000, xemu_load_buffer_p + 0x9000, 0x1000);
	free(xemu_load_buffer_p);
	xemu_load_buffer_p = NULL;
	// And store our character ROM!
	strcpy(fnbuf_target + strlen(sdl_pref_dir), CHAR_ROM_NAME);
	if (xemu_save_file(
		fnbuf_target,
		char_rom,
		CHAR_ROM_SIZE,
		"Cannot save the extracted CHAR ROM file for the updater"
	)) {
		return;
	}
	// Call the updater :)
	if (!sdcontent_handle(sdcard_get_size(), NULL, SDCONTENT_DO_FILES | SDCONTENT_OVERWRITE_FILES))
		INFO_WINDOW(
			"System files on your SD-card image seems to be updated successfully.\n"
			"Next time you may need this function, you can use MEGA65.ROM which is a backup copy of your selected ROM.\n"
			"MEGA65 emulation is about to RESET now!"
		);
	reset_mega65();
}


static const struct menu_st menu_scanlines[] = {
	{ "On",    XEMUGUI_MENUID_CALLABLE, xemugui_cb_scanlines, (void*)1 },
	{ "Off", XEMUGUI_MENUID_CALLABLE,   xemugui_cb_scanlines, (void*)0 },
	{ NULL }
};
static void reset_into_utility_menu ( void )
{
	reset_mega65_asked();
	hid_set_autoreleased_key(ALT_KEY_POS);
	KBD_PRESS_KEY(ALT_KEY_POS);
}

static void reset_into_c64_mode ( void )
{
	reset_mega65_asked();
	hid_set_autoreleased_key(0x75);
	KBD_PRESS_KEY(0x75);	// "MEGA" key is pressed for C64 mode

}


static const struct menu_st menu_display[] = {
	{ "Fullscreen",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)0 },
	{ "Window - 100%",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)1 },
	{ "Window - 200%",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)2 },
	{ "Scanlines",	   XEMUGUI_MENUID_SUBMENU,	menu_scanlines, NULL },
	{ NULL }
};
static const struct menu_st menu_sdcard[] = {
	{ "Re-format SD image",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_format_sdcard },
	{ "Update files on SD image",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_update_sdcard },
	{ NULL }
};
static const struct menu_st menu_reset[] = {
	{ "Reset M65",  		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_mega65_asked },
	{ "Reset into utility menu",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_utility_menu },
	{ "Reset into C64 mode",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c64_mode },
	{ NULL }
};
static const struct menu_st menu_main[] = {
	{ "Display",			XEMUGUI_MENUID_SUBMENU,		menu_display, NULL },
	{ "SD-card",			XEMUGUI_MENUID_SUBMENU,		menu_sdcard,  NULL },
	{ "Reset",			XEMUGUI_MENUID_SUBMENU,		menu_reset,   NULL },
	{ "Attach D81",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_attach_d81_by_browsing },
	{ "Run PRG directly",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_run_prg_by_browsing },
#ifdef BASIC_TEXT_SUPPORT
	{ "Save BASIC as text",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_save_basic_as_text },
#endif
	{ "Browse system folder",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_native_os_file_browser },
#ifdef XEMU_ARCH_WIN
	{ "System console", XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK, xemugui_cb_sysconsole, NULL },
#endif
	{ "Help (online)", XEMUGUI_MENUID_CALLABLE, xemugui_cb_call_user_data, ui_online_help },
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
