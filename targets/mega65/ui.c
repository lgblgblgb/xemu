/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "sdcard.h"
#include "sdcontent.h"
#include "xemu/emutools_hid.h"
//#include "xemu/c64_kbd_mapping.h"
#include "inject.h"
#include "input_devices.h"
#include "matrix_mode.h"
#include "uart_monitor.h"
//#include "xemu/f011_core.h"
#include "dma65.h"
#include "memory_mapper.h"
#include "audio65.h"
#include "vic4.h"
#include "configdb.h"
#include "rom.h"
#include "hypervisor.h"
#include "xemu/cpu65.h"
#include "xemu/emutools_config.h"
#include "cart.h"
#include "xemu/emutools_osk.h"


// Used by UI CBs to maintain configDB persistence
static void _mountd81_configdb_change ( const int drive, const char *fn )
{
	char **p = drive ? &configdb.disk9 : &configdb.disk8;
	DEBUGPRINT("UI: configDB change for drive #%d from <%s> to <%s>" NL, drive, *p ? *p : "NULL", fn ? fn : "NULL");
	xemucfg_set_str(p, fn);
}

#ifdef CONFIG_DROPFILE_CALLBACK
void emu_dropfile_callback ( const char *fn )
{
	DEBUGGUI("UI: file drop event, file: %s" NL, fn);
	switch (QUESTION_WINDOW("Cancel|Mount as D81|Run/inject as PRG", "What to do with the dropped file?")) {
		case 1:
			if (!sdcard_external_mount(0, fn, "D81 mount failure"))
				_mountd81_configdb_change(0, fn);
			break;
		case 2:
			reset_mega65(RESET_MEGA65_HARD | RESET_MEGA65_NO_CART);
			inject_register_prg(fn, 0, false);
			break;
	}
}
#endif

static void ui_cb_attach_default_d81 ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, 0);
	int drive = VOIDPTR_TO_INT(m->user_data);
	if ((drive & 1024))
		sdcard_default_internal_d81_mount(drive & 1);
	else {
		sdcard_default_external_d81_mount(drive & 1);
		_mountd81_configdb_change(drive & 1, NULL);	// just book this as "not mounted" (as the default). Maybe is it a FIXME?
	}
}


// Used to override default directory for UI dialogs if it was "empty" (empty string as "dir")
static void _check_file_selection_default_override ( char *dir )
{
	if (!*dir && configdb.defaultdir && strlen(configdb.defaultdir) < PATH_MAX) {
		//DEBUGPRINT("UI: specifying default directory: %s" NL, configdb.defaultdir);
		strcpy(dir, configdb.defaultdir);
	}
}


static void ui_cb_attach_d81 ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, 0);
	const int drive = VOIDPTR_TO_INT(m->user_data) & 1;	// only two possible drives (or units, or WTF ...)
	const int creat = !!(VOIDPTR_TO_INT(m->user_data) & 0x80);
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	_check_file_selection_default_override(dir);
	// if "dir" static var is empty, let's initialize (otherwise it holds the last used dir by the user)
	if (!dir[0]) {
		// If HDOS virtualization is enabled, provide hdos root dir as default to browse,
		// otherwise let's stick with sdl_pref_dir
		const char *hdos_root;
		const int hdos_virt = hypervisor_hdos_virtualization_status(-1, &hdos_root);
		strcpy(dir, hdos_virt ? hdos_root : sdl_pref_dir);
	}
	if (!xemugui_file_selector(
		(creat ? XEMUGUI_FSEL_SAVE : XEMUGUI_FSEL_OPEN) | XEMUGUI_FSEL_FLAG_STORE_DIR,
		creat ? "Create new D81 to attach" : "Select D81 to attach",
		dir,
		fnbuf,
		sizeof fnbuf
	)) {
		if (creat) {
			// append .d81 extension if user did not specify that ...
			const int fnlen = strlen(fnbuf);
			static const char d81_ext[] = ".d81";
			char fnbuf2[fnlen + strlen(d81_ext) + 1];
			strcpy(fnbuf2, fnbuf);
			if (strcasecmp(fnbuf2 + fnlen - strlen(d81_ext), d81_ext)) {
				strcpy(fnbuf2 + fnlen, d81_ext);
				// FIXME: when we appended .d81 we should check if file exists! file sel dialog only checks for the base name of course
				// However this is a bit lame this way, that there are two different kind of question, one from the save filesel dailog,
				// at the other case, we check here ...
				if (xemu_file_exists(fnbuf2)) {
					if (!ARE_YOU_SURE("Overwrite existing D81 image?", ARE_YOU_SURE_DEFAULT_YES)) {
						return;
					}
				}
			}
			if (!sdcard_external_mount_with_image_creation(drive, fnbuf2, 1, "D81 mount failure")) // third arg: allow overwrite existing D81
				_mountd81_configdb_change(drive, fnbuf2);
		} else {
			if (!sdcard_external_mount(drive, fnbuf, "D81 mount failure"))
				_mountd81_configdb_change(drive, fnbuf);
		}
	} else {
		DEBUGPRINT("UI: file selection for D81 mount was cancelled." NL);
	}
}

