/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2024 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "cart.h"
#include "xemu/emutools_files.h"
#include "audio65.h"

static Uint8 cart_mem[0x10000];
static int loaded = 0;


void cart_init ( void )
{
	memset(cart_mem, 0xFF, sizeof cart_mem);
}


Uint8 cart_read_byte  ( unsigned int addr )
{
	Uint8 data = 0xFF;
	if (addr < sizeof(cart_mem))
		data = cart_mem[addr];
	if (loaded && addr != 0x3FFDF60)	// see the comment about OPL at cart_write_byte()
		DEBUGPRINT("CART: reading byte ($%02X) at $%X" NL, data, addr + 0x4000000);
	return data;
}


void cart_write_byte ( unsigned int addr, Uint8 data )
{
	if (!loaded) {
		// a hack OPL to be able to work in the "slow device area" [if no cartridge is loaded]
		static Uint8 opl_reg_sel = 0;
		if (addr == 0x3FFDF40) {
			opl_reg_sel = data;
			return;
		} else if (addr == 0x3FFDF50) {
			audio65_opl3_write(opl_reg_sel, data);
			return;
		}
	}
	DEBUGPRINT("CART: writing byte ($%02X) at $%X" NL, data, addr + 0x4000000);
}


int cart_load_bin ( const char *fn, const unsigned int addr, const char *cry )
{
	if (!fn || !*fn)
		return 0;
	loaded = 0;
	if (addr >= sizeof(cart_mem)) {
		if (cry)
			ERROR_WINDOW("%s\nOutside of 64K range\n%s", cry, fn);
		return -1;
	}
	const int ret = xemu_load_file(fn, cart_mem + addr, 1, sizeof(cart_mem) - addr, cry);
	if (ret <= 0)
		return -1;
	DEBUGPRINT("CART: %d byte(s) loaded at offset $%04X from file %s" NL, ret, addr, fn);
	loaded = 1;
	return 0;
}


int cart_detect_id ( void )
{
	static const Uint8 cart_id[] = {'M', '6', '5'};
	return memcmp(cart_mem + 0x8007, cart_id, sizeof cart_id);
}


int cart_is_loaded ( void )
{
	return loaded;
}


void cart_copy_from ( const Uint16 cart_addr, Uint8 *target, const Uint16 size )
{
	memcpy(target, cart_mem + cart_addr, size);
}
