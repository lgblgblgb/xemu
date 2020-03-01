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

#ifdef CONFIG_EPNET_SUPPORT

#include "epnet.h"
#include "cpu.h"
#include "xemu/emutools_socketapi.h"
#include <SDL.h>
#include <unistd.h>

#define direct_mode_epnet_shift 0
#define IS_DIRECT_MODE() (!(mr1&1))

int w5300_int;
int w5300_does_work = 0;

static Uint16 wmem[0x10000];	// 128K of internal RAM of w5300
static Uint8 wregs[0x400];	// W5300 registers
static Uint8 idm_ar0, idm_ar1;
static int   idm_ar;
static Uint8 mr0, mr1;
//static int   direct_mode;
static void (*interrupt_cb)(int);



struct w5300_fifo_st {
	Uint16	*w;
	int	readpos, storepos;
	int	free, stored;
	int	size;
};

struct w5300_socket_st {
	struct w5300_fifo_st rx_fifo;
	struct w5300_fifo_st tx_fifo;
	volatile int	mode;
	struct sockaddr_in servaddr;
	volatile xemusock_socket_t sock;
	volatile int	todo;
	volatile int	resp;
};

static struct w5300_socket_st wsockets[8];

#define RX_FIFO_FILL(sn,data)	fifo_fill(&wsockets[sn].rx_fifo,data)
#define RX_FIFO_CONSUME(sn)	fifo_consume(&wsockets[sn].rx_fifo)
#define RX_FIFO_RESET(sn)	fifo_reset(&wsockets[sn].rx_fifo)
#define RX_FIFO_GET_FREE(sn)	wsockets[sn].rx_fifo.free
#define RX_FIFO_GET_USED(sn)	wsockets[sn].rx_fifo.stored
#define TX_FIFO_FILL(sn,data)	fifo_fill(&wsockets[sn].tx_fifo,data)
#define TX_FIFO_CONSUME(sn)	fifo_consume(&wsockets[sn].tx_fifo)
#define TX_FIFO_RESET(sn)	fifo_reset(&wsockets[sn].tx_fifo)
#define TX_FIFO_GET_FREE(sn)	wsockets[sn].tx_fifo.free
#define TX_FIFO_GET_USED(sn)	wsockets[sn].tx_fifo.stored

static void   fifo_fill    ( struct w5300_fifo_st *f, Uint16 data )
{
	if (XEMU_LIKELY(f->free)) {
		if (f->stored == 0) {
			f->storepos = 0;
			f->readpos = 0;
		}
		f->w[f->storepos] = data;
		f->storepos = XEMU_UNLIKELY(f->storepos == f->size - 1) ? 0 : f->storepos + 1;
		f->stored++;
		f->free = f->size - f->stored;
	} else
		DEBUGPRINT("EPNET: W5300: FIFO: fifo is full!!" NL);
}

static Uint16 fifo_consume ( struct w5300_fifo_st *f )
{
	if (XEMU_LIKELY(f->stored)) {
		Uint16 data = f->w[f->readpos];
		f->readpos = XEMU_UNLIKELY(f->readpos == f->size - 1) ? 0 : f->readpos + 1;
		f->stored--;
		f->free = f->size - f->stored;
		return data;
	} else {
		DEBUGPRINT("EPNET: W5300: FIFO: fifo is empty!!" NL);
		return 0xFFFF;	// some answer ...
	}
}

static void fifo_reset ( struct w5300_fifo_st *f )
{
	f->stored = 0;
	f->free = f->size;
	f->readpos = 0;
	f->storepos = 0;
}

static void default_w5300_config ( void )
{
	Uint16 *w = wmem;
	// FIXME!!! This only knows about the default memory layout!
	// FIXME!!! Currently not emulated other modification of this via w5300 registers!
	for (int sn = 0; sn < 8; sn++) {
		wsockets[sn].rx_fifo.w = w;
		w += 4096;
		wsockets[sn].tx_fifo.w = w;
		w += 4096;
		wsockets[sn].rx_fifo.size = 4096;
		wsockets[sn].tx_fifo.size = 4096;
		wsockets[sn].mode = 0;
		RX_FIFO_RESET(sn);
		TX_FIFO_RESET(sn);
	}
}


