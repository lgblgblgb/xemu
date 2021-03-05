/* A brave try to emulating Commodore 900 ...
 * Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
 * Copyright (C)2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "z8k1.h"
#include "z8010.h"
#include "xemu/emutools_config.h"
#include "xemu/emutools_files.h"


#define BOOT_ROM "#c900-boot-combined.rom"

static Uint8 memory[0x10000];


Uint8 z8k1_read_byte_cb ( int seg, Uint16 ofs )
{
	if (seg)
		FATAL("Currently only segment-0 can be used, given = seg#$%02X", seg);
	if (ofs >= 0x8000)
		FATAL("Not handled memory area at offset $%04X", ofs);
	return memory[ofs];
}

Uint16 z8k1_read_word_cb ( int seg, Uint16 ofs )
{
	ofs &= 0xFFFE;
	if (seg)
		FATAL("Currently only segment-0 can be used, given = seg#$%02X", seg);
	if (ofs >= 0x8000)
		FATAL("Not handled memory area at offset $%04X", ofs);
	return (memory[ofs] << 8) | memory[ofs + 1];
}

Uint16 z8k1_read_code_cb ( int seg, Uint16 ofs )
{
	return z8k1_read_word_cb(seg, ofs);
}



void clear_emu_events ( void )
{
	printf("CLEAR EMU EVENTS!\n");
}

static char *bootrom_name;


int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The new-territory-for-me Commodore 900 emulator from LGB");
	xemucfg_define_str_option("bootrom", BOOT_ROM, "Set BOOT ROM to be loaded", &bootrom_name);
	if (xemucfg_parse_all(argc, argv))
		return 1;
	if (xemu_load_file(bootrom_name, memory, 0x8000, 0x8000, "The boot ROM of Commodore 900 (combined HI+LO)") != 0x8000)
		return 1;
	z8k1_init();
	puts("\nCURRENT GOAL: only disassembly the BOOT ROM and compare it with well known disasm result done by others on boot ROMs\n");
	z8k1_reset();
	z8010_init();
	z8010_reset();
	for (;;) {
		if (z8k1_step(0) < 0)
			break;
	}
	return 0;
}

