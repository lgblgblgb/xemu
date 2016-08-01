/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 emulator.
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

/* Based on an even more ugly JavaScript version, also from me
 * -----------------------------------------------------------
 * Quick&dirty 6526 CIA emulation
 * (C)2013 LGB Gabor Lenart
 * Note: this is not an exact nor complete emulation!
 * The only goal is to support what Commodore 64/65(?) uses even
 * implemented incorrectly (not cycle exact, simplified, ignored
 * conditions, etc). 
 * Some doc:
 *      http://archive.6502.org/datasheets/mos_6526_cia.pdf
 *      http://www.c64-wiki.com/index.php/CIA
 */

#ifndef __LGB_CIA6526_H_INCLUDED
#define __LGB_CIA6526_H_INCLUDED

struct Cia6526 {
        void (*outa)(Uint8 mask, Uint8 data);
        void (*outb)(Uint8 mask, Uint8 data);
        void (*outsr)(Uint8 data);
        Uint8 (*ina)(Uint8 mask);
        Uint8 (*inb)(Uint8 mask);
        Uint8 (*insr)(void);
        void (*setint)(int level);
        const char *name;
	Uint8 PRA, PRB, DDRA, DDRB;
	int TCA, TCB;
	int intLevel;
	Uint8 TLAL, TLAH, TLBL, TLBH;
	Uint8 CRA, CRB;
	Uint8 ICRmask, ICRdata;
	Uint8 SDR;
	int tod[4];
	int todAlarm[4];
	int regWritten[16];
};


extern void cia_init(
        struct Cia6526 *cia, const char *name,
        void (*outa)(Uint8 mask, Uint8 data),
        void (*outb)(Uint8 mask, Uint8 data),
        void (*outsr)(Uint8 data),
        Uint8 (*ina)(Uint8 mask),
        Uint8 (*inb)(Uint8 mask),
        Uint8 (*insr)(void),
        void (*setint)(int level)
);
extern void  cia_reset(struct Cia6526 *cia);
extern void  cia_write(struct Cia6526 *cia, int addr, Uint8 data);
extern Uint8 cia_read (struct Cia6526 *cia, int addr);
extern void  cia_tick (struct Cia6526 *cia, int ticks);
extern void  cia_dump_state ( struct Cia6526 *cia );


#endif
