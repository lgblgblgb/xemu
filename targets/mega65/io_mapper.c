/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   I/O decoding part (used by memory_mapper.h and DMA mainly)
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/f011_core.h"
#include "dma65.h"
#include "vic4.h"
#include "vic4_palette.h"
#include "sdcard.h"
#include "hypervisor.h"
#include "ethernet65.h"
#include "input_devices.h"
#include "audio65.h"
#include "configdb.h"


int    fpga_switches = 0;		// State of FPGA board switches (bits 0 - 15), set switch 12 (hypervisor serial output)
Uint8  D6XX_registers[0x100];		// mega65 specific D6XX range, excluding the UART part (not used here!)
Uint8  D7XX[0x100];			// FIXME: hack for future M65 stuffs like ALU! FIXME: no snapshot on these!
struct Cia6526 cia1, cia2;		// CIA emulation structures for the two CIAs
int    cpu_mega65_opcodes = 0;	// used by the CPU emu as well!
static int bigmult_valid_result = 0;
int port_d607 = 0xFF;			// ugly hack to be able to read extra char row of C65 keyboard


static const Uint8 fpga_firmware_version[] = { 'X','e','m','u' };
static const Uint8 cpld_firmware_version[] = { 'N','o','w','!' };
#define xemu_query_interface_str XEMU_BUILDINFO_CDATE
static const char *xemu_query_interface_p = NULL;
static int         xemu_query_gate = 0;


