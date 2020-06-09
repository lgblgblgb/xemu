/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


/* It seems, at least on Nexys4DDR board, the used ethernet controller chip is: LAN8720A

   http://ww1.microchip.com/downloads/en/DeviceDoc/8720a.pdf

   This information is not so useful, as M65 programs in general does not "meet" it, however
   in case of MII registers, it can be an interesting topic, maybe ... */


/* Possible future work:
	* FIXME: there is no IRQ generated currently for the CPU! So only polling method is used.
	  the problem is not as trivial to solve as it seems, since a *thread* can affect IRQs
	  also updated by the main CPU emulator thread ... With using locking for all of these,
          would result in quite slow emulation :(
	* FIXME: it uses thread, and it's horrible code!! there are tons of race conditions, etc ...
	It's not so nice work to use threads, but it would be hard to do otherwise, sadly.
	* FIXME FIXME FIXME: No support for snapshotting ...
	* Maybe allow TUN devices to be supported. It does not handle ethernet level stuffs, no source and
	target MAC for example :-O But TUN devices are supported by wider range of OSes (eg: OSX) and we
	probably can emulate the missing ethernet level stuffs somehow in this source on RX (and chop
	them off, in case of TX ...).
	* Implement a many DHCP client here, so user does not need to install a DHCP server on the PC, just
	for testing M65 software with Xemu wants to use a DHCP. It would catch / answer for DHCP-specifc
	requests, instead of using really the TAP device to send, and waiting for answer.
*/


// ETH_FRAME_MAX_SIZE: now I'm a bit unsure about this with/without CRC, etc issues. However hopefully it's OK.
#define ETH_FRAME_MAX_SIZE	1536
// ETH_FRAME_MIN_SIZE: actually 64 "on the wire", but it included the CRC (4 octets) which we don't handle here at all
// this is only used to pad the frame on TX if shorter. No idea, that it's really needed for the EtherTAP device as well
// (not a real ethernet device), but still, hopefully it wouldn't hurt (?)
#define ETH_FRAME_MIN_SIZE	60
// SELECT_WAIT_USEC_MAX: max time to wait for RX/TX before trying it. This is because we don't want to burn the CPU time
// in the TAP handler thread but also we want to max the wait, if an ETH op quicks in after a certain time of inactivity
#define SELECT_WAIT_USEC_MAX	10000
// just by guessing (ehmm), on 100mbit (what M65 has) network, about 200'000 frame/sec is far the max
// that is, let's say about 5usec minimal wait
#define SELECT_WAIT_USEC_MIN	5
#define SELECT_WAIT_INC_VAL	5


#define ETH_II_FRAME_ARP	0x0806
#define ETH_II_FRAME_IPV4	0x0800

static int eth_debug;

static volatile struct {
	int	enabled;		// the whole M65 eth emulation status
	int	no_reset;		//
	int	exited;
	int	rx_enabled;		// can the TAP handler thread receive a new packet?
	int	_rx_irq;		// status of RX IRQ, note that it is also used for polling without enabled IRQs! (must be 0x20 or zero)
	int	_tx_irq;		// status of TX IRQ, note that it is also used for polling without enabled IRQs! (must be 0x10 or zero)
	int	_irq_to_cpu_routed;
	int	tx_size;		// frame size to transmit
	int	tx_trigger;		// trigger to TX
	int	rx_irq_enabled;		// RX IRQ enabled, so rx_irq actually generates an IRQ
	int	tx_irq_enabled;		// TX IRQ enabled, so tx_irq actually generates an IRQ
	int	rx_buffer_using;	// RX buffer used by the receiver thread (must be 0 or 0x800, it also means offset in rx_buffer!)
	int	rx_buffer_mapped;	// RX buffer which is mapped by the user (must be 0 or 0x800, it also means offset in rx_buffer!)
	int	sense_activity;		// handler thread senses activity, helps keeping select_wait_usec low when there is no activity to avoid burning the CPU power without reason
	int	select_wait_usec;	// uSeconds to wait at max by select(), the reason for this is the same which is expressed in the previous line
	int	video_streaming;
	int	accept_broadcast;
	int	accept_multicast;
	int	mac_filter;
	int	xemu_always_filters;
	int	disable_crc_chk;
	int	adjust_txd_phase;
	int	miim_register;
	int	phy_number;
	Uint8	rx_buffer[0x1000];	// actually, two 2048 bytes long buffers in one array
	Uint8	tx_buffer[0x0800];	// a 2048 bytes long buffer to TX
} eth65;