static void ui_cb_detach_d81 ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, 0);
	const int drive = VOIDPTR_TO_INT(m->user_data);
	sdcard_unmount(drive);
	_mountd81_configdb_change(drive, NULL);
}

static void ui_run_prg_by_browsing ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	_check_file_selection_default_override(dir);
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select PRG to directly load and run",
		dir,
		fnbuf,
		sizeof fnbuf
	)) {
		reset_mega65(RESET_MEGA65_HARD | RESET_MEGA65_NO_CART);
		inject_register_prg(fnbuf, 0, false);
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

static char dir_rom[PATH_MAX + 1] = "";

#ifdef SD_CONTENT_SUPPORT
static int is_card_writable_with_error_popup ( void )
{
	if (sdcard_is_writeable())
		return 1;
	ERROR_WINDOW("SD-card is in read-only mode, operation denied");
	return 0;
}

static void ui_format_sdcard ( void )
{
	if (!is_card_writable_with_error_popup())
		return;
	if (ARE_YOU_SURE(
		"Formatting your SD-card image file will cause all MEGA65\n"
		"system and user content to be lost on the image file.\n"
		"Are you sure to continue this self-destruction sequence? :)"
		,
		0
	)) {
		if (!sdcontent_handle(sdcard_get_size(), NULL, SDCONTENT_FORCE_FDISK | SDCONTENT_HDOS_DIR_TOO))
			INFO_WINDOW("Your SD-card file has been partitioned/formatted\nMEGA65 emulation is about to RESET now!");
	}
	reset_mega65(RESET_MEGA65_HARD);
}

static void ui_update_sdcard ( void )
{
	if (!is_card_writable_with_error_popup())
		return;
	char fnbuf[PATH_MAX + 1];
	xemu_load_buffer_p = NULL;
	// Try default ROM
	snprintf(fnbuf, sizeof fnbuf, "%sMEGA65.ROM", sdl_pref_dir);
	int ask_rom;
	if (xemu_file_exists(fnbuf))
		ask_rom = QUESTION_WINDOW("Yes|No", "Use the previously installed ROM?");
	else
		ask_rom = 1;
	if (ask_rom) {
		_check_file_selection_default_override(dir_rom);
		if (!*dir_rom)
			strcpy(dir_rom, sdl_pref_dir);
		// Select ROM image
		if (xemugui_file_selector(
			XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
			"Select your ROM image",
			dir_rom,
			fnbuf,
			sizeof fnbuf
		)) {
			WARNING_WINDOW("Cannot update: you haven't selected a ROM image");
			goto ret;
		}
	}
	// Load selected ROM image into memory, also checks the size!
	if (xemu_load_file(fnbuf, NULL, 0x20000, 0x20000, "Cannot start updating, bad C65/M65 ROM image has been selected!") != 0x20000)
		goto ret;
	// Check the loaded ROM: let's warn the user if it's open-ROMs, since it seems users are often confused to think,
	// that's the right choice for every-day usage.
	rom_detect_date(xemu_load_buffer_p);
	if (rom_date < 0) {
		if (!ARE_YOU_SURE("Selected ROM cannot be identified as a valid C65/MEGA65 ROM. Are you sure to continue?", ARE_YOU_SURE_DEFAULT_NO)) {
			INFO_WINDOW("SD-card system files update was aborted by the user.");
			goto ret;
		}
	} else {
		if (rom_is_openroms) {
			if (!ARE_YOU_SURE(
				"Are you sure you want to use Open-ROMs on your SD-card?\n\n"
				"You've selected a ROM for update which belongs to the\n"
				"Open-ROMs projects. Please note, that Open-ROMs are not\n"
				"yet ready for usage by an average user! For general usage\n"
				"currently, closed-ROMs are recommended! Open-ROMs\n"
				"currently can be interesting for mostly developers and\n"
				"for curious minds.",
				ARE_YOU_SURE_DEFAULT_NO
			))
				goto ret;
		}
		if (rom_is_stub) {
			ERROR_WINDOW(
				"The selected ROM image is an Xemu-internal ROM image.\n"
				"This cannot be used to update your emulated SD-card."
			);
			goto ret;
		}
	}
	DEBUGPRINT("UI: upgrading SD-card system files, ROM %d (%s)" NL, rom_date, rom_name);
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
		))
			goto ret;
	}
	// store our character ROM
	strcpy(fnbuf_target + strlen(sdl_pref_dir), CHAR_ROM_NAME);
	if (xemu_save_file(
		fnbuf_target,
		xemu_load_buffer_p + 0xD000,
		CHAR_ROM_SIZE,
		"Cannot save the extracted CHAR ROM file for the updater"
	))
		goto ret;
	// Call the updater :)
	if (!sdcontent_handle(sdcard_get_size(), NULL, SDCONTENT_DO_FILES | SDCONTENT_OVERWRITE_FILES | SDCONTENT_HDOS_DIR_TOO)) {
		INFO_WINDOW(
			"System files on your SD-card image seem to have been updated successfully.\n"
			"Next time you may need this function, you can use MEGA65.ROM which is a backup copy of your selected ROM.\n\n"
			"ROM: %d (%s)\n\n"
			"Your emulated MEGA65 is about to RESET now!", rom_date, rom_name
		);
	}
	reset_mega65(RESET_MEGA65_HARD);
	rom_unset_requests();
