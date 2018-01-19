/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
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

#ifdef HAVE_ETHERNET65

#ifndef HAVE_ETHERTAP
#error "HAVE_ETHERNET65 needs HAVE_ETHERTAP"
#endif


/* Possible future work:
	* FIXME: it uses thread, and it's horrible code!! Maybe there are tons of race conditions, etc ...
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


#include "xemu/emutools.h"
#include "xemu/ethertap.h"
#include "ethernet65.h"
#include "io_mapper.h"
#include <errno.h>


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


static SDL_Thread *thread_id = NULL;

static volatile struct {
	int	enabled;		// the whole M65 eth emulation status
	int	exited;
	int	rx_enabled;		// can the TAP handler thread receive a new packet?
	int	_rx_irq;		// status of RX IRQ, note that it is also used for polling without enabled IRQs! (must be 0x20 or zero)
	int	_tx_irq;		// status of TX IRQ, note that it is also used for polling without enabled IRQs! (must be 0x10 or zero)
	int	_irq_to_cpu_routed;
	int	tx_size;		// frame size to transmit, also a trigger (if non-zero) to TX requested (0=nothing to TX)
	int	rx_irq_enabled;		// RX IRQ enabled, so rx_irq actually generates an IRQ
	int	tx_irq_enabled;		// TX IRQ enabled, so tx_irq actually generates an IRQ
	int	rx_buffer_used;		// RX buffer used by the receiver thread (must be 0 or 0x800, it also means offset in rx_buffer!)
	int	rx_buffer_mapped;	// RX buffer which is mapped by the user (must be 0 or 0x800, it also means offset in rx_buffer!)
	int	rx_buffer_swap_trigger;	// triggers swapping used RX buffers by handler thread for the main thread
	int	sense_activity;		// handler thread senses activity, helps keeping select_wait_usec low when there is no activity to avoid burning the CPU power without reason
	int	select_wait_usec;	// uSeconds to wait at max by select(), the reason for this is the same which is expressed in the previous line
	Uint8	rx_buffer[0x1000];	// actually, two 2048 bytes long buffers in one array
	Uint8	tx_buffer[0x0800];	// a 2048 bytes long buffer to TX
} eth65;


// IRQs are not yet supported, especially because it's an async event from a thread, and the main thread should aware it
// at every opcode emulation, which would made that slow (ie thread-safe update of the CPU IRQ request ...)
static void __eth65_manage_irq_for_cpu ( void )
{
	int status = ((eth65._rx_irq && eth65.rx_irq_enabled) || (eth65._tx_irq && eth65.tx_irq_enabled));
	if (status != eth65._irq_to_cpu_routed) {
		printf("ETH: IRQ change should be propogated: %d -> %d" NL, eth65._irq_to_cpu_routed, status);
		eth65._irq_to_cpu_routed = status;
	}
}

#define RX_IRQ_ON()	do { eth65._rx_irq = 0x20; __eth65_manage_irq_for_cpu(); } while (0)
#define RX_IRQ_OFF()	do { eth65._rx_irq = 0;    __eth65_manage_irq_for_cpu(); } while (0)
#define TX_IRQ_ON()	do { eth65._tx_irq = 0x10; __eth65_manage_irq_for_cpu(); } while (0)
#define TX_IRQ_OFF()	do { eth65._tx_irq = 0;    __eth65_manage_irq_for_cpu(); } while (0)




/*	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	!!!!! WARNING: this function run as *THREAD* !!!!!
	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	It must be very careful not use anything too much.
	xemu_tuntap_read and write functions are used only here, so it's maybe OK
	but exchange data with the main thread (the emulator) must be done very carefully! 	*/
