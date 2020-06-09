/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __COMMON_XEMU_VIA65C22_H_INCLUDED
#define __COMMON_XEMU_VIA65C22_H_INCLUDED

struct Via65c22 {
	void (*outa)(Uint8 mask, Uint8 data);
	void (*outb)(Uint8 mask, Uint8 data);
	void (*outsr)(Uint8 data);
	Uint8 (*ina)(Uint8 mask);
	Uint8 (*inb)(Uint8 mask);
	Uint8 (*insr)(void);
	void (*setint)(int level);
	const char *name;
	Uint8 DDRB, ORB, DDRA, ORA, SR, IER, IFR, ACR, PCR, T1LL, T1LH, T2LL, T2LH;
	int T1C, T2C;
	int irqLevel, SRcount, SRmode, T1run, T2run;
};

extern void via_init(
	struct Via65c22 *via, const char *name,
	void (*outa)(Uint8 mask, Uint8 data),
	void (*outb)(Uint8 mask, Uint8 data),
	void (*outsr)(Uint8 data),
	Uint8 (*ina)(Uint8 mask),
	Uint8 (*inb)(Uint8 mask),
	Uint8 (*insr)(void),
	void (*setint)(int level)
);
extern void  via_reset(struct Via65c22 *via);
extern void  via_write(struct Via65c22 *via, int addr, Uint8 data);
extern Uint8 via_read (struct Via65c22 *via, int addr);
extern void  via_tick (struct Via65c22 *via, int ticks);

#endif
