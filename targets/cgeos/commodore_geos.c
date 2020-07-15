/* This is an odd emulator, emulating a Commodore 64 like machine only for the
   level needed for a special version of GEOS to be able to run on it.
   You should have a really special one with own disk drive etc, since there
   is no hardware support for drive emulation etc, but it's built in the emulator
   with a kind of CPU trap! The purpose: know GEOS better and slowly replace
   more and more functions on C/emulator level, so at one point it's possible
   to write a very own version of GEOS without *any* previously used code in
   the real GEOS. Then it can be even ported to other architectures GEOS wasn't
   mean to run ever.
   ---------------------------------------------------------------------------------
   One interesting plan: write a GEOS emulator which does not use VIC-II bitmapped
   screen anymore, but the GEOS functions mean to be targeted a "modern UI toolkit",
   ie GTK, so a dozens years old (unmodified) GEOS app would be able to run on a PC
   with modern look and feel, ie anti-aliased fonts, whatever ...
   ---------------------------------------------------------------------------------
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   ---------------------------------------------------------------------------------

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
#include "xemu/emutools_files.h"
#include "commodore_geos.h"
#include "xemu/cpu65.h"
#include "xemu/cia6526.h"
#include "xemu/emutools_hid.h"
#include "xemu/emutools_config.h"
#include "xemu/c64_kbd_mapping.h"
#include "geos.h"


#define DISK_IMAGE_SIZE	819200
//static Uint8 disk_image[DISK_IMAGE_SIZE + 1];

/*
      -- Port pin (bit)    $A000 to $BFFF       $D000 to $DFFF       $E000 to $FFFF
      -- 2 1 0             Read       Write     Read       Write     Read       Write
      -- --------------    ----------------     ----------------     ----------------
0     -- 0 0 0             RAM        RAM       RAM        RAM       RAM        RAM
1     -- 0 0 1             RAM        RAM       CHAR-ROM   RAM       RAM        RAM
2     -- 0 1 0             RAM        RAM       CHAR-ROM   RAM       KERNAL-ROM RAM
3     -- 0 1 1             BASIC-ROM  RAM       CHAR-ROM   RAM       KERNAL-ROM RAM
4     -- 1 0 0             RAM        RAM       RAM        RAM       RAM        RAM
5     -- 1 0 1             RAM        RAM       I/O        I/O       RAM        RAM
6     -- 1 1 0             RAM        RAM       I/O        I/O       KERNAL-ROM RAM
7     -- 1 1 1             BASIC-ROM  RAM       I/O        I/O       KERNAL-ROM RAM
*/

#define CPU_PORT_DEFAULT_VALUE0	0xFF
#define CPU_PORT_DEFAULT_VALUE1 0xFF

#define	BASIC_ROM_OFFSET	0x10000
#define	KERNAL_ROM_OFFSET	0x12000
#define	CHAR_ROM_OFFSET		0x14000
#define IO_OFFSET		0x15000

Uint8 memory[IO_OFFSET + 1];
#define IO_VIRT_ADDR  (memory + IO_OFFSET)

#define	MAP_RAM			memory
#define MAP_BASIC		memory + BASIC_ROM_OFFSET  - 0xA000
#define MAP_KERNAL		memory + KERNAL_ROM_OFFSET - 0xE000
#define MAP_CHRROM		memory + CHAR_ROM_OFFSET   - 0xD000
#define MAP_IO			memory + IO_OFFSET         - 0xD000
#define MAP_RAM_TWICE		MAP_RAM,MAP_RAM
#define MAP_RAM_10_TIMES	MAP_RAM_TWICE,MAP_RAM_TWICE,MAP_RAM_TWICE,MAP_RAM_TWICE,MAP_RAM_TWICE
#define MAP_BASIC_TWICE		MAP_BASIC,MAP_BASIC
#define MAP_KERNAL_TWICE	MAP_KERNAL,MAP_KERNAL

#define KERNAL_PATCH_ADDR	0xE388
#define PATCH_P			memory[KERNAL_PATCH_ADDR - 0xE000 + KERNAL_ROM_OFFSET]
#define PATCH_OLD_BYTE		0x6C
#define PATCH_NEW_BYTE		CPU65_TRAP_OPCODE


