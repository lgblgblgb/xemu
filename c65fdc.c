/* Test-case for a very simple and inaccurate and even not working Commodore 65 emulator.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the Commodore 65 emulation. Note: the purpose of this emulator is merely to
   test some 65CE02 opcodes, not for being a *usable* Commodore 65 emulator too much!
   If it ever able to hit the C65 BASIC-10 usage, I'll be happy :)

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

#include <stdio.h>
#include <SDL.h>

#include "c65fdc.h"
#include "emutools.h"



static Uint8 f011_cmd = 0;
static Uint8 f011_track = 0, f011_sector = 0, f011_side = 0;

static Uint8 f011_reg0 = 0;
static Uint8 f011_status_a = 0;
static Uint8 f011_status_b = 0;

static Uint8 emulate_busy = 0;	// must be a BYTE!




void fdc_write_reg ( int addr, Uint8 data )
{
        printf("FDC: writing register %d with data $%02X" NL, addr, data);
	f011_status_a = 0;
	switch (addr) {
		case 0:
			f011_reg0 = data;
			f011_side = (data >> 3) & 1;
			break;
		case 1:
			f011_cmd = data;
			//f011_status_a |= 128; // simulate busy status ...
			switch (data & 0xF8) {	// high 5 bits
				case 0x40:	// read sector
					f011_status_a = 128|16;
					break;
				case 0x80:	// write sector
					f011_status_a = 128|2;
					break;
				case 0x10:	// head step out or no step
					if (!(data & 4))
						f011_track--;
					break;	
				case 0x18:	// head step in
					f011_track++;
					break;
				case 0x20:	// motor spin up
					f011_reg0 |= 32;
					f011_status_a = 128;
					break;
				case 0x00:	// cancel running command??
					break;
				default:
					printf("FDC: unknown comand: $%02X" NL, data);
			}	
			break;
		case 4:
			f011_track = data;
			break;
		case 5:
			f011_sector = data;
			break;
		case 6:
			f011_side = data;
			break;
	}
	if (f011_status_a & 128)
		emulate_busy = 210;
}



Uint8 fdc_read_reg  ( int addr )
{
	Uint8 result;
	switch (addr) {
		case 0:
			//result = f011_irqenable | f011_led | (f011_motor << 5) | (f011_swap <<) 4 | ((f011_side & 1) << 3) | f011_ds;
			//result = ((f011_side & 1) << 3) | 32;
			result = (f011_reg0 & (0xFF - 8)) | ((f011_side & 1) << 3);
			break;
		case 1:
			result = f011_cmd;
			break;
		case 2:	// STATUS register A
			//result = f011_busy & f011_drq & f011_flag_eq & f011_rnf & f011_crc & f011_lost & f011_write_protected & f011_track0;
			if ((f011_status_a & 128)) {
				emulate_busy++;
				if (!emulate_busy)
					f011_status_a &= 127;
			}
			result = (f011_status_a & 0xFE) | (f011_track ? 0 : 1);
			break;
		case 3: // STATUS register B
			//result = f011_rsector_found & f011_wsector_found & f011_rsector_found & f011_write_gate & f011_disk_present & f011_over_index & f011_irq & f011_disk_changed;
			result = f011_status_b;
			break;
		case 4:
			result = f011_track;
			break;
		case 5:
			result = f011_sector;
			break;
		case 6:
			result = f011_side;
			break;
		case 7:
			result = 0xFF;	// DATA :-) We fake here something meaingless for now ...
			break;
		default:
			result = 0xFF;
			break;
	}

        printf("FDC: reading register %d result is $%02X" NL, addr, result);
	return result;
}

