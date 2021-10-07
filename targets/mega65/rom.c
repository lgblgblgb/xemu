/* Part of the Xemu project.  https://github.com/lgblgblgb/xemu
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
#include "rom.h"


int rom_date = 0;
int rom_is_openroms = 0;
int rom_is_stub = 0;



static int try_date_digits ( const Uint8 *rom )
{
	rom_date = 0;
	for (int a = 0; a < 6; a++) {
		rom++;
		if (*rom >= '0' && *rom <= '9') {
			rom_date = rom_date * 10 + *rom - '0';
		} else {
			rom_date = -1;
			break;
		}
	}
	return rom_date;
}


void rom_detect_date ( const Uint8 *rom )
{
	if (!rom) {
		DEBUGPRINT("ROM: version check is disabled (NULL pointer), previous version info: %d" NL, rom_date);
		return;
	}
	sha1_hash_str hash_str;
	sha1_checksum_as_string(hash_str, rom, 0x20000);
	DEBUGPRINT("ROM: SHA1 checksum is %s" NL, hash_str);
	if (rom[0x16] == 0x56 && try_date_digits(rom + 0x16) >= 0) {		// 'V' at ofs $16 for closed ROMs
		rom_is_openroms = 0;
	} else if (rom[0x10] == 0x4F && try_date_digits(rom + 0x10) >= 0) {	// 'O' at ofs $10 for open ROMs
		rom_is_openroms = 1;
	} else {
		DEBUGPRINT("ROM: version check failed (no leading 'V' or 'O' at ROM ofs $10/$16)" NL);
		rom_is_openroms = 0;
		return;
	}
	DEBUGPRINT("ROM: version check succeeded, detected version: %d (%s)" NL, rom_date, rom_is_openroms ? "Open-ROMs" : "Closed-ROMs");
}


void rom_clear_rom ( Uint8 *rom )
{
	memset(rom, 0, 0x20000);
}


#include "lamerom.c"


int rom_make_xemu_stub_rom ( Uint8 *rom )
{
	static const char *msg =
		/*
		-01234567890123456789012345678901234567890123456789012345678901234567890123456789 */
		"~"
		"Your emulated MEGA65 seems to work, welcome to the MEGA65 emulation of the\n"
		"X-Emulators (Xemu for short) framework! (yes, it's Xemu and not Zemu)~\n\n"

		"This message comes from Xemu's built-in 'stub' ROM, ready to be replaced with\n"
		"some real ROM to be able to do anything useful. Unfortunately, because of legal\n"
		"reasons, it's not possible to include the real ROM. MEGA65 project has an\n"
		"on-going effort to write an open-source free ROM called 'open-ROMs' project,\n"
		"however it's not yet ready for general usage at all. Thus you almost certainly\n"
		"need the 'proprietary' ROM, often called 'closed-ROMs' project, it's an enhanced\n"
		"and bug-fixed version of the original C65 ROM, with improved BASIC and other\n"
		"MEGA65 features. However being a derivate work based on original C65 ROM, it\n"
		"cannot be used without the blessing of the repspective owner of the original\n"
		"Commodore(TM) rights, it's not freely distributable, and certainly cannot be\n"
		"included in an open-source GNU/GPL emulator, like Xemu. That ROM on the other\n"
		"hand is legally licensed to any (real) MEGA65 owners licensed from the copyright\n"
		"holders.\n"
	;
	rom_clear_rom(rom);
	// Make a fake closed-rom version identifier Xemu to stop complain later about its missing nature
	memcpy(rom + 0x16, "V920000", 7);
	// Note: we use the "C64 kernal" port of the C65 ROM since C65/MEGA65 starts in C64 mode.
	// Fortunately it's aligned such a way as would be the real address when used as kernal ROM in C64 mode.
	rom[0xFFFE] = rom[0xFFFC] = rom[0xFFFA] = 0x00;
	rom[0xFFFF] = rom[0xFFFD] = rom[0xFFFB] = 0xE0;
	memcpy(rom + 0xE000, lamerom, sizeof lamerom);
	Uint8 *rv = rom + 0x10000;
	Uint8 *rc = rom + 0x18000;
	const Uint8 normal_colour = 0xF;	// light grey
	const Uint8 hi_colour = 7;		// yellow
	memset(rv, 0x20, 2048);			// default char (space)
	memset(rc, normal_colour, 2048);	// default background colour
	const char *m = msg;
	int highlight = 0;
	while (*m) {
		Uint8 c = *m++;
		if (c == '~') {
			highlight = !highlight;
			continue;
		} else if (c == '\n') {
			int jump = 80 - (((int)(rv - rom) - 0x10000) % 80);
			rv += jump;
			rc += jump;
			continue;
		} else if (c >= 'a' && c <= 'z') {
			c -= 96;	// some light ASCII -> PETSCII conversion for lower/upper case charset ...
		}
		*rv++ = c;
		*rc++ = highlight ? hi_colour : normal_colour;
	}
	return 0xE000;
}
