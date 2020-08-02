/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

/* Commodore LCD emulator, C version.
 * (C)2013,2014 LGB Gabor Lenart
 * Visit my site (the better, JavaScript version of the emu is here too): http://commodore-lcd.lgb.hu/
 * Can be distributed/used/modified under the terms of GNU/GPL 3 (or later), please see file COPYING
 * or visit this page: http://www.gnu.org/licenses/gpl-3.0.html
 */

#include "xemu/emutools_basicdefs.h"
#include "xemu/via65c22.h"

#define INA(via) (via->ina)(0xFF)
#define INB(via) ((via->ORB & via->DDRB) | ((via->inb)(255 - via->DDRB) & (255 - via->DDRB)))
#define OUTA(via, data) (via->outa)(via->DDRA, data)
#define OUTB(via, data) (via->outb)(via->DDRB, data)
#define INT(via, level) (via->setint)(via->irqLevel = level)
#define alert(via, msg) DEBUG("%s: ALERT: %s" NL, via->name, msg)
#define INSR(via) (via->insr)()
#define OUTSR(via, data) (via->outsr)(data)

static inline void ifr_check(struct Via65c22 *via)
{
	if (via->IFR & via->IER & 127) {
		via->IFR |= 128;
		if (!via->irqLevel) INT(via, 1);
	} else {
		via->IFR &= 127;
		if ( via->irqLevel) INT(via, 0);
	}
}

static inline void ifr_clear (struct Via65c22 *via, Uint8 mask) { via->IFR &= 255 - mask; ifr_check(via); }
static inline void ifr_set   (struct Via65c22 *via, Uint8 mask) { via->IFR |=       mask; ifr_check(via); }
static inline void ifr_on_pa (struct Via65c22 *via) { ifr_clear(via, ((via->PCR & 0x0E) == 0x02 || (via->PCR & 0x0E) == 0x06) ?    2 :    3); }
static inline void ifr_on_pb (struct Via65c22 *via) { ifr_clear(via, ((via->PCR & 0xE0) == 0x20 || (via->PCR & 0xE0) == 0x60) ? 0x10 : 0x18); }




void via_reset(struct Via65c22 *via)
{
	//via->ACR = via->PCR = via->DDRA = via->DDRB = via->ORA = via->ORB = 0;
	via->ORA = via->ORB = via->DDRA = via->DDRB = 0;
	via->SR = via->SRcount = via->SRmode = via->IER = via->IFR = via->ACR = via->PCR = 0;
	via->T1C = via->T2C = via->T1LL = via->T1LH = via->T2LL = via->T2LH = 0;
	via->T1run = via->T2run = 0; // false
	INT(via, 0);
	DEBUG("%s: RESET" NL, via->name);
}


static void  def_outa  (Uint8 mask, Uint8 data) {}
static void  def_outb  (Uint8 mask, Uint8 data) {}
static void  def_outsr (Uint8 data) {}
static Uint8 def_ina   (Uint8 mask) { return 0xFF; }
static Uint8 def_inb   (Uint8 mask) { return 0xFF; }
static Uint8 def_insr  (void)       { return 0xFF; }
static void  def_setint(int level)  {}


void via_init(
	struct Via65c22 *via, const char *name,
	void (*outa)(Uint8 mask, Uint8 data),
	void (*outb)(Uint8 mask, Uint8 data),
	void (*outsr)(Uint8 data),
	Uint8 (*ina)(Uint8 mask),
	Uint8 (*inb)(Uint8 mask),
	Uint8 (*insr)(void),
	void (*setint)(int level)
) {
	via->name = name;
	via->outa   = outa	? outa   : def_outa;
	via->outb   = outb	? outb   : def_outb;
	via->outsr  = outsr	? outsr  : def_outsr;
	via->ina    = ina	? ina    : def_ina;
	via->inb    = inb	? inb    : def_inb;
	via->insr   = insr	? insr   : def_insr;
	via->setint = setint	? setint : def_setint;
	via_reset(via);
}


