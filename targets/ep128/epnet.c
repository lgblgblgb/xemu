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
#include "xemu/z80.h"
#include <SDL.h>
#include <unistd.h>

#ifdef CONFIG_EPNET_SUPPORT

#define direct_mode_epnet_shift 0
#define IS_DIRECT_MODE() (!(mr1&1))

int w5300_int;
int w5300_does_work = 0;

static Uint8 wmem[0x20000];	// 128K of internal RAM of w5300
static Uint8 wregs[0x400];	// W5300 registers
static Uint8 idm_ar0, idm_ar1;
static int   idm_ar;
static Uint8 mr0, mr1;
//static int   direct_mode;
static void (*interrupt_cb)(int);


#ifdef XEMU_ARCH_WIN

// NOTE: Xemu framework has some networking even for WIN. However Enterprise-128 emulator is not yet
// fully integrated into the framework :( So for now, let's implement everything here. Later it's a
// TODO to re-factor the whole Enterprise-128 target within Xemu anyway, and this will go away as well then.

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
typedef int SOCKET;
#define closesocket(n) close(n)
int xemu_use_sockapi ( void ) {
	return 0;
}
void xemu_free_sockapi ( void ) {
}
#endif


static SDL_Thread *thread = NULL;
static SDL_atomic_t thread_can_go;
static struct {
	SOCKET sock;
} sockets[8];



static int net_thread ( void *ptr )
{
	SDL_AtomicSet(&thread_can_go, 2);
	while (SDL_AtomicGet(&thread_can_go) != 0)
		SDL_Delay(10);
	return 1976;
}



int start_net_thread ( void )
{
	SDL_AtomicSet(&thread_can_go, 1);
	thread = SDL_CreateThread(net_thread, "Xep128 EPNET socket thread", (void*)NULL);
	if (!thread) {
		ERROR_WINDOW("EPNET: cloud not create EPNET thread: %s", SDL_GetError());
		return -1;
	}
	Uint32 timeout = SDL_GetTicks();
	while (SDL_AtomicGet(&thread_can_go) == 1) {
		SDL_Delay(10);
		if (SDL_GetTicks() - timeout > 1000) {
			SDL_AtomicSet(&thread_can_go, 0);
			ERROR_WINDOW("EPNET: timeout for starting EPNET thread");
			return -1;
		}
	}
	DEBUGPRINT("EPNET: THREAD: thread seems to be started finely :)" NL);
	return 0;
}



static void update_interrupts ( void )
{
	int v = ((wregs[2] & wregs[4] & 0xF0) | (wregs[3] & wregs[5])) > 0;
	if (v != w5300_int) {
		w5300_int = v;
		interrupt_cb(v);
	}
}


static int socket0_fifo_pointer = 0;


static Uint8 read_reg ( int addr )
{
	Uint8 data;
	addr &= 0x3FF;
	if (addr >= 0x200) {
		int sn = (addr - 0x200) >> 6;	// sn: socket number (0-7)
		int sn_base = addr & ~0x3F;
		int sn_reg  = addr &  0x3F;
		data = wregs[addr];
		switch (sn_reg) {
			case 0x00:
			case 0x01:
			case 0x02:
			case 0x03:
			case 0x08:
			case 0x09:
				break;
			case 0x30:	// Sn_RX_FIFOR0
				data = wmem[socket0_fifo_pointer];
				socket0_fifo_pointer = (socket0_fifo_pointer + 1) & 8191;
				break;
			case 0x31:	// Sn_RX_FIFOR1
				data = wmem[socket0_fifo_pointer];
				socket0_fifo_pointer = (socket0_fifo_pointer + 1) & 8191;
				break;
			default:
				DEBUGPRINT("EPNET: W5300: reading unemulated SOCKET register $%03X S-%d/$%02X" NL, addr, sn, sn_reg);
				break;
		}
	} else {
		data = wregs[addr];
	}
	//DEBUGPRINT("EPNET: W5300-REG: reading register $%03X with data $%02X" NL, addr, data);
	return data;
}


