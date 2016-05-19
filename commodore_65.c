/* Test-case for a very simple and inaccurate and even not working Commodore 65 emulator.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the Commodore 65 emulation. Note: the purpose of this emulator is merely to
   test some 65CE02 opcodes, not for being a *usable* Commodore 65 emulator too much!
   If it ever able to hit the C65 BASIC-10 usage, I'll be happy :)

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

#include <stdio.h>

#include <SDL.h>

#include "commodore_65.h"
#include "cpu65ce02.h"
#include "cia6526.h"
#include "c65fdc.h"
#include "vic3.h"
#include "emutools.h"

//#define DEBUG_MEMORY
//#define DEBUG_STACK


Uint8 memory[0x110000];			// 65CE02 MAP'able address space (now overflow is not handled, that is the reason of higher than 1MByte, just in case ...)
static Uint8 cpu_port[2];		// CPU I/O port at 0/1 (implemented by the VIC3 for real, on C65 but for the usual - C64/6510 - name, it's the "CPU port")
static struct Cia6526 cia1, cia2;	// CIA emulation structures for the two CIAs
static Uint8 kbd_matrix[8];		// keyboard matrix state, 8 * 8 bits

// We re-map I/O requests to a high address space does not exist for real. cpu_read() and cpu_write() should handle this as an IO space request (with the lower 16 bits as addr from $D000)
#define IO_REMAP_VIRTUAL 0x110000
// Other re-mapping addresses
// Re-mapping for VIC3 reg $30
#define ROM_C000_REMAP		0x20000
#define ROM_8000_REMAP		0x30000
#define ROM_A000_REMAP		0x30000
#define ROM_E000_REMAP		0x30000
// Re-mapping for "CPU-port" related stuffs
#define ROM_C64_CHR_REMAP	0x20000
#define ROM_C64_KERNAL_REMAP	0x20000
#define ROM_C64_BASIC_REMAP	0x20000


static int addr_trans_rd[16];		// address translating offsets for READ operation (it can be added to the CPU address simply, selected by the high 4 bits of the CPU address)
static int addr_trans_wr[16];		// address translating offsets for WRITE operation (it can be added to the CPU address simply, selected by the high 4 bits of the CPU address)
static int map_mask;			// MAP mask, should be filled at the MAP opcode, *before* calling apply_memory_config() then
static int map_offset_low;		// MAP low offset, should be filled at the MAP opcode, *before* calling apply_memory_config() then
static int map_offset_high;		// MAP high offset, should be filled at the MAP opcode, *before* calling apply_memory_config() then



struct KeyMapping {
	SDL_Scancode	scan;		// SDL scancode for the given key we want to map
	Uint8		pos;		// BCD packed, high nibble / low nibble for col/row to map to.  0xFF means end of table!, high bit set on low nibble: press shift as well!
};

// Keyboard position of "shift" which is "virtually pressed" ie for cursor up/left
#define SHIFTED_CURSOR_SHIFT_POS	0x64

/* Notes:
	* This is _POSITIONAL_ mapping (not symbolic), assuming US keyboard layout for the host machine (ie: the machine you run this emulator)
	* Only 8*8 matrix is emulated currently, on C65 there is an "extra" line it seems
	* I was lazy to map some keys, see in the comments :)
*/
static const struct KeyMapping key_map[] = {
	{ SDL_SCANCODE_BACKSPACE,	0x00 },	// "backspace" for INS/DEL
	{ SDL_SCANCODE_RETURN,		0x01 }, // RETURN
	{ SDL_SCANCODE_RIGHT,		0x02 }, { SDL_SCANCODE_LEFT,	0x02 | 8 },	// Cursor Left / Right (Horizontal) [real key on C65 with the "auto-shift trick]
	{ SDL_SCANCODE_F7,		0x03 }, { SDL_SCANCODE_F8,	0x03 | 8 },	// Real C65 does not have "F8" (but DOES have cursor up...), these are just for fun :)
	{ SDL_SCANCODE_F1,		0x04 }, { SDL_SCANCODE_F2,	0x04 | 8 },
	{ SDL_SCANCODE_F3,		0x05 }, { SDL_SCANCODE_F4,	0x05 | 8 },
	{ SDL_SCANCODE_F5,		0x06 }, { SDL_SCANCODE_F6,	0x06 | 8 },
	{ SDL_SCANCODE_DOWN,		0x07 }, { SDL_SCANCODE_UP,	0x07 | 8 },	// Cursor Down / Up (Vertical) [real key on C65 with the "auto-shift" trick]
	{ SDL_SCANCODE_3,		0x10 },
	{ SDL_SCANCODE_W,		0x11 },
	{ SDL_SCANCODE_A,		0x12 },
	{ SDL_SCANCODE_4,		0x13 },
	{ SDL_SCANCODE_Z,		0x14 },
	{ SDL_SCANCODE_S,		0x15 },
	{ SDL_SCANCODE_E,		0x16 },
	{ SDL_SCANCODE_LSHIFT,		0x17 },
	{ SDL_SCANCODE_5,		0x20 },
	{ SDL_SCANCODE_R,		0x21 },
	{ SDL_SCANCODE_D,		0x22 },
	{ SDL_SCANCODE_6,		0x23 },
	{ SDL_SCANCODE_C,		0x24 },
	{ SDL_SCANCODE_F,		0x25 },
	{ SDL_SCANCODE_T,		0x26 },
	{ SDL_SCANCODE_X,		0x27 },
	{ SDL_SCANCODE_7,		0x30 },
	{ SDL_SCANCODE_Y,		0x31 },
	{ SDL_SCANCODE_G,		0x32 },
	{ SDL_SCANCODE_8,		0x33 },
	{ SDL_SCANCODE_B,		0x34 },
	{ SDL_SCANCODE_H,		0x35 },
	{ SDL_SCANCODE_U,		0x36 },
	{ SDL_SCANCODE_V,		0x37 },
	{ SDL_SCANCODE_9,		0x40 },
	{ SDL_SCANCODE_I,		0x41 },
	{ SDL_SCANCODE_J,		0x42 },
	{ SDL_SCANCODE_0,		0x43 },
	{ SDL_SCANCODE_M,		0x44 },
	{ SDL_SCANCODE_K,		0x45 },
	{ SDL_SCANCODE_O,		0x46 },
	{ SDL_SCANCODE_N,		0x47 },
	// FIXME: map something as +	0x50
	{ SDL_SCANCODE_P,		0x51 },
	{ SDL_SCANCODE_L,		0x52 },
	{ SDL_SCANCODE_MINUS,		0x53 },
	{ SDL_SCANCODE_PERIOD,		0x54 },
	{ SDL_SCANCODE_APOSTROPHE,	0x55 },	// mapped as ":"
	// FIXME: map something as @	0x56
	{ SDL_SCANCODE_COMMA,		0x57 },
	// FIXME: map something as pound0x60
	// FIXME: map something as *	0x61
	{ SDL_SCANCODE_SEMICOLON,	0x62 },
	{ SDL_SCANCODE_HOME,		0x63 },	// CLR/HOME
	{ SDL_SCANCODE_RSHIFT,		0x64 },
	{ SDL_SCANCODE_EQUALS,		0x65 },
	// FIXME: map something as Pi?	0x66
	{ SDL_SCANCODE_SLASH,		0x67 },
	{ SDL_SCANCODE_1,		0x70 },
	// FIXME: map sg. as <--	0x71
	{ SDL_SCANCODE_LCTRL,		0x72 },
	{ SDL_SCANCODE_2,		0x73 },
	{ SDL_SCANCODE_SPACE,		0x74 },
	{ SDL_SCANCODE_LALT,		0x75 },	// Commodore key, PC kbd sux, does not have C= key ... Mapping left ALT as the C= key
	{ SDL_SCANCODE_Q,		0x76 },
	{ SDL_SCANCODE_END,		0x77 },	// RUN STOP key, we map 'END' as this key
	// **** this must be the last line: end of mapping table ****
	{ 0, 0xFF }
};