static Uint8 *memcfgs[8][2][16] = {
	// 0000-9FFF         A000-BFFF        CXXX     DXXX        E000-FFFF             0000-9FFF         A000-BFFF      CXXX     DXXX     E000-FFFF
	// READ              READ             READ     READ        READ                  WRITE             WRITE          WRITE    WRITE    WRITE
	// ----------------  ---------------  -------  ----------  ----------------      ----------------  -------------  -------  -------  -------------
	{{ MAP_RAM_10_TIMES, MAP_RAM_TWICE,   MAP_RAM, MAP_RAM,    MAP_RAM_TWICE    }, { MAP_RAM_10_TIMES, MAP_RAM_TWICE, MAP_RAM, MAP_RAM, MAP_RAM_TWICE }},	// cpu port = 0
	{{ MAP_RAM_10_TIMES, MAP_RAM_TWICE,   MAP_RAM, MAP_CHRROM, MAP_RAM_TWICE    }, { MAP_RAM_10_TIMES, MAP_RAM_TWICE, MAP_RAM, MAP_RAM, MAP_RAM_TWICE }},	// cpu port = 1
	{{ MAP_RAM_10_TIMES, MAP_RAM_TWICE,   MAP_RAM, MAP_CHRROM, MAP_KERNAL_TWICE }, { MAP_RAM_10_TIMES, MAP_RAM_TWICE, MAP_RAM, MAP_RAM, MAP_RAM_TWICE }},	// cpu port = 2
	{{ MAP_RAM_10_TIMES, MAP_BASIC_TWICE, MAP_RAM, MAP_CHRROM, MAP_KERNAL_TWICE }, { MAP_RAM_10_TIMES, MAP_RAM_TWICE, MAP_RAM, MAP_RAM, MAP_RAM_TWICE }},	// cpu_port = 3
	{{ MAP_RAM_10_TIMES, MAP_RAM_TWICE,   MAP_RAM, MAP_RAM,    MAP_RAM_TWICE    }, { MAP_RAM_10_TIMES, MAP_RAM_TWICE, MAP_RAM, MAP_RAM, MAP_RAM_TWICE }},	// cpu_port = 4
	{{ MAP_RAM_10_TIMES, MAP_RAM_TWICE,   MAP_RAM, MAP_IO,     MAP_RAM_TWICE    }, { MAP_RAM_10_TIMES, MAP_RAM_TWICE, MAP_RAM, MAP_IO,  MAP_RAM_TWICE }},	// cpu_port = 5
	{{ MAP_RAM_10_TIMES, MAP_RAM_TWICE,   MAP_RAM, MAP_IO,     MAP_KERNAL_TWICE }, { MAP_RAM_10_TIMES, MAP_RAM_TWICE, MAP_RAM, MAP_IO,  MAP_RAM_TWICE }},	// cpu_port = 6
	{{ MAP_RAM_10_TIMES, MAP_BASIC_TWICE, MAP_RAM, MAP_IO,     MAP_KERNAL_TWICE }, { MAP_RAM_10_TIMES, MAP_RAM_TWICE, MAP_RAM, MAP_IO,  MAP_RAM_TWICE }}	// cpu_port = 7
};

#define GET_READ_P(a)	(memcfgs[cpu_port_memconfig][0][(a)>>12] + (a))
#define GET_WRITE_P(a)	(memcfgs[cpu_port_memconfig][1][(a)>>12] + (a))
#define IS_P_IO(p)	((p) >= IO_VIRT_ADDR)

static const char *rom_fatal_msg = "This is one of the selected ROMs. Without it, Xemu won't work.\nInstall it, or use -romXXX CLI switches to specify another path, see the -h output for help.";

static const char *memconfig_descriptions[8] = {
		"ALL RAM [v1]", "CHAR+RAM", "CHAR+KERNAL", "ALL *ROM*",
		"ALL RAM [v2]", "IO+RAM",   "IO+KERNAL",   "BASIC+IO+KERNAL"
};

static struct Cia6526 cia1, cia2;	// CIA emulation structures for the two CIAs
static int    vic2_16k_bank;
static int    scanline;
              Uint8  colour_sram[1024];
static Uint8  vic2_registers[0x40];	// though not all of them really exists
static int    cpu_port_memconfig = 7;
static Uint32 palette[16];
static int    compare_raster;		// raster compare (9 bits width) data
static int    vic2_interrupt_status;	// Interrupt status of VIC
static Uint8 *vic2_sprite_pointers;
static int    warp = 1;			// warp speed for initially to faster "boot" for C64
static int    geos_loaded = 0;
static int    nmi_level;		// please read the comment at nmi_set() below


static const Uint8 init_vic2_palette_rgb[16 * 3] = {	// VIC2 palette given by RGB components
	0x00, 0x00, 0x00,
	0xFF, 0xFF, 0xFF,
	0x74, 0x43, 0x35,
	0x7C, 0xAC, 0xBA,
	0x7B, 0x48, 0x90,
	0x64, 0x97, 0x4F,
	0x40, 0x32, 0x85,
	0xBF, 0xCD, 0x7A,
	0x7B, 0x5B, 0x2F,
	0x4f, 0x45, 0x00,
	0xa3, 0x72, 0x65,
	0x50, 0x50, 0x50,
	0x78, 0x78, 0x78,
	0xa4, 0xd7, 0x8e,
	0x78, 0x6a, 0xbd,
	0x9f, 0x9f, 0x9f
};