static void write_reg ( int addr, Uint8 data )
{
	addr &= 0x3FF;
	//DEBUGPRINT("EPNET: W5300-REG: writing register $%03X with data $%02X" NL, addr, data);
	if (addr >= 0x200) {
		int sn = (addr - 0x200) >> 6;	// sn: socket number (0-7)
		int sn_base = addr & ~0x3F;
		int sn_reg  = addr &  0x3F;
		switch (sn_reg) {
			case 0x00:	// Sn_MR0, socket mode register
			case 0x01:	// Sn_MR1
				wregs[addr] = data;
				break;
			case 0x02:	// Sn_CR0 [reserved], command register
				break;
			case 0x03:	// Sn_CR1, command register
				// "When W5300 detects any command, Sn_CR is automatically cleared to 0x00. Even though Sn_CR is
				// cleared to 0x00, the command can be still performing. It can be checked by Sn_IR or Sn_SSR if
				// command is completed or not."
				wregs[sn_base + 2] = 0;
				wregs[sn_base + 3] = 0;
				// Hack for EPNET test, we pre-set SSR now!
				// FIXME: we should parse the command etc ... this is just a wild stuff now to bypass EPNET ROM's init routine!
				DEBUGPRINT("EPNET: W5300: got command $%02X on SOCKET %d" NL, data, sn);
				switch (data) {
					case 0x01:	// OPEN
					case 0x02:	// LISTEN  only valid in TCP mode, operates as "server" mode
					case 0x04:	// CONNECT only valid in TCP mode, operates as "client" mode
					case 0x08:	// DISCON  only valid in TCP mode
					case 0x10:	// CLOSE
						// "Sn_SSR is changed to SOCK_CLOSED."
						wregs[sn_base + 8] = 0;
						wregs[sn_base + 9] = 0;
						DEBUGPRINT("EPNET: W5300: command CLOSE on SOCKET %d" NL, sn);
						break;
					case 0x20:	// SEND
					case 0x21:	// SEND_MAC
					case 0x22:	// SEND_KEEP
					case 0x40:	// RECV
					default:
						break;
				}
				if (data == 0x10) {
					wregs[sn_base + 8] = 0;
					wregs[sn_base + 9] = 0;
				} else {
					wregs[sn_base + 8] = 0;
					wregs[sn_base + 9] = 0x13;
				}
				break;
			case 0x30:	// Sn_RX_FIFOR0, for real RX can only be written in memory test mode ..
				wmem[socket0_fifo_pointer] = data;
				socket0_fifo_pointer = (socket0_fifo_pointer + 1) & 8191;
				break;
			case 0x31:	// Sn_RX_FIFOR1
				wmem[socket0_fifo_pointer] = data;
				socket0_fifo_pointer = (socket0_fifo_pointer + 1) & 8191;
				break;
			default:
				DEBUGPRINT("EPNET: W5300: writing unemulated SOCKET register $%03X S-%d/$%02X" NL, addr, sn, sn_reg);
				wregs[addr] = data;
				break;
		}
	} else switch (addr) {
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
		case 0xFE:	// IDR (ID register)
		case 0xFF:	// IDR (ID register)
			break;	// these registers are not writable, skip writing!
		default:
			wregs[addr] = data;
			break;
	}
}


static void default_interrupt_callback ( int level ) {
	DEBUGPRINT("EPNET: INTERRUPT -> %d" NL, level);
}


/* --- Interface functions --- */


void epnet_reset ( void )
{
	static const Uint8 default_mac[] = {0xC1,0xC2,0xC3,0xC4,0xC5,0xC6};
	memset(wregs, 0, sizeof wregs);
	mr0 = 0x38; mr1 = 0x00;
	//direct_mode = (mr1 & 1) ? 0 : 1;
	idm_ar0 = 0; idm_ar1 = 0; idm_ar = 0;
	w5300_int = 0;
	wregs[0x1C] = 0x07; wregs[0x1D] = 0xD0; // RTR retransmission timeout-period register
	wregs[0x1F] = 8; 			// RCR retransmission retry-count register
	memset(wregs + 0x20, 8, 16);		// TX and RX mem size conf
	wregs[0x31] = 0xFF;			// MTYPER1
	wregs[0xFE] = 0x53;			// IDR: ID register
	wregs[0xFF] = 0x00;			// IDR: ID register
	memcpy(wregs + 8, default_mac, 6);	// some default MAC, maybe it's not needed as w5300 does not have it too much AND this emulation does not care either ;)
	DEBUGPRINT("EPNET: reset, direct_mode = %s" NL, IS_DIRECT_MODE() ? "yes" : "no");
}


void epnet_init ( void (*cb)(int) )
{
	if (xemu_use_sockapi())
		w5300_does_work = 0;
	else if (!start_net_thread()) {
		w5300_does_work = 1;
		interrupt_cb = cb ? cb : default_interrupt_callback;
		DEBUGPRINT("EPNET: init seems to be OK." NL);
		epnet_reset();
		memset(wmem, 0, sizeof wmem);
		for (int a = 0; a < 8; a++) {
			sockets[a].sock = -1;
		}
	}
}





static void epnet_shutdown ( int restart )
{
	DEBUGPRINT("EPNET: shutdown pending connections (if any)" NL);
	if (thread) {
		int retval;
		SDL_AtomicSet(&thread_can_go, 0);
		SDL_WaitThread(thread, &retval);
		DEBUGPRINT("EPNET: THREAD: %p exited with code %d" NL, thread, retval);
	}
	for (int sn = 0; sn < 8; sn++) {
		if (sockets[sn].sock >= 0) {
			closesocket(sockets[sn].sock);
			sockets[sn].sock = -1;
		}
	}
	if (restart && thread) {
		start_net_thread();
	}
}

void epnet_uninit ( void )
{
	epnet_shutdown(0);
	xemu_free_sockapi();
	w5300_does_work = 0;
	DEBUGPRINT("EPNET: uninit" NL);
}