ret:
	if (xemu_load_buffer_p) {
		free(xemu_load_buffer_p);
		xemu_load_buffer_p = NULL;
	}
	// make sure we have the correct detected results again based on the actual memory content,
	// since we've used the detect function on the to-be-loaded ROM to check
	rom_detect_date(main_ram + 0x20000);
}
#endif	// SD_CONTENT_SUPPORT

static void reset_via_hyppo ( void )
{
	reset_mega65(RESET_MEGA65_HYPPO | RESET_MEGA65_ASK);
}

static void reset_without_cartridge ( void )
{
	reset_mega65(RESET_MEGA65_HARD | RESET_MEGA65_ASK | RESET_MEGA65_NO_CART);
}

static void reset_cpu_only ( void )
{
	reset_mega65(RESET_MEGA65_CPU | RESET_MEGA65_ASK);
}

static void reset_into_custom_rom ( void )
{
	char fnbuf[PATH_MAX + 1];
	_check_file_selection_default_override(dir_rom);
	if (!*dir_rom)
		strcpy(dir_rom, sdl_pref_dir);
	// Select ROM image
	if (xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select ROM image",
		dir_rom,
		fnbuf,
		sizeof fnbuf
	))
		return;
	if (rom_load_custom(fnbuf)) {
		if (!reset_mega65(RESET_MEGA65_HARD | RESET_MEGA65_ASK))
			WARNING_WINDOW("You refused reset, loaded ROM can be only activated at the next reset.");
	}
}

static void reset_into_utility_menu ( void )
{
	ERROR_WINDOW("Currently there are some problems using this function,\nIt's a known problem. You'll get empty screen after utility selection.\nOnce it's resolved this message will be removed from Xemu");
	if (reset_mega65(RESET_MEGA65_ASK | RESET_MEGA65_HARD)) {
		rom_stubrom_requested = 0;
		rom_initrom_requested = 0;
		hwa_kbd_set_fake_key(0x20);
		KBD_RELEASE_KEY(0x75);
	}
}

static void reset_into_c64_mode ( void )
{
	if (reset_mega65(RESET_MEGA65_HARD | RESET_MEGA65_ASK | RESET_MEGA65_NO_CART)) {
		rom_stubrom_requested = 0;
		rom_initrom_requested = 0;
		// we need this, because autoboot disk image would bypass the "go to C64 mode" on 'Commodore key' feature
		// this call will deny disk access, and re-enable on the READY. state.
		inject_register_allow_disk_access();
		hid_set_autoreleased_key(0x75);
		KBD_PRESS_KEY(0x75);	// "MEGA" key is pressed for C64 mode
	}

}

static void reset_hard ( void )
{
	if (reset_mega65(RESET_MEGA65_HARD | RESET_MEGA65_ASK)) {
		KBD_RELEASE_KEY(0x75);
		hwa_kbd_set_fake_key(0);
	}
}

static void reset_into_xemu_stubrom ( void )
{
	if (reset_mega65(RESET_MEGA65_HARD | RESET_MEGA65_ASK)) {
		rom_initrom_requested = 0;
		rom_stubrom_requested = 1;
	}
}

static void reset_into_xemu_initrom ( void )
{
	if (reset_mega65(RESET_MEGA65_HARD | RESET_MEGA65_ASK)) {
		rom_stubrom_requested = 0;
		rom_initrom_requested = 1;
	}
}

static void reset_into_c65_mode_noboot ( void )
{
	if (reset_mega65(RESET_MEGA65_HARD | RESET_MEGA65_ASK | RESET_MEGA65_NO_CART)) {
		rom_stubrom_requested = 0;
		rom_initrom_requested = 0;
		inject_register_allow_disk_access();
		KBD_RELEASE_KEY(0x75);
		hwa_kbd_set_fake_key(0);
	}
}

static void ui_cb_use_default_rom ( const struct menu_st *m, int *query )
{
	if (query) {
		if (!rom_is_overriden)
			*query |= XEMUGUI_MENUFLAG_HIDDEN | XEMUGUI_MENUFLAG_SEPARATOR;
		return;
	}
	if (rom_is_overriden) {
		if (reset_mega65(RESET_MEGA65_HARD | RESET_MEGA65_ASK)) {
			rom_unset_requests();
		}
	}
}

#ifdef HAS_UARTMON_SUPPORT
static void ui_cb_start_umon ( const struct menu_st *m, int *query )
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

static void ui_cb_matrix_mode ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, in_the_matrix);
	matrix_mode_toggle(!in_the_matrix);
}

