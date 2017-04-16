/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator.
   I/O decoding part (used by memory65.h and DMA mainly)
   Copyright (C)2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "io_mapper.h"
#include "memory_mapper.h"

#include "mega65.h"
#include "xemu/cpu65c02.h"
#include "xemu/cia6526.h"
#include "xemu/f011_core.h"
#include "xemu/f018_core.h"
#include "xemu/emutools_hid.h"
#include "vic3.h"
#include "xemu/sid.h"
#include "sdcard.h"
#include "uart_monitor.h"
#include "hypervisor.h"
#include "xemu/c64_kbd_mapping.h"
#include "xemu/emutools_config.h"
#include "m65_snapshot.h"


int fpga_switches = 0;	// State of FPGA board switches (bits 0 - 15), set switch 12 (hypervisor serial output)
Uint8 gs_regs[0x1000];	// mega65 specific I/O registers, currently an ugly way, as only some bytes are used, ie not VIC3/4, etc etc ...
struct Cia6526 cia1, cia2;		// CIA emulation structures for the two CIAs
struct SidEmulation sid1, sid2;		// the two SIDs


/* Internal decoder for I/O reads. Address *must* be within the 0-$4FFF (!!) range. The low 12 bits is the actual address inside the I/O area,
   while the most significant nibble shows the I/O mode the operation is meant, according to the following table:
   0 = C64 (VIC-II) I/O mode
   1 = C65 (VIC-III) I/O mode
   2 = *INVALID* should not happen, unless some maps the $FF-megabyte M65 specific area, then it should do nothing or so ...
   3 = M65 (VIC-IV) I/O mode
   4 = *CURRENT* I/O mode, will set the right one above, and re-try (actually any larger value than $3FFF */
/* Please read comments at io_reader_internal_decoder() above */