static Uint8 mac_address[6]	= {0x02,0x47,0x53,0x65,0x65,0x65};	// 00:80:10:... would be nicer, though a bit of cheating, it belongs to Commodore International(TM).
#ifdef HAVE_ETHERTAP
static const Uint8 mac_bcast[6]	= {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};	// also used as IPv4 bcast detection with filters, if only the first 4 bytes are checked
#endif

static Uint16 miimlo8[0x20], miimhi8[0x20];

#ifndef ETH65_NO_DEBUG
#define ETHDEBUG(...)	do { \
				if (XEMU_UNLIKELY(eth_debug)) { \
					DEBUGPRINT(__VA_ARGS__); \
				} \
			} while (0)
#else
#define ETHDEBUG(...)
#endif

// IRQs are not yet supported, especially because it's an async event from a thread, and the main thread should aware it
// at every opcode emulation, which would made that slow (ie thread-safe update of the CPU IRQ request ...)
static void __eth65_manage_irq_for_cpu ( void )
{
	int status = ((eth65._rx_irq && eth65.rx_irq_enabled) || (eth65._tx_irq && eth65.tx_irq_enabled));
	if (status != eth65._irq_to_cpu_routed) {
		ETHDEBUG("ETH: IRQ change should be propogated: %d -> %d" NL, eth65._irq_to_cpu_routed, status);
		eth65._irq_to_cpu_routed = status;
	}
}

#define RX_IRQ_ON()	do { eth65._rx_irq = 0x20; __eth65_manage_irq_for_cpu(); } while (0)
#define RX_IRQ_OFF()	do { eth65._rx_irq = 0;    __eth65_manage_irq_for_cpu(); } while (0)
#define TX_IRQ_ON()	do { eth65._tx_irq = 0x10; __eth65_manage_irq_for_cpu(); } while (0)
#define TX_IRQ_OFF()	do { eth65._tx_irq = 0;    __eth65_manage_irq_for_cpu(); } while (0)




/*	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	!!!!! WARNING: these things run as *THREAD* !!!!!
	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	It must be very careful not use anything too much.
	xemu_tuntap_read and write functions are used only here, so it's maybe OK
	but exchange data with the main thread (the emulator) must be done very carefully!
	FIXME: "surely", there are many race conditions still :(
		however, proper locking etc would be just too painful at this stage of the emulator
		also, using "ETHDEBUG"s (printfs after all, etc) basically not the best idea in a thread, I guess	*/

#ifdef HAVE_ETHERTAP

#include <errno.h>

static const char init_error_prefix[] = "ETH: disabled: ";

static SDL_Thread *thread_id = NULL;