static void ui_cb_hdos_virt ( const struct menu_st *m, int *query )
{
	int status = hypervisor_hdos_virtualization_status(-1, NULL);
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, status);
	status = !status;
	(void)hypervisor_hdos_virtualization_status(status, NULL);
	configdb.hdosvirt = status;	// propogate status to the configDB system as well
}

static char last_used_dump_directory[PATH_MAX + 1] = "";

static void ui_dump_memory ( void )
{
	char fnbuf[PATH_MAX + 1];
	_check_file_selection_default_override(last_used_dump_directory);
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_SAVE | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Dump main memory content into file",
		last_used_dump_directory,
		fnbuf,
		sizeof fnbuf
	)) {
		dump_memory(fnbuf);
	}
}

static void ui_dump_colram ( void )
{
	char fnbuf[PATH_MAX + 1];
	_check_file_selection_default_override(last_used_dump_directory);
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_SAVE | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Dump colour memory content into file",
		last_used_dump_directory,
		fnbuf,
		sizeof fnbuf
	)) {
		xemu_save_file(fnbuf, colour_ram, sizeof colour_ram, "Cannot dump colour RAM content into file");
	}
}

static void ui_dump_hyperram ( void )
{
	char fnbuf[PATH_MAX + 1];
	_check_file_selection_default_override(last_used_dump_directory);
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_SAVE | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Dump hyperRAM content into file",
		last_used_dump_directory,
		fnbuf,
		sizeof fnbuf
	)) {
		xemu_save_file(fnbuf, attic_ram, SLOW_RAM_SIZE, "Cannot dump hyperRAM content into file");
	}
}

static void ui_emu_info ( void )
{
	char td_stat_str[XEMU_CPU_STAT_INFO_BUFFER_SIZE];
	xemu_get_timing_stat_string(td_stat_str, sizeof td_stat_str);
	sha1_hash_str rom_now_hash_str;
	sha1_checksum_as_string(rom_now_hash_str, main_ram + 0x20000, 0x20000);
	const char *hdos_root;
	const int hdos_virt = hypervisor_hdos_virtualization_status(-1, &hdos_root);
	const int dma_rev = dma_get_revision();
	INFO_WINDOW(
		"DMA chip current revision: %d (F018 rev-%s)\n"
		"ROM version detected: %d %s (%s,%s)\n"
		"ROM SHA1: %s (%s)\n"
		"Last RESET type: %s\n"
		"Hyppo version: %s (%s)\n"
		"HDOS virtualization: %s, root = %s\n"
		"Disk8 = %s\nDisk9 = %s\n"
		"Cartridge = %s\n"
		"C64 'CPU' I/O port (low 3 bits): DDR=%d OUT=%d\n"
		"Current PC: $%04X (linear: $%07X)\n"
		"Current VIC and I/O mode: %s %s, hot registers are %s\n"
		"\n"
		"Xemu host CPU usage so far: %s\n"
		"Xemu's host OS: %s [%s] (64bit-FMTs: %s %s %s)"
		,
		dma_rev, dma_rev ? "B, new" : "A, old",
		rom_date, rom_name, rom_is_overriden ? "OVERRIDEN" : "installed", rom_is_external ? "external" : "internal",
		rom_now_hash_str, strcmp(rom_hash_str, rom_now_hash_str) ? "MANGLED" : "intact",
		last_reset_type,
		hyppo_version_string, hickup_is_overriden ?  "OVERRIDEN" : "built-in",
		hdos_virt ? "ON" : "OFF", hdos_root,
		sdcard_get_mount_info(0, NULL), sdcard_get_mount_info(1, NULL),
		cart_get_fn(),
		memory_get_cpu_io_port(0) & 7, memory_get_cpu_io_port(1) & 7,
		cpu65.pc, memory_cpurd2linear_xlat(cpu65.pc),
		iomode_names[io_mode], videostd_name, (vic_registers[0x5D] & 0x80) ? "enabled" : "disabled",
		td_stat_str,
		xemu_get_uname_string(), emu_fs_is_utf8 ? "UTF8-FS" : "ASCII-FS", PRINTF_U64, PRINTF_X64, PRINTF_S64
	);
}

static void ui_hwa_kbd_pasting ( void )
{
	char *buf = SDL_GetClipboardText();
	if (!buf || !*buf) {
		DEBUGPRINT("UI: paste buffer typing-in had no input" NL);
		SDL_free(buf);
		return;
	}
	unsigned int multi_case = 0;
	for (register char *p = buf, cc = 0; *p; p++) {
		if (*p >= 'a' && *p <= 'z')
			cc |= 1;
		else if (*p >= 'A' && *p <= 'Z')
			cc |= 2;
		else
			continue;
		if (cc == 3) {
			multi_case = QUESTION_WINDOW("Convert to single case|Paste as is|Cancel", "Paste contains mixed case letters. What to do now?");
			break;
		}
	}
	if (multi_case > 1)
		SDL_free(buf);
	else
		inject_hwa_pasting(xemu_sdl_to_native_string_allocation(buf), !multi_case);	// will free the buffer as its own
}