static int eth65_main ( void *user_data_unused )
{
	Uint8 rx_temp_buffer[0x800];
	printf("ETH-THREAD: hello from the thread listening on device %s" NL, (const char*)user_data_unused);
	while (eth65.enabled) {
		int r;
		if (eth65.sense_activity) {
			eth65.select_wait_usec = SELECT_WAIT_USEC_MIN;  // there was some real activity, lower the select wait time in the hope, there is some on-going future stuff soon
			eth65.sense_activity = 0;
		} else {
			if (eth65.select_wait_usec < SELECT_WAIT_USEC_MAX)
				eth65.select_wait_usec += SELECT_WAIT_INC_VAL;
		}
		r = xemu_tuntap_select((eth65.rx_enabled ? XEMU_TUNTAP_SELECT_R : 0) | (eth65.tx_size ? XEMU_TUNTAP_SELECT_W : 0), eth65.select_wait_usec);
		if (r < 0) {
			printf("ETH-THREAD: select error: %s" NL, strerror(errno));
			break;
		}
		/* ------------------- TO RECEIVE (RX) ------------------- */
		if (eth65.rx_enabled && (r & XEMU_TUNTAP_SELECT_R)) {
			r = xemu_tuntap_read(rx_temp_buffer, 6 + 6 + 2, sizeof rx_temp_buffer);
			if (!eth65.rx_enabled)
				continue;
			if (r == -2) {
				eth65.select_wait_usec = 1000000;	// serious problem, do 1sec wait ...
				printf("ETH-THREAD: read with EAGAIN, continue ..." NL);
				continue;
			}
			if (r < 0) {
				printf("ETH-THREAD: read error: %s" NL, strerror(errno));
				break;
			}
			if (r == 0) {
				eth65.select_wait_usec = 1000000;	// serious problem, do 1sec wait ...
				printf("ETH-THREAD: read() returned with zero! continue ..." NL);
				continue;
			}
			if (r > 2046) {	// should not happen, but who knows ... (2046=size of buffer - 2, 2 for the length parameter to be passed)
				// FIXME: maybe we should check the max frame size instead, but I guess TAP device does not allow crazy sizes anyway
				eth65.select_wait_usec = 1000000;	// serious problem, do 1sec wait ...
				printf("ETH-THREAD: read() had %d bytes, too long! continue ..." NL, r);
				continue;
			}
			if (r > 0) {
				printf("ETH-THREAD: read() returned with %d bytes read, OK!" NL, r);
				eth65.sense_activity = 1;
				// M65 stores the received frame size in "6502 byte order" as the first two bytes in the RX
				// (this makes things somewhat non-symmetric, as for TX, the size are in a pair of registers)
				eth65.rx_buffer[eth65.rx_buffer_used] = r & 0xFF;
				eth65.rx_buffer[eth65.rx_buffer_used + 1] = r >> 8;
				for (int i = 0; i < r; i++)
					eth65.rx_buffer[eth65.rx_buffer_used + 2 + i] = rx_temp_buffer[i];
				//memcpy(eth65.rx_buffer + eth65.rx_buffer_used + 2, rx_temp_buffer, r);
				eth65.rx_buffer_swap_trigger = 1;
				eth65.rx_enabled = 0;		// disable RX till user not ACK'ed
				RX_IRQ_ON();			// signal, that we have something! we don't support IRQs yet, but it's also used for polling!
			}
		}
		/* ------------------- TO TRANSMIT (TX) ------------------- */
		if (eth65.tx_size && (r & XEMU_TUNTAP_SELECT_W)) {
			r = xemu_tuntap_write((void*)eth65.tx_buffer, eth65.tx_size);
			eth65.tx_size = 0;
			if (r == -2) {	// FIXME: with read OK, but for write????
				eth65.select_wait_usec = 1000000;	// serious problem, do 1sec wait ...
				printf("ETH-THREAD: write with EAGAIN, continue ..." NL);
				continue;
			}
			if (r < 0) {
				printf("ETH-THREAD: write error: %s" NL, strerror(errno));
				break;
			}
			if (r == 0) {
				eth65.select_wait_usec = 1000000;	// serious problem, do 1sec wait ...
				printf("ETH-THREAD: write() returned with zero! continue ..." NL);
				continue;
			}
			if (r != eth65.tx_size) {
				printf("ETH-THREAD: partial write only?! %d bytes with write(), but we wanted %d bytes to TX" NL, r, eth65.tx_size);
				TX_IRQ_ON();	// not a good thing, but still we want to make user not to stuck at TX ...
				if (r != eth65.tx_size) {
					printf("ETH-THREAD: partial write only?! wanted = %d, written = %d" NL, eth65.tx_size, r);
					break;
				}
			}
			if (r == eth65.tx_size) {
				printf("ETH-THREAD: write() returned with %d bytes read, OK!" NL, r);
				eth65.sense_activity = 1;
				TX_IRQ_ON();
			}
		}
	}
	eth65.exited = 1;
	printf("ETH-THREAD: leaving ..." NL);
	xemu_tuntap_close();
	printf("ETH-THREAD: OK now ..." NL);
	return 0;
}