#define RETURN_ON_IO_READ_NOT_IMPLEMENTED(func, fb) \
	do { DEBUG("IO: NOT IMPLEMENTED read (emulator lacks feature), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_READ_NO_NEW_VIC_MODE(func, fb) \
	do { DEBUG("IO: ignored read (not new VIC mode), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_WRITE_NOT_IMPLEMENTED(func) \
	do { DEBUG("IO: NOT IMPLEMENTED write (emulator lacks feature), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)
#define RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE(func) \
	do { DEBUG("IO: ignored write (not new VIC mode), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)
#define WARN_IO_MODE_WR(func) \
	DEBUG("IO: write operation defaults (not new VIC mode) to VIC-2 registers, though it would be: \"%s\" (a=$%04X, d=$%02X)" NL, func, addr, data)
#define WARN_IO_MODE_RD(func) \
	DEBUG("IO: read operation defaults (not new VIC mode) to VIC-2 registers, though it would be: \"%s\" (a=$%04X)" NL, func, addr)


// Call this ONLY with addresses between $D000-$DFFF
// Ranges marked with (*) needs "vic_new_mode"
Uint8 io_reader_internal_decoder ( int addr )
{
	addr = 0xD000 | (addr & 0xFFF);
	// FIXME: sanity check ...
	if (addr < 0xD000 || addr > 0xDFFF)
		FATAL("io_read() decoding problem addr $%X is not in range of $D000...$DFFF", addr);
	// Future stuff: instead of slow tons of IFs, use the >> 5 maybe
	// that can have new device at every 0x20 dividible addresses,
	// that is: switch ((addr >> 5) & 127)
	// Other idea: array of function pointers, maybe separated for new/old
	// VIC modes as well so no need to check each time that either ...
	if (addr < 0xD080)	// $D000 - $D07F:	VIC3
		return vic3_read_reg(addr);
	if (addr < 0xD0A0) {	// $D080 - $D09F	DISK controller (*)
		if (vic_iomode)
			return fdc_read_reg(addr & 0xF);
		else {
			WARN_IO_MODE_RD("DISK controller");
			return vic3_read_reg(addr);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
	}
	if (addr < 0xD100) {	// $D0A0 - $D0FF	RAM expansion controller (*)
		if (vic_iomode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("RAM expansion controller", 0xFF);
		else {
			WARN_IO_MODE_RD("RAM expansion controller");
			return vic3_read_reg(addr);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
	}
	if (addr < 0xD400) {	// $D100 - $D3FF	palette red/green/blue nibbles (*)
		if (vic_iomode)
			return 0xFF; // NOT READABLE ON VIC3!
		else {
			WARN_IO_MODE_RD("palette reg/green/blue nibbles");
			return vic3_read_reg(addr);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
	}
	if (addr < 0xD440) {	// $D400 - $D43F	SID, right
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("right SID", 0xFF);
	}
	if (addr < 0xD600) {	// $D440 - $D5FF	SID, left
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("left SID", 0xFF);
	}
	if (addr < 0xD700) {	// $D600 - $D6FF	UART (*)
		if (vic_iomode == VIC4_IOMODE && addr >= 0xD609) {	// D609 - D6FF: Mega65 suffs
			if (addr >= 0xD680 && addr <= 0xD693)		// SDcard controller etc of Mega65
				return sdcard_read_register(addr - 0xD680);
			switch (addr) {
				case 0xD67C:
					return 0;	// emulate the "UART is ready" situation (used by newer kickstarts around from v0.11 or so)
				case 0xD67E:				// upgraded hypervisor signal
					if (kicked_hypervisor == 0x80)	// 0x80 means for Xemu (not for a real M65!): ask the user!
						kicked_hypervisor = QUESTION_WINDOW(
							"Not upgraded yet, it can do it|Already upgraded, I test kicked state",
							"Kickstart asks hypervisor upgrade state. What do you want Xemu to answer?\n"
							"(don't worry, it won't be asked again without RESET)"
						) ? 0xFF : 0;
					return kicked_hypervisor;
				case 0xD6F0:
					return fpga_switches & 0xFF;
				case 0xD6F1:
					return (fpga_switches >> 8) & 0xFF;
				default:
					DEBUG("MEGA65: reading Mega65 specific I/O @ $%04X result is $%02X" NL, addr, gs_regs[addr & 0xFFF]);
					return gs_regs[addr & 0xFFF];
			}
		} else if (vic_iomode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("UART", 0xFF);
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("UART", 0xFF);
	}
	if (addr < 0xD800) {	// $D700 - $D7FF	DMA (*)
		if (vic_iomode)
			return dma_read_reg(addr & 0xF);
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("DMA controller", 0xFF);
	}
	if (addr < ((vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00)) {	// $D800-$DC00/$E000	COLOUR NIBBLES, mapped to $1F800 in BANK1
		DEBUG("IO: reading colour RAM at offset $%04X" NL, addr - 0xD800);
		return colour_ram[addr - 0xD800];
	}
	if (addr < 0xDD00) {	// $DC00 - $DCFF	CIA-1
		Uint8 result = cia_read(&cia1, addr & 0xF);
		//RETURN_ON_IO_READ_NOT_IMPLEMENTED("CIA-1", 0xFF);
		DEBUG("%s: reading register $%X result is $%02X" NL, cia1.name, addr & 15, result);
		return result;
	}
	if (addr < 0xDE00) {	// $DD00 - $DDFF	CIA-2
		Uint8 result = cia_read(&cia2, addr & 0xF);
		//RETURN_ON_IO_READ_NOT_IMPLEMENTED("CIA-2", 0xFF);
		DEBUG("%s: reading register $%X result is $%02X" NL, cia2.name, addr & 15, result);
		return result;
	}
	// Only IO-1 and IO-2 areas left, if SD-card buffer is mapped for Mega65, this is our only case left!
	do {
		int result = sdcard_read_buffer(addr - 0xDE00);	// try to read SD buffer
		if (result >= 0) {	// if non-negative number got, answer is really the SD card (mapped buffer)
			DEBUG("SDCARD: BUFFER: reading SD-card buffer at offset $%03X with result $%02X PC=$%04X" NL, addr - 0xDE00, result, cpu_pc);
			return result;
		} else
			DEBUG("SDCARD: BUFFER: *NOT* mapped SD-card buffer is read, can it be a bug?? PC=$%04X" NL, cpu_pc);
	} while (0);
	if (addr < 0xDF00) {	// $DE00 - $DEFF	IO-1 external
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("IO-1 external select", 0xFF);
	}
	// The rest: IO-2 external
	RETURN_ON_IO_READ_NOT_IMPLEMENTED("IO-2 external select", 0xFF);
}




// Call this ONLY with addresses between $D000-$DFFF
// Ranges marked with (*) needs "vic_new_mode"
void io_writer_internal_decoder ( int addr, Uint8 data )
{
	addr = 0xD000 | (addr & 0xFFF);
	// FIXME: sanity check ...
	if (addr < 0xD000 || addr > 0xDFFF)
		FATAL("io_read() decoding problem addr $%X is not in range of $D000...$DFFF", addr);
	if (addr < 0xD080) {	// $D000 - $D07F:	VIC3
		vic3_write_reg(addr, data);
		return;
	}
	if (addr < 0xD0A0) {	// $D080 - $D09F	DISK controller (*)
		if (vic_iomode)
			fdc_write_reg(addr & 0xF, data);
		else {
			WARN_IO_MODE_WR("DISK controller");
			vic3_write_reg(addr, data);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
		return;
	}
	if (addr < 0xD100) {	// $D0A0 - $D0FF	RAM expansion controller (*)
		if (vic_iomode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("RAM expansion controller");
		else {
			WARN_IO_MODE_WR("RAM expansion controller");
			vic3_write_reg(addr, data);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
		return;
	}
	if (addr < 0xD400) {	// $D100 - $D3FF	palette red/green/blue nibbles (*)
		if (vic_iomode)
			vic3_write_palette_reg(addr - 0xD100, data);
		else {
			WARN_IO_MODE_WR("palette red/green/blue nibbles");
			vic3_write_reg(addr, data);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
		return;
	}
	if (addr < 0xD440) {	// $D400 - $D43F	SID, right
		sid_write_reg(&sid1, addr & 31, data);
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("right SID");
		return;
	}
	if (addr < 0xD600) {	// $D440 - $D5FF	SID, left
		sid_write_reg(&sid2, addr & 31, data);
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("left SID");
		return;
	}
	if (addr < 0xD700) {	// $D600 - $D6FF	UART (*)
		if (vic_iomode == VIC4_IOMODE && addr >= 0xD609) {	// D609 - D6FF: Mega65 suffs
			gs_regs[addr & 0xFFF] = data;
			DEBUG("MEGA65: writing Mega65 specific I/O range @ $%04X with $%02X" NL, addr, data);
			if (!in_hypervisor && addr >= 0xD640 && addr <= 0xD67F) {
				// In user mode, writing to $D640-$D67F (in VIC4 iomode) causes to enter hypervisor mode with
				// the trap number given by the offset in this range
				hypervisor_enter(addr & 0x3F);
				return;
			}
			if (addr >= 0xD680 && addr <= 0xD693) {
				sdcard_write_register(addr - 0xD680, data);
				return;
			}
			switch (addr) {
				case 0xD67C:	// hypervisor serial monitor port
					hypervisor_serial_monitor_push_char(data);
					break;
				case 0xD67D:
					DEBUG("MEGA65: features set as $%02X" NL, data);
					if ((data & 4) != rom_protect) {
						fprintf(stderr, "MEGA65: ROM protection has been turned %s." NL, data & 4 ? "ON" : "OFF");
						rom_protect = data & 4;
					}
					break;
				case 0xD67E:	// it seems any write (?) here marks the byte as non-zero?! FIXME TODO
					kicked_hypervisor = 0xFF;
					fprintf(stderr, "Writing already-kicked register $%04X!" NL, addr);
					hypervisor_debug_invalidate("$D67E was written, maybe new kickstart will boot!");
					break;
				case 0xD67F:	// hypervisor leave
					hypervisor_leave();
					break;
				default:
					DEBUG("MEGA65: this I/O port is not emulated in Xemu yet: $%04X" NL, addr);
					break;
			}
                        return;
		} else if (vic_iomode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("UART");
		else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("UART");
	}
	if (addr < 0xD800) {	// $D700 - $D7FF	DMA (*)
		DEBUG("DMA: writing register $%04X (data = $%02X)" NL, addr, data);
		if (vic_iomode) {
			dma_write_reg(addr & 0xF, data);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("DMA controller");
	}
	if (addr < ((vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00)) {	// $D800-$DC00/$E000	COLOUR NIBBLES, mapped to $1F800 in BANK1
		colour_ram[addr - 0xD800] = data;
		DEBUG("IO: writing colour RAM at offset $%04X" NL, addr - 0xD800);
		return;
	}
	if (addr < 0xDD00) {	// $DC00 - $DCFF	CIA-1
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("CIA-1");
		DEBUG("%s: writing register $%X with data $%02X" NL, cia1.name, addr & 15, data);
		cia_write(&cia1, addr & 0xF, data);
		return;
	}
	if (addr < 0xDE00) {	// $DD00 - $DDFF	CIA-2
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("CIA-2");
		DEBUG("%s: writing register $%X with data $%02X" NL, cia2.name, addr & 15, data);
		cia_write(&cia2, addr & 0xF, data);
		return;
	}
	// Only IO-1 and IO-2 areas left, if SD-card buffer is mapped for Mega65, this is our only case left!
	if (sdcard_write_buffer(addr - 0xDE00, data) >= 0)
		return;	// if return value is non-negative, buffer was mapped and written!
	if (addr < 0xDF00) {	// $DE00 - $DEFF	IO-1 external
		RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("IO-1 external select");
	}
	// The rest: IO-2 external
	RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("IO-2 external select");
}
