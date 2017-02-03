/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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

#include "xep128.h"
#include "zxemu.h"
#include "cpu.h"
#include "primoemu.h"



int zxemu_on = 0;




void zxemu_switch ( Uint8 data )
{
	data &= 128;
	if (data == zxemu_on)
		return;
	zxemu_on = data;
	DEBUG("ZXEMU: emulation is turned %s." NL, zxemu_on ? "ON" : "OFF");
	if (zxemu_on)
		primo_switch(0);
        nmi_pending = 0;
}


static int zxemu_nmi ( void )
{
	nmi_pending = zxemu_on;
	return nmi_pending;
}


void zxemu_write_ula ( Uint8 hiaddr, Uint8 data )
{
	ports[0x40] = hiaddr;	// high I/O address
	ports[0x41] = 0xFE;		// low I/O address, the ULA port
	ports[0x42] = data;		// data on the bus
	ports[0x43] = 0;		// ?? 0 = I/O kind of op
	if (!zxemu_nmi())
		DEBUG("ZXEMU: ULA write: no NMI (switched off)" NL);
	DEBUG("ZXEMU: writing ULA at %04Xh (data: %02Xh)" NL, Z80_PC, data);
}


Uint8 zxemu_read_ula ( Uint8 hiaddr )
{
	ports[0x40] = hiaddr;
	ports[0x41] = 0xFE;
	ports[0x42] = 0xFF; // ???????
	ports[0x43] = 0;
	if (!zxemu_nmi())
		DEBUG("ZXEMU: ULA read: no NMI (switched off)" NL);
	DEBUG("ZXEMU: reading ULA at %04Xh" NL, Z80_PC);
	return zxemu_on ? 0xBF : 0xFF;
}


/* This function is only allowed to be called, if zxemu_on is non-zero, and attribute area is written! */
void zxemu_attribute_memory_write ( Uint16 address, Uint8 data )
{
	ports[0x40] = address >> 8;
	ports[0x41] = address & 0xFF;
	ports[0x42] = data;
	ports[0x43] = 0x80;
	zxemu_nmi();
	DEBUG("ZXEMU: attrib-mem trap at %04Xh" NL, address);
}

