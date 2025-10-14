/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2018,2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include "xemu/emutools.h"
#include "xemu/ethertap.h"
#include "ethernet65.h"
#include "xemu/cpu65.h"


/* It seems, at least on Nexys4DDR board, the used ethernet controller chip is: LAN8720A

   http://ww1.microchip.com/downloads/en/DeviceDoc/8720a.pdf

   This information is not so useful, as M65 programs in general does not "meet" it, however
   in case of MII registers, it can be an interesting topic, maybe ... */


/* Possible future work:
	* FIXME: there is no IRQ generated currently for the CPU! So only polling method is used.
	  the problem is not as trivial to solve as it seems, since a *thread* can affect IRQs
	  also updated by the main CPU emulator thread ... With using locking for all of these,
	  would result in quite slow emulation :(
	* Maybe allow TUN devices to be supported. It does not handle ethernet level stuffs, no source and
	target MAC for example :-O But TUN devices are supported by wider range of OSes (eg: OSX) and we
	probably can emulate the missing ethernet level stuffs somehow in this source on RX (and chop
	them off, in case of TX ...).
	* Implement a mini DHCP client here, so user does not need to install a DHCP server on the PC, just
	for testing M65 software with Xemu wants to use a DHCP. It would catch / answer for DHCP-specifc
	requests, instead of using really the TAP device to send, and waiting for answer.
*/


#ifdef	ETH65_NO_DEBUG
#	warning	"Do not set ETH65_NO_DEBUG manually."
#else
#	if	defined(DISABLE_DEBUG) || !defined(HAVE_ETHERTAP)
#		define	ETH65_NO_DEBUG
#	endif
#endif

// RX_BUFFERS **must** be power of two!
#define RX_BUFFERS		32
#define PLUS1(n)		(((n) + 1) & ((RX_BUFFERS) - 1))

#define	SHUTDOWN_TIMEOUT_MSEC	100

#define ETH_II_FRAME_ARP	0x0806
#define ETH_II_FRAME_IPV4	0x0800

// Must be the same as bit positions in register #5
#define	RX_FILTER_MAC		0x01
#define	RX_FILTER_ACCEPT_BCAST	0x10
#define	RX_FILTER_ACCEPT_MCAST	0x20

#define	THREAD_STATUS_DISABLED	0
#define THREAD_STATUS_RUNNING	1
#define	THREAD_STATUS_EXIT	2
#define THREAD_STATUS_EXITED	3

#define ETH_LOCK()		SDL_AtomicLock(&eth_lock)
#define ETH_UNLOCK()		SDL_AtomicUnlock(&eth_lock)

Uint8 eth_rx_buf[0x800];		// RX buffer as seen by the CPU, read-only for CPU, CPU can read it without lock held
Uint8 eth_tx_buf[0x800];		// TX buffer as seen by the CPU, write-only for CPU, CPU can write it without lock held

