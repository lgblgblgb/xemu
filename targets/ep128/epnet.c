/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

   Partial Wiznet W5300 emulation, using the host OS (which runs the emulator)
   TCP/IP API. Thus, many of the W5300 features won't work, or limited, like:
   RAW mode, ICMP sockets, listening mode (it would be possible but eg in case
   of UNIX there would be a need for privilege, which is not so nice to
   run an emulator as "root" user), no IP/MAC setting (always uses the OS IP
   and MAC). DHCP and DNS is planned to "faked" so w5300 softwares trying to
   get IP address via DHCP or wanting resolve a DNS name would get an answer
   from this w5300 emulator instead of from the "real" network.

   BIG-FAT-WARNING: this does NOT emulate w5300 just how EPNET and EP software
   needs. Also DIRECT memory access mode is WRONG, but this is by will: on
   EPNET this is just an "artifact" to be used as a dirty way to check link
   status ;-P On EPNET only the first 8 addresses can be
   accessed, so direct mode is really a trick here only!!! Also, it implements
   only 8 bit access.

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
#include "epnet.h"

#ifdef CONFIG_EPNET_SUPPORT

#define direct_mode_epnet_shift 0

int w5300_int;
int w5300_does_work = 0;

static Uint8 wmem[0x20000];	// 128K of internal RAM of w5300
static Uint8 wregs[0x400];	// W5300 registers
static Uint8 idm_ar0, idm_ar1, idm_ar;
static Uint8 mr0, mr1;
static int direct_mode;
static void (*interrupt_cb)(int);


#ifdef XEMU_ARCH_WIN
#include <winsock2.h>
#include <windows.h>
static int _winsock_init_status = 1;    // 1 = todo, 0 = was OK, -1 = error!
int xemu_use_sockapi ( void )
{
	WSADATA wsa;
	if (_winsock_init_status <= 0)
		return _winsock_init_status;
	if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
		ERROR_WINDOW("Failed to initialize winsock2, error code: %d", WSAGetLastError());
		_winsock_init_status = -1;
		return -1;
	}
	if (LOBYTE(wsa.wVersion) != 2 || HIBYTE(wsa.wVersion) != 2) {
		WSACleanup();
		ERROR_WINDOW("No suitable winsock API in the implemantion DLL (we need v2.2, we got: v%d.%d), windows system error ...", HIBYTE(wsa.wVersion), LOBYTE(wsa.wVersion));
		_winsock_init_status = -1;
		return -1;
	}
	DEBUGPRINT("WINSOCK: initialized, version %d.%d\n", HIBYTE(wsa.wVersion), LOBYTE(wsa.wVersion));
	_winsock_init_status = 0;
	return 0;
}

void xemu_free_sockapi ( void )
{
	if (_winsock_init_status == 0) {
		WSACleanup();
		_winsock_init_status = 1;
		DEBUGPRINT("WINSOCK: uninitialized." NL);
	}
}
#else
int xemu_use_sockapi ( void ) {
	return 0;
}
void xemu_free_sockapi ( void ) {
}
#endif





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
	addr &= 0x3FF;
	Uint8 data = wregs[addr];
	DEBUGPRINT("EPNET: W5300-REG: reading register $%03X with data $%02X" NL, addr, data);
	return data;
}

static void write_reg ( int addr, Uint8 data )
{
	addr &= 0x3FF;
	DEBUGPRINT("EPNET: W5300-REG: writing register $%03X with data $%02X" NL, addr, data);
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
			break;
	}
}


static void default_interrupt_callback ( int level ) {
	DEBUGPRINT("EPNET: INTERRUPT -> %d" NL, level);
}


/* ---Interface functions --- */


void epnet_reset ( void )
{
	static const Uint8 default_mac[] = {0xC1,0xC2,0xC3,0xC4,0xC5,0xC6};
	memset(wregs, 0, sizeof wregs);
	mr0 = 0x38; mr1 = 0x00;
	direct_mode = (mr1 & 1) ? 0 : 1;
	idm_ar0 = 0; idm_ar1 = 0; idm_ar = 0;
	w5300_int = 0;
	wregs[0x1C] = 0x07; wregs[0x1D] = 0xD0; // RTR retransmission timeout-period register
	wregs[0x1F] = 8; 			// RCR retransmission retry-count register
	memset(wregs + 0x20, 8, 16);		// TX and RX mem size conf
	wregs[0x31] = 0xFF;			// MTYPER1
	wregs[0xFE] = 0x53;	// IDR: ID register
	wregs[0xFF] = 0x00;	// IDR: ID register
	memcpy(wregs + 8, default_mac, 6);
	DEBUGPRINT("EPNET: reset, direct_mode = %d" NL, direct_mode);
}

