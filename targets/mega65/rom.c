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
#include "xemu/emutools_files.h"
#include "memcontent.h"

#define CHARACTER_SET_DEFINER_8X8 const Uint8 vga_font_8x8[2048]
#include "xemu/vgafonts.c"


int rom_date = 0;
int rom_is_openroms = 0;
int rom_is_stub = 0;

int rom_stubrom_requested = 0;
int rom_initrom_requested = 0;
int rom_is_overriden = 0;
static Uint8 *external_image = NULL;

static const char _rom_name_closed[]	= "Closed-ROMs";
static const char _rom_name_open[]	= "Open-ROMs";
static const char _rom_name_xemu[]	= "Xemu-ROMs";
static const char _rom_name_bad[]	= "?unknown?";
static const char _rom_name_preboot[]	= "?before-boot?";

const char *rom_name = _rom_name_preboot;


void rom_clear_reports ( void )
{
	rom_is_overriden = 0;
	rom_is_openroms = 0;
	rom_is_stub = 0;
	rom_date = 0;
	rom_name = _rom_name_preboot;
}


void rom_unset_requests ( void )
{
	rom_stubrom_requested = 0;
	rom_initrom_requested = 0;
	rom_load_custom(NULL);	// to cancel possible already set custom ROM
}


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
	sha1_checksum_as_string(hash_str, rom, MEMINITDATA_INITROM_SIZE);
	DEBUGPRINT("ROM: SHA1 checksum is %s" NL, hash_str);
	const int res_open   = rom_detect_try(rom + 0x10, 0x4F);	// 'O' (0x4F) at ofs $10 + followed by "rom date": open-ROMs
	const int res_closed = rom_detect_try(rom + 0x16, 0x56);	// 'V' (0x56) at ofs $16 + followed by "rom date": closed-ROMs
	rom_is_stub = 0;
	rom_is_openroms = 0;
	rom_date = -1;
	rom_name = _rom_name_bad;
	if (res_open >= 0 && res_closed <  0) {
		rom_date = res_open;
		rom_is_openroms = 1;
		rom_name = _rom_name_open;
		goto ok;
	}
	if (res_open <  0 && res_closed >= 0) {
		rom_date = res_closed;
		if (!strncmp((const char*)rom + 0x16 + 7, "Xemu", 3)) {
			rom_is_stub = 1;
			rom_name = _rom_name_xemu;
		} else
			rom_name = _rom_name_closed;
		goto ok;
	}
	if (res_open <  0 && res_closed <  0)
		DEBUGPRINT("ROM: version check failed (no leading 'V' or 'O' at ROM ofs $10/$16)" NL);
	else
		ERROR_WINDOW("Serious problem: ROM can be identified both as open and closed ROM?!");
	return;
ok:
	DEBUGPRINT("ROM: %s detected with version %d" NL, rom_name, rom_date);
}


void rom_clear_rom ( Uint8 *rom )
{
	memset(rom, 0, MEMINITDATA_INITROM_SIZE);
}


static const Uint8 xemu_stub_rom[] = {
#include "rom/mega65-xemu-stub-rom.cdata"
};