// This struct is only for sematic reasons: everything here to be R/W **NEEDS** "eth_lock" held!
static struct {
	Uint8 rx_buffers[RX_BUFFERS * 0x800];	// all RX buffers
	int cpu_sel;				// CPU RX buffer #number in use (one buffer: 2K - 0x800 bytes - in rx_buffers)
	int eth_sel;				// ETH RX buffer #number in use (one buffer: 2K - 0x800 bytes - in rx_buffers)
	int tx_size;				// if non-zero: submit packet from eth_tx_buf for TX (the actual work is done by the thread!)
	bool rx_disabled;			// frame receiving is disabled (drop received frames), eg during controller reset
	bool thread_to_reset;			// signal thread to reset its internals
	bool under_reset;
	bool tx_irq;				// when triggering TX by CPU, it must be set to "false", the thread will set to "true" is it happened
	Uint8 tx_irq_enabled;
	Uint8 rx_irq_enabled;
} com;
#ifndef	ETH65_NO_DEBUG
static bool eth_debug = false;
static bool force_filters = false;
#endif
static const Uint8 default_mac[6] = {0x02,0x47,0x53,0x65,0x65,0x65};	// 00:80:10:... would be nicer, though a bit of cheating, it belongs to Commodore International(TM).
#ifdef	HAVE_ETHERTAP
static const Uint8 mac_bcast[6]	  = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};	// also used as IPv4 bcast detection with filters, if only the first 4 bytes are checked
#endif
#define miim_reg_num (eth_regs[6] & 0x1F)
static Uint8 miimlo8[0x20], miimhi8[0x20];	// not emulated too much ... all I have, that you can read/write all MIIM registers freely, and that's all
static Uint8 eth_regs[0x10];
#define mac_address (eth_regs + 9)
static SDL_SpinLock eth_lock = 0;		// the lock protecting "com" structure access: ETH_LOCK() and ETH_UNLOCK() should be used to operate it
static SDL_atomic_t threadsafe_status_regs;	// low byte: some bits of reading reg 0, high byte: some bits of reading reg 1
static SDL_atomic_t threadsafe_rx_filtering;	// values: RX_FILTER_* bitmask, CPU->thread
static SDL_atomic_t threadsafe_thread_status;	// used by the main thread to monitor the status of the ethernet thread
#ifdef	HAVE_ETHERTAP
static SDL_atomic_t stat_rx_counter, stat_tx_counter;
static unsigned int remote_ip;
static Uint8 remote_mac[6];
static char *tap_name = NULL;
#endif


#ifndef ETH65_NO_DEBUG
#define ETHDEBUG(...)	do { \
				if (XEMU_UNLIKELY(eth_debug)) { \
					DEBUGPRINT(__VA_ARGS__); \
				} \
			} while (0)
#else
#define ETHDEBUG	DEBUG
#endif




// Can be called by both of main and eth thread but MUST be called with "eth_lock" held
static void calc_status_changes ( void )
{
	static bool last_irq = false;

	const int free_buffers = (com.cpu_sel > com.eth_sel) ? com.cpu_sel - com.eth_sel : RX_BUFFERS + com.cpu_sel - com.eth_sel;
	const bool rx_irq = PLUS1(com.cpu_sel) != com.eth_sel;

	const bool final_irq = (rx_irq && com.rx_irq_enabled) || (com.tx_irq && com.tx_irq_enabled);
	if (last_irq != final_irq) {
		// The TODO place to trigger interrupt?
		// Currently it's not implemented as it's complicated, we cannot reliable do IRQ from another thread than the main emulation one :(
		// Probably:	I should have a TLS (thread local storage) variable to tell which thread we're in.
		//		If it's the main thread, just do the IRQ. If the ethernet thread: set an atomic signal, and do an IRQ "routing" periodically from the main thread based on that
		last_irq = final_irq;
	}
	// Atomic write (even though this func needs a lock) is used to store flags which can be _read_ at least from outside of the lock too
	SDL_AtomicSet(&threadsafe_status_regs,
		(
			// Register-0
			(free_buffers == 0 ? 64 : 0) +
			(com.tx_size ? 0 : 128)		// Only bit 6 and 7 of eth reg #0
		) + ((
			// Register-1
			((free_buffers > 3 ? 3 : free_buffers) << 1) +				// bit 1,2: free buffers (max 3)
			(com.tx_irq ? 16 : 0) +
			(rx_irq ? 32 : 0) +
			com.tx_irq_enabled +
			com.rx_irq_enabled
		) << 8)		// store reg-1 in the high byte, so we can have a single atomic varible to deal with

	);
}

#ifdef	HAVE_ETHERTAP

