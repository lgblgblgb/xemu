/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
 * (C)2013,2020 LGB Gabor Lenart
 * Note: this is not an exact nor complete emulation!
 * The only goal is to support what Commodore 64/65(?) uses even
 * implemented incorrectly (not cycle exact, simplified, ignored
 * conditions, etc). 
 * Some doc:
 *      http://archive.6502.org/datasheets/mos_6526_cia.pdf
 *      http://www.c64-wiki.com/index.php/CIA
 */

#ifndef XEMU_COMMON_CIA6526_H_INCLUDED
#define XEMU_COMMON_CIA6526_H_INCLUDED

#include "xemu/emutools_snapshot.h"
#include <time.h>

struct Cia6526 {
        void (*outa)(Uint8 data);
        void (*outb)(Uint8 data);
        void (*outsr)(Uint8 data);
        Uint8 (*ina)(void);
        Uint8 (*inb)(void);
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
	Uint8 tod[4];
	Uint8 todAlarm[4];
	int regWritten[16];
};


extern void cia_init(
        struct Cia6526 *cia, const char *name,
        void (*outa)(Uint8 data),
        void (*outb)(Uint8 data),
        void (*outsr)(Uint8 data),
        Uint8 (*ina)(void),
        Uint8 (*inb)(void),
        Uint8 (*insr)(void),
        void (*setint)(int level)
);
extern void  cia_reset(struct Cia6526 *cia);
extern void  cia_write(struct Cia6526 *cia, int addr, Uint8 data);
extern Uint8 cia_read (struct Cia6526 *cia, int addr);
extern void  cia_tick (struct Cia6526 *cia, int ticks);
extern void  cia_dump_state ( struct Cia6526 *cia );
extern void  cia_ugly_tod_updater ( struct Cia6526 *cia, const struct tm *t, Uint8 sec10 ) ;

#ifdef XEMU_SNAPSHOT_SUPPORT
extern int cia_snapshot_load_state ( const struct xemu_snapshot_definition_st *def , struct xemu_snapshot_block_st *block );
extern int cia_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif


#endif