#define CHECK_PIXEL_POINTER

#ifdef CHECK_PIXEL_POINTER
/* Temporary hack to be used in renders. Asserts out-of-texture accesses */
static Uint32 *pixel_pointer_check_base;
static Uint32 *pixel_pointer_check_end;
static const char *pixel_pointer_check_modn;
static inline void PIXEL_POINTER_CHECK_INIT( Uint32 *base, int tail, const char *module )
{
	pixel_pointer_check_base = base;
	pixel_pointer_check_end  = base + (320 + tail) * 200;
	pixel_pointer_check_modn = module;
}
static inline void PIXEL_POINTER_CHECK_ASSERT ( Uint32 *p )
{
	if (p < pixel_pointer_check_base)
		FATAL("FATAL ASSERT: accessing texture (%p) under the base limit (%p).\nIn program module: %s", p, pixel_pointer_check_base, pixel_pointer_check_modn);
	if (p >= pixel_pointer_check_end)
		FATAL("FATAL ASSERT: accessing texture (%p) above the upper limit (%p).\nIn program module: %s", p, pixel_pointer_check_end, pixel_pointer_check_modn);
}
static inline void PIXEL_POINTER_FINAL_ASSERT ( Uint32 *p )
{
	if (p != pixel_pointer_check_end)
		FATAL("FATAL ASSERT: final texture pointer (%p) is not the same as the desired one (%p),\nIn program module %s", p, pixel_pointer_check_end, pixel_pointer_check_modn);
}
#else
#	define PIXEL_POINTER_CHECK_INIT(base,tail,mod)
#	define PIXEL_POINTER_CHECK_ASSERT(p)
#	define PIXEL_POINTER_FINAL_ASSERT(p)
#endif





static void vic2_interrupt_checker ( void )
{
	int vic_irq_old = cpu65.irqLevel & 2;
	int vic_irq_new;
	if (vic2_interrupt_status & vic2_registers[0x1A]) {
		vic2_interrupt_status |= 128;
		vic_irq_new = 2;
	} else {
		vic2_interrupt_status &= 127;
		vic_irq_new = 0;
	}
	if (vic_irq_old != vic_irq_new) {
		DEBUG("VIC2: interrupt change %s -> %s" NL, vic_irq_old ? "active" : "inactive", vic_irq_new ? "active" : "inactive");
		if (vic_irq_new)
			cpu65.irqLevel |= 2;
		else
			cpu65.irqLevel &= ~2;
	}
}



void vic2_check_raster_interrupt ( void )
{
	// I'm lame even with VIC2 knowledge it seems
	// C65 seems to use raster interrupt to generate the usual periodic IRQ
	// (which was done with CIA on C64) in raster line 511. However as
	// raster line 511 can never be true, I really don't know what to do.
	// To be able C65 ROM to work, I assume that raster 511 is raster 0.
	// It's possible that this is an NTSC/PAL issue, as raster can be "negative"
	// according to the specification in case of NTSC. I really don't know ...
	if (scanline == compare_raster)
		vic2_interrupt_status |= 1;
	else
		vic2_interrupt_status &= 0xFE;
	vic2_interrupt_checker();
}




void vic2_write_reg ( int addr, Uint8 data )
{
	addr &= 0x3F;
	DEBUG("VIC3: write reg $%02X with data $%02X" NL, addr, data);
	if (addr > 0x2E)
		return;
	vic2_registers[addr] = data;
	switch (addr) {
		case 0x11:
			compare_raster = (compare_raster & 0xFF) | ((data & 128) ? 0x100 : 0);
			DEBUG("VIC2: compare raster is now %d" NL, compare_raster);
			break;
		case 0x12:
			compare_raster = (compare_raster & 0xFF00) | data;
			DEBUG("VIC2: compare raster is now %d" NL, compare_raster);
			break;
		case 0x19:
			vic2_interrupt_status = vic2_interrupt_status & (~data) & 15;
			vic2_interrupt_checker();
			break;
		case 0x1A:
			vic2_registers[0x1A] &= 15;
			break;
	}
}	