static XEMU_INLINE int ethernet_rx_processor ( void )
{
        Uint8 rx_temp_buffer[0x800];
	int ethertype, ret;
	ret = xemu_tuntap_read(rx_temp_buffer, 6 + 6 + 2, sizeof rx_temp_buffer);
	if (ret == -2) {
		eth65.select_wait_usec = 1000000;	// serious problem, do 1sec wait ...
		ETHDEBUG("ETH-THREAD: read with EAGAIN, continue ..." NL);
		return 0;
	}
	if (ret < 0) {
		ETHDEBUG("ETH-THREAD: read error: %s" NL, strerror(errno));
		return 1;	// vote for exit thread ...
	}
	if (ret == 0) {
		eth65.select_wait_usec = 1000000;	// serious problem, do 1sec wait ...
		ETHDEBUG("ETH-THREAD: read() returned with zero! continue ..." NL);
		return 0;
	}
	if (ret > 2046) {	// should not happen, but who knows ... (2046=size of buffer - 2, 2 for the length parameter to be passed)
		// FIXME: maybe we should check the max frame size instead, but I guess TAP device does not allow crazy sizes anyway
		eth65.select_wait_usec = 1000000;	// serious problem, do 1sec wait ...
		ETHDEBUG("ETH-THREAD: read() had %d bytes, too long! continue ..." NL, ret);
		return 0;
	}
	// something is recevied at least.
	// Still we can filter things ("m65 magic filters", etc)
	ETHDEBUG("ETH-THREAD: read() returned with %d bytes read, OK!" NL, ret);
	eth65.sense_activity = 1;
	if (ret < 14 || ret > ETH_FRAME_MAX_SIZE) {
		ETHDEBUG("ETH-THREAD: ... however, insane sized frame, skipping" NL);
		return 0;
	}
	// XEMU stuff, just to test TAP
	// it only recoginizes Ethernet-II frames, for frame types IPv4 or ARP
	ethertype = (rx_temp_buffer[12] << 8) | rx_temp_buffer[13];
	if (ethertype < 1536) {
		ETHDEBUG("ETH-THREAD: ... however, skipped by XEMU: not an Ethernet-II frame ($%04X)" NL, ethertype);
		return 0;
	}
	if (ethertype != ETH_II_FRAME_ARP && ethertype != ETH_II_FRAME_IPV4) {
		ETHDEBUG("ETH-THREAD: ... however, skipped by XEMU: not ARP or IPv4 Ethernet-II ethertype ($%04X)" NL, ethertype);
		return 0;
	}
	// MAC filter, when ON, only packets targeting our MAC, or bcast MAC address is received
	if (
		(eth65.mac_filter || eth65.xemu_always_filters) &&
		memcmp(rx_temp_buffer, mac_bcast,   6) &&
		memcmp(rx_temp_buffer, mac_address, 6)
	) {
		ETHDEBUG("ETH-THREAD: ... however, MAC filter ruled it out %02X:%02X:%02X:%02X:%02X:%02X" NL,
			rx_temp_buffer[0], rx_temp_buffer[1], rx_temp_buffer[2], rx_temp_buffer[3], rx_temp_buffer[4],rx_temp_buffer[5]
		);
		return 0;
	}
	ETHDEBUG("ETH-THREAD: ... cool, target MAC seems to survive MAC filter %02X:%02X:%02X:%02X:%02X:%02X" NL,
		rx_temp_buffer[0], rx_temp_buffer[1], rx_temp_buffer[2], rx_temp_buffer[3], rx_temp_buffer[4],rx_temp_buffer[5]
	);
	// check, if frame contains an IPv4 packet, so we can check for IPv4-specific filters as well
	if (
		ret >= 20 + 8 + 14 &&		// we need at least ??? bytes for valid IPv4 packet
		ethertype == ETH_II_FRAME_IPV4 &&
		(rx_temp_buffer[15] & 0xF0) == 0x40	// IPv4? [4=version field of IP packet]
	) {
		if ((!eth65.accept_broadcast || eth65.xemu_always_filters) && !memcmp(rx_temp_buffer + 30, mac_bcast, 4)) {
			ETHDEBUG("ETH-THREAD: ... however, IP filter ruled it out: broadcast (%d.%d.%d.%d)" NL, rx_temp_buffer[30], rx_temp_buffer[31], rx_temp_buffer[32], rx_temp_buffer[33]);
			return 0;
		}
		// check if multicast (224.0.0.0/4)
		if ((!eth65.accept_multicast || eth65.xemu_always_filters) && (rx_temp_buffer[30] & 0xF0) == 0xE0) {
			ETHDEBUG("ETH-THREAD: ... however, IP filter ruled it out: multicast (%d.%d.%d.%d)" NL, rx_temp_buffer[30], rx_temp_buffer[31], rx_temp_buffer[32], rx_temp_buffer[33]);
			return 0;
		}
		ETHDEBUG("ETH-THREAD: ... cool, target IP seems to survive IP filter %d.%d.%d.%d" NL, rx_temp_buffer[30], rx_temp_buffer[31], rx_temp_buffer[32], rx_temp_buffer[33]);
	}
	ETHDEBUG("ETH-THREAD: ... MEGA[65]-COOL: we are ready to propogate packet" NL);
	// M65 stores the received frame size in "6502 byte order" as the first two bytes in the RX
	// (this makes things somewhat non-symmetric, as for TX, the size are in a pair of registers)
	eth65.rx_buffer[eth65.rx_buffer_using] = ret & 0xFF;
	eth65.rx_buffer[eth65.rx_buffer_using + 1] = ret >> 8;
	for (int i = 0; i < ret; i++)
		eth65.rx_buffer[eth65.rx_buffer_using + 2 + i] = rx_temp_buffer[i];
	//memcpy(eth65.rx_buffer + eth65.rx_buffer_using + 2, rx_temp_buffer, r);
	eth65.rx_enabled = 0;		// disable RX till user not ACK'ed by swapping RX buffers
	RX_IRQ_ON();			// signal, that we have something! we don't support IRQs yet, but it's also used for polling!
	return 0;
}


