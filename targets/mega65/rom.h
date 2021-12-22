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

#ifndef XEMU_MEGA65_ROM_H_INCLUDED
#define XEMU_MEGA65_ROM_H_INCLUDED

#define XEMU_STUB_ROM_SAVE_FILENAME "@XEMU-STUB.ROM"

extern int rom_date;
extern const char *rom_name;
extern sha1_hash_str rom_hash_str;
extern int rom_is_openroms;
extern int rom_is_stub;
extern const Uint8 vga_font_8x8[2048];

extern int rom_stubrom_requested;
extern int rom_initrom_requested;
extern int rom_is_overriden;

extern void rom_clear_reports ( void );
extern void rom_unset_requests ( void );
extern void rom_detect_date ( const Uint8 *rom );
extern void rom_make_xemu_stub_rom ( Uint8 *rom, const char *save_file );
extern void rom_clear_rom ( Uint8 *rom );
extern int  rom_do_override ( Uint8 *rom );
extern int  rom_load_custom ( const char *fn );

#endif
