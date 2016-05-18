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
#include "emutools.h"

//#define DEBUG_MEMORY
//#define DEBUG_STACK


static Uint8 memory[0x110000];		// 65CE02 MAP'able address space (now overflow is not handled, that is the reason of higher than 1MByte, just in case ...)
//static int map_offset[8];		// 8K sized block of 65CE02 64K address space mapping offset
//static int map_mapped[8];		// is a 8K sized block of 65CE02 64K address space is mapped or not
static Uint32 rgb_palette[4096];	// all the C65 palette, 4096 colours (SDL pixel format related form)
static Uint32 vic3_palette[0x100];	// VIC3 palette in SDL pixel format related form (can be written into the texture directly to be rendered)
static Uint8 vic3_registers[0x40];	// VIC3 registers
static Uint8 vic3_palette_r[0x100], vic3_palette_g[0x100], vic3_palette_b[0x100];	// RGB nibbles of palette (0-15)
static int vic_new_mode;		// VIC3 "newVic" IO mode is activated flag
static Uint8 cpu_port[2];		// CPU I/O port at 0/1 (implemented by the VIC3 for real, on C65 but for the usual - C64/6510 - name, it's the "CPU port")
static int scanline;			// current scan line number
static struct Cia6526 cia1, cia2;	// CIA emulation structures for the two CIAs


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




#define RGB(r,g,b) rgb_palette[((r) << 8) | ((g) << 4) | (b)]

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



/* You *MUST* call this every time, when *any* of these events applies:
   * MAP 4510 opcode is issued, map_offset_low, map_offset_high, map_mask are modified
   * "CPU port" data or DDR register has been written, witn cpu_port[0 or 1] modified
   * VIC3 register $30 is written, with vic3_registers[0x30] modified 
   The rreason of this madness: do the ugly work here, as memory configuration change is
   less frequent as memory usage (read/write). Thus do more work here, but simplier
   work when doing actual memory read/writes, with a simple addition and shift, or such.
   The tables are 4K in steps, 4510 would require only 8K steps, but there are other
   reasons (ie, I/O area is only 4K long).
   More advanced technique can be used not to handle *everything* here, but it's better
   for the initial steps, to have all address translating logic at once.
*/
static void apply_memory_config ( void )
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
	cpu_irqLevel = level;
}


#if 0
static Uint8 fake ( Uint8 mask )
{
	return 0;
}
#endif