Uint8 epnet_read_cpu_port ( unsigned int port )
{
	Uint8 data;
	if (XEMU_UNLIKELY(IS_DIRECT_MODE())) {
		port += direct_mode_epnet_shift;
		if (port >= 2) {
			data = read_reg(port);
			DEBUGPRINT("EPNET: IO: reading in *DIRECT-MODE* W5300 register $%03X, data: $%02X @ PC=$%04X" NL, port, data, Z80_PC);
			return data;
		}
	}
	switch (port) {
		case 0:
			data = mr0;
			DEBUGPRINT("EPNET: IO: reading MR0: $%02X @ PC=$%04X" NL, data, Z80_PC);
			return data;
		case 1:
			data = mr1;
			DEBUGPRINT("EPNET: IO: reading MR1: $%02X @ PC=$%04X" NL, data, Z80_PC);
			return data;
		case 2:
			data = idm_ar0;
			DEBUGPRINT("EPNET: IO: reading IDM_AR0: $%02X @ PC=$%04X" NL, data, Z80_PC);
			return data;
		case 3:
			data = idm_ar1;
			DEBUGPRINT("EPNET: IO: reading IDM_AR1: $%02X @ PC=$%04X" NL, data, Z80_PC);
			return data;
		case 4:
			data = read_reg(idm_ar + 0);
			DEBUGPRINT("EPNET: IO: reading W5300 reg#$%03X through IDM_DR0: $%02X @ PC=$%04X" NL, idm_ar + 0, data, Z80_PC);
			return data;
		case 5:
			data = read_reg(idm_ar + 1);
			DEBUGPRINT("EPNET: IO: reading W5300 reg#$%03X through IDM_DR1: $%02X @ PC=$%04X" NL, idm_ar + 1, data, Z80_PC);
			return data;
		default:
			DEBUGPRINT("EPNET: IO: reading *UNDECODED* W5300 port in indirect mode: $%03X @ PC=$%04X" NL, port, Z80_PC);
			return 0xFF;
	}
}



void  epnet_write_cpu_port ( unsigned int port, Uint8 data )
{
	if (XEMU_UNLIKELY(IS_DIRECT_MODE())) {
		port += direct_mode_epnet_shift;
		if (port >= 2) {
			DEBUGPRINT("EPNET: IO: writing in *DIRECT-MODE* W5300 register $%03X, data: $%02X @ PC=$%04X" NL, port, data, Z80_PC);
			write_reg(port, data);
			return;
		}
	}
	switch (port) {
		case 0:
			DEBUGPRINT("EPNET: IO: writing MR0: $%02X @ PC=$%04X" NL, data, Z80_PC);
			if (data & 1) ERROR_WINDOW("EPNET: FIFO byte-order swap feature is not emulated");
			mr0 = (mr0 & 0xC0) | (data & 0x3F); // DBW and MPF bits cannot be overwritten by user
			break;
		case 1:
			DEBUGPRINT("EPNET: IO: writing MR1: $%02X @ PC=$%04X" NL, data, Z80_PC);
			if (data & 128) { // software reset?
				epnet_reset();
				epnet_shutdown(1);
			} else {
				if (data & 0x20)
					DEBUGPRINT("EPNET: memory-test mode by 0x20 on MR1" NL);
				if (data & 8) ERROR_WINDOW("EPNET: PPPoE mode is not emulated");
				if (data & 4) ERROR_WINDOW("EPNET: data bus byte-order swap feature is not emulated");
				//if ((data & 1) == 0) ERROR_WINDOW("EPNET: direct mode is NOT emulated, only indirect");
				if (((mr1 ^ data) & 1)) {
					DEBUGPRINT("EPNET: w5300 access mode change: %s -> %s\n",
							(mr1 & 1) ? "indirect" : "direct",
							(data & 1) ? "indirect" : "direct"
					);
				}
				mr1 = data;
			}
			break;
		case 2:
			idm_ar0 = data & 0x3F;
			idm_ar = (idm_ar0 << 8) | idm_ar1;
			//DEBUGPRINT("EPNET: IO: IDM: setting AR0 to $%02X, AR now: $%03X" NL, idm_ar0, idm_ar);
			break;
		case 3:
			idm_ar1 = data & 0xFE;	// LSB is chopped off, since the fact reading/writing IDM_DR0 _OR_ 1 will tell that ...
			idm_ar = (idm_ar0 << 8) | idm_ar1;
			//DEBUGPRINT("EPNET: IO: IDM: setting AR1 to $%02X, AR now: $%03X" NL, idm_ar1, idm_ar);
			break;
		case 4:
			DEBUGPRINT("EPNET: IO: writing W5300 reg#$%03X through IDM_DR0: $%02X @ PC=$%04X" NL, idm_ar + 0, data, Z80_PC);
			write_reg(idm_ar + 0, data);
			break;
		case 5:
			DEBUGPRINT("EPNET: IO: writing W5300 reg#$%03X through IDM_DR1: $%02X @ PC=$%04X" NL, idm_ar + 1, data, Z80_PC);
			write_reg(idm_ar + 1, data);
			break;
		default:
			DEBUGPRINT("EPNET: IO: writing *UNDECODED* W5300 port in indirect mode: $%03X @ PC=$%04X" NL, port, Z80_PC);
			break;
	}
}

#endif