static XEMU_INLINE int ethernet_tx_processor ( void )
{
	int ret, size;
	if (eth65.tx_size < 14 || eth65.tx_size > ETH_FRAME_MAX_SIZE) {
		ETHDEBUG("ETH-THREAD: skipping TX, because invalid frame size: %d" NL, eth65.tx_size);
		// still fake an OK answer FIXME ?
		TX_IRQ_ON();
		eth65.tx_trigger = 0;
		return 0;
	}
#if 0
	// maybe this is not needed, and TAP device can handle autmatically, but anyway
	if (eth65.tx_size < ETH_FRAME_MIN_SIZE) {
		memset((void*)eth65.tx_buffer + eth65.tx_size, 0, ETH_FRAME_MIN_SIZE - eth65.tx_size);
		size = ETH_FRAME_MIN_SIZE;
	} else
#endif
		size = eth65.tx_size;
	ret = xemu_tuntap_write((void*)eth65.tx_buffer, size);
	if (ret == -2) {	// FIXME: with read OK, but for write????
		eth65.select_wait_usec = 1000000;	// serious problem, do 1sec wait ...
		ETHDEBUG("ETH-THREAD: write with EAGAIN, continue ..." NL);
		return 0;
	}
	if (ret < 0) {
		ETHDEBUG("ETH-THREAD: write error: %s" NL, strerror(errno));
		return 1;
	}
	if (ret == 0) {
		eth65.select_wait_usec = 1000000;	// serious problem, do 1sec wait ...
		ETHDEBUG("ETH-THREAD: write() returned with zero! continue ..." NL);
		return 0;
	}
	if (ret != size) {
		ETHDEBUG("ETH-THREAD: partial write only?! wanted = %d, written = %d" NL, size, ret);
		return 1;
	}
	ETHDEBUG("ETH-THREAD: write() returned with %d bytes read, OK!" NL, ret);
	eth65.sense_activity = 1;
	eth65.tx_trigger = 0;
	TX_IRQ_ON();
	return 0;
}



static int ethernet_thread ( void *unused )
{
	ETHDEBUG("ETH-THREAD: hello from the thread." NL);
	while (eth65.enabled) {
		int ret;
		/* ------------------- check for RX condition ------------------- */
		if (
			(!eth65.rx_enabled) && eth65.no_reset &&
			(eth65.rx_buffer_mapped == eth65.rx_buffer_using)
		) {
			eth65.rx_enabled = 1;
			eth65.rx_buffer_using = eth65.rx_buffer_mapped ^ 0x800;
			eth65.sense_activity = 1;
			ETHDEBUG("ETH-THREAD: rx enabled flipped to OK!" NL);
		}
		/* ------------------- Throttling controll of thread's CPU usage ------------------- */
		if (eth65.sense_activity) {
			eth65.select_wait_usec = SELECT_WAIT_USEC_MIN;  // there was some real activity, lower the select wait time in the hope, there is some on-going future stuff soon
			eth65.sense_activity = 0;
		} else {
			if (eth65.select_wait_usec < SELECT_WAIT_USEC_MAX)
				eth65.select_wait_usec += SELECT_WAIT_INC_VAL;
		}
		/* ------------------- The select() stuff ------------------- */
		ret = xemu_tuntap_select(((eth65.rx_enabled && eth65.no_reset) ? XEMU_TUNTAP_SELECT_R : 0) | ((eth65.tx_trigger && eth65.no_reset) ? XEMU_TUNTAP_SELECT_W : 0), eth65.select_wait_usec);
		//ETHDEBUG("ETH-THREAD: after select with %d usecs, retval = %d, rx_enabled = %d, tx_trigger = %d" NL,
		//	eth65.select_wait_usec, ret, eth65.rx_enabled, eth65.tx_trigger
		//);
		if (ret < 0) {
			ETHDEBUG("ETH-THREAD: EMERGENCY STOP: select error: %s" NL, strerror(errno));
			break;
		}
		/* ------------------- TO RECEIVE (RX) ------------------- */
		if (eth65.rx_enabled && eth65.no_reset && (ret & XEMU_TUNTAP_SELECT_R)) {
			if (ethernet_rx_processor()) {
				ETHDEBUG("ETH-THREAD: EMERGENCY STOP: requested by RX processor." NL);
				break;
			}

		}
		/* ------------------- TO TRANSMIT (TX) ------------------- */
		if (eth65.tx_trigger && eth65.no_reset && (ret & XEMU_TUNTAP_SELECT_W)) {
			if (ethernet_tx_processor()) {
				ETHDEBUG("ETH-THREAD: EMERGENCY STOP: requested by TX processor." NL);
				break;
			}
		}
	}
	eth65.exited = 1;
	ETHDEBUG("ETH-THREAD: leaving ..." NL);
	xemu_tuntap_close();
	ETHDEBUG("ETH-THREAD: OK now ..." NL);
	return 0;
}