void via_write(struct Via65c22 *via, int addr, Uint8 data)
{
	//DEBUG("%s: write reg %02X with data %02X" NL, via->name, addr, data);
	switch (addr) {
		case 0x0: // port B data
			via->ORB = data;	// FIXED BUG
			OUTB(via, data);
			ifr_on_pb(via);
			break;
		case 0x1: // port A data
			via->ORA = data;	// FIXED BUG
			OUTA(via, data);
			ifr_on_pa(via);
			break;
		case 0x2: // port B DDR
			if (data != via->DDRB) {
				via->DDRB = data;
				OUTB(via, via->ORB);
			}
			break;
		case 0x3: // port A DDR
			if (data != via->DDRA) {
				via->DDRA = data;
				OUTA(via, via->ORA);
			}
			break;
		case 0x4: //
			via->T1LL = data;
			break;
		case 0x5:
			via->T1LH = data;
			ifr_clear(via, 64);
			via->T1run = 1;
			via->T1C = via->T1LL | (via->T1LH << 8);
			break;
		case 0x6:
			via->T1LL = data;
			break;
		case 0x7:
			via->T1LH = data;
			ifr_clear(via, 64);
			break;
		case 0x8:
			via->T2LL = data;
			break;
		case 0x9:
			via->T2LH = data;
			ifr_clear(via, 32);
			via->T2run = 1;
			via->T2C = via->T2LL | (via->T2LH << 8);
			break;
		case 0xA: // SR
			ifr_clear(via, 4);
			via->SR = data;
			if (via->SRmode) via->SRcount = 8;
			break;
		case 0xB: // ACR
			via->SRmode = (data >> 2) & 7;
			via->ACR = data;
			if (data & 32) alert(via, "pulse counting T2 mode is not supported!");
			break;
		case 0xC: // PCR
			via->PCR = data;
			break;
		case 0xD: // IFR
			ifr_clear(via, data);
			break;
		case 0xE: // IER
			if (data & 128) via->IER |= data; else via->IER &= (255 - data);
			ifr_check(via);
			break;
		case 0xF: // port A data (no handshake)
			OUTA(via, data);
			ifr_on_pa(via);
			break;
	}
}

Uint8 via_read(struct Via65c22 *via, int addr)
{
	//DEBUG("%s: read reg %02X" NL, via->name, addr);
	switch (addr) {
		case 0x0: // port B data
			ifr_on_pb(via);
			return INB(via);
		case 0x1: // port A data
			ifr_on_pa(via);
			return INA(via);
		case 0x2: // port B DDR
			return via->DDRB;
		case 0x3: // port A DDR
			return via->DDRA;
		case 0x4: //
			ifr_clear(via, 64);
			return via->T1C & 0xFF;
		case 0x5:
			return via->T1C >> 8;
		case 0x6:
			return via->T1LL;
		case 0x7:
			return via->T1C >> 8;
		case 0x8:
			ifr_clear(via, 32);
			return via->T2C & 0xFF;
		case 0x9:
			return via->T2C >> 8;
		case 0xA: // SR
			ifr_clear(via, 4);
			if (via->SRmode) via->SRcount = 8;
			return via->SR;
		case 0xB: // ACR
			return via->ACR;
		case 0xC: // PCR
			return via->PCR;
		case 0xD: // IFR
			return via->IFR;
		case 0xE: // IER
			return via->IER | 128;
		case 0xF: // port A data (no handshake)
			ifr_on_pa(via);
			return INA(via);
	}
	return 0; // make gcc happy :)
}

void via_tick(struct Via65c22 *via, int ticks)
{
	/* T1 */
	if (via->T1run) {
		via->T1C -= ticks;
		if (via->T1C <= 0) {
			//console.log("Expired T1");
			ifr_set(via, 64);
			if (via->ACR & 64) via->T1C = via->T1LL | (via->T1LH << 8); else via->T1run = 0;
		}
	}
	/* T2 */
	via->T2C -= ticks;
	if (via->T2run) {
		if (via->T2C <= 0) {
			ifr_set(via, 32);
			via->T2run = 0;
		}
	}
	via->T2C &= 0xFFFF;
	/* shift register */
	if (via->SRcount) {
		via->SRcount -= ticks;
		if (via->SRcount <= 0) {
			switch(via->SRmode) {
				case 0: via->SRcount = 0; ifr_clear(via, 4); break; // disabled
				case 2: via->SR = INSR(via); via->SRcount = 0; ifr_set(via, 4); break; // PHI2-in
				case 4: OUTSR(via, via->SR); via->SRcount = via->T2LL + 2; ifr_clear(via, 4); break; // free-T2-out, VIA2 seems to use this mode!
				default: via->SRcount = 0 ; ifr_clear(via, 4); alert(via, "SRmode is not supported!"); break;
			}
		}
	}
}