#ifdef DEBUG_STACK
static int stackguard_address = -1;
static Uint8 stackguard_data = 0;
static Uint8 cpu_old_sp;
static Uint16 cpu_old_pc_my;
static void DEBUG_WRITE_ACCESS ( int physaddr, Uint8 data )
{
	if (
		(physaddr >= 0x100 && physaddr < 0x200) ||
		(physaddr >= 0x10100 && physaddr < 0x10200)
	) {
		stackguard_address = physaddr;
		stackguard_data = data;
	}
}
#else
#define DEBUG_WRITE_ACCESS(unused1,unused2)
#endif



/* You *MUST* call this every time, when *any* of these events applies:
   * MAP 4510 opcode is issued, map_offset_low, map_offset_high, map_mask are modified
   * "CPU port" data or DDR register has been written, witn cpu_port[0 or 1] modified
   * VIC3 register $30 is written, with vic3_registers[0x30] modified 
   The reason of this madness: do the ugly work here, as memory configuration change is
   less frequent than memory usage (read/write). Thus do more work here, but simplier
   work when doing actual memory read/writes, with a simple addition and shift, or such.
   The tables are 4K in steps, 4510 would require only 8K steps, but there are other
   reasons (ie, I/O area is only 4K long, mapping is not done by the CPU).
   More advanced technique can be used not to handle *everything* here, but it's better
   for the initial steps, to have all address translating logic at once.
*/
void apply_memory_config ( void )
{
	// FIXME: what happens if VIC-3 reg $30 mapped ROM is tried to be written? Ignored, or RAM is used to write to, as with the CPU port mapping?
	// About the produced signals on the "CPU port"
	int cp = (cpu_port[1] | (~cpu_port[0]));
	// Simple ones, only CPU MAP may apply not other factors
	// Also, these are the "lower" blocks, needs the offset for the "lower" area in case of CPU MAP'ed state
	addr_trans_wr[0] = addr_trans_rd[0] = addr_trans_wr[1] = addr_trans_rd[1] = (map_mask & 1) ? map_offset_low : 0;	// $0XXX + $1XXX, MAP block 0 [mask 1]
	addr_trans_wr[2] = addr_trans_rd[2] = addr_trans_wr[3] = addr_trans_rd[3] = (map_mask & 2) ? map_offset_low : 0;	// $2XXX + $3XXX, MAP block 1 [mask 2]
	addr_trans_wr[4] = addr_trans_rd[4] = addr_trans_wr[5] = addr_trans_rd[5] = (map_mask & 4) ? map_offset_low : 0;	// $4XXX + $5XXX, MAP block 2 [mask 4]
	addr_trans_wr[6] = addr_trans_rd[6] = addr_trans_wr[7] = addr_trans_rd[7] = (map_mask & 8) ? map_offset_low : 0;	// $6XXX + $7XXX, MAP block 3 [mask 8]
	// From this point, we must use the "high" area offset if it's CPU MAP'ed
	// $8XXX and $9XXX, MAP block 4 [mask 16]
	if (vic3_registers[0x30] & 8)
		addr_trans_wr[8] = addr_trans_rd[8] = addr_trans_wr[9] = addr_trans_rd[9] = ROM_8000_REMAP;
	else if (map_mask & 16)
		addr_trans_wr[8] = addr_trans_rd[8] = addr_trans_wr[9] = addr_trans_rd[9] = map_offset_high;
	else
		addr_trans_wr[8] = addr_trans_rd[8] = addr_trans_wr[9] = addr_trans_rd[9] = 0;
	// $AXXX and $BXXX, MAP block 5 [mask 32]
	if (vic3_registers[0x30] & 16)
		addr_trans_wr[0xA] = addr_trans_rd[0xA] = addr_trans_wr[0xB] = addr_trans_rd[0xB] = ROM_A000_REMAP;
	else if ((map_mask & 32))
		addr_trans_wr[0xA] = addr_trans_rd[0xA] = addr_trans_wr[0xB] = addr_trans_rd[0xB] = map_offset_high;
	else {
		addr_trans_wr[0xA] = addr_trans_wr[0xB] = 0;
		addr_trans_rd[0xA] = addr_trans_rd[0xB] = ((cp & 3) == 3) ? ROM_C64_BASIC_REMAP : 0;
	}
	// $CXXX, MAP block 6 [mask 64]
	// Warning: all VIC3 reg $30 related ROM maps are for 8K size, *expect* of '@C000' (interface ROM) which is only 4K! Also this is in another ROM bank than the others
	if (vic3_registers[0x30] & 32)
		addr_trans_wr[0xC] = addr_trans_rd[0xC] = ROM_C000_REMAP;
	else
		addr_trans_wr[0xC] = addr_trans_rd[0xC] = (map_mask & 64) ? map_offset_high : 0;
	// $DXXX, *still* MAP block 6 [mask 64]
	if (map_mask & 64)
		addr_trans_wr[0xD] = addr_trans_rd[0xD] = map_offset_high;
	else {
		if ((cp & 7) > 4) {
			addr_trans_wr[0xD] = addr_trans_rd[0xD] = IO_REMAP_VIRTUAL;
		} else {
			addr_trans_wr[0xD] = 0;
			addr_trans_rd[0xD] = (cp & 3) ? ROM_C64_CHR_REMAP : 0;
		}
	}
	// $EXXX and $FXXX, MAP block 7 [mask 128]
	if (vic3_registers[0x30] & 128)
		addr_trans_wr[0xE] = addr_trans_rd[0xE] = addr_trans_wr[0xF] = addr_trans_rd[0xF] = ROM_E000_REMAP;
	else if (map_mask & 128)
		addr_trans_wr[0xE] = addr_trans_rd[0xE] = addr_trans_wr[0xF] = addr_trans_rd[0xF] = map_offset_high;
	else {
		addr_trans_wr[0xE] = addr_trans_wr[0xF] = 0;
		addr_trans_rd[0xE] = addr_trans_rd[0xF] = ((cp & 3) > 1) ? ROM_C64_KERNAL_REMAP : 0;
	}
}