void rom_make_xemu_stub_rom ( Uint8 *rom, const char *save_file )
{
	// The message is line-wrapped by the ROM maker code itself. '\n' works as forcing into a new line,
	// as expected, '~' toggles highlighted/normal mode. Text MUST be ended with a '\n'!
	// The text must be larger in result than 25 lines, since the ROM code expects scrolling/etc.
	static const char *msg =
		"~Your emulated MEGA65 seems to work, welcome to the MEGA65 emulation of the "
		"X-Emulators (Xemu for short) framework!~ Yes, it's ~Xemu~ and ~not~ Zemu.\n\n"

		"Use the cursor up/down keys to scroll this message.\n\n"

		"~Quick jump-start for you:~\n\n"

		"~TL;DR~ if you don't want to read all of these, you can use Xemu's menu to open "
		"a web page with your default browser which can help to get the needed ROM. To do this "
		"right click into the emulator window, choose \"~Help (online)~\" and \"~Xemu MEGA65 help "
		"page~\" within that sub-menu.\n\n"

		"~The long story:~\n\n"

		"This message comes from Xemu's built-in \"stub\" ROM, ready to be replaced with "
		"some real ROM to be able to do anything useful. The reason you see this running "
		"now is the fact of lacking MEGA65.ROM file on your emulated SD-card. Once you "
		"have a MEGA65.ROM file installed it will be used instead.\n\n"

		"Unfortunately, because of legal "
		"reasons, it's not possible to include the real ROM. MEGA65 project has an "
		"on-going effort to write an open-source free ROM called \"~open-ROMs~\" project, "
		"however it's not yet ready for general usage at all.\n\n"

		"Thus you ~almost certainly~ "
		"need the \"proprietary\" ROM, often called \"~closed-ROMs~\" project, it's an enhanced "
		"and bug-fixed version of the original C65 ROM, with improved BASIC and other "
		"MEGA65 features. However being a derivate work based on original C65 ROM, it "
		"cannot be used without the blessing of the repspective owner of the original "
		"Commodore(TM) rights, it's not freely distributable, and certainly cannot be "
		"included in an open-source GNU/GPL emulator, like Xemu. That ROM on the other "
		"hand is legally licensed to any (real) MEGA65 owners by the copyright holders.\n\n"

		"~About this ROM:~\n\n"

		"This ROM is an actual machine language code (contained by Xemu) written in "
		"assembly. Easter egg: if you're patient enough to even read this, you may want to "
		"try the 's' key, it won't do anything useful now (restart), but may do something "
		"other in the future. Who knows.\n\n"

		"Thanks for your patience and understanding.\n\n"
		"- LGB (Xemu's author)\n\n"
		"~<END OF TEXT>~"
		/* --- it's important to have an '\n' at the end! --- */
		"\n"
	;
	int dyn_rom = 0;
	if (!rom) {
		rom = xemu_malloc(MEMINITDATA_INITROM_SIZE);
		dyn_rom = 1;
	}
	rom_clear_rom(rom);
	// Make a fake closed-rom version identifier Xemu to stop complain later about its missing nature
	strcpy((char*)rom + 0x16, "V920000XemuStubROM! Part of the Xemu project.");
	memcpy(rom + 0xF800, vga_font_8x8, sizeof vga_font_8x8);
	// Note: we use the "C64 kernal" port of the C65 ROM since C65/MEGA65 starts in C64 mode.
	// Fortunately it's aligned such a way as would be the real address when used as kernal ROM in C64 mode.
	rom[0xFFFF] = rom[0xFFFD] = rom[0xFFFB] = 0xE0;	// high bytes of vectors
	rom[0xFFFC] = 0x00;	// reset vector low byte
	rom[0xFFFA] = 0x03;	// NMI vector low byte
	rom[0xFFFE] = 0x06;	// IRQ vector low byte
	memcpy(rom + 0xE000, xemu_stub_rom, sizeof xemu_stub_rom);
	const Uint8 normal_colour = 0xF;	// light grey
	const Uint8 hi_colour = 7;		// yellow
	memset(rom + 0x10000, 0x20, 0x8000);		// default char (space)
	memset(rom + 0x18000, normal_colour, 0x8000);	// default background colour
	// This madness wraps the text and renders into 80 column width screen size (though without height limit)
	const char *m = msg;
	Uint8 colour = normal_colour;
	int pos = 0;
	while (*m) {
		Uint8 c = *m++;
		if (c == '~') {
			colour = (colour == normal_colour) ? hi_colour : normal_colour;
			continue;
		} else if (c == '\n') {
			pos += 80 - (pos % 80);
			continue;
		}
		if (c == ' ' && *m > ' ') {
			const int xpos = pos % 80;
			if (!xpos)
				continue;
			const char *p = m;
			int l = 0;
			while (*p++ > ' ')
				l++;
			if (l + xpos > 79)
				pos += 79 - xpos;
		}
		rom[0x10000 + pos] = c;
		rom[0x18000 + pos] = colour;
		pos++;
	}
	rom[0x10000 + pos] = 0xFF;	// end of text marker, should be after '\n' in the source text
	if (save_file)
		xemu_save_file(save_file, rom, MEMINITDATA_INITROM_SIZE, NULL);
	if (dyn_rom)
		free(rom);
}


int rom_load_custom ( const char *fn )
{
	if (!fn || !*fn) {
		DEBUGPRINT("ROM: unsetting custom ROM (clear request)" NL);
		if (external_image) {
			free(external_image);
			external_image = NULL;
		}
		return 0;
	} else if (xemu_load_file(fn, NULL, MEMINITDATA_INITROM_SIZE, MEMINITDATA_INITROM_SIZE, "Failed to load external ROM on user's request.\nUsing the default installed, instead.") > 0) {
		DEBUGPRINT("ROM: custom ROM load was OK, setting custom ROM" NL);
		if (external_image) {
			memcpy(external_image, xemu_load_buffer_p, MEMINITDATA_INITROM_SIZE);
			free(xemu_load_buffer_p);
		} else {
			external_image = xemu_load_buffer_p;
		}
		xemu_load_buffer_p = NULL;
		rom_stubrom_requested = 0;
		rom_initrom_requested = 0;
		return 1;
	}
	DEBUGPRINT("ROM: custom ROM setting failed, not touching custom ROM request setting (now: %s)" NL, external_image ? "SET" : "UNSET");
	return 0;
}


// Called by hypervisor on exiting the first (reset) trap.
// Thus this is an ability to override the ROM what HICKUP loaded for us with some another one.
// Return value: negative: no custom ROM is needed to be loaded,
//               otherwise, the RESET vector of the ROM (CPU PC value)
int rom_do_override ( Uint8 *rom )
{
	rom_is_overriden = 0;
	if (rom_stubrom_requested) {
		DEBUGPRINT("ROM: using stub-ROM was forced" NL);
		rom_make_xemu_stub_rom(rom, XEMU_STUB_ROM_SAVE_FILENAME);
		goto overriden;
	}
	if (rom_initrom_requested) {
		DEBUGPRINT("ROM: using init-ROM was forced" NL);
		memcpy(rom, meminitdata_initrom, MEMINITDATA_INITROM_SIZE);
		goto overriden;
	}
	if (external_image) {
		DEBUGPRINT("ROM: using external pre-loaded ROM" NL);
		memcpy(rom, external_image, MEMINITDATA_INITROM_SIZE);
		goto overriden;
	}
	return -1;	// No override has been done
overriden:
	rom_is_overriden = 1;
	// return with the RESET vector of the ROM
	return rom[0xFFFC] | (rom[0xFFFD] << 8);
}
