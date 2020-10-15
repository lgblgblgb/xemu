/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_files.h"
#include "xemu/emutools_config.h"
#include "enterprise128.h"
#include "roms.h"
#include "cpu.h"
#include "emu_rom_interface.h"
#include <errno.h>
#include <string.h>


static const Uint8 xep_rom_image[] = {
#include "rom/ep128/xep_rom.hex"
};
int xep_rom_seg = -1;
int xep_rom_addr;
const char *rom_name_tab[0x100];
static int reloading = 0;	// allows to re-load ROM config run-time, this non-zero after the first call of roms_load()

#define ROM_REQUEST_LIST_MAX_SIZE 32
static struct {
	Uint8 seg;
	const char *fn;
} rom_request_list[ROM_REQUEST_LIST_MAX_SIZE];
static int rom_request_list_size = 0;


const char *rom_parse_opt ( const char *optname, const char *optvalue )
{
	//DEBUGPRINT("PARSE_ROM: optname=%s optvalue=%s" NL, optname, optvalue);
	const char *p = strchr(optname, '@');
	if (p == NULL)
		return "rom option should specify segment, ie: rom@XX (XX=hex)";
	int seg = 0;
	while (*p) {
		if ((*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f'))
			seg = (seg << 4) + (*p & 0xF) + 9;
		else if (*p >= '0' && *p <= '9')
			seg = (seg << 4) + *p - '0';
		else
			return "Invalid HEX value character after rom@ in the option name";
		if (seg >= 0xFC)
			return "Invalid segment specified after the rom@ option";
		p++;
	}	
	if (rom_request_list_size >= ROM_REQUEST_LIST_MAX_SIZE)
		return "too many -rom options!";
	rom_request_list[rom_request_list_size].seg = seg;
	rom_request_list[rom_request_list_size].fn  = xemu_strdup(optvalue);
	rom_request_list_size++;
	return NULL;
}

int sram_save_segment ( int seg )
{
	char fn[PATH_MAX];
	sprintf(fn, SRAM_BACKUP_FILE_FORMAT, seg);
	return xemu_save_file(fn, memory + (seg << 14), 0x4000, "Cannot save SRAM segment");
}

int sram_load_segment ( int seg )
{
	char fn[PATH_MAX];
	sprintf(fn, SRAM_BACKUP_FILE_FORMAT, seg);
	return xemu_load_file(fn, memory + (seg << 14), 0x4000, 0x4000, "Cannot load SRAM segment") != 0x4000;
}

int sram_save_all_segments ( void )
{
	int ret = 0;
	for (int a = 0; a < 0x100; a++ )
		if (memory_segment_map[a] == SRAM_SEGMENT)
			ret += sram_save_segment(a) ? 1 : 0;
	return ret;
}


static int load_rom_image ( int seg, const char *fn )
{
	if (seg < 0 || seg >= 0xFC)
		FATAL("Invalid ROM segment: %02Xh", seg);
	if (!fn || !*fn)
		FATAL("Invalid ROM name: NULL or empty string for segment #%02Xh", seg);
	int size = xemu_load_file(fn, NULL, 0x4000, 0x400000 - 0x10000, "Cannot open/load requested ROM");
	if (size <= 0) {
		if (!strcmp(fn, DEFAULT_ROM_FN) && seg == 0) { // this should be the auto-install functionality, with downloading stuff? TODO
		}
		return -1;
	}
	if ((size & 0x3FFF)) {
		ERROR_WINDOW("BAD ROM image \"%s\": length is not multiple of 16Kbytes!", xemu_load_filepath);
		free(xemu_load_buffer_p);
		return -1;
	}
	size >>= 14;	// size in segments from this point
	// Note: no need to check overflow of the loaded ROM, as if it would hit VRAM, it's not UNUSED_SEGMENT ...
	for (int i = seg; i < seg + size; i++) {
		if (memory_segment_map[i] != UNUSED_SEGMENT) {
			ERROR_WINDOW("ERROR while loading ROM to segment #%02Xh: segment is already in use\n%s", i, xemu_load_filepath);
			free(xemu_load_buffer_p);
			return -1;
		}
	}
	memcpy(memory + (seg << 14), xemu_load_buffer_p, size << 14);
	free(xemu_load_buffer_p);
	rom_name_tab[seg] = xemu_strdup(xemu_load_filepath);
	for (int i = seg; i < seg + size; i++) {
		// check if ROM image contains XEP128_ROM segment signature, if so, try to use XEP ROM from here
		if (!memcmp(memory + (i << 14), "XEP__ROM", 8) && xep_rom_seg == -1) {
			xep_rom_seg = i;
			memory_segment_map[i] = XEPROM_SEGMENT;
		} else
			memory_segment_map[i] = ROM_SEGMENT;
	}
	return 0;
}



/* This function also re-initializes the whole memory! Do not call it after you defined RAM for the system, but only before! */
int roms_load ( void )
{
	//int seg, last = 0;
	//char path[PATH_MAX + 1];
	if (reloading)	// in case of already defined (reloading) memory model, we want to back our SRAM segments up - if any at all ...
		sram_save_all_segments();
	for (int seg = 0; seg < 0x100; seg++ ) {
		memory_segment_map[seg] = (seg >= 0xFC ? VRAM_SEGMENT : UNUSED_SEGMENT);	// 64K VRAM is default, you cannot override that!
		if (reloading && rom_name_tab[seg])
			free((void*)rom_name_tab[seg]); // already defined (reloading) situation, we want to free used memory as well
		rom_name_tab[seg] = NULL;
	}
	reloading = 1;	// set reloading flag, in next invocation of roms_load(), it will be done in config reload mode!
	memset(memory, 0xFF, 0x400000);
	xep_rom_seg = -1;
	// Load requested list at cmdline+cfgfile parser step
	for (int i = 0; i < rom_request_list_size; i++) {
		load_rom_image(rom_request_list[i].seg, rom_request_list[i].fn);
		free((void*)rom_request_list[i].fn);
	}
	if (memory_segment_map[0] != ROM_SEGMENT && memory_segment_map[0] != UNUSED_SEGMENT) {
		ERROR_WINDOW("Invalid config, segment zero must be ROM");
		return 1;
	}
	if (memory_segment_map[0] == UNUSED_SEGMENT) {
		// no valid config (no ROM in segment zero)
		if (rom_request_list_size) {
			// if there was some ROM config (rom_request_list_size is non-zero) then it's
			// a fatal error resulted this bad configuration!
			ERROR_WINDOW("Invalid config: No ROM image was defined for segment 0!");
			return 1;
		}
		// ... though if there was no ROM config, some defaults should be nice to try,
		// so here it is:
		DEBUGPRINT("CONFIG: ROM: no ROM was defined by user. Trying the default one for segment 0: %s" NL, DEFAULT_ROM_FN);
		if (load_rom_image(0, DEFAULT_ROM_FN))
			return 1;
		// ... if it also fails, end of game.
	}
	rom_request_list_size = 0;
	/* XEP ROM: guess where to place it, or disable it ... */
	if (!xemucfg_get_bool("noxeprom")) {
		// XEP ROM is enabled with 'xeprom' directive
		if (xep_rom_seg == -1) {	// not assigned manually, try to find a place for it ...
			for (int seg = 0; seg < 0xFC; seg++) {
				if (memory_segment_map[seg] == UNUSED_SEGMENT) {
					xep_rom_seg = seg;
					break;
				}
			}
			if (xep_rom_seg != -1)
				DEBUGPRINT("CONFIG: ROM: automatic XEP ROM image placer selected segment is %02Xh" NL, xep_rom_seg);
			else
				DEBUGPRINT("CONFIG: ROM: could not find place for XEP ROM ..." NL);
		}
	} else {
		// XEP ROM is disabled (with 'xeprom' directive), _IF_ it was not assigned manually
		if (xep_rom_seg == -1) {
			DEBUGPRINT("CONFIG: ROM: XEP ROM is disabled by configuration!" NL);
			INFO_WINDOW("XEP internal ROM image is disabled by configuration.\nXep128 will work, but no XEP feature will be available.");
		}
	}
	/* XEP ROM: now install our internal ROM, if it's allowed/OK to do so */
	if (xep_rom_seg > 0) {
		if (memory_segment_map[xep_rom_seg] == UNUSED_SEGMENT || memory_segment_map[xep_rom_seg] == XEPROM_SEGMENT) {
			xep_rom_addr = xep_rom_seg << 14;
			memset(memory + xep_rom_addr, 0, 0x4000);
			memcpy(memory + xep_rom_addr, xep_rom_image, sizeof xep_rom_image);
			memory_segment_map[xep_rom_seg] = XEPROM_SEGMENT;
			DEBUGPRINT("CONFIG: ROM: XEP internal ROM image has been installed in segment %02Xh" NL, xep_rom_seg);
			xep_set_default_device_name(NULL);
		} else {
			DEBUGPRINT("CONFIG: ROM: XEP internal ROM image CANNOT be installed because segment %02Xh is used!!" NL, xep_rom_seg);
			ERROR_WINDOW("XEP internal ROM image cannot be installed.\nXep128 will work, but no XEP feature will be available.");
			xep_rom_seg = -1;
		}
	} else
		xep_rom_seg = -1;
	DEBUGPRINT("CONFIG: ROM init end." NL);
	return 0;
}