#define RETURN_ON_IO_READ_NOT_IMPLEMENTED(func, fb) \
	do { DEBUG("IO: NOT IMPLEMENTED read (emulator lacks feature), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_WRITE_NOT_IMPLEMENTED(func) \
	do { DEBUG("IO: NOT IMPLEMENTED write (emulator lacks feature), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)


static XEMU_INLINE void update_hw_multiplier ( void )
{
	register const Uint32 input_a = xemu_u8p_to_u32le(D7XX + 0x70);
	register const Uint32 input_b = xemu_u8p_to_u32le(D7XX + 0x74);
	// Set flag to valid, so we don't relculate each time
	// (this variable is used to avoid calling this function if no change was
	// done on input_a and input_b)
	bigmult_valid_result = 1;
	// --- Do the product, multiplication ---
	xemu_u64le_to_u8p(D7XX + 0x78, (Uint64)input_a * (Uint64)input_b);
	// --- Do the quotient, divide ---
	// ... but we really don't want to divide by zero, so let's test this
	if (XEMU_LIKELY(input_b)) {
		// input_b is non-zero, it's OK to divide
		xemu_u64le_to_u8p(D7XX + 0x68, (Uint64)((Uint64)input_a << 32) / (Uint64)input_b);
	} else {
		// If we divide by zero, according to the VHDL,
		// we set all bits to '1' in the result, that is $FF
		// for all registers of div output. Probably it can be
		// interpreted as some kind of "fixed point infinity"
		// or just a measure of error with this special answer.
		memset(D7XX + 0x68, 0xFF, 8);
	}
}



/* Internal decoder for I/O reads. Address *must* be within the 0-$3FFF (!!) range. The low 12 bits is the actual address inside the I/O area,
   while the most significant nibble shows the I/O mode the operation is meant, according to the following table:
   0 = C64 (VIC-II) I/O mode
   1 = C65 (VIC-III) I/O mode
   2 = *INVALID* should not happen, unless some maps the $FF-megabyte M65 specific area, then it should do nothing or so ...
   3 = M65 (VIC-IV) I/O mode.
   Even writing the "classic" I/O area at $D000 will ends up here. These addressed are stricly based on the VIC mode
   according the table above, no $D000 addresses here at all!
   FIXME: it seems I/O mode "2" may be not invalid, but some tricky stuff, C64 with extensions, or such. I have not so much idea yet :(
   NOTE: vic_read_reg() and vic_write_reg() is kinda special! It uses offset'ed register addresses for different VIC mode registers ... */

Uint8 io_read ( unsigned int addr )
{
	// DEBUG("IO: read $%03X IO_mode is %d @ PC=$%04X" NL, addr & 0xFFF, addr >> 12, cpu65.pc);
	switch (addr >> 8) {
		/* ---------------------------------------------------- */
		/* $D000-$D3FF: VIC-II, VIC-III+FDC+REC, VIC-IV+FDC+REC */
		/* ---------------------------------------------------- */
		case 0x00:	// $D000-$D0FF ~ C64 I/O mode
		case 0x01:	// $D100-$D1FF ~ C64 I/O mode
		case 0x02:	// $D200-$D2FF ~ C64 I/O mode
		case 0x03:	// $D300-$D3FF ~ C64 I/O mode
			return vic_read_reg((addr & 0x3F) | 0x100);	// VIC-II read register
		case 0x10:	// $D000-$D0FF ~ C65 I/O mode
			addr &= 0xFF;
			if (XEMU_LIKELY(addr < 0x80))
				return vic_read_reg(addr | 0x80);	// VIC-III read register
			if (XEMU_LIKELY(addr < 0xA0))
				return fdc_read_reg(addr & 0xF);
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("RAM expansion controller", 0xFF);
		case 0x30:	// $D000-$D0FF ~ M65 I/O mode
			addr &= 0xFF;
			if (XEMU_LIKELY(addr < 0x80))
				return vic_read_reg(addr);		// VIC-IV read register
			if (XEMU_LIKELY(addr < 0xA0))
				return fdc_read_reg(addr & 0xF);
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("RAM expansion controller", 0xFF);
		case 0x11:	// $D100-$D1FF ~ C65 I/O mode
		case 0x12:	// $D200-$D2FF ~ C65 I/O mode
		case 0x13:	// $D300-$D3FF ~ C65 I/O mode
			return 0xFF;	// FIXME: AFAIK C65 does not allow to read palette register back, however M65 I/O mode does allow (?)
		case 0x31:	// $D100-$D1FF ~ M65 I/O mode
			return vic4_read_palette_reg_red(addr);	// function takes care using only 8 low bits of addr, no need to do here
		case 0x32:	// $D200-$D2FF ~ M65 I/O mode
			return vic4_read_palette_reg_green(addr);
		case 0x33:	// $D300-$D3FF ~ M65 I/O mode
			return vic4_read_palette_reg_blue(addr);
		/* ------------------------------------------------ */
		/* $D400-$D7FF: SID, SID+UART+DMA, SID+UART+DMA+M65 */
		/* ------------------------------------------------ */
		case 0x04:	// $D400-$D4FF ~ C64 I/O mode
		case 0x05:	// $D500-$D5FF ~ C64 I/O mode
		case 0x06:	// $D600-$D6FF ~ C64 I/O mode
		case 0x07:	// $D700-$D7FF ~ C64 I/O mode
		case 0x14:	// $D400-$D4FF ~ C65 I/O mode
		case 0x15:	// $D500-$D5FF ~ C65 I/O mode
		case 0x34:	// $D400-$D4FF ~ M65 I/O mode
		case 0x35:	// $D500-$D5FF ~ M65 I/O mode
			switch (addr & 0x5F) {
				case 0x19: return get_mouse_x_via_sid();
				case 0x1A: return get_mouse_y_via_sid();
			}
			return 0xFF;
		case 0x16:	// $D600-$D6FF ~ C65 I/O mode
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("UART", 0xFF);	// FIXME: UART is not yet supported!
		case 0x36:	// $D600-$D6FF ~ M65 I/O mode
			addr &= 0xFF;
			if (addr < 9)
				RETURN_ON_IO_READ_NOT_IMPLEMENTED("UART", 0xFF);	// FIXME: UART is not yet supported!
			if (addr >= 0x80 && addr <= 0x93)	// SDcard controller etc of MEGA65
				return sdcard_read_register(addr - 0x80);
			if ((addr & 0xF0) == 0xE0)
				return eth65_read_reg(addr);
			switch (addr) {
				case 0x7C:
					return 0;			// emulate the "UART is ready" situation (used by newer kickstarts around from v0.11 or so)
				case 0x7E:				// upgraded hypervisor signal
					if (D6XX_registers[0x7E] == 0x80)	// 0x80 means for Xemu (not for a real M65!): ask the user!
						D6XX_registers[0x7E] = QUESTION_WINDOW(
							"Not upgraded yet, it can do it|Already upgraded, I test kicked state",
							"Kickstart asks hypervisor upgrade state. What do you want Xemu to answer?\n"
							"(don't worry, it won't be asked again without RESET)"
						) ? 0xFF : 0;
					return D6XX_registers[0x7E];
				case 0x7F:
					return in_hypervisor ? 'H' : 'U';	// FIXME: I am not sure about 'U' here (U for userspace, H for hypervisor mode)
				case 0xF0:
					return fpga_switches & 0xFF;
				case 0xF1:
					return (fpga_switches >> 8) & 0xFF;
				case 0x10:				// last keypress ASCII value
					return hwa_kbd_get_last();
				case 0x11:				// modifier keys on kbd being used
					return hwa_kbd_get_modifiers();
				case 0x13:				// $D613: direct access to the kbd matrix, read selected row (set by writing $D614), bit 0 = key pressed
					return kbd_directscan_query(D6XX_registers[0x14]);	// for further explanations please see this function in input_devices.c
				case 0x29:
					return configdb.mega65_model;		// MEGA65 model
				case 0x0F:
					// D60F bit 5, real hardware (1), emulation (0), other bits are not emulated yet by Xemu, so I give simply zero
					return 0;
				case 0x1B:
					// D61B amiga / 1531 mouse auto-detect. FIXME XXX what value we should return at this point? :-O
					return 0xFF;
				case 0x32: // D632-D635: FPGA firmware ID
				case 0x33:
				case 0x34:
				case 0x35:
					if (xemu_query_gate == 0xF) {
						Uint8 data = *xemu_query_interface_p;
						//if (!data)
						//	xemu_query_gate = 0;
						return data;
					} else {
						return fpga_firmware_version[addr - 0x32];
					}
				case 0x2C: // D62C-D62F: CPLD firmware ID
				case 0x2D:
				case 0x2E:
				case 0x2F:
					if (xemu_query_gate == 0xF) {
						Uint8 data = *xemu_query_interface_p++;
						if (!data) {
							xemu_query_gate = 0;
						}
						return data;
					} else {
						return cpld_firmware_version[addr - 0x2C];
					}
				default:
					DEBUG("MEGA65: reading MEGA65 specific I/O @ $D6%02X result is $%02X" NL, addr, D6XX_registers[addr]);
					return D6XX_registers[addr];
			}
		case 0x17:	// $D700-$D7FF ~ C65 I/O mode
			// FIXME: really a partial deconding like this? really on every 16 bytes?!
			return dma_read_reg(addr & 0xF);
		case 0x37:	// $D700-$D7FF ~ M65 I/O mode
			// FIXME: this is probably very bad! I guess DMA does not decode for every 16 addresses ... Proposed fix is here:
			addr &= 0xFF;
			if (addr < 15)		// FIXME!!!! 0x0F was part of DMA reg array, but it seems now used by divisor busy stuff??
				return dma_read_reg(addr & 0xF);
			if (addr == 0x0F)
				return 0;	// FIXME: D70F bit 7 = 32/32 bits divisor busy flag, bit 6 = 32*32 mult busy flag. We're never busy, so the zero. But the OTHER bits??? Any purpose of those??
			// ;) FIXME this is LAZY not to decode if we need to update bigmult at all ;-P
			if (XEMU_UNLIKELY(!bigmult_valid_result))
				update_hw_multiplier();
			return D7XX[addr];
		/* ----------------------- */
		/* $D800-$DBFF: COLOUR RAM */
		/* ----------------------- */
		case 0x08:	// $D800-$D8FF ~ C64 I/O mode
		case 0x09:	// $D900-$D9FF ~ C64 I/O mode
		case 0x0A:	// $DA00-$DAFF ~ C64 I/O mode
		case 0x0B:	// $DB00-$DBFF ~ C64 I/O mode
			return colour_ram[addr - 0x0800] | 0xF0;	// FIXME: is this true, that high nibble if faked to be '1' in C64 I/O mode, always?
		case 0x18:	// $D800-$D8FF ~ C65 I/O mode
		case 0x19:	// $D900-$D9FF ~ C65 I/O mode
		case 0x1A:	// $DA00-$DAFF ~ C65 I/O mode
		case 0x1B:	// $DB00-$DBFF ~ C65 I/O mode
			return colour_ram[addr - 0x1800];
		case 0x38:	// $D800-$D8FF ~ M65 I/O mode
		case 0x39:	// $D900-$D9FF ~ M65 I/O mode
		case 0x3A:	// $DA00-$DAFF ~ M65 I/O mode
		case 0x3B:	// $DB00-$DBFF ~ M65 I/O mode
			return colour_ram[addr - 0x3800];
		/* --------------------------------------- */
		/* $DC00-$DCFF: CIA#1, EXTENDED COLOUR RAM */
		/* --------------------------------------- */
		case 0x0C:	// $DC00-$DCFF ~ C64 I/O mode
			return (vic_registers[0x30] & 1) ? colour_ram[addr - 0x0800] : cia_read(&cia1, addr & 0xF);
		case 0x1C:	// $DC00-$DCFF ~ C65 I/O mode
			return (vic_registers[0x30] & 1) ? colour_ram[addr - 0x1800] : cia_read(&cia1, addr & 0xF);
		case 0x3C:	// $DC00-$DCFF ~ M65 I/O mode
			return (vic_registers[0x30] & 1) ? colour_ram[addr - 0x3800] : cia_read(&cia1, addr & 0xF);
		/* --------------------------------------- */
		/* $DD00-$DDFF: CIA#2, EXTENDED COLOUR RAM */
		/* --------------------------------------- */
		case 0x0D:	// $DD00-$DDFF ~ C64 I/O mode
			return (vic_registers[0x30] & 1) ? colour_ram[addr - 0x0800] : cia_read(&cia2, addr & 0xF);
		case 0x1D:	// $DD00-$DDFF ~ C65 I/O mode
			return (vic_registers[0x30] & 1) ? colour_ram[addr - 0x1800] : cia_read(&cia2, addr & 0xF);
		case 0x3D:	// $DD00-$DDFF ~ M65 I/O mode
			return (vic_registers[0x30] & 1) ? colour_ram[addr - 0x3800] : cia_read(&cia2, addr & 0xF);
		/* --------------------------------------------------- */
		/* $DE00-$DFFF: IO exp, EXTENDED COLOUR RAM, SD buffer */
		/* --------------------------------------------------- */
		case 0x0E:	// $DE00-$DEFF ~ C64 I/O mode
		case 0x0F:	// $DF00-$DFFF ~ C64 I/O mode
			if (vic_registers[0x30] & 1)
				return colour_ram[addr - 0x0800];
			if (XEMU_LIKELY(sd_status & SD_ST_MAPPED))
				return sd_buffer[addr - 0x0E00];
			return 0xFF;	// I/O exp is not supported
		case 0x1E:	// $DE00-$DEFF ~ C65 I/O mode
		case 0x1F:	// $DF00-$DFFF ~ C65 I/O mode
			if (vic_registers[0x30] & 1)
				return colour_ram[addr - 0x1800];
			if (XEMU_LIKELY(sd_status & SD_ST_MAPPED))
				return sd_buffer[addr - 0x1E00];
			return 0xFF;	// I/O exp is not supported
		case 0x3E:	// $DE00-$DEFF ~ M65 I/O mode
		case 0x3F:	// $DF00-$DFFF ~ M65 I/O mode
			if (vic_registers[0x30] & 1)
				return colour_ram[addr - 0x3800];
			if (XEMU_LIKELY(sd_status & SD_ST_MAPPED))
				return sd_buffer[addr - 0x3E00];
			return 0xFF;	// I/O exp is not supported
		/* --------------------------------------------------------------- */
		/* $2xxx I/O area is not supported: FIXME: what is that for real?! */
		/* --------------------------------------------------------------- */
		case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
		case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
			return 0xFF;
		default:
			FATAL("Xemu internal error: undecoded I/O area reading for address $(%X)%03X", addr >> 8, addr & 0xFFF);
	}
}



/* Please read comments at io_read() above, those apply here too.
   In nutshell: this function *NEEDS* addresses 0-$3FFF based on the given I/O (VIC) mode! */
void io_write ( unsigned int addr, Uint8 data )
{
	// DEBUG("IO: write $%03X with data $%02X IO_mode is %d @ PC=$%04X" NL, addr & 0xFFF, data, addr >> 12, cpu65.pc);
	if (XEMU_UNLIKELY(cpu_rmw_old_data >= 0)) {
		// RMW handling! FIXME: do this only in the needed I/O ports only, not here, globally!
		// however, for that, we must check this at every devices where it can make any difference ...
		Uint8 old_data = cpu_rmw_old_data;
		cpu_rmw_old_data = -1;	// set this back to minus _before_ doing self-call, or it would cause an infinite recursion-like madness ...
		DEBUG("RMW: I/O internal addr %04X old data %02X new data %02X" NL, addr, old_data, data);
		io_write(addr, old_data);
	}
	switch (addr >> 8) {
		/* ---------------------------------------------------- */
		/* $D000-$D3FF: VIC-II, VIC-III+FDC+REC, VIC-IV+FDC+REC */
		/* ---------------------------------------------------- */
		case 0x00:	// $D000-$D0FF ~ C64 I/O mode
		case 0x01:	// $D100-$D1FF ~ C64 I/O mode
		case 0x02:	// $D200-$D2FF ~ C64 I/O mode
		case 0x03:	// $D300-$D3FF ~ C64 I/O mode
			vic_write_reg((addr & 0x3F) | 0x100, data);	// VIC-II write register
			return;
		case 0x10:	// $D000-$D0FF ~ C65 I/O mode
			addr &= 0xFF;
			if (XEMU_LIKELY(addr < 0x80)) {
				vic_write_reg(addr | 0x80, data);	// VIC-III write register
				return;
			}
			if (XEMU_LIKELY(addr < 0xA0)) {
				fdc_write_reg(addr & 0xF, data);
				return;
			}
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("RAM expansion controller");
		case 0x30:	// $D000-$D0FF ~ M65 I/O mode
			addr &= 0xFF;
			if (XEMU_LIKELY(addr < 0x80)) {
				vic_write_reg(addr, data);		// VIC-IV write register
				return;
			}
			if (XEMU_LIKELY(addr < 0xA0)) {
				fdc_write_reg(addr & 0xF, data);
				return;
			}
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("RAM expansion controller");
		case 0x11:	// $D100-$D1FF ~ C65 I/O mode
			vic3_write_palette_reg_red(addr, data);		// function takes care using only 8 low bits of addr, no need to do here
			return;
		case 0x12:	// $D200-$D2FF ~ C65 I/O mode
			vic3_write_palette_reg_green(addr, data);
			return;
		case 0x13:	// $D300-$D3FF ~ C65 I/O mode
			vic3_write_palette_reg_blue(addr, data);
			return;
		case 0x31:	// $D100-$D1FF ~ M65 I/O mode
			vic4_write_palette_reg_red(addr, data);
			return;
		case 0x32:	// $D200-$D2FF ~ M65 I/O mode
			vic4_write_palette_reg_green(addr, data);
			return;
		case 0x33:	// $D300-$D3FF ~ M65 I/O mode
			vic4_write_palette_reg_blue(addr, data);
			return;
		/* ------------------------------------------------ */
		/* $D400-$D7FF: SID, SID+UART+DMA, SID+UART+DMA+M65 */
		/* ------------------------------------------------ */
		case 0x04:	// $D400-$D4FF ~ C64 I/O mode
		case 0x05:	// $D500-$D5FF ~ C64 I/O mode
		case 0x06:	// $D600-$D6FF ~ C64 I/O mode
		case 0x07:	// $D700-$D7FF ~ C64 I/O mode
		case 0x14:	// $D400-$D4FF ~ C65 I/O mode
		case 0x15:	// $D500-$D5FF ~ C65 I/O mode
		case 0x34:	// $D400-$D4FF ~ M65 I/O mode
		case 0x35:	// $D500-$D5FF ~ M65 I/O mode
			//sid_write_reg(addr & 0x40 ? &sid[1] : &sid[0], addr & 31, data);
			//DEBUGPRINT("SID #%d reg#%02X data=%02X" NL, (addr >> 5) & 3, addr & 0x1F, data);
			//sid_write_reg(&sid[(addr >> 5) & 3], addr & 0x1F, data);
			audio65_sid_write(addr, data); // We need full addr, audio65_sid_write will decide the SID instance from that!
			return;
		case 0x16:	// $D600-$D6FF ~ C65 I/O mode
			if ((addr & 0xFF) == 0x07) {
				port_d607 = data;
				return;
			} else
				RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("UART");	// FIXME: UART is not yet supported!
		case 0x36:	// $D600-$D6FF ~ M65 I/O mode
			addr &= 0xFF;
			if (!in_hypervisor && addr >= 0x40 && addr <= 0x7F) {
				// In user mode, writing to $D640-$D67F (in VIC4 iomode) causes to enter hypervisor mode with
				// the trap number given by the offset in this range
				hypervisor_enter_via_write_trap(addr & 0x3F);
				return;
			}
			D6XX_registers[addr] = data;	// I guess, the actual write won't happens if it was trapped, so I moved this to here after the previous "if"
			if (addr < 9) {
				if (addr == 7) {
					port_d607 = data;
					return;
				} else
					RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("UART");	// FIXME: UART is not yet supported!
			}
			if (addr >= 0x80 && addr <= 0x93) {			// SDcard controller etc of MEGA65
				sdcard_write_register(addr - 0x80, data);
				return;
			}
			if ((addr & 0xF0) == 0xE0) {
				eth65_write_reg(addr, data);
				return;
			}
			switch (addr) {
				case 0x10:	// ASCII kbd last press value to zero whatever the written data would be
					hwa_kbd_move_next();
					return;
				case 0x15:
				case 0x16:
				case 0x17:
					virtkey(addr - 0x15, data & 0x7F);
					return;
				case 0x7C:					// hypervisor serial monitor port
					hypervisor_serial_monitor_push_char(data);
					return;
				case 0x7D:
					DEBUG("MEGA65: features set as $%02X" NL, data);
					if ((data & 2) != cpu_mega65_opcodes) {
						DEBUG("MEGA65: enhanced opcodes have been turned %s." NL, data & 2 ? "ON" : "OFF");
						cpu_mega65_opcodes = data & 2;
					}
					if ((data & 4) != rom_protect) {
						DEBUG("MEGA65: ROM protection has been turned %s." NL, data & 4 ? "ON" : "OFF");
						rom_protect = data & 4;
					}
					return;
				case 0x7E:
					D6XX_registers[0x7E] = 0xFF;	// iomap.txt: "Hypervisor already-upgraded bit (sets permanently)"
					DEBUG("MEGA65: Writing already-kicked register $%04X!" NL, addr);
					hypervisor_debug_invalidate("$D67E was written, maybe new kickstart will boot!");
					return;
				case 0x7F:	// hypervisor leave
					hypervisor_leave();	// 0x67F is also handled on enter's state, so it will be executed only in_hypervisor mode, which is what I want
					return;
				case 0x32:
				case 0x33:
				case 0x34:
				case 0x35:
					if (data == (cpld_firmware_version[addr - 0x32] ^ fpga_firmware_version[addr - 0x32])) {
						DEBUG("QUERY: before gating: %X" NL, xemu_query_gate);
						xemu_query_gate |= (1 << (addr - 0x32));
						if (xemu_query_gate == 0xF)
							xemu_query_interface_p = xemu_query_interface_str;
					} else {
						xemu_query_gate = 0;
					}
					DEBUG("QUERY: $D6%02X reg written with data %02X excepted %02X gate is %1X ptr is %p" NL,
							addr, data, cpld_firmware_version[addr - 0x32] ^ fpga_firmware_version[addr - 0x32],
							xemu_query_gate, xemu_query_interface_p);
					return;
				default:
					DEBUG("MEGA65: this I/O port is not emulated in Xemu yet: $D6%02X (tried to be written with $%02X)" NL, addr, data);
					return;
			}
			return;
		case 0x17:	// $D700-$D7FF ~ C65 I/O mode
			// FIXME: really a partial deconding like this? really on every 16 bytes?!
			dma_write_reg(addr & 0xF, data);
			return;
		case 0x37:	// $D700-$D7FF ~ M65 I/O mode
			// FIXME: this is probably very bad! I guess DMA does not decode for every 16 addresses ... Proposed fix is here:
			addr &= 0xFF;
			if (addr < 16)
				dma_write_reg(addr & 0xF, data);
			//else if (XEMU_UNLIKELY((addr & 0xF0) == BIGMULT_ADDR))
			else if (addr >= 0x68 && addr <= 0x7F)
				bigmult_valid_result = 0;
			D7XX[addr] = data;
			return;
		/* ----------------------- */
		/* $D800-$DBFF: COLOUR RAM */
		/* ----------------------- */
		case 0x08:	// $D800-$D8FF ~ C64 I/O mode
		case 0x09:	// $D900-$D9FF ~ C64 I/O mode
		case 0x0A:	// $DA00-$DAFF ~ C64 I/O mode
		case 0x0B:	// $DB00-$DBFF ~ C64 I/O mode
			colour_ram[addr - 0x0800] = data & 0xF;		// FIXME: is this true, that high nibble is masked, so switching to C65/M65 mode high nibble will be zero??
			return;
		case 0x18:	// $D800-$D8FF ~ C65 I/O mode
		case 0x19:	// $D900-$D9FF ~ C65 I/O mode
		case 0x1A:	// $DA00-$DAFF ~ C65 I/O mode
		case 0x1B:	// $DB00-$DBFF ~ C65 I/O mode
			colour_ram[addr - 0x1800] = data;
			return;
		case 0x38:	// $D800-$D8FF ~ M65 I/O mode
		case 0x39:	// $D900-$D9FF ~ M65 I/O mode
		case 0x3A:	// $DA00-$DAFF ~ M65 I/O mode
		case 0x3B:	// $DB00-$DBFF ~ M65 I/O mode
			colour_ram[addr - 0x3800] = data;
			return;
		/* --------------------------------------- */
		/* $DC00-$DCFF: CIA#1, EXTENDED COLOUR RAM */
		/* --------------------------------------- */
		case 0x0C:	// $DC00-$DCFF ~ C64 I/O mode
			if (vic_registers[0x30] & 1)
				colour_ram[addr - 0x0800] = data;
			else
				cia_write(&cia1, addr & 0xF, data);
			return;
		case 0x1C:	// $DC00-$DCFF ~ C65 I/O mode
			if (vic_registers[0x30] & 1)
				colour_ram[addr - 0x1800] = data;
			else
				cia_write(&cia1, addr & 0xF, data);
			return;
		case 0x3C:	// $DC00-$DCFF ~ M65 I/O mode
			if (vic_registers[0x30] & 1)
				colour_ram[addr - 0x3800] = data;
			else
				cia_write(&cia1, addr & 0xF, data);
			return;
		/* --------------------------------------- */
		/* $DD00-$DDFF: CIA#2, EXTENDED COLOUR RAM */
		/* --------------------------------------- */
		case 0x0D:	// $DD00-$DDFF ~ C64 I/O mode
			if (vic_registers[0x30] & 1)
				colour_ram[addr - 0x0800] = data;
			else
				cia_write(&cia2, addr & 0xF, data);
			return;
		case 0x1D:	// $DD00-$DDFF ~ C65 I/O mode
			if (vic_registers[0x30] & 1)
				colour_ram[addr - 0x1800] = data;
			else
				cia_write(&cia2, addr & 0xF, data);
			return;
		case 0x3D:	// $DD00-$DDFF ~ M65 I/O mode
			if (vic_registers[0x30] & 1)
				colour_ram[addr - 0x3800] = data;
			else
				cia_write(&cia2, addr & 0xF, data);
			return;
		/* --------------------------------------------------- */
		/* $DE00-$DFFF: IO exp, EXTENDED COLOUR RAM, SD buffer */
		/* --------------------------------------------------- */
		case 0x0E:	// $DE00-$DEFF ~ C64 I/O mode
		case 0x0F:	// $DF00-$DFFF ~ C64 I/O mode
			if (vic_registers[0x30] & 1) {
				colour_ram[addr - 0x0800] = data;
				return;
			}
			if (XEMU_LIKELY(sd_status & SD_ST_MAPPED)) {
				sd_buffer[addr - 0x0E00] = data;
				return;
			}
			return;		// I/O exp is not supported
		case 0x1E:	// $DE00-$DEFF ~ C65 I/O mode
		case 0x1F:	// $DF00-$DFFF ~ C65 I/O mode
			if (vic_registers[0x30] & 1) {
				colour_ram[addr - 0x1800] = data;
				return;
			}
			if (XEMU_LIKELY(sd_status & SD_ST_MAPPED)) {
				sd_buffer[addr - 0x1E00] = data;
				return;
			}
			return;		// I/O exp is not supported
		case 0x3E:	// $DE00-$DEFF ~ M65 I/O mode
		case 0x3F:	// $DF00-$DFFF ~ M65 I/O mode
			if (vic_registers[0x30] & 1) {
				colour_ram[addr - 0x3800] = data;
				return;
			}
			if (XEMU_LIKELY(sd_status & SD_ST_MAPPED)) {
				sd_buffer[addr - 0x3E00] = data;
				return;
			}
			return;		// I/O exp is not supported
		/* --------------------------------------------------------------- */
		/* $2xxx I/O area is not supported: FIXME: what is that for real?! */
		/* --------------------------------------------------------------- */
		case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
		case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
			return;
		default:
			FATAL("Xemu internal error: undecoded I/O area writing for address $(%X)%03X and data $%02X", addr >> 8, addr & 0xFFF, data);
	}
}



Uint8 io_dma_reader ( int addr ) {
	return io_read(addr | (vic_iomode << 12));
}

void  io_dma_writer ( int addr, Uint8 data ) {
	io_write(addr | (vic_iomode << 12), data);
}
