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
#include "cart.h"
#include "xemu/emutools_files.h"
#include "audio65.h"
#include "string.h"
#include "errno.h"


static struct {
	bool loaded;
	bool autostart;
	char *fn;
	char name[0x21];
	Uint8 mem[0x10000];
	int total_size;
	int sections;
} cart;


void cart_init ( void )
{
	memset(cart.mem, 0xFF, sizeof cart.mem);
	cart.loaded = false;
	cart.autostart = false;
	cart.name[0] = 0;
	cart.fn = NULL;
}


Uint8 cart_read_byte  ( unsigned int addr )
{
	Uint8 data = 0xFF;
	if (addr < sizeof(cart.mem))
		data = cart.mem[addr];
	//if (cart.loaded && addr != 0x3FFDF60)	// see the comment about OPL at cart_write_byte()
	//	DEBUGPRINT("CART: reading byte ($%02X) at $%X" NL, data, addr + 0x4000000);
	return data;
}


void cart_write_byte ( unsigned int addr, Uint8 data )
{
	if (!cart.loaded) {
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
	//DEBUGPRINT("CART: writing byte ($%02X) at $%X" NL, data, addr + 0x4000000);
}


bool cart_is_attached ( void )
{
	return cart.loaded;
}


void cart_detach ( void )
{
	memset(cart.mem, 0xFF, sizeof cart.mem);
	cart.autostart = false;
	free(cart.fn);
	cart.fn = NULL;
	if (cart.loaded) {
		DEBUGPRINT("CART: cartridge has been detached" NL);
		cart.loaded = false;
	}
}


static bool _check_cartridge_str ( const Uint8 *p )
{
	static const char cartridge_bytestr[]	= { ' ', 'C', 'A', 'R', 'T', 'R', 'I', 'D', 'G', 'E' };
	for (unsigned int a = 3; a < 7; a++)
		if (!memcmp(p + a, cartridge_bytestr, sizeof cartridge_bytestr))
			return true;
	return false;
}


int cart_attach ( const char *fn )
{
	static const char m65_bytestr[]		= { 'M', '6', '5' };
	static const char mega65_bytestr[]	= { 'M', 'E', 'G', 'A', '6', '5' };
	static const char chip_bytestr[]	= { 'C', 'H', 'I', 'P' };
	cart_detach();
	if (!fn || !*fn)
		return -1;
	const int fd = xemu_open_file(fn, O_RDONLY, NULL, NULL);
	if (fd < 0) {
		ERROR_WINDOW("Cannot open cartridge file %s\nError: %s", fn, strerror(errno));
		return -1;
	}
	cart.fn = xemu_realloc(cart.fn, strlen(fn) + 1);
	strcpy(cart.fn, fn);
	Uint8 buf[0x40 + 1];
	if (xemu_safe_read(fd, buf, 0x40) != 0x40)
		goto read_error;
	if (!_check_cartridge_str(buf)) {
		// " CARTRIDGE" (with space) cannot be found in the first 0x10 bytes: not a VICE CRT format
		// let's check if it's a "raw ROM" with the "M65" mark
		if (memcmp(buf + 7, m65_bytestr, sizeof m65_bytestr)) {
			ERROR_WINDOW("Not a CRT/raw file: %s", fn);	// not that either -> error
			goto error;
		}
		// Raw binary file, with "M65" mark: the best I can do is loading from $8000
		memcpy(cart.mem + 0x8000, buf, 0x40);	// copy what we have already
		const int ret = xemu_safe_read(fd, cart.mem + 0x8000 + 0x40, 0x10000 - 0x8000 - 0x40);	// load the rest
		if (ret < 0)
			goto read_error;
		cart.loaded = true;
		cart.sections = 0;
		cart.total_size = ret + 0x40;
		cart.autostart = true;	// raw image is always auto start as I detected with 'M65' signature ...
		DEBUGPRINT("CART: raw cartridge ROM image \"%s\" has been loaded to $8000-$%X" NL, fn, 0x8000 + cart.total_size - 1);
		close(fd);
		return 1;
	}
	if (memcmp(buf, mega65_bytestr, sizeof mega65_bytestr)) {
		ERROR_WINDOW("Non-MEGA65 CRT file: %s", fn);
		goto error;
	}
	// VICE-like CRT file (MEGA65-specific though)
	DEBUGPRINT("CART: trying to attach VICE-like CRT file %s" NL, fn);
	buf[0x40] = 0;
	strcpy(cart.name, (const char*)buf + 0x20);
	cart.sections = 0;
	cart.total_size = 0;
	for (;;) {
		const int ret = xemu_safe_read(fd, buf, 0x10);	// read "CHIP" section header
		if (!ret)
			break;
		if (ret != 0x10)
			goto read_error;
		if (memcmp(buf, chip_bytestr, sizeof chip_bytestr)) {
			ERROR_WINDOW("Bad CRT file, missing CHIP section ID");
			goto error;
		}
		const unsigned int size = (buf[0xE] << 8) + buf[0xF];
		const unsigned int addr = (buf[0xC] << 8) + buf[0xD];
		DEBUGPRINT("CART: ... new CHIP section (#%d), $%04X-$%04X (%u bytes)" NL, cart.sections, addr, addr + size - 1, size);
		if (size + addr > 0x10000) {
			ERROR_WINDOW("Bad CRT file, CHIP section overflows memory");
			goto error;
		}
		if (size) {
			if (xemu_safe_read(fd, cart.mem + addr, size) != size)
				goto read_error;
		}
		cart.sections++;
		cart.total_size += size;
	}
	if (!cart.sections) {
		ERROR_WINDOW("No CHIP sections");
		goto error;
	}
	cart.loaded = true;
	cart.autostart = !memcmp(cart.mem + 0x8007, m65_bytestr, sizeof m65_bytestr);
	DEBUGPRINT("CART: attached, autostart = %d, name = \"%s\"" NL, (int)cart.autostart, cart.name);
	close(fd);
	return (int)cart.autostart;
read_error:
	ERROR_WINDOW("Cannot read (or truncated/too small) cartridge file %s", fn);
error:
	cart_detach();
	if (fd >= 0)
		close(fd);
	return -1;
}


void cart_info ( char *p, size_t size )
{
	if (!cart.loaded) {
		snprintf(p, size, "No cartridge is attached");
		return;
	}
	snprintf(p, size, "Filename: %s\nCartridge: %s\nAuto-start: %d\nSections: %d\nTotal binary size: %d\nFormat: %s",
		cart.fn, cart.name, (int)cart.autostart, cart.sections, cart.total_size,
		cart.sections ? "CRT" : "RAW"
	);
}