/* End of threaded stuffs */

#endif

/* Start of non-threaded stuffs, NOTE: they work even without ETH emulation for real, but surely it won't RX/TX too much,
   however we want to keep them, so a network-aware software won't go crazy on totally missing register-level emulation */


Uint8 eth65_read_reg ( int addr )
{
	DEBUG("ETH: reading register $%02X" NL, addr & 0xF);
	switch (addr & 0xF) {
		/* **** $D6E0 register **** */
		case 0x00:
			return eth65.no_reset;	// FIXME: not other bits emulated yet here
		/* **** $D6E1 register **** */
		case 0x01:
			return
				// Bit0: reset - should be hold '1' to use, '0' means resetting the ethernet controller chip
				eth65.no_reset |
				// Bit1: which RX buffer is mapped, we set the offset according to that
				(eth65.rx_buffer_mapped ? 0x02 : 0 ) |
				// Bit2: indicates which RX buffer was used ...
				(eth65.rx_buffer_using  ? 0x04 : 0 ) |
				// Bit3: Enable real-time video streaming via ethernet
				eth65.video_streaming |
				// Bit4: ethernet TX IRQ status, it's also used with POLLING, without IRQ enabled!!
				eth65._tx_irq |
				// Bit5: ethernet RX IRQ status, it's also used with POLLING, without IRQ enabled!!
				eth65._rx_irq |
				// Bit6: Enable ethernet TX IRQ
				eth65.tx_irq_enabled |
				// Bit7: Enable ethernet RX IRQ
				eth65.rx_irq_enabled
			;
		/* **** $D6E2 register: TX size register low **** */
		case 0x02: return eth65.tx_size & 0xFF;
		/* **** $D6E3 register: TX size register high (4 bits only) **** */
		case 0x03: return (eth65.tx_size >> 8) & 0xFF;	// 4 bits, but tx_size var cannot contain more anyway
		/* **** $D6E5 register **** */
		case 0x05:
			return
				eth65.mac_filter   |
				eth65.disable_crc_chk |
				( eth65.adjust_txd_phase << 2) |
				eth65.accept_broadcast |
				eth65.accept_multicast
			;
			break;
		/* **** $D6E6 register **** */
		case 0x06:
			return
				eth65.miim_register |
				(eth65.phy_number << 5)
			;
		/* **** $D6E7 register **** */
		case 0x07: return miimlo8[eth65.miim_register];
		/* **** $D6E8 register **** */
		case 0x08: return miimhi8[eth65.miim_register];
		/* **** $D6E9 - $D6EE registers: MAC address **** */
		case 0x09: return mac_address[0];
		case 0x0A: return mac_address[1];
		case 0x0B: return mac_address[2];
		case 0x0C: return mac_address[3];
		case 0x0D: return mac_address[4];
		case 0x0E: return mac_address[5];
		default:
			return 0xFF;
	}
}