Uint8 vic2_read_reg ( int addr )
{
	Uint8 result;
	addr &= 0x3F;
	switch (addr) {
		case 0x11:
			result =  (vic2_registers[0x11] & 0x7F) | ((scanline & 256) ? 0x80 : 0);
			break;
		case 0x12:
			result = scanline & 0xFF;
			break;
		case 0x16:
			result = vic2_registers[addr] | (128 + 64);	// unused bits
			break;
		case 0x19:
			result = vic2_interrupt_status | (64 + 32 + 16);	// unused bits
			break;
		case 0x1A:
			result = vic2_registers[addr] | 0xF0;		// unused bits
			break;
		case 0x18:
			result = vic2_registers[addr] | 1;		// unused bit
			break;
		default:
			result = vic2_registers[addr];
			if (addr >= 0x20 && addr < 0x2F)
				result |= 0xF0;				// unused bits
			break;
	}
	DEBUG("VIC2: read reg $%02X with result $%02X" NL, addr, result);
	return result;
}




/* At-frame-at-once (thus incorrect implementation)
   for "normal" text VIC mode.
   Character map memory if fixed :-/ */
static inline void vic2_render_screen_text ( Uint32 *p, int tail )
{
	Uint32 bg;
	Uint8 *vidp, *chrg, *colp = colour_sram;
	int x = 0, y = 0, charline = 0;
	// Currently, only text (no MCM, ECM) is supported,
	// and fixed chargen address.
	// Fixed character info, heh ... FIXME
	chrg = memory + CHAR_ROM_OFFSET;
	// Note: VIC2 sees ROM at some addresses thing is not emulated yet!
	vidp = memory + ((vic2_registers[0x18] & 0xF0) << 6) + vic2_16k_bank;
	vic2_sprite_pointers = vidp + 1016;
	// Target SDL pixel related format for the background colour
	bg = palette[vic2_registers[0x21] & 15];
	PIXEL_POINTER_CHECK_INIT(p, tail, "vic2_render_screen_text");
	for (;;) {
		Uint8 chrdata = chrg[((*(vidp++)) << 3) + charline];
		Uint8 coldata = *(colp++);
		Uint32 fg = palette[coldata & 15];
		// FIXME: no ECM, MCM stuff ...
		PIXEL_POINTER_CHECK_ASSERT(p + 7);
		*(p++) = chrdata & 128 ? fg : bg;
		*(p++) = chrdata &  64 ? fg : bg;
		*(p++) = chrdata &  32 ? fg : bg;
		*(p++) = chrdata &  16 ? fg : bg;
		*(p++) = chrdata &   8 ? fg : bg;
		*(p++) = chrdata &   4 ? fg : bg;
		*(p++) = chrdata &   2 ? fg : bg;
		*(p++) = chrdata &   1 ? fg : bg;
		if (x == 39) {
			p += tail;
			x = 0;
			if (charline == 7) {
				if (y == 24)
					break;
				y++;
				charline = 0;
			} else {
				charline++;
				vidp -= 40;
				colp -= 40;
			}
		} else
			x++;
	}
	PIXEL_POINTER_FINAL_ASSERT(p);
}



// VIC2 bitmap mode, now only HIRES mode (no MCM yet)
// Note: VIC2 sees ROM at some addresses thing is not emulated yet!
static inline void vic2_render_screen_bmm ( Uint32 *p, int tail )
{
	int x = 0, y = 0, charline = 0;
	Uint8 *vidp, *chrp;
	vidp = memory + ((vic2_registers[0x18] & 0xF0) << 6) + vic2_16k_bank;
	vic2_sprite_pointers = vidp + 1016;
	chrp = memory + ((vic2_registers[0x18] & 8) ? 8192 : 0) + vic2_16k_bank;
	PIXEL_POINTER_CHECK_INIT(p, tail, "vic2_render_screen_bmm");
	for (;;) {
		Uint8  data = *(vidp++);
		Uint32 bg = palette[data & 15];
		Uint32 fg = palette[data >> 4];
		data = *chrp;
		chrp += 8;
		PIXEL_POINTER_CHECK_ASSERT(p);
		p[0] = data & 128 ? fg : bg;
		p[1] = data &  64 ? fg : bg;
		p[2] = data &  32 ? fg : bg;
		p[3] = data &  16 ? fg : bg;
		p[4] = data &   8 ? fg : bg;
		p[5] = data &   4 ? fg : bg;
		p[6] = data &   2 ? fg : bg;
		p[7] = data &   1 ? fg : bg;
		p += 8;
		if (x == 39) {
			p += tail;
			x = 0;
			if (charline == 7) {
				if (y == 24)
					break;
				y++;
				charline = 0;
				chrp -= 7;
			} else {
				charline++;
				vidp -= 40;
				chrp -= 319;
			}
		} else
			x++;
	}
	PIXEL_POINTER_FINAL_ASSERT(p);
}