Uint8 eth65_read_reg_D6E1 ( void )
{
	return
		// Bit0: maybe write only
		( D6XX_registers[0xE1] & 0x01 ) |
		// Bit1: which RX buffer is mapped, we set the offset according to that
		( eth65.rx_buffer_mapped ? 0x02 : 0x00 ) |
		// Bit2: indicates which RX buffer was used ...
		( eth65.rx_buffer_used ? 0x04 : 0x00 ) |
		// Bit3: Enable real-time video streaming via ethernet
		( D6XX_registers[0xE1] & 0x08 ) |
		// Bit4: ethernet TX IRQ status, it's also used with POLLING, without IRQ enabled!! ON WRITE, it's used to clear the status!!
		eth65._tx_irq |
		// Bit5: ethernet RX IRQ status, it's also used with POLLING, without IRQ enabled!! ON WRITE, it's used to clear the status!!
		eth65._rx_irq |
		// Bit6: Enable ethernet TX IRQ
		eth65.tx_irq_enabled |
		// Bit7: Enable ethernet RX IRQ
		eth65.rx_irq_enabled
	;
}


void eth65_write_reg_D6E1 ( Uint8 data )
{
	// our thread sets this to indicate swap receive buffer
	// it's granted here, since the user needs to "ACK" anyway which means writing register $D6E1
	// this way, probably we have less race conditions ...
	if (eth65.rx_buffer_swap_trigger) {
		eth65.rx_buffer_swap_trigger = 0;
		eth65.rx_buffer_used = (eth65.rx_buffer_used ? 0 : 0x800);
	}
	// Bit0: reset PHY, it seems this actually means, that eth can RX, works like ACK the previous received packet, so the controller can receive new one (?)
	if (data & 0x01) {
		RX_IRQ_OFF(); 		// FIXME: is it needed here? and what about TX?????? FIXME
		eth65.rx_enabled = 1;
	}
	// Bit1: which RX buffer is mapped, we set the offset according to that
	eth65.rx_buffer_mapped = (data & 0x02) ? 0x800: 0;
	// Bit2: indicates which RX buffer was used ... maybe it's a read-only bit
	// Bit3: Enable real-time video streaming via ethernet (well, it's not supported by Xemu, but it would be fancy stuff btw ...)
	// Bit4: ethernet TX IRQ status, it's also used with POLLING, without IRQ enabled!! ON WRITE, it's used to clear the status!!
	// Bit5: ethernet RX IRQ status, it's also used with POLLING, without IRQ enabled!! ON WRITE, it's used to clear the status!!
	// Bit6: Enable ethernet TX IRQ, not yet supported in Xemu! FIXME
	eth65.tx_irq_enabled = (data & 0x40);	// not yet used, FIXME
	// Bit7: Enable ethernet RX IRQ, not yet supported in Xemu! FIXME
	eth65.rx_irq_enabled = (data & 0x80);	// not yet used, FIXME
	__eth65_manage_irq_for_cpu();
}