static void c65_init ( void )
{
	int r, g, b, i;
	// *** Init memory space
	memset(memory, 0xFF, sizeof memory);
	// *** Load ROM image
	if (emu_load_file("c65-system.rom", memory + 0x20000, 0x20001) != 0x20000)
		FATAL("Cannot load C65 system ROM!");
	// *** Init 4096 element palette with RGB components for faster access later on palette register changes (avoid SDL calls to convert)
	for (r = 0, i = 0; r < 16; r++)
		for (g = 0; g < 16; g++)
			for (b = 0; b < 16; b++)
				rgb_palette[i++] = SDL_MapRGBA(sdl_pix_fmt, r * 17, g * 17, b * 17, 0xFF);
	SDL_FreeFormat(sdl_pix_fmt);	// thanks, we don't need this anymore.
	// *** Init VIC3 registers and palette
	vic_new_mode = 0;
	scanline = 0;
	for (i = 0; i < 0x100; i++) {
		if (i < sizeof vic3_registers)
			vic3_registers[i] = 0;
		vic3_palette[i] = rgb_palette[0];	// black
		vic3_palette_r[i] = 0;
		vic3_palette_g[i] = 0;
		vic3_palette_b[i] = 0;
	}
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
		NULL,	// callback: INB(mask)
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


#if 0
int cpu_trap ( Uint8 opcode )
{
	return 0;	// not used here
}
#endif


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



static void vic3_write_reg ( int addr, Uint8 data )
{
	addr &= 0x3F;
	printf("VIC3: write reg $%02X with data $%02X\n", addr, data);
	if (addr == 0x2F) {
		if (!vic_new_mode && data == 0x96 && vic3_registers[0x2F] == 0xA5) {
			vic_new_mode = 1;
			printf("VIC3: switched into NEW I/O access mode :)" NL);
		} else if (vic_new_mode) {
			vic_new_mode = 0;
			printf("VIC3: switched into OLD I/O access mode :(" NL);
		}
	}
	if (!vic_new_mode && addr > 0x2F) {
		printf("VIC3: ignoring writing register $%02X (with data $%02X) because of old I/O access mode selected" NL, addr, data);
		return;
	}
	vic3_registers[addr] = data;
	if (addr == 0x30) {
		puts("MEM: applying new memory configuration because of VIC3 $30 is written");
		apply_memory_config();
	}
}	



static Uint8 vic3_read_reg ( int addr )
{
	addr &= 0x3F;
	if (!vic_new_mode && addr > 0x2F) {
		printf("VIC3: ignoring reading register $%02X because of old I/O access mode selected, answer is $FF" NL, addr);
		return 0xFF;
	}
	if (addr == 0x12)
		vic3_registers[0x12] = scanline & 0xFF;
	else if (addr == 0x11)
		vic3_registers[0x11] = (vic3_registers[0x11] & 0x7F) | ((scanline & 256) ? 0x80 : 0);
	printf("VIC3: read reg $%02X with result $%02X\n", addr, vic3_registers[addr]);
	return vic3_registers[addr];
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
	if (addr < 0xD200) {	// $D100 - $D100	palette red nibbles (*)
		if (vic_new_mode)
			return vic3_palette_r[addr & 0xFF];
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("palette red nibbles", 0xFF);
	}
	if (addr < 0xD300) {	// $D200 - $D200	palette green nibbles (*)
		if (vic_new_mode)
			return vic3_palette_g[addr & 0xFF];
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("palette green nibbles", 0xFF);
	}
	if (addr < 0xD400) {	// $D300 - $D300	palette blue nibbles (*)
		if (vic_new_mode)
			return vic3_palette_b[addr & 0xFF];
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("palette blue nibbles", 0xFF);
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


#define SET_VIC3_PALETTE_ENTRY(num) \
	do { vic3_palette[num] = RGB(vic3_palette_r[num], vic3_palette_g[num], vic3_palette_b[num]); \
	printf("VIC3: palette #$%02X is set to RGB nibbles $%X%X%X" NL, num, vic3_palette_r[num], vic3_palette_g[num], vic3_palette_b[num]); \
	} while (0)



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
	if (addr < 0xD200) {	// $D100 - $D100	palette red nibbles (*)
		if (vic_new_mode) {
			vic3_palette_r[addr & 0xFF] = data & 15;	// FIXME: bg/fg bit: what is that? (bit 4, but I ignore that now)
			SET_VIC3_PALETTE_ENTRY(addr & 0xFF);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("palette red nibbles");
	}
	if (addr < 0xD300) {	// $D200 - $D200	palette green nibbles (*)
		if (vic_new_mode) {
			vic3_palette_g[addr & 0xFF] = data & 15;
			SET_VIC3_PALETTE_ENTRY(addr & 0xFF);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("palette green nibbles");
	}
	if (addr < 0xD400) {	// $D300 - $D300	palette blue nibbles (*)
		if (vic_new_mode) {
			vic3_palette_b[addr & 0xFF] = data & 15;
			SET_VIC3_PALETTE_ENTRY(addr & 0xFF);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("palette blue nibbles");
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



void clear_emu_events ( void )
{
}


#define MEMDUMP_FILE	"dump.mem"


static void dump_on_shutdown ( void )
{
	FILE *f;
	int a;
	for (a = 0; a < 0x40; a++)
		printf("VIC-3 register $%02X is %02X" NL, a, vic3_registers[a]);
	// Dump memory, so some can inspect the result (low 128K, RAM only)
	f = fopen(MEMDUMP_FILE, "wb");
	if (f) {
		fwrite(memory, 1, 0x20000, f);
		fclose(f);
		puts("Memory is dumped into " MEMDUMP_FILE);
	}
}



static void render_screen ( void )
{
	int tail;
	Uint32 *p = emu_start_pixel_buffer_access(&tail);
	Uint8 *vidp = memory + 0x00800;
	Uint8 *colp = memory + 0x1F800;
	Uint8 *chrg = memory + 0x28000 + 0x1000 ;
	int charline = 0;
	Uint32 bg = vic3_palette[vic3_registers[0x21]];
	int x = 0, y = 0;

	for (;;) {
		Uint8 chrdata = chrg[((*(vidp++)) << 3) + charline];
		Uint8 coldata = *(colp++);
		Uint32 fg = vic3_palette[coldata];
		*(p++) = chrdata & 128 ? fg : bg;
		*(p++) = chrdata &  64 ? fg : bg;
		*(p++) = chrdata &  32 ? fg : bg;
		*(p++) = chrdata &  16 ? fg : bg;
		*(p++) = chrdata &   8 ? fg : bg;
		*(p++) = chrdata &   4 ? fg : bg;
		*(p++) = chrdata &   2 ? fg : bg;
		*(p++) = chrdata &   1 ? fg : bg;
		if (x == 79) {
			p += tail;
			x = 0;
			if (charline == 7) {
				if (y == 24)
					break;
				y++;
				charline = 0;
			} else {
				charline++;
				vidp -= 80;
				colp -= 80;
			}
		} else
			x++;
	}
	emu_update_screen();
}




static void emulate_keyboard ( SDL_Scancode key, int pressed )
{
	if (pressed) {
		if (key == SDL_SCANCODE_F11)
			emu_set_full_screen(-1);
		else if (key == SDL_SCANCODE_F9)
			exit(0);
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
	render_screen();
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
		cpu_old_sp = cpu_sp;
		cpu_old_pc_my = cpu_pc;
		stackguard_address = -1;
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
			printf("VIC3: new scanline (%d)!" NL, scanline);
			cycles -= 227;
			if (scanline == 312) {
				puts("VIC3: new frame!");
				frameskip = !frameskip;
				scanline = 0;
				if (!frameskip)
					update_emulator();
			}
		}
	}
	puts("Goodbye!");
	return 0;
}