#define SPRITE_X_START_SCREEN	24
#define SPRITE_Y_START_SCREEN	50


/* Extremely incorrect sprite emulation! BUGS:
   * Sprites cannot be behind the background (sprite priority)
   * Multicolour sprites are not supported
   * No sprite-background collision detection
   * No sprite-sprite collision detection
   * This is a simple, after-the-rendered-frame render-sprites one-by-one algorithm
   * This also requires to give up direct rendering if a sprite is enabled
   * Very ugly, quick&dirty hack, not so optimal either, even without the other mentioned bugs ...
*/
static void vic2_render_sprite ( int sprite_no, int sprite_mask, Uint8 *data, Uint32 *p, int tail )
{
	int sprite_y = vic2_registers[sprite_no * 2 + 1] - SPRITE_Y_START_SCREEN;
	int sprite_x = ((vic2_registers[sprite_no * 2] | ((vic2_registers[16] & sprite_mask) ? 0x100 : 0)) - SPRITE_X_START_SCREEN) * 1;
	Uint32 colour = palette[vic2_registers[39 + sprite_no] & 15];
	int expand_x = vic2_registers[29] & sprite_mask;
	int expand_y = vic2_registers[23] & sprite_mask;
	int lim_y = sprite_y + ((expand_y) ? 42 : 21);
	int y;
	p += (320 + tail) * sprite_y;
	for (y = sprite_y; y < lim_y; y += (expand_y ? 2 : 1), p += (320 + tail) * (expand_y ? 2 : 1))
		if (y < 0 || y >= 200)
			data += 3;	// skip one line (three bytes) of sprite data if outside of screen
		else {
			int mask, a, x = sprite_x;
			for (a = 0; a < 3; a++) {
				for (mask = 128; mask; mask >>= 1) {
					if (*data & mask) {
						if (x >= 0 && x < 320) {
							p[x] = colour;
							if (expand_y && y < 200)
								p[x + 320 + tail] = colour;
						}
						x++;
						if (expand_x && x >= 0 && x < 320) {
							p[x] = colour;
							if (expand_y && y < 200)
								p[x + 320 + tail] = colour;
							x++;
						}
					} else
						x += expand_x ? 2 : 1;
				}
				data++;
			}
		}
}




/* This is the one-frame-at-once (highly incorrect implementation, that is)
   renderer. It will call legacy VIC2 text mode render (optionally with
   80 columns mode, though, ECM, MCM, hardware attributes are not supported),
   VIC2 legacy HIRES mode (MCM is not supported), or bitplane modes (V400,
   H1280, odd scanning/interlace is not supported). Sprites, screen positioning,
   etc is not supported */
void vic2_render_screen ( void )
{
	int tail_sdl;
	Uint32 *p_sdl = xemu_start_pixel_buffer_access(&tail_sdl);
	int sprites = vic2_registers[0x15];
	if (vic2_registers[0x11] & 32)
		vic2_render_screen_bmm(p_sdl, tail_sdl);
	else
		vic2_render_screen_text(p_sdl, tail_sdl);
	if (sprites) {	// Render sprites. VERY BAD. We ignore sprite priority as well (cannot be behind the background)
		int a;
		for (a = 7; a >= 0; a--) {
			int mask = 1 << a;
			if (sprites & mask)
				vic2_render_sprite(a, mask, memory + vic2_16k_bank + (vic2_sprite_pointers[a] << 6), p_sdl, tail_sdl);	// sprite_pointers are set by the renderer functions above!
		}
	}
	xemu_update_screen();
}



static void cia1_setint_cb ( int level )
{
	DEBUG("%s: IRQ level changed to %d" NL, cia1.name, level);
	if (level)
		cpu65.irqLevel |= 1;
	else
		cpu65.irqLevel &= ~1;
}


static inline void nmi_set ( int level, int mask )
{
	// NMI is a low active _EDGE_ triggered 65xx input ... In my emulator though, the signal
	// is "high active", and also we must form the "edge" ourselves from "level". NMI level is
	// set as a 2bit number, on bit 0, CIA2, on bit 1, keyboard RESTORE key. Thus having zero
	// value for level means (in the emu!) that not RESTORE key is pressed neither CIA2 has active
	// IRQ output, non-zero value means some activation. Well, if I am not confused enough here,
	// this should mean that nmi_level zero->non-zero transit should produce the edge (which should
	// be the falling edge in the real hardware anyway ... but the rising here. heh, I should follow
	// the signal level of the hardware in my emulator, honestly ...)
	int nmi_new_level;
	if (level)
		nmi_new_level = nmi_level | mask;
	else
		nmi_new_level = nmi_level & (~mask);
	if ((!nmi_level) && nmi_new_level) {
		DEBUG("NMI edge is emulated towards the CPU (%d->%d)" NL, nmi_level, nmi_new_level);
		cpu65.nmiEdge = 1;	// the "NMI edge" trigger is deleted by the CPU emulator itself (it's not a level trigger)
	}
	nmi_level = nmi_new_level;
}