static void ui_put_screen_text_into_paste_buffer ( void )
{
	char *result = vic4_textshot();
	if (!result)
		return;
	if (*result) {
		if (SDL_SetClipboardText(result))
			ERROR_WINDOW("Cannot insert text into the OS paste buffer: %s", SDL_GetError());
		else
			OSD(-1, -1, "Copied to OS paste buffer.");
	} else
		INFO_WINDOW("Screen is empty, nothing to capture.");
	free(result);
}

static void ui_put_screen_text_into_file ( void )
{
	char fnbuf[PATH_MAX + 1];
	_check_file_selection_default_override(last_used_dump_directory);
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_SAVE | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Dump screen ASCII content into file",
		last_used_dump_directory,
		fnbuf,
		sizeof fnbuf
	)) {
		dump_screen(fnbuf);
	}
}

static void ui_put_paste_buffer_into_screen_text ( void )
{
	char *t = SDL_GetClipboardText();
	if (!t)
		goto no_clipboard;
	char *t2 = t;
	while (*t2 && (*t2 == '\t' || *t2 == '\r' || *t2 == '\n' || *t2 == ' '))
		t2++;
	if (!*t2)
		goto no_clipboard;
	vic4_textinsert(t2);
	SDL_free(t);
	return;
no_clipboard:
	SDL_free(t);
	ERROR_WINDOW("Clipboard query error, or clipboard was empty");
}


static void ui_cb_mono_downmix ( const struct menu_st *m, int *query )
{
	const bool st = audio65_get_mono_downmix();
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, st);
	audio65_set_mono_downmix(!st);
}

static void ui_cb_audio_output ( const struct menu_st *m, int *query )
{
	const int val = audio65_get_output();
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, VOIDPTR_TO_INT(m->user_data) == val);
	audio65_set_output(VOIDPTR_TO_INT(m->user_data));
}

static void ui_cb_audio_volume ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, VOIDPTR_TO_INT(m->user_data) == audio65_get_volume());
	audio65_set_volume(VOIDPTR_TO_INT(m->user_data));
}

static void ui_cb_video_standard ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, VOIDPTR_TO_INT(m->user_data) == videostd_id);
	configdb.videostd = VOIDPTR_TO_INT(m->user_data);
	vic4_set_videostd(configdb.videostd, "requested by UI");
}

static void ui_cb_video_standard_disallow_change ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, configdb.lock_videostd);
	configdb.lock_videostd = !configdb.lock_videostd;
	vic4_disallow_videostd_change = configdb.lock_videostd;
}

static void ui_cb_fullborders ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, configdb.fullborders);
	configdb.fullborders = !configdb.fullborders;
	vic_readjust_sdl_viewport = 1;		// To force readjust viewport on the next frame open.
}

static void ui_cb_sids_enabled ( const struct menu_st *m, int *query )
{
	const int mask = VOIDPTR_TO_INT(m->user_data);
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, (configdb.sidmask & mask));
	configdb.sidmask ^= mask;
}

static void ui_cb_render_scale_quality ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, VOIDPTR_TO_INT(m->user_data) == configdb.sdlrenderquality);
	char req_str[] = { VOIDPTR_TO_INT(m->user_data) + '0', 0 };
	SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, req_str, SDL_HINT_OVERRIDE);
	configdb.sdlrenderquality = VOIDPTR_TO_INT(m->user_data);
	register_new_texture_creation = 1;
}

#if 0
static void ui_cb_displayenable ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, vic_registers[0x11] & 0x10);
	vic_registers[0x11] ^= 0x10;
}
#endif

static void ui_cb_mega65_model ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, VOIDPTR_TO_INT(m->user_data) == configdb.mega65_model);
	if (mega65_set_model(VOIDPTR_TO_INT(m->user_data)))
		WARNING_WINDOW("It is recommended to reset emulation at this point");
}

static void ui_cb_colour_effect ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, VOIDPTR_TO_INT(m->user_data) == configdb.colour_effect);
	vic4_set_emulation_colour_effect(VOIDPTR_TO_INT(m->user_data));
}

static void ui_cb_attach_cart ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, cart_is_attached());
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	_check_file_selection_default_override(dir);
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select cartridge",
		dir,
		fnbuf,
		sizeof fnbuf
	)) {
		const int ret = cart_attach(fnbuf);
		if (ret >= 0) {
			xemucfg_set_str(&configdb.cart, fnbuf);
			if (ret)
				reset_mega65(RESET_MEGA65_HARD);
			else
				INFO_WINDOW("No auto-start 'M65' sequence at $8007, skipping reset");
		}
	} else
		DEBUGPRINT("UI: file selection cartridge insertion was cancelled." NL);
}

static void ui_cart_info ( void )
{
	char buf[4096];
	cart_info(buf, sizeof buf);
	INFO_WINDOW("%s", buf);
}