static void cia_setint_cb ( int level )
{
	printf("%s: IRQ level changed to %d" NL, cia1.name, level);
	if (level)
		cpu_irqLevel |= 1;
	else
		cpu_irqLevel &= ~1;
}



void clear_emu_events ( void )
{
	memset(kbd_matrix, 0xFF, sizeof kbd_matrix);	// initialize keyboard matrix [bit 1 = unpressed, thus 0xFF for a line]
}


#define KBSEL cia1.PRA


static Uint8 cia_read_keyboard ( Uint8 ddr_mask_unused )
{
	return
		((KBSEL &   1) ? 0xFF : kbd_matrix[0]) &
		((KBSEL &   2) ? 0xFF : kbd_matrix[1]) &
		((KBSEL &   4) ? 0xFF : kbd_matrix[2]) &
		((KBSEL &   8) ? 0xFF : kbd_matrix[3]) &
		((KBSEL &  16) ? 0xFF : kbd_matrix[4]) &
		((KBSEL &  32) ? 0xFF : kbd_matrix[5]) &
		((KBSEL &  64) ? 0xFF : kbd_matrix[6]) &
		((KBSEL & 128) ? 0xFF : kbd_matrix[7])
	;
}



static void c65_init ( void )
{
	clear_emu_events();
	// *** Init memory space
	memset(memory, 0xFF, sizeof memory);
	// *** Load ROM image
	if (emu_load_file("c65-system.rom", memory + 0x20000, 0x20001) != 0x20000)
		FATAL("Cannot load C65 system ROM!");
	// *** Initialize VIC3
	vic3_init();
	// *** Memory configuration
	cpu_port[0] = cpu_port[1] = 0xFF;	// the "CPU I/O port" on 6510/C64, implemented by VIC3 for real in C65!
	map_mask = 0;				// as all 8K blocks are unmapped, we don't need to worry about the low/high offset to set here
	apply_memory_config();			// VIC3 $30 reg is already filled, so it's OK to call this now
	// *** CIAs
	cia_init(&cia1, "CIA-1",
		NULL,	// callback: OUTA(mask, data)
		NULL,	// callback: OUTB(mask, data)
		NULL,	// callback: OUTSR(mask, data)
		NULL,	// callback: INA(mask)
		cia_read_keyboard,	// callback: INB(mask)
		NULL,	// callback: INSR(mask)
		cia_setint_cb	// callback: SETINT(level)
	);
	cia_init(&cia2, "CIA-2",
		NULL,	// callback: OUTA(mask, data)
		NULL,	// callback: OUTB(mask, data)
		NULL,	// callback: OUTSR(mask, data)
		NULL,	// callback: INA(mask)
		NULL,	// callback: INB(mask)
		NULL,	// callback: INSR(mask)
		NULL	// callback: SETINT(level)	that would be NMI in our case
	);
	// *** RESET CPU, also fetches the RESET vector into PC
	cpu_reset();
	puts("INIT: end of initialization!");
}



