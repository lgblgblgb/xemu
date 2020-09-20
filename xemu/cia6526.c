/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is a *VERY* lame CIA 6526 emulation, lacks of TOD, mostly to SDR stuff, timing,
   and other problems as well ... Hopefully enough for C65 to boot, what is its only reason ...

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


#include "xemu/emutools_basicdefs.h"
#include "xemu/cia6526.h"


#define ICR_CHECK() \
	do { \
		if (cia->ICRmask & cia->ICRdata & 31) { \
			cia->ICRdata |= 128; \
			if (!cia->intLevel) { cia->intLevel = 1; cia->setint(1); DEBUG("%s IRQ to 1" NL, cia->name); } \
		} else { \
			cia->ICRdata &= 127; \
			if ( cia->intLevel) { cia->intLevel = 0; cia->setint(0); DEBUG("%s IRQ to 0" NL, cia->name); } \
		} \
	} while(0)
#define ICR_SET(mask) \
	do { \
		cia->ICRdata |= (mask) & 31; \
		DEBUG("%s ICR set to data $%02X, mask is $%02X" NL, cia->name, cia->ICRdata, cia->ICRmask); \
		ICR_CHECK(); \
	} while(0)
#define ICR_CLEAR(mask) \
	do { \
		cia->ICRdata &= 255 - (mask); \
		ICR_CHECK(); \
	} while(0)



void cia_reset ( struct Cia6526 *cia )
{
	int a;
	for (a = 0; a < 16; a++)
		cia->regWritten[a] = -1;
	cia->PRA = cia->PRB = cia->DDRA = cia->DDRB = 0;
	cia->TCA = cia->TCB = 0;
	cia->CRA = cia->CRB = 0;
	cia->TLAL = cia->TLAH = cia->TLBL = cia->TLBH = 0;
	cia->ICRmask = cia->ICRdata = 0;
	cia->SDR = 0;
	cia->tod[0] = cia->tod[1] = cia->tod[2] = cia->tod[3] = 0;
	cia->intLevel = 0;
	cia->setint(cia->intLevel);
	DEBUG("%s: RESET" NL, cia->name);
}



void cia_flag ( struct Cia6526 *cia )
{
	ICR_SET(16);
}



static void  def_outa  (Uint8 data) {}
static void  def_outb  (Uint8 data) {}
static void  def_outsr (Uint8 data) {}
static Uint8 def_ina   (void      ) { return 0xFF; }
static Uint8 def_inb   (void)       { return 0xFF; }
static Uint8 def_insr  (void)       { return 0xFF; }
static void  def_setint(int level)  {}


void cia_init (
	struct Cia6526 *cia, const char *name,
	void (*outa)(Uint8 data),
	void (*outb)(Uint8 data),
	void (*outsr)(Uint8 data),
	Uint8 (*ina)(void),
	Uint8 (*inb)(void),
	Uint8 (*insr)(void),
	void (*setint)(int level)
) {
	cia->name = name;
	cia->outa   = outa      ? outa   : def_outa;
	cia->outb   = outb      ? outb   : def_outb;
	cia->outsr  = outsr     ? outsr  : def_outsr;
	cia->ina    = ina       ? ina    : def_ina;
	cia->inb    = inb       ? inb    : def_inb;
	cia->insr   = insr      ? insr   : def_insr;
	cia->setint = setint    ? setint : def_setint;
	cia_reset(cia);
}



Uint8 cia_read ( struct Cia6526 *cia, int addr )
{
	Uint8 temp;
	switch (addr & 0xF) {
		case  0:	// reg#0: port A data
			// Note: actually - it seems - CIA always reads the pins even if output DDR is set
			// We leave the implementation details of the emulator targets to think about this ...
			return cia->ina();
		case  1:	// reg#1: port B data
			// Note: see the note above, with port A
			return cia->inb();
		case  2:	// reg#2: DDR A
			return cia->DDRA;
		case  3:	// reg#3: DDR B
			return cia->DDRB;
		case  4:	// reg#4: timer A counter low
			return cia->TCA & 0xFF;
		case  5:	// reg#5: timer A counter high
			return cia->TCA >>   8;
		case  6:	// reg#6: timer B counter low
			return cia->TCB & 0xFF;
		case  7:	// reg#7: timer B counter high
			return cia->TCB >>   8;
		case  8:	// reg#8: TOD 10ths
			return cia->tod[0];
		case  9:	// reg#9: TOD sec
			return cia->tod[1];
		case 10:	// reg#A: TOD min
			return cia->tod[2];
		case 11:	// reg#B: TOD hours + PM
			return cia->tod[3];
		case 12:	// reg#C: SP
			return cia->SDR;
		case 13: 	// reg#D: ICR data
			temp = cia->ICRdata;
			cia->ICRdata = 0;
			ICR_CHECK();
			return temp;
		case 14:	// reg#E: CRA
			return cia->CRA;
		case 15:	// reg#F: CRB
			return cia->CRB;
	}
	return 0;	// to make GCC happy :-/
}



