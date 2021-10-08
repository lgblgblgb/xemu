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

static const char *const rom_names[] = {
//	0		1		2		3		4
	"Closed-ROMs",	"Open-ROMs",	"Xemu-ROMs",	"?unknown?",	"?before-boot?"
};
const char *rom_name = rom_names[4];


static int rom_detect_try ( const Uint8 *rom, const Uint8 rom_id )
{
	if (*rom != rom_id)
		return -1;
	int ret = 0;
	for (int a = 0; a < 6; a++) {
		rom++;
		if (*rom >= '0' && *rom <= '9')
			ret = ret * 10 + *rom - '0';
		else
			return -1;
	}
	return ret;
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
	const int res_open   = rom_detect_try(rom + 0x10, 0x4F);	// 'O' (0x4F) at ofs $10 + followed by "rom date": open-ROMs
	const int res_closed = rom_detect_try(rom + 0x16, 0x56);	// 'V' (0x56) at ofs $16 + followed by "rom date": closed-ROMs
	rom_is_stub = 0;
	if (res_open >= 0 && res_closed <  0) {
		rom_is_openroms = 1;
		rom_date = res_open;
		rom_name = rom_names[1];
		goto ok;
	}
	if (res_open <  0 && res_closed >= 0) {
		rom_is_openroms = 0;
		rom_date = res_closed;
		rom_is_stub = !strncmp((const char*)rom + 0x16 + 7, "Xemu", 3);
		rom_name = rom_names[rom_is_stub ? 2 : 0];
		goto ok;
	}
	if (res_open <  0 && res_closed <  0) {
		rom_is_openroms = 0;
		rom_date = -1;
		rom_name = rom_names[3];
		return;
	}
	ERROR_WINDOW("Serious problem: ROM can be identified both as open and closed ROM?!");
	rom_is_openroms = 0;
	rom_date = -1;
	rom_name = rom_names[3];
	return;
ok:
	DEBUGPRINT("ROM: %s detected with version %d" NL, rom_name, rom_date);
}