static SDL_Thread *thread = NULL;
static SDL_atomic_t thread_can_go;
struct net_task_data_st {
	volatile int	task;
};
static struct {
	SDL_SpinLock	lock;		// this lock MUST be held in case of accessing fields I mark with
	volatile xemusock_socket_t	sock;		// other than init, it's the net thread only stuff, no need to lock
	//volatile int	task;		// LOCK!
	volatile int	response;	// LOCK!
	Uint8		source_ip[4];	// LOCK!
	Uint8		target_ip[4];	// LOCK!
	int		source_port;	// LOCK!
	int		target_port;	// LOCK!
	int		proto;		// LOCK!
	int		tx_size;	// LOCK!
	int		rx_size;	// LOCK!
	Uint8		rx_buf[0x20000];// LOCK!
	Uint8		tx_buf[0x20000];// LOCK!
	struct net_task_data_st data;
} net_thread_tasks[8];


// WARNING: this code runs as a _THREAD_!!
// Must be very careful what data is touched an in what way, because of the probability of race-condition with the main thread!
// SURELY the same warning applies for the main thread want to access "shared" structures!
static int net_thread ( void *ptr )
{
	int delay = 0;
	SDL_AtomicSet(&thread_can_go, 2);
	while (SDL_AtomicGet(&thread_can_go) != 0) {
		int activity = 0;
		for (int sn = 0; sn < 8; sn++) {
			Uint8 buffer[0x20000];
			struct net_task_data_st data;
			/* LOCK BEGINS */
			if (SDL_AtomicTryLock(&net_thread_tasks[sn].lock) == SDL_FALSE) {
				activity = 1;
				continue;	// if could not locked, let's continue the check on w5300 sockets, maybe we have better luck next time ...
			}
			memcpy(&data, &net_thread_tasks[sn].data, sizeof(data));
			SDL_AtomicUnlock(&net_thread_tasks[sn].lock);
			/* LOCK ENDS */
			// We don't want to held lock while doing things, like socket operations!
			switch (data.task) {
				case 0:		// there was no task ... no need to answer, etc
					continue;
				case 1:		// UDP send request
					break;
			}
			// the response should be done while locking too!
			/* LOCK BEGINS */
			SDL_AtomicLock(&net_thread_tasks[sn].lock);	// we can't use "TryLock" here, as we MUST write the response before checking the next w5300 soket!!
			memcpy(&net_thread_tasks[sn].data, &data, sizeof(data));
			SDL_AtomicUnlock(&net_thread_tasks[sn].lock);
			/* LOCK ENDS */
		}
		if (activity) {
			delay = 0;
		} else {
			if (delay < 50)
				delay++;
			SDL_Delay(delay);
		}
	}
	return 1976;
}


static void init_net_thread_task_list ( void )
{
	for (int sn = 0; sn < 8; sn++) {
		net_thread_tasks[sn].sock = XS_INVALID_SOCKET;
		net_thread_tasks[sn].data.task = 0;
	}
}


