/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

   Partial Wiznet W5300 emulation, using the host OS (which runs the emulator)
   TCP/IP API. Thus, many of the W5300 features won't work, or limited, like:
   RAW mode, ICMP sockets, listening mode (it would be possible but eg in case
   of UNIX there would be a need for privilege, which is not so nice to
   run an emulator as "root" user), no IP/MAC setting (always uses the OS IP
   and MAC). DHCP and DNS is planned to "faked" so w5300 softwares trying to
   get IP address via DHCP or wanting resolve a DNS name would get an answer
   from this w5300 emulator instead of from the "real" network.

   Note: I've just discovered that FUSE Spectrum emulator does have some kind
   of w5100 emulation. Though w5100 and w5300 are not the very same, some things
   are similar, so their sources may help me in the future, thanks for their
   work (also a GNU/GPL software, so there is no license problem here).

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
#include "w5300.h"



#ifdef CONFIG_W5300_SUPPORT

int w5300_int;

//static Uint8 wmem[0x20000];	// 128K of internal RAM of w5300
static Uint8 wregs[0x400];	// W5300 registers
static Uint8 idm_ar0, idm_ar1, idm_ar;
static Uint8 mr0, mr1;
static void (*interrupt_cb)(int);


static void update_interrupts ( void )
{
	int v = ((wregs[2] & wregs[4] & 0xF0) | (wregs[3] & wregs[5])) > 0;
	if (v != w5300_int) {
		w5300_int = v;
		interrupt_cb(v);
	}
}



static Uint8 read_reg ( int addr )
{
	return wregs[addr];
}

static void write_reg ( int addr, Uint8 data )
{
	switch (addr) {
		case 2: // IR0
		case 3: // IR1
			wregs[addr] &= 255 - data;
			update_interrupts();
			break;
		case 4: // IMR0
		case 5: // IMR1
			wregs[addr] = data;
			update_interrupts();
			break;
		default:
			wregs[addr] = data;
			break;
	}
}


static void default_interrupt_callback ( int level ) {
}


/* ---Interface functions --- */


void w5300_reset ( void )
{
	memset(wregs, 0, sizeof wregs);
	mr0 = 0x38; mr1 = 0x00;
	idm_ar0 = 0; idm_ar1 = 0; idm_ar = 0;
	w5300_int = 0;
	wregs[0x1C] = 0x07; wregs[0x1D] = 0xD0; // RTR retransmission timeout-period register
	wregs[0x1F] = 8; 			// RCR retransmission retry-count register
	memset(wregs + 0x20, 8, 16);		// TX and RX mem size conf
	wregs[0x31] = 0xFF;			// MTYPER1
	DEBUG("W5300: reset" NL);
}

void w5300_init ( void (*cb)(int) )
{
	interrupt_cb = cb ? cb : default_interrupt_callback;
	DEBUG("W5300: init" NL);
}

void w5300_shutdown ( void )
{
	DEBUG("W5300: shutdown pending connections (if any)" NL);
}

void w5300_write_mr0 ( Uint8 data ) {		// high byte of MR
	if (data & 1) ERROR_WINDOW("W5300: FIFO byte-order swap feature is not emulated");
	mr0 = data & 0x3F; // DBW and MPF bits cannot be overwritten by user
}
void w5300_write_mr1 ( Uint8 data ) {		// low byte of MR
	if (data & 128) { // software reset?
		w5300_reset();
		w5300_shutdown();	// shuts down host OS connections, etc, emulator is being done
	} else {
		if (data & 8) ERROR_WINDOW("W5300: PPPoE mode is not emulated");
		if (data & 4) ERROR_WINDOW("W5300: data bus byte-order swap feature is not emulated");
		if ((data & 1) == 0) ERROR_WINDOW("W5300: direct mode is NOT emulated, only indirect");
		mr1 = data;
	}
}
void w5300_write_idm_ar0 ( Uint8 data ) {	// high byte of address
	idm_ar0 = data;
	idm_ar = (idm_ar & 0xFF) | ((data & 0x3F) << 8);
}
void w5300_write_idm_ar1 ( Uint8 data ) {	// low byte of address
	idm_ar1 = data;
	idm_ar = (idm_ar & 0xFF00) | (data & 0xFE);
}
void w5300_write_idm_dr0 ( Uint8 data ) {	// high byte of adta
	write_reg(idm_ar, data);
}
void w5300_write_idm_dr1 ( Uint8 data ) {	// low byte of data
	write_reg(idm_ar | 1, data);
}
Uint8 w5300_read_mr0 ( void ) {
	return mr0;
}
Uint8 w5300_read_mr1 ( void ) {
	return mr1;
}
Uint8 w5300_read_idm_ar0 ( void ) {
	return idm_ar0;
}
Uint8 w5300_read_idm_ar1 ( void ) {
	return idm_ar1;
}
Uint8 w5300_read_idm_dr0 ( void ) {
	return read_reg(idm_ar);
}
Uint8 w5300_read_idm_dr1 ( void ) {
	return read_reg(idm_ar | 1);
}

#endif