// Used by the ethernet thread only to filter incoming ethernet framed based on "filters"
static bool do_rx_filtering ( const Uint8 *buf, const int size, const Uint8 filters )
{
	if (size < 14 || size >= 0x800 - 2) {
		DEBUGPRINT("ETH: thread: skipping frame; invalid frame size (%s%d)" NL, size >= 0x800 - 2 ? ">=" : "", size);
		return false;
	}
	const int ethertype = (buf[12] << 8) | buf[13];
	// FIXME/TODO: as with the ARP/IPV4 filter: do we want to drop a frame just because it's not ethernet-II?
	// ... or allow it, just we can't apply some filters then?
	if (ethertype < 1536) {
		DEBUGPRINT("ETH: thread: skipping frame; not an Ethernet-II frame ($%04X)" NL, ethertype);
		return false;
	}
	// TODO: this would render IPv6 not working
	// TODO: even if I allow that, I am not sure if MEGA65-core can filter IP bcast/unicast for IPv6 (and not only IPv4)
	if (ethertype != ETH_II_FRAME_ARP && ethertype != ETH_II_FRAME_IPV4) {
		DEBUGPRINT("ETH: thread: skipping frame; not ARP or IPv4 Ethernet-II ethertype ($%04X)" NL, ethertype);
		return false;
	}
	// Do MAC filtering
	if (
		((filters & RX_FILTER_MAC)) &&
		memcmp(buf, mac_bcast,   6) &&
		memcmp(buf, mac_address, 6)
	) {
		DEBUGPRINT("ETH: thread: MAC filter skipping %02X:%02X:%02X:%02X:%02X:%02X" NL,
			buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]
		);
		return false;
	}
	// Do IPv4 filtering
	// finally, we accept the frame :-)
	return true;
}