void cia_write ( struct Cia6526 *cia, int addr, Uint8 data )
{
	switch (addr & 0xF) {
		case 0:		// reg#0: port A
			// Note: we leave the details for the emulator targets to handle
			// the situation what is really output in relation of the DDR register too ...
			cia->PRA = data;
			cia->outa(data);
			break;
		case 1:		// reg#1: port B
			// Note: see the note above with Port A
			cia->PRB = data;
			cia->outb(data);
			break;
		case 2:		// reg#2: DDR A
			// Note: we "notify" the emulation target that DDR changed with re-output the value
			// Note: see the notes with registers 0, 1 on output/DDR related thing
			if (cia->DDRA != data) {
				cia->DDRA = data;
				cia->outa(cia->PRA);
			}
			break;
		case 3:		// reg#3: DDR B
			// Note: see the note above with DDR of port A
			if (cia->DDRB != data) {
				cia->DDRB = data;
				cia->outb(cia->PRB);
			}
			break;
		case 4:		// reg#4: timer A latch low
			cia->TLAL = data;
			break;
		case 5:		// reg#5: timer A latch high
			cia->TLAH = data;
			if (!(cia->CRA & 1) || (cia->CRA & 16))
				cia->TCA = cia->TLAL | (cia->TLAH << 8);
			break;
		case 6:		// reg#6: timer B latch low
			cia->TLBL = data;
			break;
		case 7:		// reg#7: timer B latch high
			cia->TLBH = data;
			if (!(cia->CRB & 1) || (cia->CRB & 16))
				cia->TCB = cia->TLBL | (cia->TLBH << 8);
			break;
		case 8:		// reg#8: TOD 10ths
			if (cia->CRB & 128)
				cia->todAlarm[0] = data & 15;
			else
				cia->tod[0] = data & 15;
			break;
		case 9:		// reg#9: TOD sec
			if (cia->CRB & 128)
				cia->todAlarm[1] = data & 127;
			else
				cia->tod[1] = data & 127;
			break;
		case 10:	// reg#A: TOD min
			if (cia->CRB & 128)
				cia->todAlarm[2] = data & 127;
			else
				cia->tod[2] = data & 127;
			break;
		case 11:	// reg#B: TOD hours + PM
			if (cia->CRB & 128)
				cia->todAlarm[3] = data;
			else
				cia->tod[3] = data;
			break;
		case 12:	// reg#C: SP
			cia->SDR = data;
			if (cia->CRA & 64)
				cia->outsr(data);
			break;
		case 13:	// reg#D: ICR mask
			if (data & 128)
				cia->ICRmask |= data & 31;
			else
				cia->ICRmask &= 255 - (data & 31);
			DEBUG("%s set ICRmask to %02X with byte of %02X" NL, cia->name, cia->ICRmask, data);
			ICR_CHECK();
			break;
		case 14:	// reg#E: CRA
			cia->CRA = data;
			if (data & 16) {	// force load?
				cia->CRA &= 255 - 16;	// strobe bit, force to be on zero
				cia->TCA = cia->TLAL | (cia->TLAH << 8);
			}
			break;
		case 15:	// reg#F: CRB
			cia->CRB = data;
			if (data & 16) {	// force load?
				cia->CRB &= 255 - 16;	// storbe bit, force to be on zero
				cia->TCB = cia->TLBL | (cia->TLBH << 8);
			}
			break;
	}
	cia->regWritten[addr] = data;
}


