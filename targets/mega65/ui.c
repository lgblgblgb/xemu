/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
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
#include "input_devices.h"
#include "uart_monitor.h"
#include "xemu/f011_core.h"
#include "dma65.h"
#include "memory_mapper.h"
#include "xemu/basic_text.h"
#include "audio65.h"
#include "vic4.h"
#include "configdb.h"


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
	switch (QUESTION_WINDOW("Cancel|Mount as D81|Run/inject as PRG", "What to do with the dropped file?")) {
		case 1:
			attach_d81(fn);
			break;
		case 2:
			reset_mega65();
			inject_register_prg(fn, 0);
			break;
	}
}
#endif


static void ui_attach_d81 ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, 0);
	//ui_attach_d81_by_browsing();
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


static void ui_detach_d81 ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, 0);
	forget_external_d81();
}


static void ui_run_prg_by_browsing ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select PRG to directly load and run",
		dir,
		fnbuf,
		sizeof fnbuf
	)) {
		reset_mega65();
		inject_register_prg(fnbuf, 0);
	} else
		DEBUGPRINT("UI: file selection for PRG injection was cancelled." NL);
}

#ifdef CBM_BASIC_TEXT_SUPPORT
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
		"Are you sure to continue this self-destruction sequence? :)"
		,
		0
	)) {
		if (!sdcontent_handle(sdcard_get_size(), NULL, SDCONTENT_FORCE_FDISK))
			INFO_WINDOW("Your SD-card file has been partitioned/formatted\nMEGA65 emulation is about to RESET now!");
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


static void reset_into_utility_menu ( void )
{
	if (reset_mega65_asked()) {
		hwa_kbd_fake_key(0x20);
		KBD_RELEASE_KEY(0x75);
	}
}

static void reset_into_c64_mode ( void )
{
	if (reset_mega65_asked()) {
		// we need this, because autoboot disk image would bypass the "go to C64 mode" on 'Commodore key' feature
		// this call will deny disk access, and re-enable on the READY. state.
		inject_register_allow_disk_access();
		hid_set_autoreleased_key(0x75);
		KBD_PRESS_KEY(0x75);	// "MEGA" key is pressed for C64 mode
	}

}

static void reset_into_c65_mode ( void )
{
	if (reset_mega65_asked()) {
		KBD_RELEASE_KEY(0x75);
		hwa_kbd_fake_key(0);
	}
}

static void reset_into_c65_mode_noboot ( void )
{
	if (reset_mega65_asked()) {
		inject_register_allow_disk_access();
		KBD_RELEASE_KEY(0x75);
		hwa_kbd_fake_key(0);
	}
}

#ifdef HAS_UARTMON_SUPPORT
static void ui_start_umon ( const struct menu_st *m, int *query )
{
	int is_active = uartmon_is_active();
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, is_active);
	if (is_active) {
		INFO_WINDOW("UART monitor is already active.\nCurrently stopping it is not supported.");
		return;
	}
	if (!uartmon_init(UMON_DEFAULT_PORT))
		INFO_WINDOW("UART monitor has been starton on " UMON_DEFAULT_PORT);
}
#endif

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

static void ui_cb_show_drive_led ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, configdb.show_drive_led);
	configdb.show_drive_led = !configdb.show_drive_led;
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
		"C64 'CPU' I/O port (low 3 bits): DDR=%d OUT=%d\n"
		"Current VIC I/O mode: %s, hot registers are %s\n"
		"\n"
		"Xemu host CPU usage so far: %s\n"
		"Xemu's host OS: %s"
		,
		dma_chip_revision, dma_chip_revision ? "B, new" : "A, old",
		rom_date, rom_date > 0 ? "" : " (unknown or bad ROM signature)",
		memory_get_cpu_io_port(0) & 7, memory_get_cpu_io_port(1) & 7,
		vic_iomode < 4 ? iomode_names[vic_iomode] : "?INVALID?", (vic_registers[0x5D] & 0x80) ? "enabled" : "disabled",
		td_stat_str,
		uname_str
	);
}