static int ethernet_thread ( void *unused )
{
	int select_wait = 10;	// MSEC. TODO: make it auto-adaptive
	static Uint8 rx_temp[0x800], tx_temp[0x800];
	int rx_temp_size = 0, tx_temp_size = 0;
	bool rx_overflow_state = false, rx_overflow_warning = false;
	bool first_run = true;		// do the initial reset
	SDL_AtomicSet(&threadsafe_thread_status, THREAD_STATUS_RUNNING);
	for (;;) {
		// Locked part, in practice this is quite quick, and no syscalls made in this part.
		// If we need copying buffer it takes a bit longer (for 2K per RX or TX) but that's
		// rare case and "not too bad" either. The important thing: we can manage without
		// _any_ lock at all in the select() and read()/write() part!
		ETH_LOCK();
		bool reset_done;
		bool status_change = false;
		if (com.thread_to_reset || first_run) {
			com.thread_to_reset = false;
			first_run = false;
			rx_temp_size = 0;
			tx_temp_size = 0;
			rx_overflow_state = false;
			rx_overflow_warning = false;
			com.tx_size = 0;
			com.tx_irq = false;
			status_change = true;
			reset_done = true;
		} else
			reset_done = false;
		if (!tx_temp_size && com.tx_size) {
			// TX signal from the main thread
			if (com.tx_size <= 0x800) {	// should always fit into 0x800 as it is checked by the main thread before, but anyway ...
				// we copy the full buffer (regardless of tx_size) as there can dirty tricks used by programs to rely on previous buffer content?
				memcpy(tx_temp, eth_tx_buf, 0x800);
				tx_temp_size = com.tx_size;
			}
			com.tx_size = 0;
			com.tx_irq = true;
			status_change = true;
		}
		if (com.rx_disabled || com.under_reset)
			rx_temp_size = 0;	// dropping possible already received frame in the local buffer when RX is disabled
		if (rx_temp_size) {
			// we have filled rx_temp, we can copy it into the common ring buffer, if we have free buffer ...
			const int eth_next = PLUS1(com.eth_sel);
			if (eth_next != com.cpu_sel) {
				Uint8 *p = com.rx_buffers + (com.eth_sel * 0x800);
				p[0] = rx_temp_size & 0xFF;
				p[1] = rx_temp_size >> 8;
				memcpy(p + 2, rx_temp, rx_temp_size);
				// move to next buffer
				com.eth_sel = eth_next;
				rx_overflow_state = false;
			} else {
				if (!rx_overflow_state) {
					rx_overflow_state = true;
					rx_overflow_warning = true;
				}
			}
			rx_temp_size = 0;
			status_change = true;
		}
		if (status_change)
			calc_status_changes();
		ETH_UNLOCK();
		if (rx_overflow_warning) {
			rx_overflow_warning = false;
			DEBUGPRINT("ETH: thread: RX buffer overflow, lost frame(s)!" NL);
		}
		if (reset_done)
			DEBUGPRINT("ETH: thread: thread-level reset" NL);
		const int select_flags = (rx_temp_size ? 0 : XEMU_TUNTAP_SELECT_R) | (tx_temp_size ? XEMU_TUNTAP_SELECT_W : 0);
		//DEBUGPRINT("ETH: selflags = %d" NL, select_flags);
		int slept = SDL_GetTicks();
		const int selres = xemu_tuntap_select(select_flags, select_wait * 1000);
		slept = SDL_GetTicks() - slept;
		if (SDL_AtomicGet(&threadsafe_thread_status) != THREAD_STATUS_RUNNING)
			break;
		if (selres == 0) {
			// select() timed out
			//DEBUGPRINT("ETH: thread: timed out" NL);
			continue;
		}
		if (selres < 0) {
			DEBUGPRINT("ETH: thread: select error, aborting: %s" NL, xemu_tuntap_error());
			break;
		}
		//DEBUGPRINT("ETH: selres = %d" NL, selres);
		bool activity = false;
		if ((selres & XEMU_TUNTAP_SELECT_R) && !rx_temp_size) {
			const int r = xemu_tuntap_read(rx_temp, 0x800 - 2);
			if (r == -1) {
				DEBUGPRINT("ETH: thread: read error, aborting: %s" NL, xemu_tuntap_error());
				break;
			}
			if (r == 0) {
				DEBUGPRINT("ETH: BUGGY!" NL);
			}
			if (r > 0) {
				DEBUGPRINT("ETH: thread: activity, read %d bytes" NL, r);
				activity = true;
				if (do_rx_filtering(rx_temp, r, force_filters ? 0xFF : SDL_AtomicGet(&threadsafe_rx_filtering))) {
					rx_temp_size = r;	// accept it, if filtering says to do so
					SDL_AtomicAdd(&stat_rx_counter, 1);
				}
			}
		}
		if ((selres & XEMU_TUNTAP_SELECT_W) && tx_temp_size) {
			const int r = xemu_tuntap_write(tx_temp, tx_temp_size);
			if (r == -1) {
				DEBUGPRINT("ETH: thread: write error (wanting to write %d bytes), aboring: %s" NL, tx_temp_size, xemu_tuntap_error());
				break;
			}
			if (r == tx_temp_size) {
				tx_temp_size = 0;	// TX was OK, let's free our local buffer
				SDL_AtomicAdd(&stat_tx_counter, 1);
			} else {
				DEBUGPRINT("ETH: thread: partial / blocked write?!" NL);
				// FIXME: WTF? can it happen at all?!
				tx_temp_size = 0;	// pretend to be OK.
			}
			activity = true;
		}
		if (!activity) {
			DEBUGPRINT("ETH: should sleep: %d, actual sleep %d" NL, select_wait, slept);
			//SDL_Delay(select_wait);
		} else {
			//DEBUGPRINT("Not sleeping!" NL);
		}
	}
	DEBUGPRINT("ETH: thread: exiting ..." NL);
	SDL_AtomicSet(&stat_rx_counter, 0);
	SDL_AtomicSet(&stat_tx_counter, 0);
	SDL_AtomicSet(&threadsafe_thread_status, THREAD_STATUS_EXITED);
	return 0;
}

#endif	// HAVE_ETHERTAP

// Can be called ONLY by the main ("CPU") thread
static inline void trigger_rx_buffer_swap ( void )
{
	bool switched = false;
	ETH_LOCK();
	const int cpu_next = PLUS1(com.cpu_sel);
	if (cpu_next != com.eth_sel) {
		memcpy(eth_rx_buf, com.rx_buffers + (cpu_next * 0x800), 0x800);
		com.cpu_sel = cpu_next;
		calc_status_changes();
		switched = true;
	}
	ETH_UNLOCK();
	if (!switched) {
		// Messages etc are not nice to be produced within the lock section as that must be minimalized in execution time!
		DEBUGPRINT("ETH: warning, CPU wants to move over the current ethernet RX buffer! PC=$%04X" NL, cpu65.old_pc);
	} else {
		const int size = eth_rx_buf[0] + (eth_rx_buf[1] << 8);
		if (size > 0x800 - 2)
			DEBUGPRINT("ETH: warning, invalid size (%d) in the RX buffer!" NL, size);
		else
			DEBUGPRINT("ETH: cool, we got a new buffer (#%d) in the CPU view, %d+2 bytes of ethernet frame." NL, cpu_next, size);
	}
}


