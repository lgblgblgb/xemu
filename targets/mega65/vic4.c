/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the VIC-IV "emulation". Currently it does one-frame-at-once
   kind of horrible work, and only a subset of VIC2 and VIC3 knowledge
   is implemented, with some light VIC-IV features, to be able to "boot"
   of MEGA65 with standard configuration (kickstart, SD-card).
   Some of the missing features (VIC-2/3): hardware attributes,
   DAT, sprites, screen positioning, H1280 mode, V400 mode, interlace,
   chroma killer, VIC2 MCM, ECM, 38/24 columns mode, border.
   VIC-4: almost everything :(

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
#include "mega65.h"
#include "xemu/cpu65.h"
#include "vic4.h"
#include "vic4_palette.h"
#include "memory_mapper.h"

//#define RGB(r,g,b) rgb_palette[((r) << 8) | ((g) << 4) | (b)]

static const char *iomode_names[4] = { "VIC2", "VIC3", "BAD!", "VIC4" };

//static Uint32 rgb_palette[4096];	// all the C65 palette, 4096 colours (SDL pixel format related form)
//static Uint32 vic3_palette[0x100];	// VIC3 palette in SDL pixel format related form (can be written into the texture directly to be rendered)
//static Uint32 vic3_rom_palette[0x100];	// the "ROM" palette, for C64 colours (with some ticks, ie colours above 15 are the same as the "normal" programmable palette)
//static Uint32 *palette;			// the selected palette ...
//static Uint8 vic3_palette_nibbles[0x300];

Uint8 vic_registers[0x80];		// VIC-4 registers
int vic_iomode;				// VIC2/VIC3/VIC4 mode
int force_fast;				// POKE 0,64 and 0,65 trick ...
int scanline;				// current scan line number
int cpu_cycles_per_scanline;
static int compare_raster;		// raster compare (9 bits width) data
static int interrupt_status;		// Interrupt status of VIC
int vic2_16k_bank;			// VIC-2 modes' 16K BANK address within 64K (NOT the traditional naming of banks with 0,1,2,3)
static Uint8 *sprite_pointers;		// Pointer to sprite pointers :)
static Uint8 *sprite_bank;
int vic3_blink_phase;			// blinking attribute helper, state.
static Uint8 raster_colours[512];
Uint8 c128_d030_reg;			// C128-like register can be only accessed in VIC-II mode but not in others, quite special!

int vic_vidp_legacy = 1, vic_chrp_legacy = 1, vic_sprp_legacy = 1;

#if 0
// UGLY: decides to use VIC-II/III method (val!=0), or the VIC-IV "precise address" selection (val == 0)
// this is based on the idea that VIC-II compatible register writing will set that, overriding the "precise" setting if there was any, before.
static int vic2_vidp_method = 1;
static int vic2_chrp_method = 1;
#endif


//#define CHECK_PIXEL_POINTER


#ifdef CHECK_PIXEL_POINTER
/* Temporary hack to be used in renders. Asserts out-of-texture accesses */
static Uint32 *pixel_pointer_check_base;
static Uint32 *pixel_pointer_check_end;
static const char *pixel_pointer_check_modn;
static inline void PIXEL_POINTER_CHECK_INIT( Uint32 *base, int tail, const char *module )
{
	pixel_pointer_check_base = base;
	pixel_pointer_check_end  = base + (640 + tail) * 200;
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




void vic_init ( void )
{
	vic4_init_palette();
	force_fast = 0;
	// *** Init VIC3 registers and palette
	vic2_16k_bank = 0;
	vic_iomode = VIC2_IOMODE;
	interrupt_status = 0;
	// FIXME: add ROM palette by default? What is ROM palette on MEGA65?
	scanline = 0;
	compare_raster = 0;
	// *** Just a check to try all possible regs (in VIC2,VIC3 and VIC4 modes), it should not panic ...
	// It may also sets/initializes some internal variables sets by register writes, which would cause a crash on screen rendering without prior setup!
	for (int i = 0; i < 0x140; i++) {	// $140=the last $40 register for VIC-2 mode, when we have fewer ones
		vic_write_reg(i, 0);
		(void)vic_read_reg(i);
	}
	c128_d030_reg = 0xFE;	// this may be set to 2MHz in the previous step, so be sure to set to FF here, BUT FIX: bit 0 should be inverted!!
	machine_set_speed(0);
	//vic_registers[0x30] = 4;	// ROM palette?
	//palette = vic_palettes + 0x400;
	DEBUG("VIC4: has been initialized." NL);
}



static void vic3_interrupt_checker ( void )
{
	int vic_irq_old = cpu65.irqLevel & 2;
	int vic_irq_new;
	if ((interrupt_status & vic_registers[0x1A])) {
		interrupt_status |= 128;
		vic_irq_new = 2;
	} else {
		interrupt_status &= 127;
		vic_irq_new = 0;
	}
	if (vic_irq_old != vic_irq_new) {
		DEBUG("VIC3: interrupt change %s -> %s" NL, vic_irq_old ? "active" : "inactive", vic_irq_new ? "active" : "inactive");
		if (vic_irq_new)
			cpu65.irqLevel |= 2;
		else
			cpu65.irqLevel &= ~2;
	}
}



void vic3_check_raster_interrupt ( void )
{
	raster_colours[scanline] = vic_registers[0x21];	// ugly hack to make some kind of raster-bars visible :-/
	if (scanline == compare_raster)
		interrupt_status |= 1;
	else
		interrupt_status &= 0xFE;
	vic3_interrupt_checker();
}


/* DESIGN of vic_read_reg() and vic_write_reg() functions:
   addr = 00-7F, VIC-IV registers 00-7F (ALWAYS, regardless of current I/O mode!)
   addr = 80-FF, VIC-III registers 00-7F (ALWAYS, regardless of current I/O mode!) [though for VIC-III, many registers are ignored after the last one]
   addr = 100-13F, VIC-II registers 00-3F (ALWAYS, regardless of current I/O mode!)
   NOTES:
	* on a real VIC-II last used register is $2E. However we need the KEY register ($2F) and the C128-style 2MHz mode ($30) on M65 too.
	* ALL cases must be handled!! from 000-13F for both of reading/writing funcs, otherwise Xemu will panic! this is a safety stuff
	* on write, later an M65-alike solution is needed: ie "hot registers" for VIC-II,VIC-III also writes VIC-IV specific registers then
	* currently MANY things are not handled, it will be the task of "move to VIC-IV internals" project ...
	* the purpose of ugly "tons of case" implementation that it should compile into a simple jump-table, which cannot be done faster too much ...
	* do not confuse these "vic reg mode" ranges with the vic_iomode variable, not so much direct connection between them! vic_iomode referred
	  for the I/O mode used on the "classic $D000 area" and DMA I/O access only
*/


static const char vic_registers_internal_mode_names[] = {'4', '3', '2'};

#define CASE_VIC_2(n) case n+0x100
#define CASE_VIC_3(n) case n+0x080
#define CASE_VIC_4(n) case n
#define CASE_VIC_ALL(n) CASE_VIC_2(n): CASE_VIC_3(n): CASE_VIC_4(n)
#define CASE_VIC_3_4(n) CASE_VIC_3(n): CASE_VIC_4(n)


void vic_write_reg ( unsigned int addr, Uint8 data )
{
	DEBUG("VIC%c: write reg $%02X (internally $%03X) with data $%02X" NL, XEMU_LIKELY(addr < 0x180) ? vic_registers_internal_mode_names[addr >> 7] : '?', addr & 0x7F, addr, data);
	// IMPORTANT NOTE: writing of vic_registers[] happens only *AFTER* this switch/case construct! This means if you need to do this before, you must do it manually at the right "case"!!!!
	// if you do so, you can even use "return" instead of "break" to save the then-redundant write of the register
	switch (addr) {
		CASE_VIC_ALL(0x00): CASE_VIC_ALL(0x01): CASE_VIC_ALL(0x02): CASE_VIC_ALL(0x03): CASE_VIC_ALL(0x04): CASE_VIC_ALL(0x05): CASE_VIC_ALL(0x06): CASE_VIC_ALL(0x07):
		CASE_VIC_ALL(0x08): CASE_VIC_ALL(0x09): CASE_VIC_ALL(0x0A): CASE_VIC_ALL(0x0B): CASE_VIC_ALL(0x0C): CASE_VIC_ALL(0x0D): CASE_VIC_ALL(0x0E): CASE_VIC_ALL(0x0F):
		CASE_VIC_ALL(0x10):
			break;		// Sprite coordinates: simple write the VIC reg in all I/O modes.
		CASE_VIC_ALL(0x11):
			compare_raster = (compare_raster & 0xFF) | ((data & 0x80) << 1);
			DEBUG("VIC: compare raster is now %d" NL, compare_raster);
			break;
		CASE_VIC_ALL(0x12):
			compare_raster = (compare_raster & 0xFF00) | data;
			DEBUG("VIC: compare raster is now %d" NL, compare_raster);
			break;
		CASE_VIC_ALL(0x13): CASE_VIC_ALL(0x14):
			return;		// FIXME: writing light-pen registers?????
		CASE_VIC_ALL(0x15):	// sprite enabled
		CASE_VIC_ALL(0x16):	// control-reg#2, we allow write even if non-used bits here
		CASE_VIC_ALL(0x17):	// sprite-Y expansion
			break;
		CASE_VIC_ALL(0x18):	// memory pointers
			if (!vic_vidp_legacy) {
				vic_vidp_legacy = 1;
				DEBUGPRINT("VIC4: compatibility screen address mode" NL);
			}
			if (!vic_chrp_legacy) {
				vic_chrp_legacy = 1;
				DEBUGPRINT("VIC4: compatibility character address mode" NL);
			}
			if (!vic_sprp_legacy) {
				vic_sprp_legacy = 1;
				DEBUGPRINT("VIC4: compatibility sprite pointer address mode" NL);
			}
			data &= 0xFE;
			break;
		CASE_VIC_ALL(0x19):
			interrupt_status = interrupt_status & (~data) & 0xF;
			vic3_interrupt_checker();
			break;
		CASE_VIC_ALL(0x1A):
			data &= 0xF;
			break;
		CASE_VIC_ALL(0x1B):	// sprite data priority
		CASE_VIC_ALL(0x1C):	// sprite multicolour
		CASE_VIC_ALL(0x1D):	// sprite-X expansion
			break;
		CASE_VIC_ALL(0x1E):	// sprite-sprite collision
		CASE_VIC_ALL(0x1F):	// sprite-data collision
			return;		// NOT writeable!
		CASE_VIC_2(0x20): CASE_VIC_2(0x21): CASE_VIC_2(0x22): CASE_VIC_2(0x23): CASE_VIC_2(0x24): CASE_VIC_2(0x25): CASE_VIC_2(0x26): CASE_VIC_2(0x27):
		CASE_VIC_2(0x28): CASE_VIC_2(0x29): CASE_VIC_2(0x2A): CASE_VIC_2(0x2B): CASE_VIC_2(0x2C): CASE_VIC_2(0x2D): CASE_VIC_2(0x2E):
			data &= 0xF;	// colour-related registers are 4 bit only for VIC-II
			break;
		CASE_VIC_3(0x20): CASE_VIC_3(0x21): CASE_VIC_3(0x22): CASE_VIC_3(0x23): CASE_VIC_3(0x24): CASE_VIC_3(0x25): CASE_VIC_3(0x26): CASE_VIC_3(0x27):
		CASE_VIC_3(0x28): CASE_VIC_3(0x29): CASE_VIC_3(0x2A): CASE_VIC_3(0x2B): CASE_VIC_3(0x2C): CASE_VIC_3(0x2D): CASE_VIC_3(0x2E):
			// FIXME TODO IS VIC-III also 4 bit only for colour regs?! according to c65manual.txt it seems! However according to M65's implementation it seems not ...
			// It seems, M65 policy for this VIC-III feature is: enable 8 bit colour entires if D031.5 is set (also extended attributes)
			if (!(vic_registers[0x31] & 32))
				data &= 0xF;
			break;
		CASE_VIC_4(0x20): CASE_VIC_4(0x21): CASE_VIC_4(0x22): CASE_VIC_4(0x23): CASE_VIC_4(0x24): CASE_VIC_4(0x25): CASE_VIC_4(0x26): CASE_VIC_4(0x27):
		CASE_VIC_4(0x28): CASE_VIC_4(0x29): CASE_VIC_4(0x2A): CASE_VIC_4(0x2B): CASE_VIC_4(0x2C): CASE_VIC_4(0x2D): CASE_VIC_4(0x2E):
			break;		// colour-related registers are full 8 bit for VIC-IV
		CASE_VIC_ALL(0x2F):	// the KEY register, it must be handled in ALL VIC modes, to be able to set VIC I/O mode
			do {
				int vic_new_iomode;
				if (data == 0x96 && vic_registers[0x2F] == 0xA5)
					vic_new_iomode = VIC3_IOMODE;
				else if (data == 0x53 && vic_registers[0x2F] == 0x47)
					vic_new_iomode = VIC4_IOMODE;
				else
					vic_new_iomode = VIC2_IOMODE;
				if (vic_new_iomode != vic_iomode) {
					DEBUG("VIC: changing I/O mode %d(%s) -> %d(%s)" NL, vic_iomode, iomode_names[vic_iomode], vic_new_iomode, iomode_names[vic_new_iomode]);
					vic_iomode = vic_new_iomode;
				}
			} while(0);
			break;
		CASE_VIC_2(0x30):	// this register is _SPECIAL_, and exists only in VIC-II (C64) I/O mode: C128-style "2MHz fast" mode ...
			c128_d030_reg = data;
			machine_set_speed(0);
			return;		// it IS important to have return here, since it's not a "real" VIC-4 mode register's view in another mode!!
		/* --- NO MORE VIC-II REGS FROM HERE --- */
		CASE_VIC_3_4(0x30):
			memory_set_vic3_rom_mapping(data);
			check_if_rom_palette(data & 4);
			break;
		CASE_VIC_3_4(0x31):
			vic_registers[0x31] = data;	// we need this work-around, since reg-write happens _after_ this switch statement, but machine_set_speed above needs it ...
			machine_set_speed(0);
			return;				// since we DID the write, it's OK to return here and not using "break"
		CASE_VIC_3_4(0x32): CASE_VIC_3_4(0x33): CASE_VIC_3_4(0x34): CASE_VIC_3_4(0x35): CASE_VIC_3_4(0x36): CASE_VIC_3_4(0x37): CASE_VIC_3_4(0x38):
		CASE_VIC_3_4(0x39): CASE_VIC_3_4(0x3A): CASE_VIC_3_4(0x3B): CASE_VIC_3_4(0x3C): CASE_VIC_3_4(0x3D): CASE_VIC_3_4(0x3E): CASE_VIC_3_4(0x3F):
		CASE_VIC_3_4(0x40): CASE_VIC_3_4(0x41): CASE_VIC_3_4(0x42): CASE_VIC_3_4(0x43): CASE_VIC_3_4(0x44): CASE_VIC_3_4(0x45): CASE_VIC_3_4(0x46):
		CASE_VIC_3_4(0x47):
			break;
		/* --- NO MORE VIC-III REGS FROM HERE --- */
		CASE_VIC_4(0x48): CASE_VIC_4(0x49): CASE_VIC_4(0x4A): CASE_VIC_4(0x4B): CASE_VIC_4(0x4C): CASE_VIC_4(0x4D): CASE_VIC_4(0x4E): CASE_VIC_4(0x4F):
		CASE_VIC_4(0x50): CASE_VIC_4(0x51): CASE_VIC_4(0x52): CASE_VIC_4(0x53):
			break;
		CASE_VIC_4(0x54):
			vic_registers[0x54] = data;	// we need this work-around, since reg-write happens _after_ this switch statement, but machine_set_speed above needs it ...
			machine_set_speed(0);
			return;				// since we DID the write, it's OK to return here and not using "break"
		CASE_VIC_4(0x55): CASE_VIC_4(0x56): CASE_VIC_4(0x57): CASE_VIC_4(0x58): CASE_VIC_4(0x59): CASE_VIC_4(0x5A): CASE_VIC_4(0x5B): CASE_VIC_4(0x5C):
		CASE_VIC_4(0x5D): CASE_VIC_4(0x5E): CASE_VIC_4(0x5F): /*CASE_VIC_4(0x60): CASE_VIC_4(0x61): CASE_VIC_4(0x62): CASE_VIC_4(0x63):*/ CASE_VIC_4(0x64):
		CASE_VIC_4(0x65): CASE_VIC_4(0x66): CASE_VIC_4(0x67): /*CASE_VIC_4(0x68): CASE_VIC_4(0x69): CASE_VIC_4(0x6A):*/ CASE_VIC_4(0x6B): /*CASE_VIC_4(0x6C):
		CASE_VIC_4(0x6D): CASE_VIC_4(0x6E):*/ CASE_VIC_4(0x6F): /*CASE_VIC_4(0x70):*/ CASE_VIC_4(0x71): CASE_VIC_4(0x72): CASE_VIC_4(0x73): CASE_VIC_4(0x74):
		CASE_VIC_4(0x75): CASE_VIC_4(0x76): CASE_VIC_4(0x77): CASE_VIC_4(0x78): CASE_VIC_4(0x79): CASE_VIC_4(0x7A): CASE_VIC_4(0x7B): CASE_VIC_4(0x7C):
		CASE_VIC_4(0x7D): CASE_VIC_4(0x7E): CASE_VIC_4(0x7F):
			break;
		CASE_VIC_4(0x60): CASE_VIC_4(0x61): CASE_VIC_4(0x62): CASE_VIC_4(0x63):
			if (vic_vidp_legacy) {
				vic_vidp_legacy = 0;
				DEBUGPRINT("VIC4: precise video address mode" NL);
			}
			break;
		CASE_VIC_4(0x68): CASE_VIC_4(0x69): CASE_VIC_4(0x6A):
			if (vic_chrp_legacy) {
				vic_chrp_legacy = 0;
				DEBUGPRINT("VIC4: precise character address mode" NL);
			}
			break;
		CASE_VIC_4(0x6C): CASE_VIC_4(0x6D): CASE_VIC_4(0x6E):
			if (vic_sprp_legacy) {
				vic_sprp_legacy = 0;
				DEBUGPRINT("VIC4: precise sprite pointer address mode" NL);
			}
			break;
		CASE_VIC_4(0x70):	// VIC-IV palette selection register
			palette		= ((data & 0x03) << 8) + vic_palettes;
			spritepalette	= ((data & 0x0C) << 6) + vic_palettes;
			altpalette	= ((data & 0x30) << 4) + vic_palettes;
			palregaccofs	= ((data & 0xC0) << 2);
			check_if_rom_palette(vic_registers[0x30] & 4);
			break;
		/* --- NON-EXISTING REGISTERS --- */
		CASE_VIC_2(0x31): CASE_VIC_2(0x32): CASE_VIC_2(0x33): CASE_VIC_2(0x34): CASE_VIC_2(0x35): CASE_VIC_2(0x36): CASE_VIC_2(0x37): CASE_VIC_2(0x38):
		CASE_VIC_2(0x39): CASE_VIC_2(0x3A): CASE_VIC_2(0x3B): CASE_VIC_2(0x3C): CASE_VIC_2(0x3D): CASE_VIC_2(0x3E): CASE_VIC_2(0x3F):
			DEBUG("VIC2: this register does not exist for this mode, ignoring write." NL);
			return;		// not existing VIC-II registers, do not write!
		CASE_VIC_3(0x48): CASE_VIC_3(0x49): CASE_VIC_3(0x4A): CASE_VIC_3(0x4B): CASE_VIC_3(0x4C): CASE_VIC_3(0x4D): CASE_VIC_3(0x4E): CASE_VIC_3(0x4F):
		CASE_VIC_3(0x50): CASE_VIC_3(0x51): CASE_VIC_3(0x52): CASE_VIC_3(0x53): CASE_VIC_3(0x54): CASE_VIC_3(0x55): CASE_VIC_3(0x56): CASE_VIC_3(0x57):
		CASE_VIC_3(0x58): CASE_VIC_3(0x59): CASE_VIC_3(0x5A): CASE_VIC_3(0x5B): CASE_VIC_3(0x5C): CASE_VIC_3(0x5D): CASE_VIC_3(0x5E): CASE_VIC_3(0x5F):
		CASE_VIC_3(0x60): CASE_VIC_3(0x61): CASE_VIC_3(0x62): CASE_VIC_3(0x63): CASE_VIC_3(0x64): CASE_VIC_3(0x65): CASE_VIC_3(0x66): CASE_VIC_3(0x67):
		CASE_VIC_3(0x68): CASE_VIC_3(0x69): CASE_VIC_3(0x6A): CASE_VIC_3(0x6B): CASE_VIC_3(0x6C): CASE_VIC_3(0x6D): CASE_VIC_3(0x6E): CASE_VIC_3(0x6F):
		CASE_VIC_3(0x70): CASE_VIC_3(0x71): CASE_VIC_3(0x72): CASE_VIC_3(0x73): CASE_VIC_3(0x74): CASE_VIC_3(0x75): CASE_VIC_3(0x76): CASE_VIC_3(0x77):
		CASE_VIC_3(0x78): CASE_VIC_3(0x79): CASE_VIC_3(0x7A): CASE_VIC_3(0x7B): CASE_VIC_3(0x7C): CASE_VIC_3(0x7D): CASE_VIC_3(0x7E): CASE_VIC_3(0x7F):
			DEBUG("VIC3: this register does not exist for this mode, ignoring write." NL);
			return;		// not existing VIC-III registers, do not write!
		/* --- FINALLY, IF THIS IS HIT, IT MEANS A MISTAKE SOMEWHERE IN MY CODE --- */
		default:
			FATAL("Xemu: invalid VIC internal register numbering on write: $%X", addr);
	}
	vic_registers[addr & 0x7F] = data;
}



Uint8 vic_read_reg ( int unsigned addr )
{
	Uint8 result = vic_registers[addr & 0x7F];	// read the answer by default (mostly this will be), allow to override/modify in the switch construct if needed
	switch (addr) {
		CASE_VIC_ALL(0x00): CASE_VIC_ALL(0x01): CASE_VIC_ALL(0x02): CASE_VIC_ALL(0x03): CASE_VIC_ALL(0x04): CASE_VIC_ALL(0x05): CASE_VIC_ALL(0x06): CASE_VIC_ALL(0x07):
		CASE_VIC_ALL(0x08): CASE_VIC_ALL(0x09): CASE_VIC_ALL(0x0A): CASE_VIC_ALL(0x0B): CASE_VIC_ALL(0x0C): CASE_VIC_ALL(0x0D): CASE_VIC_ALL(0x0E): CASE_VIC_ALL(0x0F):
		CASE_VIC_ALL(0x10):
			break;		// Sprite coordinates
		CASE_VIC_ALL(0x11):
			result = (result & 0x7F) | ((scanline & 0x100) >> 1);
			break;
		CASE_VIC_ALL(0x12):
			result = scanline & 0xFF;
			break;
		CASE_VIC_ALL(0x13): CASE_VIC_ALL(0x14):
			break;		// light-pen registers
		CASE_VIC_ALL(0x15):	// sprite enabled
			break;
		CASE_VIC_ALL(0x16):	// control-reg#2
			result |= 0xC0;
			break;
		CASE_VIC_ALL(0x17):	// sprite-Y expansion
			break;
		CASE_VIC_ALL(0x18):	// memory pointers
			result |= 1;
			break;
		CASE_VIC_ALL(0x19):
			result = interrupt_status | (64 + 32 + 16);
			break;
		CASE_VIC_ALL(0x1A):
			result |= 0xF0;
			break;
		CASE_VIC_ALL(0x1B):	// sprite data priority
		CASE_VIC_ALL(0x1C):	// sprite multicolour
		CASE_VIC_ALL(0x1D):	// sprite-X expansion
			break;
		CASE_VIC_ALL(0x1E):	// sprite-sprite collision
		CASE_VIC_ALL(0x1F):	// sprite-data collision
			vic_registers[addr & 0x7F] = 0;	// 1E and 1F registers are cleared on read!
			break;
		CASE_VIC_2(0x20): CASE_VIC_2(0x21): CASE_VIC_2(0x22): CASE_VIC_2(0x23): CASE_VIC_2(0x24): CASE_VIC_2(0x25): CASE_VIC_2(0x26): CASE_VIC_2(0x27):
		CASE_VIC_2(0x28): CASE_VIC_2(0x29): CASE_VIC_2(0x2A): CASE_VIC_2(0x2B): CASE_VIC_2(0x2C): CASE_VIC_2(0x2D): CASE_VIC_2(0x2E):
			result |= 0xF0;	// colour-related registers are 4 bit only for VIC-II
			break;
		CASE_VIC_3(0x20): CASE_VIC_3(0x21): CASE_VIC_3(0x22): CASE_VIC_3(0x23): CASE_VIC_3(0x24): CASE_VIC_3(0x25): CASE_VIC_3(0x26): CASE_VIC_3(0x27):
		CASE_VIC_3(0x28): CASE_VIC_3(0x29): CASE_VIC_3(0x2A): CASE_VIC_3(0x2B): CASE_VIC_3(0x2C): CASE_VIC_3(0x2D): CASE_VIC_3(0x2E):
			// FIXME TODO IS VIC-III also 4 bit only for colour regs?! according to c65manual.txt it seems! However according to M65's implementation it seems not ...
			// It seems, M65 policy for this VIC-III feature is: enable 8 bit colour entires if D031.5 is set (also extended attributes)
			if (!(vic_registers[0x31] & 32))
				result |= 0xF0;
			break;
		CASE_VIC_4(0x20): CASE_VIC_4(0x21): CASE_VIC_4(0x22): CASE_VIC_4(0x23): CASE_VIC_4(0x24): CASE_VIC_4(0x25): CASE_VIC_4(0x26): CASE_VIC_4(0x27):
		CASE_VIC_4(0x28): CASE_VIC_4(0x29): CASE_VIC_4(0x2A): CASE_VIC_4(0x2B): CASE_VIC_4(0x2C): CASE_VIC_4(0x2D): CASE_VIC_4(0x2E):
			break;		// colour-related registers are full 8 bit for VIC-IV
		CASE_VIC_ALL(0x2F):	// the KEY register
			break;
		CASE_VIC_2(0x30):	// this register is _SPECIAL_, and exists only in VIC-II (C64) I/O mode: C128-style "2MHz fast" mode ...
			result = c128_d030_reg;	// ... so we override "result" read before the "switch" statement!
			break;
		/* --- NO MORE VIC-II REGS FROM HERE --- */
		CASE_VIC_3_4(0x30):
			break;
		CASE_VIC_3_4(0x31):
			break;
		CASE_VIC_3_4(0x32): CASE_VIC_3_4(0x33): CASE_VIC_3_4(0x34): CASE_VIC_3_4(0x35): CASE_VIC_3_4(0x36): CASE_VIC_3_4(0x37): CASE_VIC_3_4(0x38):
		CASE_VIC_3_4(0x39): CASE_VIC_3_4(0x3A): CASE_VIC_3_4(0x3B): CASE_VIC_3_4(0x3C): CASE_VIC_3_4(0x3D): CASE_VIC_3_4(0x3E): CASE_VIC_3_4(0x3F):
		CASE_VIC_3_4(0x40): CASE_VIC_3_4(0x41): CASE_VIC_3_4(0x42): CASE_VIC_3_4(0x43): CASE_VIC_3_4(0x44): CASE_VIC_3_4(0x45): CASE_VIC_3_4(0x46):
		CASE_VIC_3_4(0x47):
			break;
		/* --- NO MORE VIC-III REGS FROM HERE --- */
		CASE_VIC_4(0x48): CASE_VIC_4(0x49): CASE_VIC_4(0x4A): CASE_VIC_4(0x4B): CASE_VIC_4(0x4C): CASE_VIC_4(0x4D): CASE_VIC_4(0x4E): CASE_VIC_4(0x4F):
		CASE_VIC_4(0x50): CASE_VIC_4(0x51):
			break;
		CASE_VIC_4(0x52):
			result = (scanline << 1) & 0xFF;	// hack: report phys raster always double of vic-II raster
			break;
		CASE_VIC_4(0x53):
			result = ((scanline << 1) >> 8) & 7;
			break;
		CASE_VIC_4(0x54):
			break;
		CASE_VIC_4(0x55): CASE_VIC_4(0x56): CASE_VIC_4(0x57): CASE_VIC_4(0x58): CASE_VIC_4(0x59): CASE_VIC_4(0x5A): CASE_VIC_4(0x5B): CASE_VIC_4(0x5C):
		CASE_VIC_4(0x5D): CASE_VIC_4(0x5E): CASE_VIC_4(0x5F): CASE_VIC_4(0x60): CASE_VIC_4(0x61): CASE_VIC_4(0x62): CASE_VIC_4(0x63): CASE_VIC_4(0x64):
		CASE_VIC_4(0x65): CASE_VIC_4(0x66): CASE_VIC_4(0x67): CASE_VIC_4(0x68): CASE_VIC_4(0x69): CASE_VIC_4(0x6A): CASE_VIC_4(0x6B): CASE_VIC_4(0x6C):
		CASE_VIC_4(0x6D): CASE_VIC_4(0x6E): CASE_VIC_4(0x6F): CASE_VIC_4(0x70): CASE_VIC_4(0x71): CASE_VIC_4(0x72): CASE_VIC_4(0x73): CASE_VIC_4(0x74):
		CASE_VIC_4(0x75): CASE_VIC_4(0x76): CASE_VIC_4(0x77): CASE_VIC_4(0x78): CASE_VIC_4(0x79): CASE_VIC_4(0x7A): CASE_VIC_4(0x7B): CASE_VIC_4(0x7C):
		CASE_VIC_4(0x7D): CASE_VIC_4(0x7E): CASE_VIC_4(0x7F):
			break;
		/* --- NON-EXISTING REGISTERS --- */
		CASE_VIC_2(0x31): CASE_VIC_2(0x32): CASE_VIC_2(0x33): CASE_VIC_2(0x34): CASE_VIC_2(0x35): CASE_VIC_2(0x36): CASE_VIC_2(0x37): CASE_VIC_2(0x38):
		CASE_VIC_2(0x39): CASE_VIC_2(0x3A): CASE_VIC_2(0x3B): CASE_VIC_2(0x3C): CASE_VIC_2(0x3D): CASE_VIC_2(0x3E): CASE_VIC_2(0x3F):
			DEBUG("VIC2: this register does not exist for this mode, $FF for read answer." NL);
			result = 0xFF;		// not existing VIC-II registers
			break;
		CASE_VIC_3(0x48): CASE_VIC_3(0x49): CASE_VIC_3(0x4A): CASE_VIC_3(0x4B): CASE_VIC_3(0x4C): CASE_VIC_3(0x4D): CASE_VIC_3(0x4E): CASE_VIC_3(0x4F):
		CASE_VIC_3(0x50): CASE_VIC_3(0x51): CASE_VIC_3(0x52): CASE_VIC_3(0x53): CASE_VIC_3(0x54): CASE_VIC_3(0x55): CASE_VIC_3(0x56): CASE_VIC_3(0x57):
		CASE_VIC_3(0x58): CASE_VIC_3(0x59): CASE_VIC_3(0x5A): CASE_VIC_3(0x5B): CASE_VIC_3(0x5C): CASE_VIC_3(0x5D): CASE_VIC_3(0x5E): CASE_VIC_3(0x5F):
		CASE_VIC_3(0x60): CASE_VIC_3(0x61): CASE_VIC_3(0x62): CASE_VIC_3(0x63): CASE_VIC_3(0x64): CASE_VIC_3(0x65): CASE_VIC_3(0x66): CASE_VIC_3(0x67):
		CASE_VIC_3(0x68): CASE_VIC_3(0x69): CASE_VIC_3(0x6A): CASE_VIC_3(0x6B): CASE_VIC_3(0x6C): CASE_VIC_3(0x6D): CASE_VIC_3(0x6E): CASE_VIC_3(0x6F):
		CASE_VIC_3(0x70): CASE_VIC_3(0x71): CASE_VIC_3(0x72): CASE_VIC_3(0x73): CASE_VIC_3(0x74): CASE_VIC_3(0x75): CASE_VIC_3(0x76): CASE_VIC_3(0x77):
		CASE_VIC_3(0x78): CASE_VIC_3(0x79): CASE_VIC_3(0x7A): CASE_VIC_3(0x7B): CASE_VIC_3(0x7C): CASE_VIC_3(0x7D): CASE_VIC_3(0x7E): CASE_VIC_3(0x7F):
			DEBUG("VIC3: this register does not exist for this mode, $FF for read answer." NL);
			result = 0xFF;
			break;			// not existing VIC-III registers
		/* --- FINALLY, IF THIS IS HIT, IT MEANS A MISTAKE SOMEWHERE IN MY CODE --- */
		default:
			FATAL("Xemu: invalid VIC internal register numbering on read: $%X", addr);
	}
	DEBUG("VIC%c: read reg $%02X (internally $%03X) with result $%02X" NL, XEMU_LIKELY(addr < 0x180) ? vic_registers_internal_mode_names[addr >> 7] : '?', addr & 0x7F, addr, result);
	vic_registers[0x51]++; 	//ugly hack, MEGAWAT wants this to change or what?!
	return result;
}


#undef CASE_VIC_2
#undef CASE_VIC_3
#undef CASE_VIC_4
#undef CASE_VIC_ALL
#undef CASE_VIC_3_4


static inline Uint8 *vic2_get_chargen_pointer ( void )
{
	if (vic_chrp_legacy) {
		int offs = (vic_registers[0x18] & 14) << 10;	// character generator address address within the current VIC2 bank
		//int crom = vic_registers[0x30] & 64;
		//DEBUG("VIC2: chargen: BANK=%04X OFS=%04X CROM=%d" NL, vic2_16k_bank, offs, crom);
		if ((vic2_16k_bank == 0x0000 || vic2_16k_bank == 0x8000) && (offs == 0x1000 || offs == 0x1800)) {  // check if chargen info is in ROM
			// In case of MEGA65, fetching char-info from ROM means to access the "WOM"
			// FIXME: what should I do with bit 6 of VIC-III register $30 ["CROM"] ?!
			return char_wom + offs - 0x1000;
		} else
			return main_ram + vic2_16k_bank + offs;
	} else {
		return main_ram + ((vic_registers[0x68] | (vic_registers[0x69] << 8) | (vic_registers[0x6A] << 16)) & ((512 << 10) - 1));
	}
}


//#define BG_FOR_Y(y) vic_registers[0x21]
#define BG_FOR_Y(y) raster_colours[(y) + 50]



/* At-frame-at-once (thus incorrect implementation) renderer for H640 (80 column)
   and "normal" (40 column) text VIC modes. Hardware attributes are not supported!
   No support for MCM and ECM!  */
static inline void vic2_render_screen_text ( Uint32 *p, int tail )
{
	Uint32 bg;
	Uint8 *vidp, *colp = colour_ram;
	int x = 0, y = 0, xlim, ylim, charline = 0;
	Uint8 *chrg = vic2_get_chargen_pointer();
	int inc_p = (vic_registers[0x54] & 1) ? 2 : 1;	// VIC-IV (MEGA65) 16 bit text mode?
	int scanline = 0;
	if (vic_registers[0x31] & 128) { // check H640 bit: 80 column mode?
		xlim = 79;
		ylim = 24;
		// Note: VIC2 sees ROM at some addresses thing is not emulated yet for other thing than chargen memory!
		// Note: according to the specification bit 4 has no effect in 80 columns mode!
		vidp = main_ram + ((vic_registers[0x18] & 0xE0) << 6) + vic2_16k_bank;
		sprite_pointers = vidp + 2040;
	} else {
		xlim = 39;
		ylim = 24;
		// Note: VIC2 sees ROM at some addresses thing is not emulated yet for other thing than chargen memory!
		vidp = main_ram + ((vic_registers[0x18] & 0xF0) << 6) + vic2_16k_bank;
		sprite_pointers = vidp + 1016;
	}
	// Ugly hack, override video ram if no legacy starting address policy applied
	if (!vic_vidp_legacy) {
		vidp = main_ram + ((vic_registers[0x60] | (vic_registers[0x61] << 8) | (vic_registers[0x62] << 16)) & ((512 << 10) - 1));
	}
	if (!vic_sprp_legacy) {
		sprite_pointers = main_ram + ((vic_registers[0x6C] | (vic_registers[0x6D] << 8) | (vic_registers[0x6E] << 16)) & ((512 << 10) - 1));
	}
	//DEBUGPRINT("VIC4: vidp = $%X, vic_vidp_legacy=%X" NL, (unsigned int)(vidp - main_ram), vic_vidp_legacy);
	// Target SDL pixel related format for the background colour
	bg = palette[BG_FOR_Y(0)];
	PIXEL_POINTER_CHECK_INIT(p, tail, "vic2_render_screen_text");
	for (;;) {
		Uint8 coldata = *colp;
		Uint32 fg;
		if (
			inc_p == 2 && (		// D054 bit 0 controlled stuff (16bit mode)
			(vidp[1] == 0 && (vic_registers[0x54] & 2)) ||	// enabled for =<$FF chars
			(vidp[1] && (vic_registers[0x54] & 4))		// enabled for >$FF chars
		)) {
			if (vidp[0] == 0xFF && vidp[1] == 0xFF) {
				// end of line marker, let's use background to fill the rest of the line ...
				// FIXME: however in the current situation we can't do that because of the "fixed" line length for 80 or 40 chars ... :(
				p += xlim == 39 ? 16 : 8;	// so we just ignore ... FIXME !!
			} else {
				int a;
				Uint8 *cp = main_ram + (((vidp[0] << 6) + (charline << 3) + (vidp[1] << 14)) & 0x7ffff); // and-mask: wrap-around @ 512K of RAM [though only 384K is used by M65]
				for (a = 0; a < 8; a++) {
					if (xlim != 79)
						*(p++) = palette[*cp];
					*(p++) = palette[*(cp++)];
				}
			}
		} else {
			Uint8 chrdata = chrg[(*vidp << 3) + charline];
			if (vic_registers[0x31] & 32) { 	// ATTR bit mode
				if ((coldata & 0xF0) == 0x10) {	// only the blink bit for the character is set
					if (vic3_blink_phase)
						chrdata = 0;	// blinking character, in one phase, the character "disappears", ie blinking
					coldata &= 15;
				} else if ((!(coldata & 0x10)) || vic3_blink_phase) {
					if (coldata & 0x80 && charline == 7)	// underline (must be before reverse, as underline can be reversed as well!)
						chrdata = 0XFF; // the underline
					if (coldata & 0x20)	// reverse bit for char
						chrdata = ~chrdata;
					if (coldata & 0x40)	// highlight, this must be the LAST, since it sets the low nibble of coldata ...
						coldata = 0x10 | (coldata & 15);
					else
						coldata &= 15;
				} else
					coldata &= 15;
			} else
				coldata &= 15;
			fg = palette[coldata];
			// FIXME: no ECM, MCM stuff ...
			if (xlim == 79) {
				PIXEL_POINTER_CHECK_ASSERT(p + 7);
				*(p++) = chrdata & 128 ? fg : bg;
				*(p++) = chrdata &  64 ? fg : bg;
				*(p++) = chrdata &  32 ? fg : bg;
				*(p++) = chrdata &  16 ? fg : bg;
				*(p++) = chrdata &   8 ? fg : bg;
				*(p++) = chrdata &   4 ? fg : bg;
				*(p++) = chrdata &   2 ? fg : bg;
				*(p++) = chrdata &   1 ? fg : bg;
			} else {
				PIXEL_POINTER_CHECK_ASSERT(p + 15);
				p[ 0] = p[ 1] = chrdata & 128 ? fg : bg;
				p[ 2] = p[ 3] = chrdata &  64 ? fg : bg;
				p[ 4] = p[ 5] = chrdata &  32 ? fg : bg;
				p[ 6] = p[ 7] = chrdata &  16 ? fg : bg;
				p[ 8] = p[ 9] = chrdata &   8 ? fg : bg;
				p[10] = p[11] = chrdata &   4 ? fg : bg;
				p[12] = p[13] = chrdata &   2 ? fg : bg;
				p[14] = p[15] = chrdata &   1 ? fg : bg;
				p += 16;
			}
		}
		colp += inc_p;
		vidp += inc_p;
		if (x == xlim) {
			p += tail;
			x = 0;
			if (charline == 7) {
				if (y == ylim)
					break;
				y++;
				charline = 0;
			} else {
				charline++;
				vidp -= (xlim + 1) * inc_p;
				colp -= (xlim + 1) * inc_p;
			}
			bg = palette[BG_FOR_Y(++scanline)];
		} else
			x++;
	}
	PIXEL_POINTER_FINAL_ASSERT(p);
}



// VIC2 bitmap mode, now only HIRES mode (no MCM yet), without H640 VIC3 feature!!
// I am not even sure if H640 would work here, as it needs almost all the 16K of area what VIC-II can see,
// that is, not so much RAM for the video matrix left would be used for the attribute information.
// Note: VIC2 sees ROM at some addresses thing is not emulated yet!
static inline void vic2_render_screen_bmm ( Uint32 *p, int tail )
{
	int x = 0, y = 0, charline = 0;
	Uint8 *vidp, *chrp;
	vidp = main_ram + ((vic_registers[0x18] & 0xF0) << 6) + vic2_16k_bank;
	sprite_pointers = vidp + 1016;
	chrp = main_ram + ((vic_registers[0x18] & 8) ? 8192 : 0) + vic2_16k_bank;
	PIXEL_POINTER_CHECK_INIT(p, tail, "vic2_render_screen_bmm");
	for (;;) {
		Uint8  data = *(vidp++);
		Uint32 bg = palette[data & 15];
		Uint32 fg = palette[data >> 4];
		data = *chrp;
		chrp += 8;
		PIXEL_POINTER_CHECK_ASSERT(p);
		p[ 0] = p[ 1] = data & 128 ? fg : bg;
		p[ 2] = p[ 3] = data &  64 ? fg : bg;
		p[ 4] = p[ 5] = data &  32 ? fg : bg;
		p[ 6] = p[ 7] = data &  16 ? fg : bg;
		p[ 8] = p[ 9] = data &   8 ? fg : bg;
		p[10] = p[11] = data &   4 ? fg : bg;
		p[12] = p[13] = data &   2 ? fg : bg;
		p[14] = p[15] = data &   1 ? fg : bg;
		p += 16;
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



// Renderer for bit-plane mode
// NOTE: currently H1280 and V400 is NOT implemented
// Note: I still think that bitplanes are children of evil, my brain simply cannot get them
// takes hours and many confusions all the time, even if I *know* what they are :)
// And hey dude, if it's not enough, there is time multiplex of bitplanes (not supported),
// V400 + interlace odd/even scan addresses, and the original C64-like non-linear build-up
// of the bitplane structure. Phewwww ....
static inline void vic3_render_screen_bpm ( Uint32 *p, int tail )
{
	int bitpos = 128, charline = 0, offset = 0;
	int xlim, x = 0, y = 0, h640 = (vic_registers[0x31] & 128);
	Uint8 bpe, *bp[8];
	bp[0] = main_ram + ((vic_registers[0x33] & (h640 ? 12 : 14)) << 12);
	bp[1] = main_ram + ((vic_registers[0x34] & (h640 ? 12 : 14)) << 12) + 0x10000;
	bp[2] = main_ram + ((vic_registers[0x35] & (h640 ? 12 : 14)) << 12);
	bp[3] = main_ram + ((vic_registers[0x36] & (h640 ? 12 : 14)) << 12) + 0x10000;
	bp[4] = main_ram + ((vic_registers[0x37] & (h640 ? 12 : 14)) << 12);
	bp[5] = main_ram + ((vic_registers[0x38] & (h640 ? 12 : 14)) << 12) + 0x10000;
	bp[6] = main_ram + ((vic_registers[0x39] & (h640 ? 12 : 14)) << 12);
	bp[7] = main_ram + ((vic_registers[0x3A] & (h640 ? 12 : 14)) << 12) + 0x10000;
	bpe = vic_registers[0x32];	// bit planes enabled mask
	if (h640) {
		bpe &= 15;		// it seems, with H640, only 4 bitplanes can be used (on lower 4 ones)
		xlim = 79;
		sprite_pointers = bp[2] + 0x3FF8;	// FIXME: just guessing
	} else {
		xlim = 39;
		sprite_pointers = bp[2] + 0x1FF8;	// FIXME: just guessing
	}
        DEBUG("VIC3: bitplanes: enable_mask=$%02X comp_mask=$%02X H640=%d" NL,
		bpe, vic_registers[0x3B], h640 ? 1 : 0
	);
	PIXEL_POINTER_CHECK_INIT(p, tail, "vic3_render_screen_bpm");
	for (;;) {
		Uint32 col = palette[((				// Do not try this at home ...
			(((*(bp[0] + offset)) & bitpos) ?   1 : 0) |
			(((*(bp[1] + offset)) & bitpos) ?   2 : 0) |
			(((*(bp[2] + offset)) & bitpos) ?   4 : 0) |
			(((*(bp[3] + offset)) & bitpos) ?   8 : 0) |
			(((*(bp[4] + offset)) & bitpos) ?  16 : 0) |
			(((*(bp[5] + offset)) & bitpos) ?  32 : 0) |
			(((*(bp[6] + offset)) & bitpos) ?  64 : 0) |
			(((*(bp[7] + offset)) & bitpos) ? 128 : 0)
			) & bpe) ^ vic_registers[0x3B]
		];
		PIXEL_POINTER_CHECK_ASSERT(p);
		*(p++) = col;
		if (!h640) {
			PIXEL_POINTER_CHECK_ASSERT(p);
			*(p++) = col;
		}
		if (bitpos == 1) {
			if (x == xlim) {
				if (charline == 7) {
					if (y == 24)
						break;
					y++;
					charline = 0;
					offset -= 7;
				} else {
					charline++;
					offset -= h640 ? 639 : 319;
				}
				p += tail;
				x = 0;
			} else
				x++;
			bitpos = 128;
			offset += 8;
		} else
			bitpos >>= 1;
	}
	PIXEL_POINTER_FINAL_ASSERT(p);
}


#define SPRITE_X_START_SCREEN	24
#define SPRITE_Y_START_SCREEN	50


#if 0
/* Extremely incorrect sprite emulation! BUGS:
   * Sprites cannot be behind the background (sprite priority)
   * Multicolour sprites are not supported
   * No sprite-background collision detection
   * No sprite-sprite collision detection
   * This is a simple, after-the-rendered-frame render-sprites one-by-one algorithm
   * This also requires to give up direct rendering if a sprite is enabled
   * Very ugly, quick&dirty hack, not so optimal either, even without the other mentioned bugs ...
*/
static void render_sprite ( int sprite_no, int sprite_mask, Uint8 *data, Uint32 *p, int tail )
{
	int sprite_y = vic_registers[sprite_no * 2 + 1] - SPRITE_Y_START_SCREEN;
	int sprite_x = ((vic_registers[sprite_no * 2] | ((vic_registers[16] & sprite_mask) ? 0x100 : 0)) - SPRITE_X_START_SCREEN) * 2;
	Uint32 colour = palette[vic_registers[39 + sprite_no] & 15];
	int expand_x = vic_registers[29] & sprite_mask;
	int expand_y = vic_registers[23] & sprite_mask;
	int lim_y = sprite_y + ((expand_y) ? 42 : 21);
	int y;
	p += (640 + tail) * sprite_y;
	for (y = sprite_y; y < lim_y; y += (expand_y ? 2 : 1), p += (640 + tail) * (expand_y ? 2 : 1))
		if (y < 0 || y >= 200)
			data += 3;	// skip one line (three bytes) of sprite data if outside of screen
		else {
			int mask, a, x = sprite_x;
			for (a = 0; a < 3; a++) {
				for (mask = 128; mask; mask >>= 1) {
					if (*data & mask) {
						if (x >= 0 && x < 640) {
							p[x] = p[x + 1] = colour;
							if (expand_y && y < 200)
								p[x + 640 + tail] = p[x + 641 + tail] = colour;
						}
						x += 2;
						if (expand_x && x >= 0 && x < 640) {
							p[x] = p[x + 1] = colour;
							if (expand_y && y < 200)
								p[x + 640 + tail] = p[x + 641 + tail] = colour;
							x += 2;
						}
					} else
						x += expand_x ? 4 : 2;
				}
				data++;
			}
		}
}


#else

// kust temporaty to bridge the differences between my C65 emu (where I copy this code from)
// and current M65 emu implementation. This WILL change a lot in the future, the whole VIC-II/III/IV stuff ...
#define TOP_BORDER_SIZE 0
#define LEFT_BORDER_SIZE 0
//#define VIC_REG_COLOUR(n) palette[vic_registers[n] & 15]
#define VIC_REG_COLOUR(n) palette[vic_registers[n]]

/* Extremely incorrect sprite emulation! BUGS:
   * Sprites cannot be behind the background (sprite priority)
   * No sprite-background collision detection
   * No sprite-sprite collision detection
   * This is a simple, after-the-rendered-frame render-sprites one-by-one algorithm
   * Very ugly, quick&dirty hack, not so optimal either, even without the other mentioned bugs ...
*/
static void render_sprite ( int sprite_no, int sprite_mask, Uint8 *data, Uint32 *p, int tail )
{
	Uint32 colours[4];
	int sprite_y = vic_registers[sprite_no * 2 + 1] - SPRITE_Y_START_SCREEN;
	int sprite_x = ((vic_registers[sprite_no * 2] | ((vic_registers[16] & sprite_mask) ? 0x100 : 0)) - SPRITE_X_START_SCREEN) * 2;
	int expand_x = vic_registers[29] & sprite_mask;
	int expand_y = vic_registers[23] & sprite_mask;
	int lim_y = sprite_y + ((expand_y) ? 42 : 21);
	int mcm = vic_registers[0x1C] & sprite_mask;
	int y;
	colours[2] = VIC_REG_COLOUR(39 + sprite_no);
	if (mcm) {
		colours[0] = 0;	// transparent, not a real colour, just signaling of transparency
		colours[1] = VIC_REG_COLOUR(0x25);
		colours[3] = VIC_REG_COLOUR(0x26);
	}
	p += SCREEN_WIDTH * (sprite_y + TOP_BORDER_SIZE) + LEFT_BORDER_SIZE;
	for (y = sprite_y; y < lim_y; y += (expand_y ? 2 : 1), p += SCREEN_WIDTH * (expand_y ? 2 : 1))
		if (y < 0 || y >= 200)
			data += 3;	// skip one line (three bytes) of sprite data if outside of screen
		else {
			int mask, a, x = sprite_x;
			for (a = 0; a < 3; a++) {
				if (mcm) {
					for (mask = 6; mask >=0; mask -= 2) {
						Uint32 col = colours[(*data >> mask) & 3];
						if (col) {
							if (x >= 0 && x < 640) {
								p[x] = p[x + 1] = p[x + 2] = p[x + 3] = col;
								if (expand_y && y < 200)
									p[x + SCREEN_WIDTH] = p[x + SCREEN_WIDTH + 1] = p[x + SCREEN_WIDTH + 2] = p[x + SCREEN_WIDTH + 3] = col;
							}
							x += 4;
							if (expand_x && x >= 0 && x < 640) {
								p[x] = p[x + 1] = p[x + 2] = p[x + 3] = col;
								if (expand_y && y < 200)
									p[x + SCREEN_WIDTH] = p[x + SCREEN_WIDTH + 1] = p[x + SCREEN_WIDTH + 2] = p[x + SCREEN_WIDTH + 3] = col;
								x += 4;
							}
						} else
							x += expand_x ? 8 : 4;
					}
				} else {
					for (mask = 128; mask; mask >>= 1) {
						if (*data & mask) {
							if (x >= 0 && x < 640) {
								p[x] = p[x + 1] = colours[2];
								if (expand_y && y < 200)
									p[x + SCREEN_WIDTH] = p[x + SCREEN_WIDTH + 1] = colours[2];
							}
							x += 2;
							if (expand_x && x >= 0 && x < 640) {
								p[x] = p[x + 1] = colours[2];
								if (expand_y && y < 200)
									p[x + SCREEN_WIDTH] = p[x + SCREEN_WIDTH + 1] = colours[2];
								x += 2;
							}
						} else
							x += expand_x ? 4 : 2;
					}
				}
				data++;
			}
		}
}


#endif


/* This is the one-frame-at-once (highly incorrect implementation, that is)
   renderer. It will call legacy VIC2 text mode render (optionally with
   80 columns mode, though, ECM, MCM, hardware attributes are not supported),
   VIC2 legacy HIRES mode (MCM is not supported), or bitplane modes (V400,
   H1280, odd scanning/interlace is not supported). Sprites, screen positioning,
   etc is not supported */
void vic_render_screen ( void )
{
	int tail_sdl;
	Uint32 *p_sdl = xemu_start_pixel_buffer_access(&tail_sdl);
	int sprites = vic_registers[0x15];
	if (vic_registers[0x31] & 16) {
	        sprite_bank = main_ram + ((vic_registers[0x35] & 12) << 12);	// FIXME: just guessing: sprite bank is bitplane 2 area, always 16K regardless of H640?
		vic3_render_screen_bpm(p_sdl, tail_sdl);
	} else {
		sprite_bank = vic2_16k_bank + main_ram;				// VIC2 legacy modes uses the VIC2 bank for sure, as the sprite bank too
		if (vic_registers[0x11] & 32)
			vic2_render_screen_bmm(p_sdl, tail_sdl);
		else
			vic2_render_screen_text(p_sdl, tail_sdl);
	}
	if (sprites) {	// Render sprites. VERY BAD. We ignore sprite priority as well (cannot be behind the background)
		//if (warn_sprites) {
		//	INFO_WINDOW("WARNING: Sprite emulation is really bad! (enabled_mask=$%02X)", sprites);
		//	warn_sprites = 0;
		//}
		for (int a = 7; a >= 0; a--) {
			int mask = 1 << a;
			if ((sprites & mask))
				render_sprite(a, mask, sprite_bank + (sprite_pointers[a] << 6), p_sdl, tail_sdl);	// sprite_pointers are set by the renderer functions above!
		}
	}

#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
	// Screenshot
	if (XEMU_UNLIKELY(register_screenshot_request)) {
		register_screenshot_request = 0;
		if (!xemu_screenshot_png(
			"@", "screenshot.png",
			1,
			2,
			NULL,	// allow function to figure it out ;)
			SCREEN_WIDTH,
			SCREEN_HEIGHT
		))
			OSD(-1, -1, "Screenshot has been taken");
	}
#endif
	xemu_update_screen();
}


/* --- SNAPSHOT RELATED --- */


#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_VIC4_BLOCK_VERSION	2
#define SNAPSHOT_VIC4_BLOCK_SIZE	(0x100 + ((NO_OF_PALETTE_REGS) * 3))

int vic4_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_VIC4_BLOCK_SIZE];
	int a;
	if (block->block_version != SNAPSHOT_VIC4_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad VIC-4 block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	for (a = 0; a < 0x80; a++)
		vic_write_reg(a, buffer[a + 0x80]);
	c128_d030_reg = buffer[0x7F];
	memcpy(vic_palette_bytes_red,   buffer + 0x100                         , NO_OF_PALETTE_REGS);
	memcpy(vic_palette_bytes_green, buffer + 0x100 +     NO_OF_PALETTE_REGS, NO_OF_PALETTE_REGS);
	memcpy(vic_palette_bytes_blue,  buffer + 0x100 + 2 * NO_OF_PALETTE_REGS, NO_OF_PALETTE_REGS);
	vic4_revalidate_all_palette();
	vic_iomode = buffer[0];
	DEBUG("SNAP: VIC: changing I/O mode to %d(%s)" NL, vic_iomode, iomode_names[vic_iomode]);
	interrupt_status = (int)P_AS_BE32(buffer + 1);
	return 0;
}


int vic4_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_VIC4_BLOCK_SIZE];
	int a = xemusnap_write_block_header(def->idstr, SNAPSHOT_VIC4_BLOCK_VERSION);
	if (a) return a;
	memset(buffer, 0xFF, sizeof buffer);
	/* saving state ... */
	memcpy(buffer + 0x80,  vic_registers, 0x80);		//  $80 bytes
	buffer[0x7F] = c128_d030_reg;
	memcpy(buffer + 0x100                         , vic_palette_bytes_red,   NO_OF_PALETTE_REGS);
	memcpy(buffer + 0x100 +     NO_OF_PALETTE_REGS, vic_palette_bytes_green, NO_OF_PALETTE_REGS);
	memcpy(buffer + 0x100 + 2 * NO_OF_PALETTE_REGS, vic_palette_bytes_blue,  NO_OF_PALETTE_REGS);
	buffer[0] = vic_iomode;
	U32_AS_BE(buffer + 1, interrupt_status);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