static void ui_put_screen_text_into_paste_buffer ( void )
{
	char text[8192];
	char *result = xemu_cbm_screen_to_text(
		text,
		sizeof text,
		main_ram + ((vic_registers[0x31] & 0x80) ? (vic_registers[0x18] & 0xE0) << 6 : (vic_registers[0x18] & 0xF0) << 6),	// pointer to screen RAM, try to audo-tected: FIXME: works only in bank0!
		(vic_registers[0x31] & 0x80) ? 80 : 40,		// number of columns, try to auto-detect it
		25,						// number of rows
		(vic_registers[0x18] & 2)			// lowercase font? try to auto-detect by checking selected address chargen addr, LSB
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


static void ui_put_paste_buffer_into_screen_text ( void )
{
	char *t = SDL_GetClipboardText();
	if (t == NULL)
		goto no_clipboard;
	char *t2 = t;
	while (*t2 && (*t2 == '\t' || *t2 == '\r' || *t2 == '\n' || *t2 == ' '))
		t2++;
	if (!*t2)
		goto no_clipboard;
	xemu_cbm_text_to_screen(
		main_ram + ((vic_registers[0x31] & 0x80) ? (vic_registers[0x18] & 0xE0) << 6 : (vic_registers[0x18] & 0xF0) << 6),	// pointer to screen RAM, try to audo-tected: FIXME: works only in bank0!
		(vic_registers[0x31] & 0x80) ? 80 : 40,		// number of columns, try to auto-detect it
		25,						// number of rows
		t2,						// text buffer as input
		(vic_registers[0x18] & 2)			// lowercase font? try to auto-detect by checking selected address chargen addr, LSB
	);
	SDL_free(t);
	return;
no_clipboard:
	if (t)
		SDL_free(t);
	ERROR_WINDOW("Clipboard query error, or clipboard was empty");
}


static void ui_cb_mono_downmix ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, VOIDPTR_TO_INT(m->user_data) == stereo_separation);
	audio_set_stereo_parameters(AUDIO_UNCHANGED_VOLUME, VOIDPTR_TO_INT(m->user_data));
}


/**** MENU SYSTEM ****/


static const struct menu_st menu_display[] = {
	{ "Fullscreen",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)0 },
	{ "Window - 100%",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)1 },
	{ "Window - 200%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_SEPARATOR,	xemugui_cb_windowsize, (void*)2 },
	{ "Enable mouse grab + emu",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_set_mouse_grab, NULL },
	{ "Show drive LED",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_show_drive_led, NULL },
#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
	{ "Screenshot",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_set_integer_to_one, &register_screenshot_request },
#endif
	{ "Screen to OS paste buffer",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_put_screen_text_into_paste_buffer },
	{ "OS paste buffer to screen",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_put_paste_buffer_into_screen_text },
	{ NULL }
};
static const struct menu_st menu_sdcard[] = {
	{ "Re-format SD image",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_format_sdcard },
	{ "Update files on SD image",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_update_sdcard },
	{ NULL }
};
static const struct menu_st menu_reset[] = {
	{ "Reset M65",  		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c65_mode        },
	{ "Reset M65 without autoboot",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c65_mode_noboot },
	{ "Reset into utility menu",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_utility_menu    },
	{ "Reset into C64 mode",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c64_mode        },
	{ NULL }
};
static const struct menu_st menu_debug[] = {
#ifdef HAS_UARTMON_SUPPORT
	{ "Start umon on " UMON_DEFAULT_PORT,
					XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_start_umon, NULL },
#endif
	{ "OSD key debugger",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_osd_key_debugger, NULL },
	{ "Dump memory info file",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_dump_memory },
	{ "Emulation state info",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_emu_info },
	{ NULL }
};
#ifdef HAVE_XEMU_EXEC_API
static const struct menu_st menu_help[] = {
	{ "Xemu MEGA65 help page",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_web_help_main, "help" },
	{ "Check update / useful MEGA65 links",
					XEMUGUI_MENUID_CALLABLE,	xemugui_cb_web_help_main, "versioncheck" },
	{ "Xemu download page",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_web_help_main, "downloadpage" },
	{ "Download MEGA65 book",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_web_help_main, "downloadmega65book" },
	{ NULL }
};
#endif
static const struct menu_st menu_d81[] = {
	{ "Attach user D81",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_attach_d81, NULL },
	{ "Use internal D81",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_detach_d81, NULL },
	{ NULL }
};
static const struct menu_st menu_audio[] = {
	{ "Hard stereo separation",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*) 100 },
	{ "Mono downmix 80%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*)  80 },
	{ "Mono downmix 60%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*)  60 },
	{ "Mono downmix 40%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*)  40 },
	{ "Mono downmix 20%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*)  20 },
	{ "Mono downmix",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*)   0 },
	{ "Mono downmix -20%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*) -20 },
	{ "Mono downmix -40%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*) -40 },
	{ "Mono downmix -60%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*) -60 },
	{ "Mono downmix -80%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*) -80 },
	{ "Hard stereo - reserved",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, (void*)-100 },
	{ NULL }
};
static const struct menu_st menu_main[] = {
	{ "Display",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_display },
	{ "Audio",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_audio   },
	{ "SD-card",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_sdcard  },
	{ "FD D81",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_d81     },
	{ "Reset",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_reset   },
	{ "Debug",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_debug   },
	{ "Run PRG directly",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_run_prg_by_browsing },
#ifdef CBM_BASIC_TEXT_SUPPORT
	{ "Save BASIC as text",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_save_basic_as_text },
#endif
#ifdef XEMU_ARCH_WIN
	{ "System console",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_sysconsole, NULL },
#endif
#ifdef HAVE_XEMU_EXEC_API
	{ "Help (online)",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_help },
#endif
	{ "About",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_about_window, NULL },
#ifdef HAVE_XEMU_EXEC_API
	{ "Browse system folder",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_native_os_prefdir_browser, NULL },
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