// *** Implements the MAP opcode of 4510, called by the 65CE02 emulator
void cpu_do_aug ( void )
{
	cpu_inhibit_interrupts = 1;	// disable interrupts to the next "EOM" (ie: NOP) opcode
	printf("CPU: MAP opcode, input A=$%02X X=$%02X Y=$%02X Z=$%02X" NL, cpu_a, cpu_x, cpu_y, cpu_z);
	map_offset_low  = (cpu_a << 8) | ((cpu_x & 15) << 16);	// offset of lower half (blocks 0-3)
	map_offset_high = (cpu_y << 8) | ((cpu_z & 15) << 16);	// offset of higher half (blocks 4-7)
	map_mask        = (cpu_z & 0xF0) | (cpu_x >> 4);	// "is mapped" mask for blocks (1 bit for each)
	puts("MEM: applying new memory configuration because of MAP CPU opcode");
	printf("LOW -OFFSET = $%X" NL, map_offset_low);
	printf("HIGH-OFFSET = $%X" NL, map_offset_high);
	printf("MASK        = $%02X" NL, map_mask);
	apply_memory_config();
}



// *** Implements the EOM opcode of 4510, called by the 65CE02 emulator
void cpu_do_nop ( void )
{
	if (cpu_inhibit_interrupts) {
		cpu_inhibit_interrupts = 0;
		puts("CPU: EOM, interrupts were disabled because of MAP till the EOM");
	} else
		puts("CPU: NOP not reated as EOM (no MAP before)");
}



// IO: redirect to IO space? if 1, and $D000 area (??)
// DIR: direction if 0 -> increment
// MOD: UNKNOWN details (modulo)
// HLD: hold address if 0 -> don't hold (so: inc/dec based on DIR)
#define DMA_IO(p)  ((p) & 0x800000)
#define DMA_DIR(p) ((p) & 0x400000)
#define DMA_MOD(p) ((p) & 0x200000)
#define DMA_HLD(p) ((p) & 0x100000)