#ifndef XEMU_ARCH_HTML
static void ui_restore_config ( void )
{
	if (!xemucfg_delete_default_config_file(
		"This option DELETES your saved (if there was any!) default configuration.\n"
		"So using this option will reset emulator config to the default.\n"
		"No user data (disk images, SD-image, ...) will be lost though.\n\n"
		"Are you sure to do this?"
	))
		WARNING_WINDOW("DONE.\nYou must re-start Xemu to have any effect");
}
#endif

static void ui_save_current_window_position ( void )
{
	xemu_default_win_pos_file_op('w');
	INFO_WINDOW("Current window position has been stored.");
}

#ifdef HID_KBD_NO_F_HOTKEYS
static void ui_cb_default_emu_f_hotkeys ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, configdb.emu_f_hotkeys);
	configdb.emu_f_hotkeys = !configdb.emu_f_hotkeys;
	if (configdb.emu_f_hotkeys) {
		hid_set_default_emu_f_hotkeys();
	} else {
		INFO_WINDOW("You must save configuration AND restart Xemu to take effect");
	}
}
#endif

static void ui_reset_type ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, configdb.resethotkeytype == VOIDPTR_TO_INT(m->user_data));
	configdb.resethotkeytype = VOIDPTR_TO_INT(m->user_data);
}

#ifdef XEMU_OSK_SUPPORT
static void ui_cb_show_osk ( const struct menu_st *m, int *query )
{
	const bool status = osk_status();
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, status);
	osk_show(!status);
}
#endif


/**** MENU SYSTEM ****/


