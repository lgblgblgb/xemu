/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   I/O decoding part (used by memory_mapper.h and DMA mainly)
   Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "matrix_mode.h"
#include "audio65.h"
#include "configdb.h"
#include "mega65.h"


int    fpga_switches = 0;		// State of FPGA board switches (bits 0 - 15), set switch 12 (hypervisor serial output)
Uint8  D6XX_registers[0x100];		// mega65 specific D6XX range, excluding the UART part (not used here!)
Uint8  D7XX[0x100];			// FIXME: hack for future M65 stuffs like ALU!
static Uint8 hw_errata_level = 0;
struct Cia6526 cia1, cia2;		// CIA emulation structures for the two CIAs
int    cpu_mega65_opcodes = 0;	// used by the CPU emu as well!
static int bigmult_valid_result = 0;
int    port_d607 = 0xFF;			// ugly hack to be able to read extra char row of C65 keyboard
int    core_age_in_days;


static const Uint8 fpga_firmware_version[] = { 'X','e','m','u' };
static const Uint8 cpld_firmware_version[] = { 'N','o','w','!' };


#define RETURN_ON_IO_READ_NOT_IMPLEMENTED(func, fb) \
	do { DEBUG("IO: NOT IMPLEMENTED read (emulator lacks feature), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_WRITE_NOT_IMPLEMENTED(func) \
	do { DEBUG("IO: NOT IMPLEMENTED write (emulator lacks feature), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)


// ------------------ BEGIN: VDC specific ------------------


static Uint8 vdc_reg_sel = 0;
#define VDC_ENABLED() ((D7XX[0x10] & 4) && !in_hypervisor)
static Uint8 vdc_regs[0x40];


//#define VDC_DEBUG	DEBUGPRINT
#define VDC_DEBUG(...)

static XEMU_INLINE Uint8 *vdc2vicptr ( const Uint16 vdc_address )
{
	// The logic here is from mega65-core's VHDL source
	const unsigned int line = vdc_address / 80U;
	const unsigned int col  = vdc_address % 80U;
	const unsigned int viciv_line = line / 8U;
	const unsigned int result = (viciv_line * 640U) + (line % 8U) + (col * 8U);
	// Assuming hires bitmap screen always at address $40000
	return main_ram + result + 0x40000U;
}


static inline Uint8 vdc_read_register ( const Uint8 reg )
{
	VDC_DEBUG("VDC: reading register $%02X" NL, reg);
	if (reg == 0x1F) {	// reading the data register which means reading a byte through VDC
		Uint16 vdc_mem_addr = (vdc_regs[0x12] << 8) + vdc_regs[0x13];
		VDC_DEBUG("VDC: reading VRAM at VDC address $%04X" NL, vdc_mem_addr);
		const Uint8 result = *vdc2vicptr(vdc_mem_addr++);
		vdc_regs[0x12] = vdc_mem_addr >> 8;
		vdc_regs[0x13] = vdc_mem_addr & 0xFF;
		return result;
	}
	return vdc_regs[reg];
}


static inline void vdc_write_register ( const Uint8 reg, const Uint8 data )
{
	VDC_DEBUG("VDC: writing register $%02X with data $%02X" NL, reg, data);
	vdc_regs[reg] = data;
	if (reg == 0x1F) {	// writing the data register which means writing a byte through VDC
		Uint16 vdc_mem_addr = (vdc_regs[0x12] << 8) + vdc_regs[0x13];
		VDC_DEBUG("VDC: writing VRAM at VDC address $%04X with data $%02X" NL, vdc_mem_addr, data);
		*vdc2vicptr(vdc_mem_addr++) = data;
		vdc_regs[0x12] = vdc_mem_addr >> 8;
		vdc_regs[0x13] = vdc_mem_addr & 0xFF;
		return;
	}
	// FIXME: this block copy/write emulation is really bad. Though I am unsure if anyone ever uses/used it. If so, it must be fixed in the future.
	if (reg == 0x1E) {	// writing the count register triggers a block write or block copy operation
		// NOTE: block commands are totally not tested and missing many parts!!! Like the busy check
		// And maybe block "dimensions" as well. Also these ops are done in one step, stalling emulation. VERY BAD!
		Uint16 vdc_mem_addr = (vdc_regs[0x12] << 8) + vdc_regs[0x13];			// VDC memory update address
		int vdc_word_count = data;							// block copy fill/word count (the current register value: reg $1E)
		if (vdc_regs[0x18] & 0x80) {	// ---[ BLOCK COPY OPERATION ]----------
			Uint16 vdc_mem_addr_src = (vdc_regs[0x20] << 8) + vdc_regs[0x21];	// block copy source address
			VDC_DEBUG("VDC: block copy of VRAM at VDC addresses $%04X -> $%04X with count = $%X" NL, vdc_mem_addr_src, vdc_mem_addr, vdc_word_count);
			while (vdc_word_count-- > 0)
				*vdc2vicptr(vdc_mem_addr++) = *vdc2vicptr(vdc_mem_addr_src++);
			vdc_regs[0x20] = vdc_mem_addr_src >> 8;
			vdc_regs[0x21] = vdc_mem_addr_src & 0xFF;
		} else {			// ---[ BLOCK WRITE OPERATION ]---------
			VDC_DEBUG("VDC: block write of RAM at VDC address $%04X with data = $%02X and count = $%X" NL, vdc_mem_addr, vdc_regs[0x1F], vdc_word_count);
			while (vdc_word_count-- > 0)
				*vdc2vicptr(vdc_mem_addr++) = vdc_regs[0x2A];			// normally attrib control, but on block write, this is the data to be written
		}
		vdc_regs[0x12] = vdc_mem_addr >> 8;
		vdc_regs[0x13] = vdc_mem_addr & 0xFF;
		return;
	}
}


// ------------------ END: VDC specific ------------------


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


// Writes the colour RAM. !!ONLY!! use this, if it's in the range of the first 2K of the colour RAM though, or you will be in big trouble!
static XEMU_INLINE void write_colour_ram ( const Uint32 addr, const Uint8 data )
{
	colour_ram[addr] = data;
	main_ram[addr + 0x1F800U] = data;
}


void set_hw_errata_level ( const Uint8 desired_level, const char *reason )
{
	const Uint8 level = desired_level > HW_ERRATA_MAX_LEVEL ? HW_ERRATA_MAX_LEVEL : desired_level;
	if (level == desired_level && level == hw_errata_level)
		return;
	DEBUGPRINT("HW_ERRATA: %u -> %u (wanted: %u, xemu-max: %u) [%s]" NL, hw_errata_level, level, desired_level, HW_ERRATA_MAX_LEVEL, reason);
	if (level == hw_errata_level)
		return;
	hw_errata_level = level;
	// --- Do the actual work, set things based on the new hardware errata level:
	vic4_set_errata_level(level);
}


void reset_hw_errata_level ( void )
{
	hw_errata_level = 0xFF;	// to force change on calling set_hw_errata_level()
	set_hw_errata_level(HW_ERRATA_RESET_LEVEL, "RESET");
}


/* Internal decoder for I/O reads. Address *must* be within the 0-$3FFF (!!) range. The low 12 bits is the actual address inside the I/O area,
   while the most significant nibble shows the I/O mode the operation is meant, according to the following table:
   0 = C64 (VIC-II) I/O mode
   1 = C65 (VIC-III) I/O mode
   2 = M65 (VIC-IV) I/O mode BUT with ethernet buffer visible at the second 2K of the I/O space!
   3 = M65 (VIC-IV) I/O mode.
   Even writing the "classic" I/O area at $D000 will ends up here. These addressed are stricly based on the VIC mode
   according the table above, no $D000 addresses here at all!
   FIXME: it seems I/O mode "2" may be not invalid, but some tricky stuff, C64 with extensions, or such. I have not so much idea yet :(
   NOTE: vic_read_reg() and vic_write_reg() is kinda special! It uses offset'ed register addresses for different VIC mode registers ... */

Uint8 io_read ( unsigned int addr )
{
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
		case 0x20:
			addr &= 0xFF;
			if (XEMU_LIKELY(addr < 0x80))
				return vic_read_reg(addr);		// VIC-IV read register
			if (addr == 0x8F)
				return hw_errata_level;
			if (XEMU_LIKELY(addr < 0xA0))
				return fdc_read_reg(addr & 0xF);
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("RAM expansion controller", 0xFF);
		case 0x11:	// $D100-$D1FF ~ C65 I/O mode
		case 0x12:	// $D200-$D2FF ~ C65 I/O mode
		case 0x13:	// $D300-$D3FF ~ C65 I/O mode
			return 0xFF;	// FIXME: AFAIK C65 does not allow to read palette register back, however M65 I/O mode does allow
		case 0x31:	// $D100-$D1FF ~ M65 I/O mode
		case 0x21:
			return vic4_read_palette_reg_red(addr);	// function takes care using only 8 low bits of addr, no need to do here
		case 0x32:	// $D200-$D2FF ~ M65 I/O mode
		case 0x22:
			return vic4_read_palette_reg_green(addr);
		case 0x33:	// $D300-$D3FF ~ M65 I/O mode
		case 0x23:
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
		case 0x24:
		case 0x25:
			switch (addr & 0x5F) {
				case 0x19: return get_mouse_x_via_sid();
				case 0x1A: return get_mouse_y_via_sid();
				case 0x1B: return rand();	// oscillator-3 status read/only, generate some random byte here, even if it's not correct
				case 0x1C: return rand();	// -- "" --
			}
			return 0xFF;
		case 0x16:	// $D600-$D6FF ~ C65 I/O mode
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("UART", 0xFF);	// FIXME: UART is not yet supported!
		case 0x36:	// $D600-$D6FF ~ M65 I/O mode
		case 0x26:
			addr &= 0xFF;
			if (addr <= 1 && VDC_ENABLED())					// $D600 or $D601 if VDC is enabled
				return addr ? vdc_read_register(vdc_reg_sel) : 0x80;	// 0x80 = always return with "READY" as VDC status if $D600 is read
			if (addr < 9)
				RETURN_ON_IO_READ_NOT_IMPLEMENTED("UART", 0xFF);	// FIXME: UART is not yet supported!
			if (addr >= 0x80 && addr <= 0x93)	// SDcard controller etc of MEGA65
				return sdcard_read_register(addr - 0x80);
			if ((addr & 0xF0) == 0xE0)
				return eth65_read_reg(addr);
			switch (addr) {
				case 0x7C:
					return 0;			// emulate the "UART is ready" situation (used by some HICKUPs around from v0.11 or so)
				case 0x7E:				// upgraded hypervisor signal
					if (D6XX_registers[0x7E] == 0x80)	// 0x80 means for Xemu (not for a real M65!): ask the user!
						D6XX_registers[0x7E] = QUESTION_WINDOW(
							"Not upgraded yet, it can do it|Already upgraded, I test hicked state",
							"HICKUP asks hypervisor upgrade state. What do you want Xemu to answer?\n"
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
					return hwa_kbd_get_queued_ascii();
				case 0x11:				// modifier keys on kbd being used
					return hwa_kbd_get_current_modkeys();
				case 0x13:				// $D613: direct access to the kbd matrix, read selected row (set by writing $D614), bit 0 = key pressed
					return kbd_directscan_query(D6XX_registers[0x14]);	// for further explanations please see this function in input_devices.c
				case 0x0A:
					return hwa_kbd_get_queued_modkeys();
				case 0x0F:
					// GS $D60F
					// bit 0: cursor left key is pressed
					// bit 1: cursor up key is pressed
					// bit 5: 1=real hardware, 0=emulation
					return kbd_query_leftup_status() | (!!configdb.realhw << 5);	// do bit 0/1 forming in input_devices.c, other bits should be zero, so it's ok to call only this
				case 0x1B:
					// D61B amiga / 1531 mouse auto-detect. FIXME XXX what value we should return at this point? :-O
					return 0xFF;
				case 0x19:
					return hwa_kbd_get_queued_petscii();
				case 0x20: // GS $D620 UARTMISC:POTAX Read Port A paddle X, without having to fiddle with SID/CIA settings.
				case 0x22: // GS $D622 UARTMISC:POTBX Read Port B paddle X, without having to fiddle with SID/CIA settings.
					return get_mouse_x_via_sid();
				case 0x21: // GS $D621 UARTMISC:POTAY Read Port A paddle Y, without having to fiddle with SID/CIA settings.
				case 0x23: // GS $D623 UARTMISC:POTBY Read Port B paddle Y, without having to fiddle with SID/CIA settings.
					return get_mouse_y_via_sid();
				case 0x29: // GS $D629: UARTMISC:M65MODEL MEGA65 model ID.
					return configdb.mega65_model;
				case 0x2A: // GS $D62A KBD:FWDATEL LSB of keyboard firmware date stamp (days since 1 Jan 2020)
				case 0x30: // GS $D630 FPGA:FWDATEL LSB of MEGA65 FPGA design date stamp (days since 1 Jan 2020)
					return core_age_in_days & 0xFF;
				case 0x2B: // GS $D62B KBD:FWDATEH MSB of keyboard firmware date stamp (days since 1 Jan 2020)
				case 0x31: // GS $D631 FPGA:FWDATEH MSB of MEGA65 FPGA design date stamp (days since 1 Jan 2020)
					return core_age_in_days >> 8;
				case 0x32: // D632-D635: FPGA firmware ID
				case 0x33:
				case 0x34:
				case 0x35:
					return fpga_firmware_version[addr - 0x32];
				case 0x2C: // D62C-D62F: CPLD firmware ID
				case 0x2D:
				case 0x2E:
				case 0x2F:
					return cpld_firmware_version[addr - 0x2C];
				case 0xDE: // D6DE: FPGA die temperatore, low byte: assuming to be 0, but the low nybble contains a kind of "noise" only
					return rand() & 0xF;
				case 0xDF: // D6DF: FPGA die temperature, high byte: assuming to be 164 (just because I see that on a real MEGA65 currently at my room's temperature ...)
					return 164;
				case 0xF4:
					return mixer_register;
				case 0xF5:
					return audio65_read_mixer_register();
				default:
					DEBUG("MEGA65: reading MEGA65 specific I/O @ $D6%02X result is $%02X" NL, addr, D6XX_registers[addr]);
					return D6XX_registers[addr];
			}
		case 0x17:	// $D700-$D7FF ~ C65 I/O mode
			// FIXME: really a partial deconding like this? really on every 16 bytes?!
			return dma_read_reg(addr & 0xF);
		case 0x37:	// $D700-$D7FF ~ M65 I/O mode
		case 0x27:
			// FIXME: this is probably very bad! I guess DMA does not decode for every 16 addresses ... Proposed fix is here:
			addr &= 0xFF;
			if (addr < 15)		// FIXME!!!! 0x0F was part of DMA reg array, but it seems now used by divisor busy stuff??
				return dma_read_reg(addr & 0xF);
			if (addr == 0x0F)
				return 0;	// FIXME: D70F bit 7 = 32/32 bits divisor busy flag, bit 6 = 32*32 mult busy flag. We're never busy, so the zero. But the OTHER bits??? Any purpose of those??
			if (addr == 0xEF)	// $D7EF CPU:RAND Hardware random number generator
				return rand() & 0xFF;
			if (addr == 0xFE)
				return D7XX[0xFE] & 0x7F;	// $D7FE.7 CPU:HWRNG!NOTRDY Hardware Real RNG random number not ready -> but we're always ready!
			if (addr == 0xFA)	// $D7FA CPU:FRAMECOUNT Count number of elapsed video frames
				return vic_frame_counter & 0xFFU;
			// ;) FIXME this is LAZY not to decode if we need to update bigmult at all ;-P
			if (XEMU_UNLIKELY(!bigmult_valid_result))
				update_hw_multiplier();
			return D7XX[addr];
		/* ----------------------------------------------- */
		/* $D800-$DFFF: in case of ethernet I/O mode only! */
		/* ----------------------------------------------- */
		case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
			return eth65_read_rx_buffer(addr - 0x2800);
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
		case 0x1C:	// $DC00-$DCFF ~ C65 I/O mode
		case 0x3C:	// $DC00-$DCFF ~ M65 I/O mode
			return (vic_registers[0x30] & 1) ? colour_ram[0x400 + (addr & 0xFF)] : cia_read(&cia1, addr & 0xF);
		/* --------------------------------------- */
		/* $DD00-$DDFF: CIA#2, EXTENDED COLOUR RAM */
		/* --------------------------------------- */
		case 0x0D:	// $DD00-$DDFF ~ C64 I/O mode
		case 0x1D:	// $DD00-$DDFF ~ C65 I/O mode
		case 0x3D:	// $DD00-$DDFF ~ M65 I/O mode
			return (vic_registers[0x30] & 1) ? colour_ram[0x500 + (addr & 0xFF)] : cia_read(&cia2, addr & 0xF);
		/* ----------------------------------------------------- */
		/* $DE00-$DFFF: IO exp, EXTENDED COLOUR RAM, disk buffer */
		/* ----------------------------------------------------- */
		case 0x0E:	// $DE00-$DEFF ~ C64 I/O mode
		case 0x0F:	// $DF00-$DFFF ~ C64 I/O mode
		case 0x1E:	// $DE00-$DEFF ~ C65 I/O mode
		case 0x1F:	// $DF00-$DFFF ~ C65 I/O mode
		case 0x3E:	// $DE00-$DEFF ~ M65 I/O mode
		case 0x3F:	// $DF00-$DFFF ~ M65 I/O mode
			// FIXME: is it really true for *ALL* I/O modes, that colour RAM expansion to 2K and
			// disk buffer I/O mapping works in all of them??
			if (vic_registers[0x30] & 1)
				return colour_ram[0x600 + (addr & 0x1FF)];
			if (XEMU_LIKELY(sd_status & SD_ST_MAPPED))
				return disk_buffer_io_mapped[addr & 0x1FF];
			return 0xFF;	// I/O exp is not supported
		default:
			FATAL("Xemu internal error: undecoded I/O area reading for address $(%X)%03X", addr >> 8, addr & 0xFFF);
	}
}



/* Please read comments at io_read() above, those apply here too.
   In nutshell: this function *NEEDS* addresses 0-$3FFF based on the given I/O (VIC) mode! */
void io_write ( unsigned int addr, Uint8 data )
{
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
		case 0x20:
			addr &= 0xFF;
			if (XEMU_LIKELY(addr < 0x80)) {
				vic_write_reg(addr, data);		// VIC-IV write register
				return;
			}
			if (addr == 0x8F) {
				set_hw_errata_level(data, "D08F change");
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
		case 0x21:
			vic4_write_palette_reg_red(addr, data);
			return;
		case 0x32:	// $D200-$D2FF ~ M65 I/O mode
		case 0x22:
			vic4_write_palette_reg_green(addr, data);
			return;
		case 0x33:	// $D300-$D3FF ~ M65 I/O mode
		case 0x23:
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
		case 0x24:
		case 0x25:
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
		case 0x26:
			addr &= 0xFF;
			if (addr <= 1 && VDC_ENABLED()) {			// $D600 or $D601 if VDC is enabled
				if (addr)
					vdc_write_register(vdc_reg_sel, data);	// register data to be written ($D601)
				else
					vdc_reg_sel = data & 0x3F;		// register selection (on write of $D600): bit7&6 are not used though
				return;
			}
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
			if (addr == 0xF4) {		// audio mixer co-efficient address
				mixer_register = data;
				return;
			}
			if (addr == 0xF5) {		// audio mixer co-efficient value
				audio65_write_mixer_register(data);
				return;
			}
			static int d6cf_exit_status = 0x42;
			switch (addr) {
				case 0x0A:	// write bit 7 is zero -> flush the queue
					if (!(data & 0x80))
						hwa_kbd_flush_queue();
					return;
				case 0x10:	// dequeue an item if ASCII or PETSCII hw accel kbd scanner registers are written
				case 0x19:
					hwa_kbd_move_next();
					return;
				case 0x11:
					hwa_kbd_disable_selector(data & 0x80);
					return;
				case 0x15:
				case 0x16:
				case 0x17:
					virtkey(addr - 0x15, data & 0x7F);
					return;
				case 0x72:	// "$D672.6 HCPU:MATRIXEN Enable composited Matrix Mode, and disable UART access to serial monitor."
					matrix_mode_toggle(!!(data & 0x40));
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
					memory_set_rom_protection(!!(data & 4));
					return;
				case 0x7E:
					D6XX_registers[0x7E] = 0xFF;	// iomap.txt: "Hypervisor already-upgraded bit (sets permanently)"
					DEBUG("MEGA65: Writing already-hicked register $%04X!" NL, addr);
					hypervisor_debug_invalidate("$D67E was written, maybe new HICKUP will boot!");
					return;
				case 0x7F:	// hypervisor leave
					hypervisor_leave();	// 0x67F is also handled on enter's state, so it will be executed only in_hypervisor mode, which is what I want
					return;
				case 0xCF:	// $D6CF - FPGA reconfiguration reg (if $42 is written). In testing mode, Xemu invents some new values here, though!
					if (data == 0x42) {
						if (configdb.testing) {	// in testing mode, writing $42 would mean to exit emulation!
							if (!emu_exit_code)
								emu_exit_code = d6cf_exit_status;
							XEMUEXIT(0);
							return;
						} else if (ARE_YOU_SURE("FPGA reconfiguration request. System must be reset.\nIs it OK to do now?\nAnswering NO may crash your program requesting this task though,\nor can result in endless loop of trying.", ARE_YOU_SURE_DEFAULT_YES)) {
							reset_mega65(RESET_MEGA65_HARD);
						}
					}
					d6cf_exit_status = data;
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
		case 0x27:
			// FIXME: this is probably very bad! I guess DMA does not decode for every 16 addresses ... Proposed fix is here:
			addr &= 0xFF;
			if (addr < 16)
				dma_write_reg(addr & 0xF, data);
			//else if (XEMU_UNLIKELY((addr & 0xF0) == BIGMULT_ADDR))
			else if (addr >= 0x68 && addr <= 0x7F)
				bigmult_valid_result = 0;
			D7XX[addr] = data;
			return;
		/* ----------------------------------------------- */
		/* $D800-$DFFF: in case of ethernet I/O mode only! */
		/* ----------------------------------------------- */
		case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
			eth65_write_tx_buffer(addr - 0x2800, data);
			return;
		/* ----------------------- */
		/* $D800-$DBFF: COLOUR RAM */
		/* ----------------------- */
		case 0x08:	// $D800-$D8FF ~ C64 I/O mode
		case 0x09:	// $D900-$D9FF ~ C64 I/O mode
		case 0x0A:	// $DA00-$DAFF ~ C64 I/O mode
		case 0x0B:	// $DB00-$DBFF ~ C64 I/O mode
			write_colour_ram(addr - 0x0800, data & 0xF);	// FIXME: is this true, that high nibble is masked, so switching to C65/M65 mode high nibble will be zero??
			return;
		case 0x18:	// $D800-$D8FF ~ C65 I/O mode
		case 0x19:	// $D900-$D9FF ~ C65 I/O mode
		case 0x1A:	// $DA00-$DAFF ~ C65 I/O mode
		case 0x1B:	// $DB00-$DBFF ~ C65 I/O mode
			write_colour_ram(addr - 0x1800, data);
			return;
		case 0x38:	// $D800-$D8FF ~ M65 I/O mode
		case 0x39:	// $D900-$D9FF ~ M65 I/O mode
		case 0x3A:	// $DA00-$DAFF ~ M65 I/O mode
		case 0x3B:	// $DB00-$DBFF ~ M65 I/O mode
			write_colour_ram(addr - 0x3800, data);
			return;
		/* --------------------------------------- */
		/* $DC00-$DCFF: CIA#1, EXTENDED COLOUR RAM */
		/* --------------------------------------- */
		case 0x0C:	// $DC00-$DCFF ~ C64 I/O mode
		case 0x1C:	// $DC00-$DCFF ~ C65 I/O mode
		case 0x3C:	// $DC00-$DCFF ~ M65 I/O mode
			if (vic_registers[0x30] & 1)
				write_colour_ram(0x400 + (addr & 0xFF), data);
			else
				cia_write(&cia1, addr & 0xF, data);
			return;
		/* --------------------------------------- */
		/* $DD00-$DDFF: CIA#2, EXTENDED COLOUR RAM */
		/* --------------------------------------- */
		case 0x0D:	// $DD00-$DDFF ~ C64 I/O mode
		case 0x1D:	// $DD00-$DDFF ~ C65 I/O mode
		case 0x3D:	// $DD00-$DDFF ~ M65 I/O mode
			if (vic_registers[0x30] & 1)
				write_colour_ram(0x500 + (addr & 0xFF), data);
			else
				cia_write(&cia2, addr & 0xF, data);
			return;
		/* ----------------------------------------------------- */
		/* $DE00-$DFFF: IO exp, EXTENDED COLOUR RAM, disk buffer */
		/* ----------------------------------------------------- */
		case 0x0E:	// $DE00-$DEFF ~ C64 I/O mode
		case 0x0F:	// $DF00-$DFFF ~ C64 I/O mode
		case 0x1E:	// $DE00-$DEFF ~ C65 I/O mode
		case 0x1F:	// $DF00-$DFFF ~ C65 I/O mode
		case 0x3E:	// $DE00-$DEFF ~ M65 I/O mode
		case 0x3F:	// $DF00-$DFFF ~ M65 I/O mode
			// FIXME: is it really true for *ALL* I/O modes, that colour RAM expansion to 2K and
			// disk buffer I/O mapping works in all of them??
			if (vic_registers[0x30] & 1) {
				write_colour_ram(0x600 + (addr & 0x1FF), data);
				return;
			}
			if (XEMU_LIKELY(sd_status & SD_ST_MAPPED)) {
				disk_buffer_io_mapped[addr & 0x1FF] = data;
				return;
			}
			return;		// I/O exp is not supported
		default:
			FATAL("Xemu internal error: undecoded I/O area writing for address $(%X)%03X and data $%02X", addr >> 8, addr & 0xFFF, data);
	}
}