#define DMA_NEXT_BYTE(p,ad) \
	if (DMA_HLD(p) == 0) { \
		ad += DMA_DIR(p) ? -1 : 1; \
	}


static void dma_write_reg ( int addr, Uint8 data )
{
	// DUNNO about DMAgic too much. It's merely guessing from my own ROM assembly tries, C65gs/Mega65 VHDL, and my ideas :)
	// Also, it DOES things while everything other (ie CPU) emulation is stopped ...
	static Uint8 dma_registers[4];
	Uint8 command; // DMAgic command
	int dma_list;
	dma_registers[addr & 3] = data;
	dma_list = dma_registers[0] | (dma_registers[1] << 8) | (dma_registers[2] << 16);
	if (addr)
		return;
	printf("DMA: list address is $%06X now, just written to register %d value $%02X" NL, dma_list, addr & 3, data);
	do {
		int source, target, length, spars, tpars;
		command = memory[dma_list++]      ;
		length  = memory[dma_list++]      ;
		length |= memory[dma_list++] <<  8;
		source	= memory[dma_list++]      ;
		source |= memory[dma_list++] <<  8;
		source |= memory[dma_list++] << 16;
		target  = memory[dma_list++]      ;
		target |= memory[dma_list++] <<  8;
		target |= memory[dma_list++] << 16;
		spars 	= source;
		tpars 	= target;
		source &= 0xFFFFF;
		target &= 0xFFFFF;
		printf("DMA: $%05X[%c%c%c%c] -> $%05X[%c%c%c%c] (L=$%04X) CMD=%d (%s)" NL,
			source, DMA_IO(spars) ? 'I' : 'i', DMA_DIR(spars) ? 'D' : 'd', DMA_MOD(spars) ? 'M' : 'm', DMA_HLD(spars) ? 'H' : 'h',
			target, DMA_IO(tpars) ? 'I' : 'i', DMA_DIR(tpars) ? 'D' : 'd', DMA_MOD(tpars) ? 'M' : 'm', DMA_HLD(tpars) ? 'H' : 'h',
			length, command & 3, (command & 4) ? "chain" : "last"
		);
		if ((command & 3) == 3) {		// fill command?
			while (length--) {
				if (target < 0x20000 && target >= 0) {
					DEBUG_WRITE_ACCESS(target, data);
					memory[target] = source & 0xFF;
				}
				//DMA_NEXT_BYTE(spars, source);	// DOES it have any sense? Maybe to write linear pattern of bytes? :-P
				DMA_NEXT_BYTE(tpars, target);
			}
		} else if ((command & 3) == 0) {	// copy command?
			while (length--) {
				Uint8 data = ((source < 0x40000 && source >= 0) ? memory[source] : 0xFF);
				DMA_NEXT_BYTE(spars, source);
				if (target < 0x20000 && target >= 0) {
					DEBUG_WRITE_ACCESS(target, data);
					memory[target] = data;
				}
				DMA_NEXT_BYTE(tpars, target);
			}
		} else
			puts("DMA: unimplemented command!!");
	} while (command & 4);	// chained? continue if so!
}