void epnet_init ( void (*cb)(int) )
{
	if (xemu_use_sockapi())
		w5300_does_work = 0;
	else {
		w5300_does_work = 1;
		interrupt_cb = cb ? cb : default_interrupt_callback;
		DEBUGPRINT("EPNET: init" NL);
		epnet_reset();
		memset(wmem, 0, sizeof wmem);
	}
}

void epnet_shutdown ( void )
{
	DEBUGPRINT("EPNET: shutdown pending connections (if any)" NL);
}

void epnet_uninit ( void )
{
	epnet_shutdown();
	xemu_free_sockapi();
	w5300_does_work = 0;
	DEBUGPRINT("EPNET: uninit" NL);
}

Uint8 epnet_read_cpu_port ( int port )
{
	Uint8 data;
	switch (port) {
		case 0:
			//data = (direct_mode ? read_reg(port + direct_mode_epnet_shift) : mr0);
			data = mr0;
			break;
		case 1:
			//data = (direct_mode ? read_reg(port + direct_mode_epnet_shift) : mr1);
			data = mr1;
			break;
		case 2:
			data = (direct_mode ? read_reg(port + direct_mode_epnet_shift) : idm_ar0);
			break;
		case 3:
			data = (direct_mode ? read_reg(port + direct_mode_epnet_shift) : idm_ar1);
			break;
		case 4:
			data = read_reg(direct_mode ? (port + direct_mode_epnet_shift) : idm_ar);
			break;
		case 5:
			data = read_reg(direct_mode ? (port + direct_mode_epnet_shift) : (idm_ar + 1));
			break;
		case 6:
		case 7:
			data = (direct_mode ? read_reg(port + direct_mode_epnet_shift) : 0xFF);
			break;
		default:
			// Note: ports 8-15 are for CF on EPNET, but not emulated yet!
			data = 0xFF;
			break;
	}
	if ((port & 7) == port)
		DEBUGPRINT("EPNET: IO: after reading EPNET CPU-port $%03X by CPU in %s mode, result: $%02X" NL, port, direct_mode ? "*DIRECT*" : "indirect", data);
	return data;
}


void  epnet_write_cpu_port ( int port, Uint8 data )
{
	if ((port & 7) == port)
		DEBUGPRINT("EPNET: IO: before writing EPNET CPU-port $%03X by CPU in %s mode, data: $%02X" NL, port, direct_mode ? "*DIRECT*" : "indirect", data);
	switch (port) {
		case 0:
			DEBUGPRINT("EPNET: writing MR0 with data $%02X" NL, data);
			if (data & 1) ERROR_WINDOW("EPNET: FIFO byte-order swap feature is not emulated");
			mr0 = mr0 | (data & 0x3F); // DBW and MPF bits cannot be overwritten by user
			break;
		case 1:
			DEBUGPRINT("EPNET: writing MR1 with data $%02X" NL, data);
			if (data & 128) { // software reset?
				epnet_reset();
				epnet_shutdown();
			} else {
				if (data & 8) ERROR_WINDOW("EPNET: PPPoE mode is not emulated");
				if (data & 4) ERROR_WINDOW("EPNET: data bus byte-order swap feature is not emulated");
				//if ((data & 1) == 0) ERROR_WINDOW("EPNET: direct mode is NOT emulated, only indirect");
				if (((mr1 ^ data) & 1)) {
					direct_mode = (data & 1) ? 0 : 1;
					DEBUGPRINT("EPNET: w5300 access mode change: %s -> %s, new val: %s\n",
							(mr1 & 1) ? "indirect" : "direct",
							(data & 1) ? "indirect" : "direct",
							direct_mode ? "direct" : "indirect"
					);
				}
				mr1 = data;
			}
			break;
		case 2:
			if (direct_mode) {
				write_reg(port + direct_mode_epnet_shift, data);
			} else {
				idm_ar0 = data & 0x3F;
				idm_ar = (idm_ar0 << 8) | idm_ar1;
			}
			break;
		case 3:
			if (direct_mode) {
				write_reg(port + direct_mode_epnet_shift, data);
			} else {
				idm_ar1 = data & 0xFE;	// LSB is chopped off, since reading/writing IDM_DR0 and 1 will tell that ...
				idm_ar = (idm_ar0 << 8) | idm_ar1;
			}
			break;
		case 4:
			if (direct_mode) {
				write_reg(port + direct_mode_epnet_shift, data);
			} else {
				write_reg(idm_ar, data);
			}
			break;
		case 5:
			if (direct_mode) {
				write_reg(port + direct_mode_epnet_shift, data);
			} else {
				write_reg(idm_ar + 1, data);
			}
			break;
		case 6:
			if (direct_mode)
				write_reg(port + direct_mode_epnet_shift, data);
			break;
		case 7:
			if (direct_mode)
				write_reg(port + direct_mode_epnet_shift, data);
			break;
		default:
			// Note: ports 8-15 are for CF on EPNET, but not emulated yet!
			break;
	}
}

#endif