// Can be called ONLY by the main ("CPU") thread
static void trigger_tx_transmit ( int size )
{
	if ((size >= 0) && (size < 8 || size > 0x800)) {
		DEBUGPRINT("ETH: invalid frame size (%d) to TX, refusing" NL, size);
		size = -1;
	}
	ETH_LOCK();
	if (size < 0) {
		com.tx_size = 0;
		com.tx_irq = true;
	} else {
		com.tx_size = size;
		com.tx_irq = false;
	}
	calc_status_changes();
	ETH_UNLOCK();
}


static void do_reset_begin ( void )
{
	bool error = false;
	ETH_LOCK();
	if (!com.under_reset) {
		// FIXME: do I realy need to clear all registers on reset, ven MIIM ones?
		memset(&eth_regs, 0, 9);	// only clear the first 9 registers
		eth_regs[5] = 53;		// Filtering options FIXME: check this!
		memset(&miimlo8, 0, sizeof miimlo8);
		memset(&miimhi8, 0, sizeof miimhi8);
		SDL_AtomicSet(&threadsafe_rx_filtering, eth_regs[5]);
		com.cpu_sel = 0;
		com.eth_sel = 1;
		memcpy(eth_rx_buf, com.rx_buffers, 0x800);
		com.thread_to_reset = true;
		com.under_reset = true;
		com.tx_irq_enabled = 0;
		com.rx_irq_enabled = 0;
		calc_status_changes();
#ifdef		HAVE_ETHERTAP
		SDL_AtomicSet(&stat_rx_counter, 0);
		SDL_AtomicSet(&stat_tx_counter, 0);
#endif
	} else
		error = true;
	ETH_UNLOCK();
	if (error)
		DEBUGPRINT("ETH: warning: reset-begin ignored with prior reset-begin" NL);
}


static void do_reset_end ( void )
{
	bool error = false;
	ETH_LOCK();
	if (com.under_reset) {
		com.under_reset = false;
		calc_status_changes();
	} else
		error = true;
	ETH_UNLOCK();
	if (error)
		DEBUGPRINT("ETH: warning: reset-end ignored without prior reset-begin" NL);
}


Uint8 eth65_read_reg ( const unsigned int addr )
{
	DEBUG("ETH: reading register $%02X" NL, addr);
	switch (addr) {
		case 0x00:
			return (SDL_AtomicGet(&threadsafe_status_regs) & (128 + 64)) + (eth_regs[0] & 63);
		case 0x01:
			return SDL_AtomicGet(&threadsafe_status_regs) >> 8;
		case 0x04:
			return RX_BUFFERS;	// $D6E4 register: on _reading_ it seems to gives back the total number of RX buffers what system has
		case 0x07:
			return miimlo8[miim_reg_num];
		case 0x08:
			return miimhi8[miim_reg_num];
		case 0x0F:			// FIXME: what is this?
			return 0xFF;
		default:
			if (XEMU_UNLIKELY(addr > 0x1F))
				FATAL("Invalid ethernet register (%d) to be read!", addr);
			break;
	}
	return eth_regs[addr];
}