static void cia2_setint_cb ( int level )
{
	nmi_set(level, 1);
}


void clear_emu_events ( void )
{
	hid_reset_events(1);
}


static Uint8 cia1_in_b ( void )
{
	return c64_keyboard_read_on_CIA1_B(
		cia1.PRA | (~cia1.DDRA),
		cia1.PRB | (~cia1.DDRB),
		joystick_emu == 1 ? c64_get_joy_state() : 0xFF
	);
}


static Uint8 cia1_in_a ( void )
{
	return c64_keyboard_read_on_CIA1_A(
		cia1.PRB | (~cia1.DDRB),
		cia1.PRA | (~cia1.DDRA),
		joystick_emu == 2 ? c64_get_joy_state() : 0xFF
	);
}


static void cia2_out_a ( Uint8 data )
{
	vic2_16k_bank = ((~(data | (~cia2.DDRA))) & 3) << 14;
	DEBUG("VIC2: 16K BANK is set to $%04X (CIA mask=$%02X)" NL, vic2_16k_bank, cia2.DDRA);
}



// Just for easier test to have a given port value for CIA input ports
static Uint8 cia_port_in_dummy ( void )
{
	return 0xFF;
}



static void cpu_port_write ( int addr, Uint8 data )
{
	memory[addr] = data;
	if (addr) {
		if (cpu_port_memconfig == (data & 7))
			DEBUG("MEM: memory configuration is the SAME: %d %s @ PC = $%04X" NL, cpu_port_memconfig, memconfig_descriptions[cpu_port_memconfig], cpu65.pc);
		else {
			DEBUG("MEM: memory configuration is CHANGED : %d %s (from %d %s) @ PC = $%02X" NL,
				data & 7,           memconfig_descriptions[data & 7],
				cpu_port_memconfig, memconfig_descriptions[cpu_port_memconfig],
				cpu65.pc
			);
			cpu_port_memconfig = data & 7;
		}
	}
}


static void geosemu_init ( void )
{
	hid_init(
		c64_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// joy HID events enabled
	);
	joystick_emu = 1;
	nmi_level = 0;
	// *** Init memory space
	memset(memory, 0xFF, sizeof memory);
	cpu_port_write(0, CPU_PORT_DEFAULT_VALUE0);
	cpu_port_write(1, CPU_PORT_DEFAULT_VALUE1);
	// *** Load ROM image
	if (
		xemu_load_file(xemucfg_get_str("rombasic"),  memory + BASIC_ROM_OFFSET,  8192, 8192, rom_fatal_msg) < 0 ||
		xemu_load_file(xemucfg_get_str("romkernal"), memory + KERNAL_ROM_OFFSET, 8192, 8192, rom_fatal_msg) < 0 ||
		xemu_load_file(xemucfg_get_str("romchar"),   memory + CHAR_ROM_OFFSET,   4096, 4096, rom_fatal_msg) < 0
	)
		XEMUEXIT(1);
	// *** Patching ROM for custom GEOS loader
	if (PATCH_P != PATCH_OLD_BYTE)
		FATAL("FATAL: ROM problem, patching point does not contain the expected value!");
	PATCH_P = PATCH_NEW_BYTE;
	// *** Initialize VIC2 ... sort of :)
	memset(colour_sram, 0xFF, sizeof colour_sram);
	memset(vic2_registers, 0, sizeof vic2_registers);
	vic2_16k_bank = 0;
	scanline = 0;
	vic2_interrupt_status = 0;
	compare_raster = 0; 
	// *** CIAs
	cia_init(&cia1, "CIA-1",
		NULL,			// callback: OUTA
		NULL,			// callback: OUTB
		NULL,			// callback: OUTSR
		cia1_in_a,		// callback: INA ~ joy#2
		cia1_in_b,		// callback: INB ~ keyboard
		NULL,			// callback: INSR
		cia1_setint_cb		// callback: SETINT
	);
	cia_init(&cia2, "CIA-2",
		cia2_out_a,		// callback: OUTA ~ eg VIC-II bank
		NULL,			// callback: OUTB
		NULL,			// callback: OUTSR
		cia_port_in_dummy,	// callback: INA
		NULL,			// callback: INB
		NULL,			// callback: INSR
		cia2_setint_cb		// callback: SETINT ~ that would be NMI in our case
	);
	// Initialize Disk Image
	// TODO
	// *** RESET CPU, also fetches the RESET vector into PC
	cpu65_reset();
	DEBUG("INIT: end of initialization!" NL);
}