static int add_net_thread_task ( int sn, Uint8 *buf )
{
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



static Uint8 read_reg ( int addr )
{
	Uint8 data;
	addr &= 0x3FF;
	if (addr >= 0x200) {
		int sn = (addr - 0x200) >> 6;	// sn: socket number (0-7)
		int sn_base = addr & ~0x3F;
		int sn_reg  = addr &  0x3F;
		switch (sn_reg) {
			case 0x00:
			case 0x01:
			case 0x02:
			case 0x03:
			case 0x06:	// Sn_IR0
			case 0x07:	// Sn_IR1
			case 0x08:	// Sn_SSR0 [reserved]
			case 0x09:	// Sn_SSR1
			case 0x0A:	// Sn_PORTR0, source port register
			case 0x0B:	// Sn_PORTR1, source port register
			case 0x14:	// Sn_DIPR0, Destination IP Address Register
			case 0x15:	// Sn_DIPR1, Destination IP Address Register
			case 0x16:	// Sn_DIPR2, Destination IP Address Register
			case 0x17:	// Sn_DIPR3, Destination IP Address Register
			case 0x20:	// Sn_TX_WRSR0, TX Write Size Register, but all bits are unused so ...
				// for these registers are KNOWN it's OK to pass the value in "wregs" as answer
				data = wregs[addr];
				break;
			case 0x12:	// Sn_DPORTR0, destination port register, !!WRITE-ONLY-REGISTER!!
				data = 0xFF;
				break;
			case 0x13:	// Sn_DPORTR1, destination port register, !!WRITE-ONLY-REGISTER!!
				data = 0xFF;
				break;
			case 0x21:	// Sn_TX_WRSR1, TX Write Size Register
				data = wregs[addr] & 1;	// only LSB one bit is valid!
				break;
			case 0x22:	// Sn_TX_WRSR2, TX Write Size Register
				data = wregs[addr];
				break;
			case 0x23:	// Sn_TX_WRSR3, TX Write Size Register
				data = wregs[addr];
				break;
			case 0x25:	// Sn_TX_FSR1 (Sn_TX_FSR0 is not written, since the value cannot be so big, even this FSR1 only 1 bit LSB is used!)
				data = (TX_FIFO_GET_FREE(sn) >> 16) & 0x01;
				break;
			case 0x26:	// Sn_TX_FSR2
				data = (TX_FIFO_GET_FREE(sn) >>  8) & 0xFF;
				break;
			case 0x27:	// Sn_TX_FSR3
				data =  TX_FIFO_GET_FREE(sn)        & 0xFF;
				break;
			case 0x2E:	// Sn_TX_FIFOR0, for real TX FIFO can only be read in memory test mode ... FIXME
				{ Uint16 data16 = TX_FIFO_CONSUME(sn);
				data = data16 >> 8;
				wregs[addr + 1] = data16 & 0xFF; }
				break;
			case 0x2F:	// Sn_TX_FIFOR1, for real TX FIFO can only be read in memory test mode ... FIXME
				data = wregs[addr];	// access of reg Sn_BASE + 0x2E stored the needed value here!
				break;
			case 0x30:	// Sn_RX_FIFOR0
				{ Uint16 data16 = RX_FIFO_CONSUME(sn);
				data = data16 >> 8;
				wregs[addr + 1] = data16 & 0xFF; }
				break;
			case 0x31:	// Sn_RX_FIFOR1
				data = wregs[addr];	// access of reg Sn_BASE + 0x30 stored the needed value here!
				break;
			default:
				DEBUGPRINT("EPNET: W5300: reading unemulated SOCKET register $%03X S-%d/$%02X" NL, addr, sn, sn_reg);
				data = wregs[addr];	// no idea, just pass back the value ...
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
		int xerrno;
		switch (sn_reg) {
			case 0x00:	// Sn_MR0, socket mode register
			case 0x01:	// Sn_MR1
				wregs[addr] = data;
				break;
			case 0x02:	// Sn_CR0 [reserved], command register, this byte cannot be written
			case 0x08:	// SSR0 [reserved] cannot be written by the USER
			case 0x09:	// SSR1 cannot be written by the USER
				break;
			case 0x03:	// Sn_CR1, command register
				// "When W5300 detects any command, Sn_CR is automatically cleared to 0x00. Even though Sn_CR is
				// cleared to 0x00, the command can be still performing. It can be checked by Sn_IR or Sn_SSR if
				// command is completed or not."
				wregs[addr] = 0;
				DEBUGPRINT("EPNET: W5300: got command $%02X on SOCKET %d" NL, data, sn);
				switch (data) {
					case 0x01:	// OPEN
						// See the protocol the socket wanted to be open with. Currently supporting only TCP and UDP, no raw or anything ...
						switch (wregs[sn_base + 1] & 15) {
							case 1: // TCP
								if (wsockets[sn].sock != XS_INVALID_SOCKET) {
									xemusock_close(wsockets[sn].sock, NULL);
									wsockets[sn].sock = XS_INVALID_SOCKET;
								}
								wsockets[sn].mode = 1;
								wregs[sn_base + 9] = 0x13;	// SOCK_INIT status, telling that socket is open in TCP mode
								xemusock_fill_servaddr_for_inet(
									&wsockets[sn].servaddr,
									xemusock_ipv4_netoctetarray_to_netlong(wregs + sn_base + 0x14),	// pointer to IP address bytes
									(wregs[sn_base + 0x12] << 8) | wregs[sn_base + 0x13]	// port number
								);
								//wsockets[sn].todo = TODO_TCP_CREATE;
								DEBUGPRINT("EPNET: W5300: OPEN: socket %d is open in TCP mode now towards %d.%d.%d.%d:%d" NL,
									sn,
									wregs[sn_base + 0x14], wregs[sn_base + 0x15], wregs[sn_base + 0x16], wregs[sn_base + 0x17],	// IP
									(wregs[sn_base + 0x12] << 8) | wregs[sn_base + 0x13]	// port number
								);
								break;
							case 2: // UDP
								if (wsockets[sn].sock != XS_INVALID_SOCKET) {
									xemusock_close(wsockets[sn].sock, NULL);
									wsockets[sn].sock = XS_INVALID_SOCKET;
								}
								xemusock_fill_servaddr_for_inet(
									&wsockets[sn].servaddr,
									xemusock_ipv4_netoctetarray_to_netlong(wregs + sn_base + 0x14),	// pointer to IP address bytes
									(wregs[sn_base + 0x12] << 8) | wregs[sn_base + 0x13]	// port number
								);
								wsockets[sn].sock = xemusock_create_for_inet(XEMUSOCK_UDP, XEMUSOCK_NONBLOCKING, &xerrno);
								if (wsockets[sn].sock != XS_INVALID_SOCKET) {
									wsockets[sn].mode = 2;
									wregs[sn_base + 9] = 0x22;	// SOCK_UDP status, telling that socket is open in UDP mode
									DEBUGPRINT("EPNET: W5300: OPEN: socket %d is open in UDP mode now towards %d.%d.%d.%d:%d" NL,
										sn,
										wregs[sn_base + 0x14], wregs[sn_base + 0x15], wregs[sn_base + 0x16], wregs[sn_base + 0x17], // IP
										(wregs[sn_base + 0x12] << 8) | wregs[sn_base + 0x13]	// port number
									);
								} else {
									DEBUGPRINT("EPNET: EMU: network problem: %s" NL, xemusock_strerror(xerrno));
									wsockets[sn].mode = 0;
									wregs[sn_base + 9] = 0;
								}
								break;
							default:
								if (wsockets[sn].sock != XS_INVALID_SOCKET) {
									xemusock_close(wsockets[sn].sock, NULL);
									wsockets[sn].sock = XS_INVALID_SOCKET;
								}
								wsockets[sn].mode = 0;		// ???
								wregs[sn_base + 9] = 0;
								DEBUGPRINT("EPNET: W5300: OPEN: unknown protocol on SOCKET %d: %d" NL, sn, wregs[sn_base + 1] & 15);
								break;
						}
						break;
					case 0x10:	// CLOSE
						wsockets[sn].todo = 0;
						wsockets[sn].mode = 0;
						wregs[sn_base + 9] = 0;	// "Sn_SSR is changed to SOCK_CLOSED."
						DEBUGPRINT("EPNET: W5300: command CLOSE on SOCKET %d" NL, sn);
						if (wsockets[sn].sock != XS_INVALID_SOCKET) {
							xemusock_close(wsockets[sn].sock, NULL);
							wsockets[sn].sock = XS_INVALID_SOCKET;
						}
						break;
					case 0x20:	// SEND
						DEBUGPRINT("EPNET: W5300: SEND command on SOCKET %d" NL, sn);
						if (wregs[sn_base + 9] == 0x22) {
							// UDP-SEND!!
							DEBUGPRINT("Target is %d.%d.%d.%d:%d UDP, source port: %d TXLEN=%d" NL,
									wregs[sn_base + 0x14],
									wregs[sn_base + 0x15],
									wregs[sn_base + 0x16],
									wregs[sn_base + 0x17],
									(wregs[sn_base + 0x12] << 8) + wregs[sn_base + 0x13],
									(wregs[sn_base + 0x0A] << 8) + wregs[sn_base + 0x0B],
									(wregs[sn_base + 0x21] << 16) + (wregs[sn_base + 0x22] << 8) +   wregs[sn_base + 0x23]
							);
							int b = 0;
							Uint8 debug[1024];
							while (TX_FIFO_GET_USED(sn)) {
								Uint16 data = TX_FIFO_CONSUME(sn);
								debug[b++] = data >> 8;
								debug[b++] = data & 0xFF;
							}
							for (int a = 0; a < b; a++) {
								printf("HEXDUMP @ %04X  %02X  [%c]\n", a, debug[a], debug[a] >= 0x20 && debug[a] < 127 ? debug[a] : '?');
							}
							//add_net_thread_tx(sn, sn_base);
						}
						break;
					case 0x40:	// RECV
					case 0x08:	// DISCON  only valid in TCP mode
					case 0x04:	// CONNECT only valid in TCP mode, operates as "client" mode
					case 0x02:	// LISTEN  only valid in TCP mode, operates as "server" mode
					case 0x21:	// SEND_MAC
					case 0x22:	// SEND_KEEP
					default:
						DEBUGPRINT("EPNET: W5300: command *UNKNOWN* ($%02X) on SOCKET %d" NL, data, sn);
						break;
				}
				break;
			case 0x06:	// Sn_IR0: not used for too much (but see Sn_IR1)
				break;
			case 0x07:	// Sn_IR1: Socket interrupt register (Sn_IR0 part is not so much used)
				wregs[addr] &= ~data;	// All bits written to '1' will cause that bit to be CLEARED!
				// TODO: if we have all zero bits now, no interrupt pending, and IR for the given socket should be cleared!
				//update_socket_interrupts(sn, wregs[addr]);
				break;
			case 0x0A:	// Sn_PORTR0, source port register [should be set before OPEN]
				wregs[addr] = data;
				break;
			case 0x0B:	// Sn_PORTR1, source port register [should be set before OPEN]
				wregs[addr] = data;
				break;
			case 0x12:	// Sn_DPORTR0, destination port register
				wregs[addr] = data;
				break;
			case 0x13:	// Sn_DPORTR1, destination port register
				wregs[addr] = data;
				break;
			case 0x14:	// Sn_DIPR0, Destination IP Address Register
				wregs[addr] = data;
				break;
			case 0x15:	// Sn_DIPR1, Destination IP Address Register
				wregs[addr] = data;
				break;
			case 0x16:	// Sn_DIPR3, Destination IP Address Register
				wregs[addr] = data;
				break;
			case 0x17:	// Sn_DIPR4, Destination IP Address Register
				wregs[addr] = data;
				break;
			case 0x2E:	// Sn_TX_FIFOR0
				wregs[addr] = data;
				break;
			case 0x2F:	// Sn_TX_FIFOR1
				TX_FIFO_FILL(sn, (wregs[addr - 1] << 8) | data);	// access of reg Sn_BASE + 0x2E stored the needed value we're referencing here!
				break;
			case 0x20:	// Sn_TX_WRSR0, TX Write Size Register, but all bits are unused so ...
				break;
			case 0x21:	// Sn_TX_WRSR1, TX Write Size Register
				wregs[addr] = data & 1;	// only LSB one bit is valid!
				break;
			case 0x22:	// Sn_TX_WRSR2, TX Write Size Register
				wregs[addr] = data;
				break;
			case 0x23:	// Sn_TX_WRSR3, TX Write Size Register
				wregs[addr] = data;
				break;
			case 0x30:	// Sn_RX_FIFOR0, for real RX FIFO can only be written in memory test mode ... FIXME
				wregs[addr] = data;
				break;
			case 0x31:	// Sn_RX_FIFOR1, for real RX FIFO can only be written in memory test mode ... FIXME
				RX_FIFO_FILL(sn, (wregs[addr - 1] << 8) | data);	// access of reg Sn_BASE + 0x30 stored the needed value we're referencing here!
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
	DEBUGPRINT("EPNET: reset, direct_mode = %s" NL, IS_DIRECT_MODE() ? "yes" : "no");
}


static Uint8 *patch_rom_add_config_pair ( Uint8 *p, const char *name, const char *value )
{
	int name_len = strlen(name);
	int value_len = strlen(value);
	*p++ = name_len;
	memcpy(p, name, name_len);
	p += name_len;
	*p++ = value_len;
	memcpy(p, value, value_len);
	p += value_len;
	*p++ = 0;
	*p = 0;	// this will be overwritten if there is more call of this func, otherwise it will close the list of var=val pair list
	return p;
}

static int patch_rom ( void )
{
	Uint8 *epnet = NULL;
	// Search for our ROM ...
	for (Uint8 *p = memory; p < memory + sizeof(memory); p += 0x4000)
		if (!memcmp(p, "EXOS_ROM", 8) && !memcmp(p + 0xD, "EPNET", 5)) {
			if (epnet) {
				ERROR_WINDOW("Multiple instances of EPNET ROM in memory?\nIt won't work too well!");
				return -1;
			} else
				epnet = p;
		}
	if (epnet) {
		int segment = (int)(epnet - memory) >> 14;
		DEBUGPRINT("EPNET: found ROM in segment %03Xh" NL, segment);
		// Found our EPNET ROM :-) Now, it's time to patch things up!
		/*if (epnet[0x18] == EPNET_IO_BASE) {
			ERROR_WINDOW("EPNET ROM has been already patched?!");
			return -1;
		}*/
		Uint8 *q = epnet + 0x4000 + (epnet[0x19] | (epnet[0x1A] << 8));
		if (q > epnet + 0x7F00) {
			ERROR_WINDOW("Bad EPNET ROM, invalid position of settings area!");
			//return -1;
		} else {
			//DEBUGPRINT("EPNET: byte at set area: %02Xh" NL, *q);
			q = patch_rom_add_config_pair(q, "DHCP",	"n");
			q = patch_rom_add_config_pair(q, "NTP",		"n");
			q = patch_rom_add_config_pair(q, "IP",		"192.168.192.168");
			q = patch_rom_add_config_pair(q, "SUBNET",	"255.255.255.0");
			q = patch_rom_add_config_pair(q, "GATEWAY",	"192.168.192.169");
			q = patch_rom_add_config_pair(q, "DNS",		"8.8.8.8");
			q = patch_rom_add_config_pair(q, "XEP128",	"this-is-an-easter-egg-from-lgb");
		}
		epnet[0x18] = EPNET_IO_BASE;	// using fixed port (instead of EPNET's decoding based on the ROM position), which matches our emulation of course.
		// MAC address setup :) According to Bruce's suggestion, though it does not matter in the emulation too much (we don't use eth level stuff anyway, but ask our host OS to do TCP/IP things ...)
		static const Uint8 mac_address[] = {0x00,0x00,0xF6,0x42,0x42,0x76};
		memcpy(epnet + 0x20, mac_address, sizeof(mac_address));
		return 0;
	} else {
		DEBUGPRINT("EPNET: cannot found EPNET ROM! Maybe it's not loaded?! EPNET emulation will not work!" NL);
		return -1;
	}
}


void epnet_init ( void (*cb)(int) )
{
	w5300_does_work = 0;
	patch_rom();
	char sockapi_error_msg[256];
	if (xemusock_init(sockapi_error_msg)) {
		ERROR_WINDOW("Cannot intiailize socket API:\n%s", sockapi_error_msg);
		w5300_does_work = 0;
	} else if (!start_net_thread()) {
		w5300_does_work = 1;
		interrupt_cb = cb ? cb : default_interrupt_callback;
		DEBUGPRINT("EPNET: init seems to be OK." NL);
		epnet_reset();
		memset(wmem, 0, sizeof wmem);
		default_w5300_config();
		init_net_thread_task_list();
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
	// since thread does not run here, it's safe to "play" with net_thread_tasks[] stuffs without any lock!
	for (int sn = 0; sn < 8; sn++) {
		if (net_thread_tasks[sn].sock != XS_INVALID_SOCKET) {
			xemusock_close(net_thread_tasks[sn].sock, NULL);
			net_thread_tasks[sn].sock = XS_INVALID_SOCKET;
		}
	}
	if (restart && thread) {
		start_net_thread();
	}
}

void epnet_uninit ( void )
{
	epnet_shutdown(0);
	//xemu_free_sockapi();
	xemusock_uninit();
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

// CONIG_EPNET_SUPPORT
#endif