void eth65_write_reg ( const unsigned int addr, const Uint8 data )
{
	DEBUG("ETH: writing register $%02X with data $%02X" NL, addr, data);
	switch (addr) {
		case 0x00:
			if ((eth_regs[0] & 3) == 3 && (data & 3) != 3) {
				DEBUGPRINT("ETH: reset-begin established, PC=$%04X" NL, cpu65.old_pc);
				do_reset_begin();
			}
			if ((eth_regs[0] & 3) != 3 && (data & 3) == 3) {
				DEBUGPRINT("ETH: reset-end established, PC=$%04X" NL, cpu65.old_pc);
				do_reset_end();
			}
			break;
		case 0x01:
			if (!(eth_regs[1] & 2) && (data & 2)) {	// "rising edge" of this bit written triggers the RX buffer swap
				trigger_rx_buffer_swap();
			}
			if ((eth_regs[1] ^ data) & (0x80 + 0x40)) {
				ETH_LOCK();
				com.tx_irq_enabled = data & 0x40;
				com.rx_irq_enabled = data & 0x80;
				calc_status_changes();
				ETH_UNLOCK();
			}
			break;
		case 0x04:
			// The only real and important ethernet controller command is 1 (transmit TX buffer),
			// though command 0 sometimes mentioned to "cancel possible submitted TX" (or so)
			if (data < 2)
				trigger_tx_transmit(data ? eth_regs[2] + ((eth_regs[3] & 0xF) << 8) : -1);
			else
				DEBUGPRINT("ETH: invalid/unknown ethernet controller command: $%02X" NL, data);
			break;
		case 0x05:
			if ((eth_regs[5] ^ data) & (RX_FILTER_MAC|RX_FILTER_ACCEPT_BCAST|RX_FILTER_ACCEPT_MCAST)) {
				SDL_AtomicSet(&threadsafe_rx_filtering, data);
				DEBUGPRINT("ETH: setting RX filters; mac-filter=%d, bcast-accept=%d, mcast-accept=%d" NL,
					!!(data & RX_FILTER_MAC),
					!!(data & RX_FILTER_ACCEPT_BCAST),
					!!(data & RX_FILTER_ACCEPT_MCAST)
				);
			}
			break;
		case 0x07:
			miimlo8[miim_reg_num] = data;
			break;
		case 0x08:
			miimhi8[miim_reg_num] = data;
			break;
		default:
			if (XEMU_UNLIKELY(addr > 0x1F))
				FATAL("Invalid ethernet register (%d) to be written!", addr);
			break;
	}
	eth_regs[addr] = data;
}


void eth65_shutdown ( void )
{
	const int status = SDL_AtomicGet(&threadsafe_thread_status);
	if (status == THREAD_STATUS_EXITED)
		DEBUGPRINT("ETH: shutting down: handler thread has already exited" NL);
	else if (status == THREAD_STATUS_RUNNING) {
		SDL_AtomicSet(&threadsafe_thread_status, THREAD_STATUS_EXIT);
		for (const Uint32 t = SDL_GetTicks();;) {
			const int age = SDL_GetTicks() - t;
			const bool timed_out = (age >= SHUTDOWN_TIMEOUT_MSEC);
			if (timed_out || SDL_AtomicGet(&threadsafe_thread_status) == THREAD_STATUS_EXITED) {
				DEBUGPRINT("ETH: shutting down thread: %s, after %u msec" NL, timed_out ? "TIMED OUT" : "done", age);
				break;
			}
			SDL_Delay(5);
		}
	} else
		DEBUGPRINT("ETH: shutting down: handler thread seems hasn't been even running (not enabled?)" NL);
#ifdef	HAVE_ETHERTAP
	xemu_tuntap_close();
#endif
}


void eth65_reset ( void )
{
	ETH_LOCK();
	com.under_reset = false;	// to be sure, we can start reset
	ETH_UNLOCK();
	do_reset_begin();
	do_reset_end();
	eth_regs[0] |= 3;		// not in reset anymore: eth65_reset() is called by the emulator not via CPU I/O writes!
}


#ifdef	HAVE_ETHERTAP
unsigned int eth65_get_stat ( char *buf, const unsigned int buf_size, unsigned int *rxcnt, unsigned int *txcnt )
{
	if (rxcnt)
		*rxcnt = SDL_AtomicGet(&stat_rx_counter);
	if (txcnt)
		*txcnt = SDL_AtomicGet(&stat_tx_counter);
	if (SDL_AtomicGet(&threadsafe_thread_status) !=  THREAD_STATUS_RUNNING)
		return snprintf(buf, buf_size, "Not running/configured");
	return snprintf(buf, buf_size, "device attached: \"%s\", initial MAC: %02X:%02X:%02X:%02X:%02X:%02X, remote MAC: %02X:%02X:%02X:%02X:%02X:%02X, remote IP: %u.%u.%u.%u",
		tap_name,
		mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5],
		remote_mac[0], remote_mac[1], remote_mac[2], remote_mac[3], remote_mac[4], remote_mac[5],
		remote_ip >> 24, (remote_ip >> 16) & 0xFF, (remote_ip >> 8) & 0xFF, remote_ip & 0xFF
	);
}
#endif