static XEMU_INLINE Uint8 to_bdc_byte ( Uint8 b )
{
	return ((b / 10) << 4) + (b % 10);
}



void cia_ugly_tod_updater ( struct Cia6526 *cia, const struct tm *t, Uint8 sec10 )
{
	// Ugly CIA trick to maintain realtime TOD in CIAs :)
	// FIXME: of course, that's simple crazy, not in sync with emu, no "stopping" clock on read, no setting etc ...
	cia->tod[0] = to_bdc_byte(sec10);
	cia->tod[1] = to_bdc_byte(t->tm_sec);
	cia->tod[2] = to_bdc_byte(t->tm_min);
	cia->tod[3] = to_bdc_byte(t->tm_hour);
}


void cia_tick ( struct Cia6526 *cia, int ticks )
{
	/* Timer A */
	if (cia->CRA & 1) {
		cia->TCA -= ticks;
		if (cia->TCA <= 0) {
			DEBUG("%s timer-A expired!" NL, cia->name);
			ICR_SET(1);
			cia->TCA = cia->TLAL | (cia->TLAH << 8);
			if (cia->CRA & 8)
				cia->CRA &= 254; // one shot mode: reset bit 1 (timer stop)
		}
	}
	/* Timer B */
	if (cia->CRB & 1) {
		cia->TCB -= ticks;
		if (cia->TCB <= 0) {
			DEBUG("%s timer-B expired!" NL, cia->name);
			ICR_SET(2);
			cia->TCB = cia->TLBL | (cia->TLBH << 8);
			if (cia->CRB & 8)
				cia->CRB &= 254; // one shot mode: reset bit 1 (timer stop)
		}
	}
	/* TOD stuffs are ignored ;-/ */
}


void cia_dump_state ( struct Cia6526 *cia )
{
	int a;
	DEBUG("%s registers written:", cia->name);
	for (a = 0; a < 16; a++)
		if (cia->regWritten[a] >= 0)
			DEBUG(" %02X", cia->regWritten[a]);
		else
			DEBUG(" ??");
	DEBUG(NL "%s timer-A=%d time-B=%d" NL, cia->name, cia->TCA, cia->TCB);
}


/* --- SNAPSHOT RELATED --- */


#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_CIA_BLOCK_VERSION	0
#define SNAPSHOT_CIA_BLOCK_SIZE		256

int cia_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_CIA_BLOCK_SIZE];
	struct Cia6526 *cia = (struct Cia6526 *)def->user_data;
	int a;
	if (block->block_version != SNAPSHOT_CIA_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad CIA block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	for (a = 0; a < 16; a++) {
		cia_write(cia, a, (cia->regWritten[a] = buffer[a + 16] ? -1 : buffer[a]));
	}
	cia->TCA = (int)P_AS_BE32(buffer + 128);
	cia->TCB = (int)P_AS_BE32(buffer + 132);
	cia->intLevel = (int)P_AS_BE32(buffer + 136);
	cia->CRA = buffer[140];
	cia->CRB = buffer[141];
	cia->ICRdata = buffer[142];
	cia->ICRmask = buffer[143];
	ICR_CHECK();
	cia->setint(cia->intLevel);	// just to be sure ...
	cia->outa(cia->PRA);
	cia->outb(cia->PRB);
	return 0;
}


int cia_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_CIA_BLOCK_SIZE];
	struct Cia6526 *cia = (struct Cia6526 *)def->user_data;
	int a = xemusnap_write_block_header(def->idstr, SNAPSHOT_CIA_BLOCK_VERSION);
	if (a) return a;
	memset(buffer, 0xFF, sizeof buffer);
	/* saving state ... */
	for (a = 0; a < 16; a++) {
		buffer[a] = cia->regWritten[a] & 0xFF;
		buffer[a + 16] = cia->regWritten[a] == -1 ? 0xFF : 0x00;
	}
	U32_AS_BE(buffer + 128, (Uint32)cia->TCA);
	U32_AS_BE(buffer + 132, (Uint32)cia->TCB);
	U32_AS_BE(buffer + 136, (Uint32)cia->intLevel);
	buffer[140] = cia->CRA;
	buffer[141] = cia->CRB;
	buffer[142] = cia->ICRdata;
	buffer[143] = cia->ICRmask;
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
