/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "enterprise128.h"
#include "z180.h"
#include "cpu.h"

#ifdef CONFIG_Z180

int z180_port_start;
static Uint8 z180_ports[0x40];
static int z180_incompatibility_reported = 0;


// These "default" values from a real Z180, at least reading it from software (non-readable ports can be a problem though)
static const Uint8 _z180_ports_default[0x40] = {
	0x10, 0x00, 0x27, 0x07, 0x04, 0x02, 0xFF, 0xFF,
	0xFF, 0xFF, 0x0F, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
	0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
	0x7A, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0x80,
	0x00, 0xFE, 0x00, 0x00, 0xFE, 0x00, 0x00, 0xFE,
	0x00, 0xFE, 0x00, 0x00, 0xFE, 0x00, 0x00, 0xFE,
	0x32, 0xC1, 0x00, 0x00, 0x39, 0xFF, 0xFC, 0xFF,
	0x00, 0x00, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F
};


/* A callback only used in Z180 mode */
void z80ex_z180_cb (Z80EX_WORD pc, Z80EX_BYTE prefix, Z80EX_BYTE series, Z80EX_BYTE opcode, Z80EX_BYTE itc76)
{
	z180_ports[0x34] = (z180_ports[0x34] & 0x3F) | itc76; // set ITC register up
	DEBUG("Z180: setting ICT register to: %02Xh" NL, z180_ports[0x34]);
	DEBUG("Z180: Invalid Z180 opcode <prefix=%02Xh series=%02Xh opcode=%02Xh> at PC=%04Xh [%02Xh:%04Xh]" NL,
		prefix, series, opcode,
		pc,
		ports[0xB0 | (pc >> 14)],
		pc & 0x3FFF
	);
	if (z180_incompatibility_reported) return;
	z180_incompatibility_reported = 1;
	INFO_WINDOW("Z180: Invalid Z180 opcode <prefix=%02Xh series=%02Xh opcode=%02Xh> at PC=%04Xh [%02Xh:%04Xh]\nThere will be NO further error reports about this kind of problem to avoid window flooding :)",
		prefix, series, opcode,
		pc,
		ports[0xB0 | (pc >> 14)],
		pc & 0x3FFF
	);
}


void z180_internal_reset ( void )
{
	z180_port_start = 0;
	memcpy(z180_ports, _z180_ports_default, 0x40);
	z180_incompatibility_reported = 0;
	// z180_ports[0x34] = 0x39;	// ITC register
	// z180_ports[0x3F] = 0x1F;	// ICR - I/O control register
}


void z180_port_write ( Uint8 port, Uint8 value )
{
	DEBUG("Z180: write internal port (%02Xh/%02Xh) data = %02Xh" NL, port, port | z180_port_start, value);
	switch (port) {
		case 0x34:	// ITC register
			value = (z180_ports[port] & 0x07) | 0x38; // change was: 47->07 in hex
			break;
		case 0x3F:	// I/O control register (ICR)
			z180_port_start = value & 0xC0;
			DEBUG("Z180: internal ports are moved to %02Xh-%02Xh" NL, z180_port_start, z180_port_start + 0x3F);
			value = (value & 0xE0) | 0x1F;	// only first three bits are interpreted, the rest are '1' for read
			break;
	}
	z180_ports[port] = value;
}


Uint8 z180_port_read ( Uint8 port )
{
	DEBUG("Z180: read internal port (%02Xh/%02Xh)" NL, port, port | z180_port_start);
	return z180_ports[port];
}

#endif