void eth65_write_reg ( int addr, Uint8 data )
{
	DEBUG("ETH: writing register $%02X with data $%02X" NL, addr & 0xF, data);
	switch (addr & 0xF) {
		/* **** $D6E0 register **** */
		case 0x00:
			eth65.no_reset = (data & 0x01);	// FIXME: it seems to be the same as $D6E1 bit0???
			break;
		/* **** $D6E1 register **** */
		case 0x01:
			// Bit0: It seems, it's an output signal of the FPGA for the ethernet controller chip, and does not do too much other ...
			// ... however it also means, that if this bit is zero, the ethernet controller chip does not do too much, I guess.
			// that is, for usage, you want to keep this bit '1'.
			eth65.no_reset = (data & 0x01);
			// Bit1: which RX buffer is mapped, we set the offset according to that
			// it seems, RX only works, if ctrl not in reset (bit0) and the ctrl actually waits for user to swap buffer (it can only receive into the not mapped one!)
			eth65.rx_buffer_mapped = (data & 0x02) ? 0x800 : 0;
			// Bit2: indicates which RX buffer was used ... maybe it's a read-only bit, we don't care
			// Bit3: Enable real-time video streaming via ethernet (well, it's not supported by Xemu, but it would be fancy stuff btw ...) -- we don't care on this one
			eth65.video_streaming = (data & 0x08);
			// Bit4: ethernet TX IRQ status, this will be cleared on writing $D6E1, regardless of value written!
			// Bit5: ethernet RX IRQ status, this will be cleared on writing $D6E1, regardless of value written!
			// Bit6: Enable ethernet TX IRQ, not yet supported in Xemu! FIXME
			eth65.tx_irq_enabled = (data & 0x40);	// not yet used, FIXME
			// Bit7: Enable ethernet RX IRQ, not yet supported in Xemu! FIXME
			eth65.rx_irq_enabled = (data & 0x80);	// not yet used, FIXME
			// see comments above with bits 4 and 5
			TX_IRQ_OFF();
			RX_IRQ_OFF();
			ETHDEBUG("ETH: $D6E1 has been written with data $%02X" NL, data);
			break;
		/* **** $D6E2 register: TX size low **** */
		case 0x02:
			eth65.tx_size = (eth65.tx_size & 0xFF00) | data;
			break;
		/* **** $D6E3 register: TX size high (4 bits only) **** */
		case 0x03:
			eth65.tx_size = ((data & 0x0F) << 8) | (eth65.tx_size & 0xFF);
			break;
		/* **** $D6E4 register **** */
		case 0x04:
			// trigger TX if $01 is written?
			// clear (pending?) TX is $00 [marked as 'DEBUG' in iomap.txt']: FIXME? not implemented
			if (data == 0) {	// but anyway, here you are the debug stuff as well ...
				eth65.tx_trigger = 0;
			} else if (data == 1) {	// the only "official" command here: TX trigger!
				if (eth65.tx_size >= 14 || eth65.tx_size <= ETH_FRAME_MAX_SIZE) {
					eth65.sense_activity = 1;
					// FIXME: I guess, TX IRQ must be cleared by the user ... so we don't do here
					eth65.tx_trigger = 1;		// now ask the triggy stuff!
				} else {
					// leave this for the handler thread to check, but warning here
					DEBUGPRINT("ETH: warning, invalid sized (%d) frame tried to be TX'ed" NL, eth65.tx_size);
				}
			}
			break;
		/* **** $D6E5 register **** */
		case 0x05:
			eth65.mac_filter       = (data & 0x01);
			eth65.disable_crc_chk  = (data & 0x02);
			eth65.adjust_txd_phase = (data >> 2) & 3;
			eth65.accept_broadcast = (data & 0x10);
			eth65.accept_multicast = (data & 0x20);
			break;
		/* **** $D6E6 register **** */
		case 0x06:
			eth65.miim_register = (data & 0x1F);
			eth65.phy_number    = (data & 0xE0) >> 5;
			break;
		/* **** $D6E7 register **** */
		case 0x07:
			miimlo8[eth65.miim_register] = data;
			break;
		/* **** $D6E8 register **** */
		case 0x08:
			miimhi8[eth65.miim_register] = data;
			break;
		/* **** $D6E9 - $D6EE registers: MAC address **** */
		case 0x09: mac_address[0] = data; break;
		case 0x0A: mac_address[1] = data; break;
		case 0x0B: mac_address[2] = data; break;
		case 0x0C: mac_address[3] = data; break;
		case 0x0D: mac_address[4] = data; break;
		case 0x0E: mac_address[5] = data; break;
	}
}