#define RETURN_ON_IO_READ_NOT_IMPLEMENTED(func, fb) \
	do { printf("IO: NOT IMPLEMENTED read (emulator lacks feature), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_READ_NO_NEW_VIC_MODE(func, fb) \
	do { printf("IO: ignored read (not new VIC mode), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_WRITE_NOT_IMPLEMENTED(func) \
	do { printf("IO: NOT IMPLEMENTED write (emulator lacks feature), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)
#define RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE(func) \
	do { printf("IO: ignored write (not new VIC mode), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)



// Call this ONLY with addresses between $D000-$DFFF
// Ranges marked with (*) needs "vic_new_mode"
static Uint8 io_read ( int addr )
{
	if (addr < 0xD080)	// $D000 - $D07F:	VIC3
		return vic3_read_reg(addr);
	if (addr < 0xD0A0) {	// $D080 - $D09F	DISK controller (*)
		if (vic_new_mode) {
			return fdc_read_reg(addr & 0x1F);
#if 0
			/*if (addr == 0xD082 || addr == 0xD089) {
				puts("WARN: hacking 127 as the answer for reading $D082");
				return 16; // hack
			} */
			//return 0;
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("DISK controller", 0x7F);	// emulation stops here with 0xFF
#endif
		} else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("DISK controller", 0xFF);
	}
	if (addr < 0xD100) {	// $D0A0 - $D0FF	RAM expansion controller (*)
		if (vic_new_mode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("RAM expansion controller", 0xFF);
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("RAM expansion controller", 0xFF);
	}
	if (addr < 0xD400) {	// $D100 - $D3FF	palette red/green/blue nibbles (*)
		if (vic_new_mode)
			return 0xFF; // NOT READABLE!
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("palette reg/green/blue nibbles", 0xFF);
	}
	if (addr < 0xD440) {	// $D400 - $D43F	SID, right
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("right SID", 0xFF);
	}
	if (addr < 0xD600) {	// $D440 - $D5FF	SID, left
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("left SID", 0xFF);
	}
	if (addr < 0xD700) {	// $D600 - $D6FF	UART (*)
		if (vic_new_mode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("UART", 0xFF);
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("UART", 0xFF);
	}
	if (addr < 0xD800) {	// $D700 - $D7FF	DMA (*)
		if (vic_new_mode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("DMA controller", 0xFF);
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("DMA controller", 0xFF);
	}
	if (addr < ((vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00)) {	// $D800-$DC00/$E000	COLOUR NIBBLES, mapped to $1F800 in BANK1
		printf("IO: reading colour RAM at offset $%04X" NL, addr - 0xD800);
		return memory[0x1F800 + addr - 0xD800];
	}
	if (addr < 0xDD00) {	// $DC00 - $DCFF	CIA-1
		Uint8 result = cia_read(&cia1, addr & 0xF);
		//RETURN_ON_IO_READ_NOT_IMPLEMENTED("CIA-1", 0xFF);
		printf("%s: reading register $%X result is $%02X" NL, cia1.name, addr & 15, result);
		return result;
	}
	if (addr < 0xDE00) {	// $DD00 - $DDFF	CIA-2
		Uint8 result = cia_read(&cia2, addr & 0xF);
		//RETURN_ON_IO_READ_NOT_IMPLEMENTED("CIA-2", 0xFF);
		printf("%s: reading register $%X result is $%02X" NL, cia2.name, addr & 15, result);
		return result;
	}
	if (addr < 0xDF00) {	// $DE00 - $DEFF	IO-1 external
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("IO-1 external select", 0xFF);
	}
	// The rest: IO-2 external
	RETURN_ON_IO_READ_NOT_IMPLEMENTED("IO-2 external select", 0xFF);
}




// Call this ONLY with addresses between $D000-$DFFF
// Ranges marked with (*) needs "vic_new_mode"
static void io_write ( int addr, Uint8 data )
{
	if (addr < 0xD080)	// $D000 - $D07F:	VIC3
		return vic3_write_reg(addr, data);
	if (addr < 0xD0A0) {	// $D080 - $D09F	DISK controller (*)
		if (vic_new_mode) {
			fdc_write_reg(addr & 0x1F, data);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("DISK controller");
	}
	if (addr < 0xD100) {	// $D0A0 - $D0FF	RAM expansion controller (*)
		if (vic_new_mode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("RAM expansion controller");
		else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("RAM expansion controller");
	}
	if (addr < 0xD400) {	// $D100 - $D3FF	palette red/green/blue nibbles (*)
		if (vic_new_mode) {
			vic3_write_palette_reg(addr - 0xD100, data);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("palette red/green/blue nibbles");
	}
	if (addr < 0xD440) {	// $D400 - $D43F	SID, right
		RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("right SID");
	}
	if (addr < 0xD600) {	// $D440 - $D5FF	SID, left
		RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("left SID");
	}
	if (addr < 0xD700) {	// $D600 - $D6FF	UART (*)
		if (vic_new_mode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("UART");
		else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("UART");
	}
	if (addr < 0xD800) {	// $D700 - $D7FF	DMA (*)
		if (vic_new_mode) {
			// RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("DMA controller");
			dma_write_reg(addr & 3, data);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("DMA controller");
	}
	if (addr < ((vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00)) {	// $D800-$DC00/$E000	COLOUR NIBBLES, mapped to $1F800 in BANK1
		memory[0x1F800 + addr - 0xD800] = data;
       //return memory[0x1F800 + addr - 0xD800];
		printf("IO: writing colour RAM at offset $%04X" NL, addr - 0xD800);
		return;
	}
	if (addr < 0xDD00) {	// $DC00 - $DCFF	CIA-1
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("CIA-1");
		printf("%s: writing register $%X with data $%02X" NL, cia1.name, addr & 15, data);
		cia_write(&cia1, addr & 0xF, data);
		return;
	}
	if (addr < 0xDE00) {	// $DD00 - $DDFF	CIA-2
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("CIA-2");
		printf("%s: writing register $%X with data $%02X" NL, cia2.name, addr & 15, data);
		cia_write(&cia2, addr & 0xF, data);
		return;
	}
	if (addr < 0xDF00) {	// $DE00 - $DEFF	IO-1 external
		RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("IO-1 external select");
	}
	// The rest: IO-2 external
	RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("IO-2 external select");
}



// This function is called by the 65CE02 emulator in case of reading a byte (regardless of data or code)
Uint8 cpu_read ( Uint16 addr )
{
	int phys_addr = addr_trans_rd[addr >> 12] + addr;	// translating address with the table created by apply_memory_config()
	if (phys_addr >= IO_REMAP_VIRTUAL) {
		if ((addr & 0xF000) != 0xD000) {
			fprintf(stderr, "Internal error: IO is not on the IO space!\n");
			exit(1);
		}
		return io_read(addr);	// addr should be in $DXXX range to hit this, hopefully ...
	}
	if (phys_addr >= 0x40000)
		printf("MEM: WARN: addressing memory over ROM for reading: %X" NL, phys_addr);
	phys_addr &= 0xFFFFF;
	if (phys_addr < 2)
		return cpu_port[phys_addr & 1];
	if (phys_addr < 0x40000) {
#ifdef DEBUG_MEMORY
		printf("MEM: read @ $%04X [PC=$%04X] (REAL=$%05X) result is $%02X" NL, addr, cpu_pc, phys_addr, memory[phys_addr]);
#endif
		return memory[phys_addr];
	}
	printf("MEM: WARN: reading undecoded memory area @ $%04X [PC=$%04X] (REAL=$%05X) result is $%02X" NL, addr, cpu_pc, phys_addr, 0xFF);
	return 0xFF;
}



// This function is called by the 65CE02 emulator in case of writing a byte
void cpu_write ( Uint16 addr, Uint8 data )
{
	int phys_addr = addr_trans_wr[addr >> 12] + addr;	// translating address with the table created by apply_memory_config()
	if (phys_addr >= IO_REMAP_VIRTUAL) {
		if ((addr & 0xF000) != 0xD000) {
			fprintf(stderr, "Internal error: IO is not on the IO space!\n");
			exit(1);
		}
		io_write(addr, data);	// addr should be in $DXXX range to hit this, hopefully ...
		return;
	}
	if (phys_addr >= 0x40000)
		printf("MEM: WARN: addressing memory over ROM for writing: %X" NL, phys_addr);
	phys_addr &= 0xFFFFF;
	if (phys_addr < 2) {
		cpu_port[phys_addr & 1] = data;
		puts("MEM: applying new memory configuration because of CPU port writing");
		apply_memory_config();
		return;
	}
	if (phys_addr < 0x20000) {
#ifdef DEBUG_MEMORY
		printf("MEM write @ $%04X [PC=$%04X] (REAL=$%05X) with data $%02X" NL, addr, cpu_pc, phys_addr, data);
#endif
		DEBUG_WRITE_ACCESS(phys_addr, data);
		memory[phys_addr] = data;
		return;
	}
	printf("MEM: WARN: writing undecoded memory area or ROM @ $%04X [PC=$%04X] (REAL=$%05X) with data $%02X" NL, addr, cpu_pc, phys_addr, data);
}



#define MEMDUMP_FILE	"dump.mem"


static void dump_on_shutdown ( void )
{
	FILE *f;
	int a;
	for (a = 0; a < 0x40; a++)
		printf("VIC-3 register $%02X is %02X" NL, a, vic3_registers[a]);
	cia_dump_state (&cia1);
	cia_dump_state (&cia2);
	// Dump memory, so some can inspect the result (low 128K, RAM only)
	f = fopen(MEMDUMP_FILE, "wb");
	if (f) {
		fwrite(memory, 1, 0x20000, f);
		fclose(f);
		puts("Memory is dumped into " MEMDUMP_FILE);
	}
	printf("Execution has been stopped at PC=$%04X [$%05X]" NL, cpu_pc, addr_trans_rd[cpu_pc >> 12] + cpu_pc);
}



#define KBD_PRESS_KEY(a)        kbd_matrix[(a) >> 4] &= 255 - (1 << ((a) & 0x7))
#define KBD_RELEASE_KEY(a)      kbd_matrix[(a) >> 4] |= 1 << ((a) & 0x7)
#define KBD_SET_KEY(a,state) do {	\
	if (state)			\
		KBD_PRESS_KEY(a);	\
	else				\
		KBD_RELEASE_KEY(a);	\
} while (0)



static void emulate_keyboard ( SDL_Scancode key, int pressed )
{
	const struct KeyMapping *map;
	if (pressed) {
		if (key == SDL_SCANCODE_F11) {
			emu_set_full_screen(-1);
			return;
		} else if (key == SDL_SCANCODE_F9)
			exit(0);
	}
	map = key_map;
	while (map->pos != 0xFF) {
		if (map->scan == key) {
			if (map->pos & 8)			// shifted key emu?
				KBD_SET_KEY(SHIFTED_CURSOR_SHIFT_POS, pressed);	// maintain the shift key
			KBD_SET_KEY(map->pos, pressed);
			break;	// key found, end.
		}
		map++;
	}
}



static void update_emulator ( void )
{
	SDL_Event e;
	while (SDL_PollEvent(&e) != 0) {
		switch (e.type) {
			case SDL_QUIT:
				exit(0);
			case SDL_KEYUP:
			case SDL_KEYDOWN:
				if (e.key.repeat == 0 && (e.key.windowID == sdl_winid || e.key.windowID == 0))
					emulate_keyboard(e.key.keysym.scancode, e.key.state == SDL_PRESSED);
				break;
		}
	}
	// Screen rendering: begin
	vic3_render_screen();
	// Screen rendering: end
	emu_sleep(40000);
}




int main ( int argc, char **argv )
{
	int cycles, frameskip;
	printf("**** The Unusable Commodore 65 emulator from LGB" NL
	"INFO: Texture resolution is %dx%d" NL "%s" NL,
		SCREEN_WIDTH, SCREEN_HEIGHT,
		emulators_disclaimer
	);
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
        if (emu_init_sdl(
		"Commodore 65 / LGB",		// window title
		"nemesys.lgb", "xclcd-c65",	// app organization and name, used with SDL pref dir formation
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT * 2,// logical size
		SCREEN_WIDTH, SCREEN_HEIGHT * 2,// window size
		SCREEN_FORMAT,			// pixel format
		0,				// we have *NO* pre-defined colours (too many we need). we want to do this ourselves!
		NULL,				// -- "" --
		NULL,				// -- "" --
		RENDER_SCALE_QUALITY,		// render scaling quality
		USE_LOCKED_TEXTURE,		// 1 = locked texture access
		dump_on_shutdown		// registered shutdown function
	))
		return 1;
	// Initialize C65 ...
	c65_init();
	memory[0x3FB93] = 0xA9;
	memory[0x3FB94] = 0x00;
#if 0
	memory[0x200] = 0x83;
	memory[0x201] = 0xfd;
	memory[0x202] = 0xbd;
	// 83 fd fd 
	memory[0x201] = 0xfd;
	memory[0x202] = 0xfd;
	cpu_pc = 0x200;
#endif

	// Hack!
	//memset(memory + 0x20000 + 0x1D, 0x60, 0x4000 - 0x1D);
#if 0
	//memory[0x20000 + 0x1D] = 0x60;
	printf("%d\n", emu_load_file("/tmp/65C02_extended_opcodes_test.bin", memory + 0xa, 65526 + 1));
//	if (emu_load_file("/tmp/65C02_extended_opcodes_test.bin", memory + 0xa, 65526 + 1) != 65526);
//		FATAL("Cannot load test ROM!");
	cpu_port[1] = 0;
	apply_memory_config();
	cpu_pc = 0x400;
	memcpy(memory + 0x100, hacky, sizeof hacky);
	cpu_pc = 0x100;
#endif
	// Start!!
	cycles = 0;
	frameskip = 0;
	emu_timekeeping_start();
	for (;;) {
		int opcyc;
#ifdef DEBUG_STACK
		cpu_old_sp = cpu_sp;
		cpu_old_pc_my = cpu_pc;
		stackguard_address = -1;
#endif
		opcyc = cpu_step();
#ifdef DEBUG_STACK
		if (cpu_sp != cpu_old_sp) {
			printf("STACK: pointer [OP=$%02X] change $%02X -> %02X [diff=%d]\n", cpu_op, cpu_old_sp, cpu_sp, cpu_old_sp - cpu_sp);
			cpu_old_sp = cpu_sp;
		} else {
			if (stackguard_address > -1) {
				printf("STACK: WARN: somebody modified stack-like memory at $%X [SP=$%02X] with data $%02X [PC=$%04X]" NL, stackguard_address, cpu_sp, stackguard_data, cpu_old_pc_my);
			}
		}
#endif
		cia_tick(&cia1, opcyc);
		cia_tick(&cia2, opcyc);
		cycles += opcyc;
		if (cycles >= 227) {
			scanline++;
			//printf("VIC3: new scanline (%d)!" NL, scanline);
			cycles -= 227;
			if (scanline == 312) {
				//puts("VIC3: new frame!");
				frameskip = !frameskip;
				scanline = 0;
				if (!frameskip)	// well, let's only render every full frames (~ie 25Hz)
					update_emulator();
			}
			//printf("RASTER=%d COMPARE=%d\n",scanline,compare_raster);
			//vic_interrupt();
			vic3_check_raster_interrupt();
		}
	}
	puts("Goodbye!");
	return 0;
}