void eth65_write_reg_D6E4 ( Uint8 data )
{
	// trigger TX if $01 is written?
	// clear (pending?) TX is $00 [marked as 'DEBUG' in iomap.txt']: FIXME? not implemented
	if (data == 1) {
		int tx_size = D6XX_registers[0xE2] | (D6XX_registers[0xE3] << 8);
		if (tx_size > 0 || tx_size <= ETH_FRAME_MAX_SIZE) {
			if (tx_size < ETH_FRAME_MIN_SIZE) {
				// maybe this is not needed, and TAP device can handle autmatically, but anyway:
				// most ETH controllers would pad frame with zeroes, if it's shorter then the minimum size for the medium
				memset((void*)eth65.tx_buffer + tx_size, 0, ETH_FRAME_MIN_SIZE - tx_size);
				tx_size = ETH_FRAME_MIN_SIZE;
			}
			eth65.sense_activity = 1;
			TX_IRQ_OFF();		// FIXME: we want to clear this, or it's the user task after the previous TX?
			eth65.tx_size = tx_size;	// propogate the TX frame size, this is also the trigger for the thread, that we want to transmit
		}
	}
}



Uint8 eth65_read_rx_buffer ( int offset )
{
	return eth65.rx_buffer[eth65.rx_buffer_mapped + (offset & 0x7FF)];
}

void eth65_write_tx_buffer ( int offset, Uint8 data )
{
	eth65.tx_buffer[offset & 0x7FF] = data;
}



void eth65_shutdown ( void )
{
	if (eth65.exited)
		DEBUGPRINT("ETH: shutting down: handler thread already has exited" NL);
	else if (eth65.enabled)
		DEBUGPRINT("ETH: shutting down: handler thread seems to activeted" NL);
	else
		DEBUGPRINT("ETH: shutting down: handler thread seems hasn't been even running" NL);
	eth65.enabled = 0;
	xemu_tuntap_close();
}


void eth65_reset ( void )
{
	eth65.rx_irq_enabled = 0;
	eth65.tx_irq_enabled = 0;
	RX_IRQ_OFF();
	TX_IRQ_OFF();
	eth65.tx_size = 0; 	// nothing to TX
	memset((void*)eth65.rx_buffer, 0xFF, sizeof eth65.rx_buffer);
	memset((void*)eth65.tx_buffer, 0xFF, sizeof eth65.tx_buffer);
	eth65.select_wait_usec = SELECT_WAIT_USEC_MAX;
	eth65.tx_size = 0;
	eth65.rx_enabled = 0;
	eth65.sense_activity = 0;
	eth65.rx_buffer_used = 0;	// RX buffer is used to RX @ ofs 0
	eth65.rx_buffer_mapped = 0;	// RX buffer mapped @ ofs 0
	eth65.rx_buffer_swap_trigger = 0;
	D6XX_registers[0xE1] = 0;
	D6XX_registers[0xE2] = 0;
	D6XX_registers[0xE3] = 0;
	D6XX_registers[0xE4] = 0;
}


void eth65_init ( const char *device_name )
{
	eth65.exited = 0;
	eth65.enabled = 0;
	eth65._irq_to_cpu_routed = 1;
	eth65_reset();
	if (!device_name) {
		DEBUGPRINT("ETH: not enabled" NL);
	} else {
		if (xemu_tuntap_alloc(device_name, NULL, 0, XEMU_TUNTAP_IS_TAP | XEMU_TUNTAP_NO_PI | XEMU_TUNTAP_NONBLOCKING_IO) < 0)
			ERROR_WINDOW("ETH: Ethernet TAP device \"%s\" error: %s", device_name, strerror(errno));
		else {
			eth65.enabled = 1;
			// Initialize our thread for device read/write ...
			thread_id = SDL_CreateThread(eth65_main, "Xemu-EtherTAP", (void*)device_name);
			if (thread_id) {
				DEBUGPRINT("ETH: enabled - thread %p started, name = \"%s\"" NL, thread_id, device_name);
			} else {
				eth65.enabled = 0;
				xemu_tuntap_close();
				ERROR_WINDOW("ETH: Error creating thread for Ethernet emulation: %s", SDL_GetError());
				
			}
		}
	}
}




#endif
