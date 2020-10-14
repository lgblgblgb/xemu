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
#include "enterprise128.h"
#include "roms.h"
#include "cpu.h"
#include "configuration.h"
#include "emu_rom_interface.h"
#include <errno.h>


static const Uint8 xep_rom_image[] = {
#include "rom/ep128/xep_rom.hex"
};
int xep_rom_seg = -1;
int xep_rom_addr;
const char *rom_name_tab[0x100];
static int reloading = 0;	// allows to re-load ROM config run-time, this non-zero after the first call of roms_load()



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

/* This function also re-initializes the whole memory! Do not call it after you defined RAM for the system, but only before! */
int roms_load ( void )
{
	int seg, last = 0;
	//char path[PATH_MAX + 1];
	if (reloading)	// in case of already defined (reloading) memory model, we want to back our SRAM segments up - if any at all ...
		sram_save_all_segments();
	for (seg = 0; seg < 0x100; seg++ ) {
		memory_segment_map[seg] = (seg >= 0xFC ? VRAM_SEGMENT : UNUSED_SEGMENT);	// 64K VRAM is default, you cannot override that!
		if (reloading && rom_name_tab[seg])
			free((void*)rom_name_tab[seg]); // already defined (reloading) situation, we want to free used memory as well
		rom_name_tab[seg] = NULL;
	}
	reloading = 1;	// set reloading flag, in next invocation of roms_load(), it will be done in config reload mode!
	memset(memory, 0xFF, 0x400000);
	xep_rom_seg = -1;
	for (seg = 0; seg < 0x100; seg++ ) {
		void *option = config_getopt("rom", seg, NULL);
		if (option) {
			const char *name;
			int lseg = seg;
			config_getopt_pointed(option, &name);
			if (!strcasecmp(name, "XEP") && seg) {
				if (memory_segment_map[seg] == UNUSED_SEGMENT) {
					DEBUG("CONFIG: ROM: segment %02Xh assigned to internal XEP ROM" NL, seg);
					xep_rom_seg = seg;
					memory_segment_map[seg] = XEPROM_SEGMENT;
				} else
					ERROR_WINDOW("XEP ROM forced segment assignment cannot be done since segment %02X is not unused", seg);
				continue;
			}
			DEBUG("CONFIG: ROM: segment %02Xh file %s" NL, seg, name);
			int size = xemu_load_file(name, NULL, 0x4000, 0x400000 - 0x10000, "Cannot open/load requested ROM");
			if (size <= 0) {
				if (!strcmp(name, DEFAULT_ROM_FN)) { // this should be the auto-install functionality, with downloading stuff?
				}
				return -1;
			}
			if ((size & 0x3FFF)) {
				ERROR_WINDOW("BAD ROM image \"%s\": length is not multiple of 16Kbytes!", xemu_load_filepath);
				return -1;
			}
			DEBUG("CONFIG: ROM: ... file path is %s size: %Xh." NL, xemu_load_filepath, size);
			size >>= 14;
			//if (rom_name_tab[seg])
			//	free((void*)rom_name_tab[seg]);
			rom_name_tab[seg] = xemu_strdup(xemu_load_filepath);
			Uint8 *buffer = xemu_load_buffer_p;
			for (;;) {
				// Note: lseg overflow is not needed to be tested, as VRAM marks will stop reading of ROM image in the worst case ...
				if (memory_segment_map[lseg] != UNUSED_SEGMENT) {
					free(xemu_load_buffer_p);
					forget_emu_file(xemu_load_filepath);
					ERROR_WINDOW("While reading ROM image \"%s\" into segment %02Xh: already used segment (\"%s\")!", xemu_load_filepath, lseg, memory_segment_map[lseg]);
					return -1;
				}
				memcpy(memory + (lseg << 14), buffer, 0x4000);
				buffer += 0x4000;
				// check if ROM image contains XEP128_ROM segment signature, if so, try to use XEP ROM from here
				if (!memcmp(memory + (lseg << 14), "XEP__ROM", 8) && xep_rom_seg == -1) {
					xep_rom_seg = lseg;
					memory_segment_map[lseg] = XEPROM_SEGMENT;
				} else
					memory_segment_map[lseg] = ROM_SEGMENT;
				if (lseg > last)
					last = lseg;
				if (!--size)
					break;
				lseg++;
			}
			free(xemu_load_buffer_p);
			forget_emu_file(xemu_load_filepath);
		} else if (!seg) {
			ERROR_WINDOW("Fatal ROM image error: No ROM defined for segment 00h, no EXOS is requested!");
			return -1;
		}
	}
	/* XEP ROM: guess where to place it, or disable it ... */
	if (config_getopt_int("xeprom")) {
		// XEP ROM is enabled with 'xeprom' directive
		if (xep_rom_seg == -1) {	// not assigned manually, try to find a place for it ...
			xep_rom_seg = last + 1;	// ... with simply using the segment after the last used ROM segment
			DEBUGPRINT("CONFIG: ROM: automatic XEP ROM image placer selected segment is %02Xh" NL, xep_rom_seg);
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
			xep_set_default_device_name(NULL);
			DEBUGPRINT("CONFIG: ROM: XEP internal ROM image has been installed in segment %02Xh" NL, xep_rom_seg);
		} else {
			DEBUGPRINT("CONFIG: ROM: XEP internal ROM image CANNOT be installed because segment %02Xh is used!!" NL, xep_rom_seg);
			ERROR_WINDOW("XEP internal ROM image cannot be installed.\nXep128 will work, but no XEP feature will be available.");
			xep_rom_seg = -1;
		}
	} else
		xep_rom_seg = -1;
	return 0;
}