int eth65_init ( const char *options )
{
#ifdef	HAVE_ETHERTAP
	static const char init_error_prefix[] = "ETH: disabled on error:\n";
#endif
	SDL_AtomicSet(&threadsafe_thread_status, THREAD_STATUS_DISABLED);
	eth65_reset();
	static bool init_mac = true;
	if (init_mac) {
		init_mac = false;
		memcpy(eth_regs + 9, default_mac, sizeof(default_mac));
	}
	if (options && *options) {
#ifdef	HAVE_ETHERTAP
		char device_name[64];
		*device_name = 0;
		for (;;) {
			char *r = strchr(options, ',');
			size_t len = r ? r - options : strlen(options);
			if (!len || len >= sizeof device_name) {
				ERROR_WINDOW("%sinvalid CLI switch/config: empty or too long parameter", init_error_prefix);
				return 1;
			}
			if (!*device_name) {
				strncpy(device_name, options, len);
				device_name[len] = 0;
#ifndef			ETH65_NO_DEBUG
			} else if (len ==  5 && !strncmp(options, "debug", len)) {
				eth_debug = true;
#endif
			} else if (len == 13 && !strncmp(options, "alwaysfilters", len)) {
				force_filters = true;
			} else if (!strncmp(options, "mac=", 4)) {
				if (len != 16 || sscanf(
					options + 4,
					"%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
					mac_address + 0, mac_address + 1, mac_address + 2,
					mac_address + 3, mac_address + 4, mac_address + 5
				) != 6) {
					ERROR_WINDOW("%sbad mac= option, invalid MAC", init_error_prefix);
					return 1;
				}
			} else {
				strncpy(device_name, options, len);	// use device_name as a temp storage for now, we won't need it anymore, as final error.
				device_name[len] = 0;
				ERROR_WINDOW("%sunknown/bad sub-option given in CLI/config: %s", init_error_prefix, device_name);
				return 1;
			}
			if (r)
				options = r + 1;
			else
				break;
		}
		if (xemu_tuntap_alloc(device_name, NULL, 0, XEMU_TUNTAP_IS_TAP | XEMU_TUNTAP_NO_PI | XEMU_TUNTAP_NONBLOCKING_IO) < 0) {
			ERROR_WINDOW("%sTAP device \"%s\" opening error: %s", init_error_prefix, device_name, xemu_tuntap_error());
			return 1;
		}
		tap_name = xemu_strdup(device_name);
		remote_ip = xemu_tuntap_get_ipv4();
		xemu_tuntap_get_mac(remote_mac);
		// Initialize our thread for device read/write ...
		SDL_AtomicSet(&threadsafe_thread_status, THREAD_STATUS_RUNNING);
		const SDL_Thread *thread_id = SDL_CreateThread(ethernet_thread, "Xemu-EtherTAP", NULL);
		if (thread_id) {
			char stat[128];
			(void)eth65_get_stat(stat, sizeof stat, NULL, NULL);
			DEBUGPRINT("ETH: enabled - thread %p started, %s" NL, thread_id, stat);
		} else {
			SDL_AtomicSet(&threadsafe_thread_status, THREAD_STATUS_DISABLED);
			ERROR_WINDOW("%serror creating thread for Ethernet emulation:\n%s", init_error_prefix, SDL_GetError());
			xemu_tuntap_close();
			return 1;
		}
		return 0;
#else
		ERROR_WINDOW("Ethernet emulation is not supported/was compiled into this Xemu");
		return 1;
#endif
	} else {
#ifdef	HAVE_ETHERTAP
		DEBUGPRINT("ETH: not enabled by config/command line" NL);
#else
		DEBUG("ETH: Ethernet emulation is not supported/was compiled into this Xemu");
#endif
		return 0;
	}
}