static Uint8 io_read ( int addr )
{
	DEBUG("IO: reading $%04X @ PC=$%04X" NL, addr, cpu65.pc);
	if (addr < 0xD400)		// D000-D3FF  VIC-II
		return vic2_read_reg(addr);
	if (addr < 0xD800)		// D400-D7FF  SID   (not emulated here)
		return 0xFF;
	if (addr < 0xDC00)		// D800-DBFF  Colour SRAM (1K, 4 bit)
		return colour_sram[addr & 0x3FF];
	if (addr < 0xDD00)		// DC00-DCFF  CIA-1
		return cia_read(&cia1, addr & 15);
	if (addr < 0xDE00)		// DD00-DDFF  CIA-2
		return cia_read(&cia2, addr & 15);
	return 0xFF;			// DE00-DFFF  the rest, I/O-1 and I/O-2 exp area
}



static void io_write ( int addr, Uint8 data )
{
	DEBUG("IO: writing $%04X with $%02X @ PC=$%04X" NL, addr, data, cpu65.pc);
	if (addr < 0xD400) {		// D000-D3FF  VIC-II
		vic2_write_reg(addr, data);
		return;
	}
	if (addr < 0xD800)		// D400-D7FF  SID   (not emulated here)
		return;
	if (addr < 0xDC00) {		// D800-DBFF  Colour SRAM (1K, 4 bit)
		colour_sram[addr & 0x3FF] = data | 0xF0;	// 4 upper bits are always set (the SRAM has only 4 bits)
		return;
	}
	if (addr < 0xDD00) {		// DC00-DCFF  CIA-1
		cia_write(&cia1, addr & 15, data);
		return;
	}
	if (addr < 0xDE00) {		// DD00-DDFF  CIA-2
		cia_write(&cia2, addr & 15, data);
		return;
	}
					// DE00-DFFF  the rest, I/O-1 and I/O-2 exp area
}



int cpu65_trap_callback ( Uint8 opcode )
{
	Uint8 *pc_p = GET_READ_P(cpu65.pc);
	if (pc_p != 1 + &PATCH_P) {
		if (warp)
			FATAL("FATAL: CPU trap at unknown address in warp mode (pre-GEOS loading) PC=$%04X OP=$%02X" NL, cpu65.pc, opcode);
		if (pc_p >= memory + 0x10000)
			FATAL("FATAL: unknown CPU trap not in the RAM PC=$%04X OP=$%02X" NL, cpu65.pc, opcode);
		if (geos_loaded != 2)
			FATAL("FATAL: unknown CPU without GEOS loaded PC=$%04X OP=$%02X" NL, cpu65.pc, opcode);
		geos_cpu_trap(opcode);
		return 1;
	}
	warp = 0;	// turn warp speed off
	// Try to load a custom GEOS kernal directly into the RAM
	if (geos_loaded) {
		cpu65.pc = memory[0x300] | (memory[0x301] << 8);
		return 1;
	}
	if (!geos_load_kernal(xemucfg_get_str("geoskernal"))) {
		geos_loaded = 2;	// GEOS was OK!!!!
		return 1;	// if no error, return with '1' (as not zero) to signal CPU emulator that trap should not be executed
	}
	geos_loaded = 1;
	// In case if we cannot load some GEOS kernal stuff, continue in "C64 mode" ... :-/
	// Some ugly method to produce custom "startup screen" :)
	inject_screencoded_message(41, "**** Can't load GEOS, boot as C64 ****");
	cpu65.pc = memory[0x300] | (memory[0x301] << 8);
	return 1;	// do NOT execute the trap op
}


void cpu65_illegal_opcode_callback ( void )
{
	ERROR_WINDOW("Unemulated NMOS 6502 opcode $%02X at PC=$%04X", cpu65.op, cpu65.pc - 1);
	cpu65_reset();
}



// This function is called by the 65C02 emulator in case of reading a byte (regardless of data or code)
Uint8 cpu65_read_callback ( Uint16 addr )
{
	Uint8 *p = GET_READ_P(addr);
	return IS_P_IO(p) ? io_read(addr) : *p;
}



// This function is called by the 65C02 emulator in case of writing a byte
void cpu65_write_callback ( Uint16 addr, Uint8 data )
{
	Uint8 *p = GET_WRITE_P(addr);
	if (IS_P_IO(p))
		io_write(addr, data);
	else if (addr > 1)
		*p = data;
	else
		cpu_port_write(addr, data);
}