static const struct menu_st menu_cartridge[] = {
	{ "Attach cartridge",		XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_attach_cart, NULL },
	{ "Deatch cartridge",		XEMUGUI_MENUID_CALLABLE,				xemugui_cb_call_user_data, cart_detach },
	{ "Cartridge info",		XEMUGUI_MENUID_CALLABLE,				xemugui_cb_call_user_data, ui_cart_info },
	{ NULL }
};
static const struct menu_st menu_colour_effects[] = {
	{ "Normal colours",		XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_colour_effect, (void*)0 },
	{ "Grayscale",			XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_colour_effect, (void*)1 },
	{ "Green monochrome monitor",	XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_colour_effect, (void*)2 },
	{ "Reduced red channel",	XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_colour_effect, (void*)3 },
	{ "Missing red channel",	XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_colour_effect, (void*)4 },
	{ "Reduced green channel",	XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_colour_effect, (void*)5 },
	{ "Missing green channel",	XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_colour_effect, (void*)6 },
	{ "Reduced blue channel",	XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_colour_effect, (void*)7 },
	{ "Missing blue channel",	XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_colour_effect, (void*)8 },
	{ NULL }
};
static const struct menu_st menu_mega65_model[] = {
	{ "MEGA65 r1",			XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x01	},
	{ "MEGA65 r2",			XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x02	},
	{ "MEGA65 r3",			XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x03	},
	{ "MEGA65 r4",			XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x04	},
	{ "MEGA65 r5",			XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x05	},
	{ "MEGAphone r1",		XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x21 },
	{ "MEGAphone r4",		XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x22	},
	{ "Nexys4",			XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x40	},
	{ "Nexys4DDR",			XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x41	},
	{ "Nexys4DDR-widget",		XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x42	},
	{ "QMtech A100T",		XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x60	},
	{ "QMtech A200T",		XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x61	},
	{ "QMtech A325T",		XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0x62	},
	{ "Wukong",			XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0xFD	},
	{ "Simulation",			XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0xFE	},
	{ "Emulator/other",		XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mega65_model, (void*)0xFF	},
	{ NULL }
};
static const struct menu_st menu_show_scanlines[] = {
	{ "Disallow change by programs",XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_SEPARATOR |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int_inverted, (void*)&configdb.allow_scanlines },
	{ "Show scanlines",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int, (void*)&configdb.show_scanlines },
	{ NULL }
};
static const struct menu_st menu_video_standard[] = {
	{ "Disallow change by programs",XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_SEPARATOR |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_video_standard_disallow_change, NULL },
	{ "PAL @ 50Hz",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_video_standard, (void*)0 },
	{ "NTSC @ 60Hz",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_video_standard, (void*)1 },
	{ NULL }
};
static const struct menu_st menu_window_size[] = {
	// TODO: unfinished work, see: https://github.com/lgblgblgb/xemu/issues/246
#if 0
	{ "Fullscreen",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_windowsize, (void*)0 },
	{ "Window - 100%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_windowsize, (void*)1 },
	{ "Window - 200%",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_windowsize, (void*)2 },
#endif
	{ "Fullscreen",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)0 },
	{ "Window - 100%",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)1 },
	{ "Window - 200%",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)2 },
	{ NULL }
};
static const struct menu_st menu_render_scale_quality[] = {
	{ "Nearest pixel sampling",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_render_scale_quality, (void*)0 },
	{ "Linear filtering",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_render_scale_quality, (void*)1 },
	{ "Anisotropic (Direct3D only)",XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_render_scale_quality, (void*)2 },
	{ NULL }
};
static const struct menu_st menu_display[] = {
	{ "Render scale quality",	XEMUGUI_MENUID_SUBMENU,		NULL, menu_render_scale_quality },
	{ "Window size / fullscreen",	XEMUGUI_MENUID_SUBMENU,		NULL, menu_window_size },
	{ "Video standard",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_video_standard },
	{ "Show scanlines at V200",	XEMUGUI_MENUID_SUBMENU,		NULL, menu_show_scanlines },
	{ "Colour effects",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_colour_effects },
	{ "Show full borders",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_fullborders, NULL },
	{ "Show drive LED",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK |
					XEMUGUI_MENUFLAG_SEPARATOR,	xemugui_cb_toggle_int, (void*)&configdb.show_drive_led },
#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
	{ "Screenshot",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_set_integer_to_one, &vic4_registered_screenshot_request },
#endif
	{ "Save main window placement",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_save_current_window_position },
	{ "Screen to OS clipboard",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_put_screen_text_into_paste_buffer },
	{ "Screen to ASCII file",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_put_screen_text_into_file },
	{ "OS clipboard typing-in",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_hwa_kbd_pasting },
	{ "OS clipboard to screen",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_put_paste_buffer_into_screen_text },
	{ NULL }
};
static const struct menu_st menu_reset[] = {
	{ "Reset back to default ROM",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_use_default_rom, NULL				},
	{ "Reset", 			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_hard			},
	{ "Reset + cartridge detach",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_without_cartridge	},
	{ "Reset without autoboot",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c65_mode_noboot	},
	{ "Reset into utility menu",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_utility_menu	},
	{ "Reset into C64 mode",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c64_mode		},
	{ "Reset into Xemu stub-ROM",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_xemu_stubrom	},
	{ "Reset into boot init-ROM",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_xemu_initrom	},
	{ "Reset via HYPPO",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_via_hyppo		},
	{ "Reset CPU only",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_cpu_only		},
	{ "Reset/use custom ROM file",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_custom_rom	},
	{ NULL }
};
static const struct menu_st menu_reset_hotkey_type[] = {
	{ "HARD",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_reset_type, (void*)RESET_MEGA65_HARD  },
	{ "CPU",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_reset_type, (void*)RESET_MEGA65_CPU   },
	{ "HYPPO",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_reset_type, (void*)RESET_MEGA65_HYPPO },
	{ NULL }
};
static const struct menu_st menu_inputdevices[] = {
	{ "Enable mouse grab",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_set_mouse_grab, NULL },
	{ "Disable mouse emulation",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int, (void*)&configdb.nomouseemu },
#ifdef	XEMU_OSK_SUPPORT
	{ "Show OSK",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_show_osk, NULL },
#endif
	{ "Use OSD key debugger",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_osd_key_debugger, NULL },
	{ "Cursor keys as joystick",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int, (void*)&hid_joy_on_cursor_keys },
	{ "Swap emulated joystick port",XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, input_toggle_joy_emu },
#if 0
	{ "Devices as joy port 2 (vs 1)",	XEMUGUI_MENUID_SUBMENU,		NULL, menu_joy_devices },
#endif
#ifdef HID_KBD_NO_F_HOTKEYS
	{ "Use F9..F11 as hotkeys",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_default_emu_f_hotkeys, NULL },
#endif
	{ "Reset hotkey type",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_reset_hotkey_type},
	{ NULL }
};
static const struct menu_st menu_debug[] = {
	{ "MEGA65 model",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_mega65_model },
	{ "Fastboot (turbo on boot)",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int, (void*)&configdb.fastboot },
#ifdef HAS_UARTMON_SUPPORT
	{ "Start umon on " UMON_DEFAULT_PORT,
					XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_start_umon, NULL },
#endif
#ifdef XEMU_ARCH_WIN
	{ "System console",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_sysconsole, NULL },
#endif
	{ "Allow freezer trap",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int, (void*)&configdb.allowfreezer },
	{ "Try external ROM first",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int, (void*)&rom_from_prefdir_allowed },
	{ "HDOS virtualization",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_hdos_virt, NULL },
#if 0
	// removed now, because it's misleading, would require an xemu-restart anyway ...
	{ "mega65.d81 mount from SD",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int, (void*)&configdb.defd81fromsd },
#endif
#if 0
	{ "Display enable VIC reg",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_displayenable, NULL },
#endif
	{ "Matrix mode",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_matrix_mode, NULL },
	{ "Matrix hotkey disable",	XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_SEPARATOR |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int, (void*)&configdb.matrixdisable },
	{ "Emulation state info",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_emu_info },
#ifdef HAVE_XEMU_EXEC_API
	{ "Browse system folder",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_native_os_prefdir_browser, NULL },
#endif
	{ "Dump main RAM info file",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_dump_memory },
	{ "Dump colour RAM into file",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_dump_colram },
	{ "Dump hyperRAM into file",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_dump_hyperram },
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
#ifdef SD_CONTENT_SUPPORT
static const struct menu_st menu_sdcard[] = {
	{ "Re-format SD image",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_format_sdcard },
	{ "Update files on SD image",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_update_sdcard },
	{ NULL }
};
#endif
static const struct menu_st menu_drv8[] = {
	{ "Attach D81",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_attach_d81, (void*)0 },
	{ "Attach default D81",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_attach_default_d81, (void*)0 },
	{ "Attach default on-SD D81",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_attach_default_d81, (void*)(0 + 1024) },
	{ "Detach D81",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_detach_d81, (void*)0 },
	{ "Create and attach new D81",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_attach_d81, (void*)(0 | 0x80) },
	{ NULL }
};
static const struct menu_st menu_drv9[] = {
	{ "Attach D81",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_attach_d81, (void*)1 },
#if 0
	// Currently, there is no default disk image for drv9 too much.
	{ "Attach default D81",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_attach_default_d81, (void*)1 },
#endif
	{ "Detach D81",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_detach_d81, (void*)1 },
	{ "Create and attach new D81",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_attach_d81, (void*)(1 | 0x80) },
	{ NULL }
};
static const struct menu_st menu_disks[] = {
	{ "Drive-8",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_drv8    },
	{ "Drive-9",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_drv9    },
#ifdef SD_CONTENT_SUPPORT
	{ "SD-card",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_sdcard  },
#endif
	{ "Cartridge",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_cartridge },
	{ NULL }
};
static const struct menu_st menu_audio_volume[] = {
	{ "100%",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_volume, (void*) 100 },
	{ "90%",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_volume, (void*)  90 },
	{ "80%",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_volume, (void*)  80 },
	{ "70%",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_volume, (void*)  70 },
	{ "60%",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_volume, (void*)  60 },
	{ "50%",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_volume, (void*)  50 },
	{ "40%",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_volume, (void*)  40 },
	{ "30%",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_volume, (void*)  30 },
	{ "20%",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_volume, (void*)  20 },
	{ "10%",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_volume, (void*)  10 },
	{ NULL }
};
static const struct menu_st menu_audio_sids[] = {
	{ "SID @ $D400",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_sids_enabled, (void*)1 },
	{ "SID @ $D420",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_sids_enabled, (void*)2 },
	{ "SID @ $D440",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_sids_enabled, (void*)4 },
	{ "SID @ SD460",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_sids_enabled, (void*)8 },
	{ NULL }
};
static const struct menu_st menu_audio_output[] = {
	{ "HDMI / speaker",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_output, (void*)AUDIO_OUTPUT_SPEAKERS },
	{ "Headphones",			XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_audio_output, (void*)AUDIO_OUTPUT_HEADPHONES },
	{ NULL }
};
static const struct menu_st menu_audio[] = {
	{ "Audio enabled",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int_inverted, (void*)&configdb.nosound },
	{ "Force mono downmix",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_mono_downmix, NULL },
	{ "Restore mixer to default",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, audio65_reset_mixer },
	{ "Clear audio registers",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, audio65_clear_regs },
	{ "OPL3 emulation",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int_inverted, (void*)&configdb.noopl3 },
	{ "Emulated SIDs",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_audio_sids   },
	{ "Emulator volume level",	XEMUGUI_MENUID_SUBMENU,		NULL, menu_audio_volume },
	{ "Emulated audio output",	XEMUGUI_MENUID_SUBMENU,		NULL, menu_audio_output },
	{ NULL }
};
#ifndef XEMU_ARCH_HTML
static const struct menu_st menu_config[] = {
	{ "Confirmation on exit/reset",	XEMUGUI_MENUID_CALLABLE | XEMUGUI_MENUFLAG_SEPARATOR |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_toggle_int_inverted, (void*)&i_am_sure_override },
	//{ "Load saved default config",XEMUGUI_MENUID_CALLABLE,	xemugui_cb_cfgfile, (void*)XEMUGUICFGFILEOP_LOAD_DEFAULT },
	{ "Save config as default",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_cfgfile, (void*)XEMUGUICFGFILEOP_SAVE_DEFAULT },
	//{ "Load saved custom config",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_cfgfile, (void*)XEMUGUICFGFILEOP_LOAD_CUSTOM  },
	{ "Save config as custom file",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_cfgfile, (void*)XEMUGUICFGFILEOP_SAVE_CUSTOM  },
	{ "Restore default config",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_restore_config },
	{ NULL }
};
#endif
static const struct menu_st menu_main[] = {
	{ "Display",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_display	},
	{ "Input devices",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_inputdevices	},
	{ "Audio",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_audio	},
	{ "Disks / Cart",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_disks	},
	{ "Reset / ROM switching",	XEMUGUI_MENUID_SUBMENU,		NULL, menu_reset	},
	{ "Debug / Advanced",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_debug	},
#ifndef XEMU_ARCH_HTML
	{ "Configuration",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_config	},
#endif
#ifdef HAVE_XEMU_EXEC_API
	{ "Help (online)",		XEMUGUI_MENUID_SUBMENU,		NULL, menu_help },
#endif
	{ "Run PRG directly",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_run_prg_by_browsing },
#ifdef CBM_BASIC_TEXT_SUPPORT
	{ "Save BASIC as text",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_save_basic_as_text },
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