Uint8 eth65_read_rx_buffer ( int offset )
{
	// FIXME what happens if M65 receives frame but user switches buffer meanwhile. Not so nice :-O
	return eth65.rx_buffer[eth65.rx_buffer_mapped + (offset & 0x7FF)];
}

void eth65_write_tx_buffer ( int offset, Uint8 data )
{
	eth65.tx_buffer[offset & 0x7FF] = data;
}



void eth65_shutdown ( void )
{
	if (eth65.exited)
		DEBUGPRINT("ETH: shutting down: handler thread has already exited" NL);
	else if (eth65.enabled) {
		eth65.enabled = 0;
		eth65.no_reset = 0;
		eth65.rx_enabled = 0;
		eth65.tx_trigger = 0;
		eth65.sense_activity = 1;
		eth65.select_wait_usec = 0;
		DEBUGPRINT("ETH: shutting down: handler thread seems to be running" NL);
	} else
		DEBUGPRINT("ETH: shutting down: handler thread seems hasn't been even running (not enabled?)" NL);
#ifdef HAVE_ETHERTAP
	xemu_tuntap_close();
#endif
}


// This is not the reset what eth65.no_reset stuff does, but reset of M65 (also called by eth65_init)
void eth65_reset ( void )
{
	memset(&miimlo8, 0, sizeof miimlo8);
	memset(&miimhi8, 0, sizeof miimhi8);
	memset((void*)&eth65, 0, sizeof eth65);
	RX_IRQ_OFF();
	TX_IRQ_OFF();
	memset((void*)eth65.rx_buffer, 0xFF, sizeof eth65.rx_buffer);
	memset((void*)eth65.tx_buffer, 0xFF, sizeof eth65.tx_buffer);
	eth65.select_wait_usec = SELECT_WAIT_USEC_MAX;
	eth65.rx_buffer_using    = 0x800;	// RX buffer is used to RX @ ofs 0
	eth65.rx_buffer_mapped   = 0x000;	// RX buffer mapped @ ofs 0
	eth65.accept_broadcast = 16;
	eth65.accept_multicast = 32;
	eth65.mac_filter = 1;
}


int eth65_init ( const char *options )
{
	eth65.exited = 0;
	eth65.enabled = 0;
	eth65._irq_to_cpu_routed = 1;
	eth65_reset();
	eth_debug = 0;
	if (options && *options) {
#ifdef HAVE_ETHERTAP
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
			} else if (len ==  5 && !strncmp(options, "debug", len)) {
				eth_debug = 1;
			} else if (len == 13 && !strncmp(options, "alwaysfilters", len)) {
				eth65.xemu_always_filters = 1;
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
			ERROR_WINDOW("%sTAP device \"%s\" opening error: %s", init_error_prefix, device_name, strerror(errno));
			return 1;
		}
		eth65.enabled = 1;
		// Initialize our thread for device read/write ...
		thread_id = SDL_CreateThread(ethernet_thread, "Xemu-EtherTAP", NULL);
		if (thread_id) {
			DEBUGPRINT("ETH: enabled - thread %p started, device attached: \"%s\", initial MAC: %02X:%02X:%02X:%02X:%02X:%02X" NL,
				thread_id, device_name,
				mac_address[0], mac_address[1], mac_address[2],
				mac_address[3], mac_address[4], mac_address[5]
			);
		} else {
			ERROR_WINDOW("%serror creating thread for Ethernet emulation: %s", init_error_prefix, SDL_GetError());
			eth65.enabled = 0;
			xemu_tuntap_close();
			return 1;
		}
		return 0;
#else
		ERROR_WINDOW("Ethernet emulation is not supported/was compiled into this Xemu");
		return 1;
#endif
	} else {
#ifdef HAVE_ETHERTAP
		DEBUGPRINT("ETH: not enabled by config/command line" NL);
#else
		DEBUG("ETH: Ethernet emulation is not supported/was compiled into this Xemu");
#endif
		return 0;
	}
}