// Called in case of an RMW (read-modify-write) opcode write access.
// Original NMOS 6502 would write the old_data first, then new_data.
// It has no inpact in case of normal RAM, but it *does* with an I/O register in some cases!
void cpu65_write_rmw_callback ( Uint16 addr, Uint8 old_data, Uint8 new_data )
{
	Uint8 *p = GET_WRITE_P(addr);
	if (IS_P_IO(p)) {
		io_write(addr, old_data);
		io_write(addr, new_data);
	} else if (addr > 1)
		*p = new_data;
	else
		cpu_port_write(addr, new_data);
}



static void shutdown_callback ( void )
{
	DEBUG("SHUTDOWN: @ PC=$%04X" NL, cpu65.pc);
}


// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
        if (pressed) {
		if (key == SDL_SCANCODE_F10) {
			cpu_port_write(0, CPU_PORT_DEFAULT_VALUE0);
			cpu_port_write(1, CPU_PORT_DEFAULT_VALUE1);
			cpu65_reset();
			nmi_level = 0;
			DEBUG("RESET!" NL);
		} else if (key == SDL_SCANCODE_KP_ENTER)
			c64_toggle_joy_emu();
	}
	return 0;
}


static void update_emulator ( void )
{
	hid_handle_all_sdl_events();
	nmi_set(IS_RESTORE_PRESSED(), 2);	// Custom handling of the restore key ...
	// Screen rendering: begin
	vic2_render_screen();
	// Screen rendering: end
	xemu_timekeeping_delay(40000);
	// Ugly CIA trick to maintain realtime TOD in CIAs :)
	const struct tm *t = xemu_get_localtime();
	const Uint8 sec10ths = xemu_get_microseconds() / 100000;
	cia_ugly_tod_updater(&cia1, t, sec10ths);
	cia_ugly_tod_updater(&cia2, t, sec10ths);
}




int main ( int argc, char **argv )
{
	int cycles, frameskip;
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Unexplained Commodore GEOS emulator from LGB");
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	xemucfg_define_str_option("geosimg", NULL, "Select GEOS disk image to use (NOT USED YET!)");
	xemucfg_define_str_option("geoskernal", "#geos-kernal.bin", "Select GEOS KERNAL to use");
	xemucfg_define_str_option("drive", NULL, "Path/name of a D81 GEOS will see as the disk drive");
	xemucfg_define_str_option("rombasic", "#c64-basic.rom", "Select BASIC ROM to use");
	xemucfg_define_str_option("romchar", "#c64-chargen.rom", "Select CHARACTER ROM to use");
	xemucfg_define_str_option("romkernal", "#c64-kernal.rom", "Select KERNAL ROM to use");
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)");
	if (xemucfg_parse_all(argc, argv))
		return 1;
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
        if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2,// logical size (used with keeping aspect ratio by the SDL render stuffs)
		SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2,// window size
		SCREEN_FORMAT,			// pixel format
		16,				// we have 16 colours
		init_vic2_palette_rgb,		// initialize palette from this constant array
		palette,			// initialize palette into this stuff
		RENDER_SCALE_QUALITY,		// render scaling quality
		USE_LOCKED_TEXTURE,		// 1 = locked texture access
		shutdown_callback		// registered shutdown function
	))
		return 1;
	// Initialize
	geosemu_init();
	cycles = 0;
	frameskip = 0;
	xemu_set_full_screen(xemucfg_get_bool("fullscreen"));
	if (!xemucfg_get_bool("syscon"))
		sysconsole_close(NULL);
	// Start!!
	xemu_timekeeping_start();
	for (;;) {
		int opcyc = cpu65_step();
		cia_tick(&cia1, opcyc);
		cia_tick(&cia2, opcyc);
		cycles += opcyc;
		if (cycles >= 63) {
#if 0
			vic2_registers[0] = 80;
			vic2_registers[1] = 80;
			vic2_registers[16] = 0;
			vic2_registers[21] = 0xFF;
#endif
			scanline++;
			//DEBUG("VIC3: new scanline (%d)!" NL, scanline);
			cycles -= 63;
			if (scanline == 312) {
				//DEBUG("VIC3: new frame!" NL);
				frameskip = !frameskip;
				scanline = 0;
				if (!frameskip && !warp)	// well, let's only render every full frames (~ie 25Hz)
					update_emulator();
			}
			//DEBUG("RASTER=%d COMPARE=%d" NL,scanline,compare_raster);
			//vic_interrupt();
			vic2_check_raster_interrupt();
		}
	}
	return 0;
}
