/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the VIC-IV "emulation". Currently it does one-frame-at-once
   kind of horrible work, and only a subset of VIC2 and VIC3 knowledge
   is implemented, with some light VIC-IV features, to be able to "boot"
   of Mega-65 with standard configuration (kickstart, SD-card).
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
#include "mega65.h"
#include "xemu/cpu65.h"
#include "vic4.h"
#include "memory_mapper.h"

#define RGB(r,g,b) rgb_palette[((r) << 8) | ((g) << 4) | (b)]

static const char *iomode_names[4] = { "VIC2", "VIC3", "BAD!", "VIC4" };

// (SDL) target texture rendering pointers
static Uint32 *current_pixel;			// current_pixel pointer to the rendering target (one current_pixel: 32 bit)
static Uint32 *pixel_end, *pixel_start;	// points to the end and start of the buffer
static Uint32 *pixel_raster_start;		// first pixel of current raster
static Uint32 rgb_palette[4096];		// all the C65 palette, 4096 colours (SDL current_pixel format related form)
static Uint32 vic3_palette[0x100];		// VIC3 palette in SDL current_pixel format related form (can be written into the texture directly to be rendered)
static Uint32 vic3_rom_palette[0x100];	// the "ROM" palette, for C64 colours (with some ticks, ie colours above 15 are the same as the "normal" programmable palette)
static Uint32 *palette;					// the selected palette ...
static Uint8 vic3_palette_nibbles[0x300];
Uint8 vic_registers[0x80];				// VIC-3 registers. It seems $47 is the last register. But to allow address the full VIC3 reg I/O space, we use $80 here
int vic_iomode;							// VIC2/VIC3/VIC4 mode
int force_fast;							// POKE 0,64 and 0,65 trick ...
int cpu_cycles_per_scanline;
static int compare_raster;				// raster compare (9 bits width) data
static int interrupt_status;			// Interrupt status of VIC
int vic2_16k_bank;						// VIC-2 modes' 16K BANK address within 64K (NOT the traditional naming of banks with 0,1,2,3)
int vic3_blink_phase;					// blinking attribute helper, state.
Uint8 c128_d030_reg;					// C128-like register can be only accessed in VIC-II mode but not in others, quite special!
static Uint8 reg_d018_screen_addr = 0;     // Legacy VIC-II $D018 screen address register
static int vic_hotreg_touched = 0; 		// If any "legacy" registers were touched
static int vic4_sideborder_touched = 0;  // If side-border register were touched
static int border_x_left= 0;			 // Side border left 
static int border_x_right= 0;			 // Side border right
static int xcounter = 0, ycounter = 0;   // video counters
static Uint8* screen_ram_current_ptr = NULL;
static Uint8* colour_ram_current_ptr = NULL;
extern int user_scanlines_setting;

static int warn_ctrl_b_lo = 1;

// VIC-IV Modeline Parameters
// ----------------------------------------------------
const int text_height_200 = 400;
const int text_height_400 = 400;
const int chargen_y_scale_200 = 2;
const int chargen_y_scale_400 = 1;
const int chargen_y_pixels= 0;
const int top_borders_height_200 = SCREEN_HEIGHT - text_height_200;
const int top_borders_height_400=  SCREEN_HEIGHT - text_height_200;
const int single_top_border_200 = top_borders_height_200 >> 1;
const int single_top_border_400 = top_borders_height_400 >> 1;

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
	int r, g, b, i;
	// *** Init 4096 element palette with RGB components for faster access later on palette register changes (avoid SDL calls to convert)
	// TODO: for M65 mode, where a colour component is 8 bit, this is kind of problematic though :(
	for (r = 0, i = 0; r < 16; r++)
		for (g = 0; g < 16; g++)
			for (b = 0; b < 16; b++)
				rgb_palette[i++] = SDL_MapRGBA(sdl_pix_fmt, r * 17, g * 17, b * 17, 0xFF); // 15*17=255, last arg 0xFF: alpha channel for SDL
	force_fast = 0;
	// *** Init VIC3 registers and palette
	vic2_16k_bank = 0;
	vic_iomode = VIC2_IOMODE;
	interrupt_status = 0;
	palette = vic3_rom_palette;
	compare_raster = 0;
	for (i = 0; i < 0x100; i++) {	// Initiailize all palette registers to zero, initially, to have something ...
		if (i < sizeof vic_registers)
			vic_registers[i] = 0;	// Also the VIC registers ...
		vic3_rom_palette[i] = vic3_palette[i] = rgb_palette[0];
		vic3_palette_nibbles[i] = 0;
		vic3_palette_nibbles[i + 0x100] = 0;
		vic3_palette_nibbles[i + 0x200] = 0;
	}
	// *** the ROM palette "fixed" colours
	vic3_rom_palette[ 0] = RGB( 0,  0,  0);	// black
	vic3_rom_palette[ 1] = RGB(15, 15, 15);	// white
	vic3_rom_palette[ 2] = RGB(15,  0,  0);	// red
	vic3_rom_palette[ 3] = RGB( 0, 15, 15);	// cyan
	vic3_rom_palette[ 4] = RGB(15,  0, 15);	// magenta
	vic3_rom_palette[ 5] = RGB( 0, 15,  0);	// green
	vic3_rom_palette[ 6] = RGB( 0,  0, 15);	// blue
	vic3_rom_palette[ 7] = RGB(15, 15,  0);	// yellow
	vic3_rom_palette[ 8] = RGB(15,  6,  0);	// orange
	vic3_rom_palette[ 9] = RGB(10,  4,  0);	// brown
	vic3_rom_palette[10] = RGB(15,  7,  7);	// pink
	vic3_rom_palette[11] = RGB( 5,  5,  5);	// dark grey
	vic3_rom_palette[12] = RGB( 8,  8,  8);	// medium grey
	vic3_rom_palette[13] = RGB( 9, 15,  9);	// light green
	vic3_rom_palette[14] = RGB( 9,  9, 15);	// light blue
	vic3_rom_palette[15] = RGB(11, 11, 11);	// light grey
	// *** Just a check to try all possible regs (in VIC2,VIC3 and VIC4 modes), it should not panic ...
	// It may also sets/initializes some internal variables sets by register writes, which would cause a crash on screen rendering without prior setup!
	for (i = 0; i < 0x140; i++) {
		vic_write_reg(i, 0);
		(void)vic_read_reg(i);
	}
	c128_d030_reg = 0xFE;	// this may be set to 2MHz in the previous step, so be sure to set to FF here, BUT FIX: bit 0 should be inverted!!
	machine_set_speed(0);
	
	screen_ram_current_ptr = main_ram + SCREEN_ADDR;
	colour_ram_current_ptr = colour_ram;

	DEBUG("VIC4: has been initialized." NL);
}

void vic4_open_frame_access()
{
	int tail_sdl;
	current_pixel = pixel_start = xemu_start_pixel_buffer_access(&tail_sdl);
	pixel_end = current_pixel + (SCREEN_WIDTH * SCREEN_HEIGHT);
	if (tail_sdl)
		FATAL("tail_sdl is not zero!");
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
	//raster_colours[scanline] = vic_registers[0x21];	// ugly hack to make some kind of raster-bars visible :-/
	if (ycounter == compare_raster)
		interrupt_status |= 1;
	else
		interrupt_status &= 0xFE;
	vic3_interrupt_checker();
}

static void vic4_update_sideborder_dimensions()
{
	if (REG_CSEL) // 40-columns?
	{
		border_x_left = FRAME_H_FRONT + SINGLE_SIDE_BORDER;

		if (!REG_H640)
		{
			border_x_right = FRAME_H_FRONT + SCREEN_WIDTH - SINGLE_SIDE_BORDER;
		}
		else //80-col mode
		{
			border_x_right = FRAME_H_FRONT + SCREEN_WIDTH - SINGLE_SIDE_BORDER + 1;
		}
	}
	else // 38-columns
	{
		border_x_right = FRAME_H_FRONT + SCREEN_WIDTH - SINGLE_SIDE_BORDER - 18;

		if (!REG_H640)
		{
			border_x_left = FRAME_H_FRONT + SINGLE_SIDE_BORDER + 14;
		}
		else //78-col mode
		{
			border_x_left = FRAME_H_FRONT + SINGLE_SIDE_BORDER + 15;
		}
	}
}

static void vic4_interpret_legacy_mode_registers()
{
	// See https://github.com/MEGA65/mega65-core/blob/257d78aa6a21638cb0120fd34bc0e6ab11adfd7c/src/vhdl/viciv.vhdl#L1277

	const int vsync_delay_drive = 0;

	vic4_update_sideborder_dimensions();

	if (REG_CSEL) // 40-columns? 
	{
		if (!REG_H640) 
		{
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL));
		}
		else //80-col mode
		{
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL) - 1);
		}
	}
	else // 38-columns
	{ 
		if (!REG_H640) 
		{
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL));
		}
		else //78-col mode
		{
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL) - 1);
		}
	}

	if (!REG_V400) // Standard mode (200-lines)
	{
		if (REG_RSEL) // 25-row
		{
			SET_BORDER_Y_TOP(RASTER_CORRECTION + single_top_border_200 + vsync_delay_drive);
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + SCREEN_HEIGHT - single_top_border_200 + vsync_delay_drive);
		}
		else
		{
			SET_BORDER_Y_TOP(RASTER_CORRECTION + single_top_border_200 + vsync_delay_drive + 8);
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + SCREEN_HEIGHT + vsync_delay_drive - single_top_border_200 - 8);
		}

		SET_CHARGEN_Y_START(RASTER_CORRECTION + single_top_border_200 + vsync_delay_drive - 6 + REG_VIC2_YSCROLL * 2);
	}
	else // V400
	{
		if (REG_RSEL) // 25-line+V400
		{
			SET_BORDER_Y_TOP(RASTER_CORRECTION + single_top_border_400 + vsync_delay_drive);
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + SCREEN_HEIGHT - single_top_border_400 + vsync_delay_drive);
		}
		else
		{
			SET_BORDER_Y_TOP(RASTER_CORRECTION + single_top_border_400 + vsync_delay_drive + 8);
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + SCREEN_HEIGHT + vsync_delay_drive - single_top_border_400 - 8);
		}

		SET_CHARGEN_Y_START(RASTER_CORRECTION + single_top_border_400 + vsync_delay_drive - 6 + REG_VIC2_YSCROLL * 2);
	}

	REG_CHRCOUNT = REG_H640 ? 80 : 40;
	SET_VIRTUAL_ROW_WIDTH( (REG_H640) ? 80 : 40);
	
	REG_SCRNPTR_B1 &= 0xC0;
	REG_SCRNPTR_B1 |= REG_H640 ?  ((reg_d018_screen_addr & 14) << 2) : (reg_d018_screen_addr << 2);
	REG_SCRNPTR_B0 = 0;

	REG_SPRPTR_B0 = 0xF8;
	REG_SPRPTR_B1 &= 0xC0;
	REG_SPRPTR_B1 |= (reg_d018_screen_addr << 2) | 0x3;
	if (REG_H640 | REG_V400)
		REG_SPRPTR_B1 |= 4;

	SET_COLORRAM_BASE(0);
	DEBUGPRINT("VIC4: vic4_interpret_legacy_mode_registers(): chrcount=%d,border yt=%d,yb=%d,xl=%d,xr=%d,textxpos=%d,textypos=%d,"
	          "screen_ram=$%06x,charset=$%06x,sprite=$%06x" NL, REG_CHRCOUNT,
		BORDER_Y_TOP, BORDER_Y_BOTTOM, border_x_left, border_x_right, CHARGEN_X_START, CHARGEN_Y_START,
		SCREEN_ADDR, CHARSET_ADDR, SPRITE_POINTER_ADDR);
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

/* - If HOTREG register is enabled, VICIV will trigger recalculation of border and such on next raster,
     on any "legacy" register write. For the VIC-IV such "hot" registers are:

	  -- @IO:C64 $D011 VIC-II control register
	  -- @IO:C64 $D016 VIC-II control register
	  -- @IO:C64 $D018 VIC-II RAM addresses
	  -- @IO:C65 $D031 VIC-III Control Register B
*/
void vic_write_reg ( unsigned int addr, Uint8 data )
{
	//DEBUGPRINT("VIC%c: write reg $%02X (internally $%03X) with data $%02X" NL, XEMU_LIKELY(addr < 0x180) ? vic_registers_internal_mode_names[addr >> 7] : '?', addr & 0x7F, addr, data);
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
			vic_hotreg_touched = 1;
			break;
		CASE_VIC_ALL(0x12):
			compare_raster = (compare_raster & 0xFF00) | data;
			DEBUG("VIC: compare raster is now %d" NL, compare_raster);
			break;
		CASE_VIC_ALL(0x13): CASE_VIC_ALL(0x14):
			return;		// FIXME: writing light-pen registers?????
		CASE_VIC_ALL(0x15):	// sprite enabled
		CASE_VIC_ALL(0x16):	// control-reg#2, we allow write even if non-used bits here
			vic_hotreg_touched = 1;
			break;
		CASE_VIC_ALL(0x17):	// sprite-Y expansion
			break;
		CASE_VIC_ALL(0x18):	// memory pointers.
			// Real $D018 does not get written in VIC-IV.
			// (and reads are mapped to extended registers!)
			// So we just store the D018 Legacy Screen Address to be referenced elsewhere.
			//
			DEBUGPRINT("WRITE 0x18: $%02x" NL , data);
			REG_CHARPTR_B1 = (data & 14) << 2;
			REG_CHARPTR_B0 = 0;
			REG_SCRNPTR_B2 &= 0xF0;
			reg_d018_screen_addr = (data & 0xF0) >> 4;
			vic_hotreg_touched = 1;  
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
			palette = (data & 4) ? vic3_palette : vic3_rom_palette;
			break;
		CASE_VIC_3_4(0x31):
			vic_registers[0x31] = data;	// we need this work-around, since reg-write happens _after_ this switch statement, but machine_set_speed above needs it ...
			machine_set_speed(0);
			if (data & 8) {
				DEBUG("VIC3: V400 Mode enabled EXPERIMENTAL");
			}
			if ((data & 15) && warn_ctrl_b_lo) {
				INFO_WINDOW("VIC3 control-B register H1280, MONO and INT features are not emulated yet!");
				warn_ctrl_b_lo = 0;
			}
			vic_hotreg_touched = 1;
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
		CASE_VIC_4(0x55): CASE_VIC_4(0x56): CASE_VIC_4(0x57): CASE_VIC_4(0x58): CASE_VIC_4(0x59): CASE_VIC_4(0x5A): CASE_VIC_4(0x5B): 
			break;
		CASE_VIC_4(0x5C):
		CASE_VIC_4(0x5D): 
			vic4_sideborder_touched = 1;
			break;		
		
		CASE_VIC_4(0x5E): CASE_VIC_4(0x5F): /*CASE_VIC_4(0x60): CASE_VIC_4(0x61): CASE_VIC_4(0x62): CASE_VIC_4(0x63):*/ CASE_VIC_4(0x64):
		CASE_VIC_4(0x65): CASE_VIC_4(0x66): CASE_VIC_4(0x67): /*CASE_VIC_4(0x68): CASE_VIC_4(0x69): CASE_VIC_4(0x6A):*/ CASE_VIC_4(0x6B): /*CASE_VIC_4(0x6C):
		CASE_VIC_4(0x6D): CASE_VIC_4(0x6E):*/ CASE_VIC_4(0x6F): CASE_VIC_4(0x70): CASE_VIC_4(0x71): CASE_VIC_4(0x72): CASE_VIC_4(0x73): CASE_VIC_4(0x74):
		CASE_VIC_4(0x75): CASE_VIC_4(0x76): CASE_VIC_4(0x77): CASE_VIC_4(0x78): CASE_VIC_4(0x79): CASE_VIC_4(0x7A): CASE_VIC_4(0x7B): CASE_VIC_4(0x7C):
		CASE_VIC_4(0x7D): CASE_VIC_4(0x7E): CASE_VIC_4(0x7F):
			break;
		CASE_VIC_4(0x60): CASE_VIC_4(0x61): CASE_VIC_4(0x62): CASE_VIC_4(0x63):
			break;
		CASE_VIC_4(0x68): CASE_VIC_4(0x69): CASE_VIC_4(0x6A):
			break;
		CASE_VIC_4(0x6C): CASE_VIC_4(0x6D): CASE_VIC_4(0x6E):
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
			result = (result & 0x7F) | ((ycounter & 0x100) >> 1);
			break;
		CASE_VIC_ALL(0x12):
			result = ycounter & 0xFF;
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
			// Always mapped to VIC-IV extended "precise" registers
			result = ((REG_SCRNPTR_B1 & 60) << 2) | ((REG_CHARPTR_B1 & 60) >> 2);
			DEBUGPRINT("READ 0x81: $%02x" NL, result);
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
		CASE_VIC_4(0x50): CASE_VIC_4(0x51): CASE_VIC_4(0x52): CASE_VIC_4(0x53):
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
	return result;
}


#undef CASE_VIC_2
#undef CASE_VIC_3
#undef CASE_VIC_4
#undef CASE_VIC_ALL
#undef CASE_VIC_3_4


// "num" is 0-$ff for red, $100-$1ff for green and $200-$2ff for blue nibbles
void vic3_write_palette_reg ( int num, Uint8 data )
{
	vic3_palette_nibbles[num] = data & 15;
	// recalculate the given RGB entry based on the new data as well
	vic3_palette[num & 0xFF] = RGB(
		vic3_palette_nibbles[ num & 0xFF],
		vic3_palette_nibbles[(num & 0xFF) | 0x100],
		vic3_palette_nibbles[(num & 0xFF) | 0x200]
	);
	// Also, update the "ROM based" palette struct, BUT only colours above 15,
	// since the lower 16 are "ROM based"! This is only a trick to be able
	// to have full 256 colours for ROMPAL sel and without that too!
	// The low 16 colours are the one which are ROM based for real, that's why
	// we don't want to update them here!
	if ((num & 0xF0))
		vic3_rom_palette[num & 0xFF] = vic3_palette[num & 0xFF];
}

// TODO: for VIC-4 mode, the palette registers are 8 bit, reversed nibble order to be compatible with C65
// however, yet I don't support it, so only 4 bits can be used still by colour channel :(
void vic4_write_palette_reg ( int num, Uint8 data )
{
	vic3_write_palette_reg(num, data);	// TODO: now only call the VIC-3 solution, which is not so correct for M65/VIC-4
}

static inline Uint32 get_charset_effective_addr()
{
	// cache this? 
	switch (CHARSET_ADDR)
	{
	case 0x1000:
		return 0x2D000;
	case 0x9000:
		return 0x3D000;
	case 0x1800:
		return 0x2D800;
	case 0x9800:
		return 0x3D800;
	}
	return CHARSET_ADDR;
}

// Raster buffer bookkeeping
static int char_row = 0;

static void vic4_visible_area_raster()
{
	Uint8 bg_pixel_state[1024]; // See FOREGROUND_PIXEL and BACKGROUND_PIXEL constants
	const float x_step = (REG_CHARXSCALE / 120.0f) / (REG_H640 ? 1 : 2); /* Cache this */
		
	Uint8 char_bgcolor = REG_SCREEN_COLOR;
	Uint8* colour_ram_row_start = colour_ram_current_ptr;
	Uint8* screen_ram_row_start = screen_ram_current_ptr;

	// Charset x-displacement
	for (int i = 0; i < (CHARGEN_X_START - border_x_left); ++i)
	{
		*(current_pixel++) = vic3_rom_palette[char_bgcolor];
		xcounter++;
	}

	while (xcounter < border_x_right)
	{
		Uint16 char_value = *(screen_ram_current_ptr++);
		char_value |= REG_16BITCHARSET ? (*(screen_ram_current_ptr++) << 8) : 0;
		Uint16 color_data = *(colour_ram_current_ptr++);
		color_data |= REG_16BITCHARSET ? (*(colour_ram_current_ptr++) << 8) : 0;
		// if (REG_EBM)
		// {

		// }
		// else 
		// {

		// }
		
		Uint16 char_id = char_value & 0x1FFF; // Screen RAM 13-bits for up to 8192 characters
		Uint16 foreground_color = color_data & 0xF;
		// int r, g, b;

		// Calculate character-width

		Uint8 glyph_width_deduct = SXA_TRIM_RIGHT_BITS012(char_id) + (SXA_TRIM_RIGHT_BIT3(char_id) ? 8 : 0);
		Uint8 glyph_width = (SXA_4BIT_PER_PIXEL(color_data) ? 16 : 8) - glyph_width_deduct;
		Uint8* char_row_data = main_ram +  get_charset_effective_addr() + (char_id * 8) + char_row;

		for (float cx = 0; cx < glyph_width && xcounter < border_x_right; cx += x_step)
		{
						const Uint8 char_pixel = (*char_row_data & (0x80 >> (int)cx));
			Uint32 pixel_color = char_pixel ? vic3_rom_palette[foreground_color] : vic3_rom_palette[char_bgcolor];
			*(current_pixel++) = pixel_color;
			bg_pixel_state[xcounter++] = char_pixel ? FOREGROUND_PIXEL : BACKGROUND_PIXEL;
		}
	}

	if (++char_row > 7)
	{
		char_row = 0;
	}
	else
	{
		colour_ram_current_ptr = colour_ram_row_start;
		screen_ram_current_ptr = screen_ram_row_start;
	}

	// Fetch and sequence sprites.
	// 
	// NOTE about Text/Bitmap Graphics Background/foreground semantics:
	// In multicolor mode (MCM=1), the bit combinations “00” and “01” belong to the background
	// and “10” and “11” to the foreground whereas in standard mode (MCM=0), 
	// cleared pixels belong to the background and set pixels to the foreground.
	//
	for (int sprnum = 7; sprnum >= 0; --sprnum)
	{
		if (REG_SPRITE_ENABLE & (1 << sprnum))
		{
			Uint8* sprite_data_pointer;
			if (REG_SPRPTR_B2 & 0x80) // 8 or 16-bit pointer address?
			{
				// 16-bit sprite pointers, allowing sprites to be sourced from
				// anywhere in first 4MB of chip RAM 
				//sprite_data = main_ram + ();
			}
			else 
			{
				// "VIC-II type" 8-bit pointers
				sprite_data_pointer = main_ram + SPRITE_POINTER_ADDR + sprnum;
			}

			Uint8* sprite_data = main_ram + 64 * (*sprite_data_pointer);
			int sprite_row_in_raster = ycounter - SPRITE_POS_Y(sprnum);

			DEBUGPRINT("sprite_data_pointer $%08x SPRITE_POS_Y = %d SPRITE_POS_X = %d" NL, sprite_data_pointer, SPRITE_POS_Y(sprnum), SPRITE_POS_X(sprnum)); 

			// Draw 3-byte row 
			if (sprite_row_in_raster >=0 && sprite_row_in_raster < 20)
			{
				// High-res mode.
				Uint8* row_data = sprite_data + 3 * sprite_row_in_raster;
				int xpos = SPRITE_POS_X(sprnum);
				for (int byte = 0; byte < 3; ++byte) 
				{	
					for (int xbit = 0; xbit < 8; ++xbit) // gcc/clang are happily unrolling this with -Ofast
					{
						const Uint8 pixel = *row_data & (0x80 >> xbit);
						if (pixel && (!SPRITE_IS_BACK(sprnum) || (SPRITE_IS_BACK(sprnum) && bg_pixel_state[xpos] != FOREGROUND_PIXEL)))
						{
							*(pixel_raster_start + xpos) = vic3_rom_palette[SPRITE_COLOR(sprnum)];	
						}
						xpos++;
					}				
					row_data++; 
				}
			}
		}
	}



}

int vic4_render_scanline() 
{
	// Work this first. DO NOT OPTIMIZE EARLY.
	xcounter = 0;
	pixel_raster_start = current_pixel;
	
	// "Double-scan hack"
	if (!REG_V400 && (ycounter & 1))
	{
		for (int i = 0; i < SCREEN_WIDTH; i++, current_pixel++)
		{
			*current_pixel = user_scanlines_setting ? 0 : *(current_pixel - SCREEN_WIDTH) ;
		}
	}
	else
	{
		if (REG_HOTREG)
		{
			if (vic_hotreg_touched)
			{
				vic4_interpret_legacy_mode_registers();
				vic_hotreg_touched = 0;
				vic4_sideborder_touched = 0;
			}

			if (vic4_sideborder_touched)
			{
				vic4_update_sideborder_dimensions();
				vic4_sideborder_touched = 0;
			}
		}

		while (xcounter < SCREEN_WIDTH)
		{
			if (xcounter < border_x_left ||
				xcounter >= border_x_right ||
				ycounter < BORDER_Y_TOP ||
				ycounter >= BORDER_Y_BOTTOM ||
				!REG_DISPLAYENABLE)
			{
				*(current_pixel++) = vic3_rom_palette[REG_BORDER_COLOR & 0xF];
				++xcounter;
			}
			else
			{
				vic4_visible_area_raster();
			}
		}
	}
	ycounter++;

	// End of frame?
	if (ycounter == SCREEN_HEIGHT)
	{
		// setup next frame fetch.
		current_pixel = pixel_start;
		screen_ram_current_ptr = main_ram + SCREEN_ADDR;
		colour_ram_current_ptr = colour_ram;
		ycounter = 0;
		return 1;
	}

	return 0;
}

/* At-frame-at-once (thus incorrect implementation) renderer for H640 (80 column)
   and "normal" (40 column) text VIC modes. Hardware attributes are not supported!
   No support for MCM and ECM!  
static inline void vic2_render_screen_text ( Uint32 *p, int tail )
{
	int v400_enabled = (vic_registers[0x31] & 8) >> 3;
	Uint32 bg;
	Uint8 *vidp, *colp = colour_ram;
	int x = 0, y = 0, xlim, ylim, charline = 0;
	Uint8 *chrg = vic2_get_chargen_pointer();
	int inc_p = (vic_registers[0x54] & 1) ? 2 : 1;	// VIC-IV (Mega-65) 16 bit text mode?
	int scanline = 0;
	if (vic_registers[0x31] & 128) { // check H640 bit: 80 column mode?
		xlim = 79;
		ylim = 24 << v400_enabled;
		// Note: VIC2 sees ROM at some addresses thing is not emulated yet for other thing than chargen memory!
		// Note: according to the specification bit 4 has no effect in 80 columns mode!
		vidp = main_ram + ((vic_registers[0x18] & 0xE0) << 6) + vic2_16k_bank;
		sprite_pointers = vidp + 2040;
	} else {
		xlim = 39;
		ylim = 24 << v400_enabled;
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
	// Target SDL current_pixel related format for the background colour
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
   etc is not supported 
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
		int a;
		if (warn_sprites) {
			INFO_WINDOW("WARNING: Sprite emulation is really bad! (enabled_mask=$%02X)", sprites);
			warn_sprites = 0;
		}
		for (a = 7; a >= 0; a--) {
			int mask = 1 << a;
			if ((sprites & mask))
				render_sprite(a, mask, sprite_bank + (sprite_pointers[a] << 6), p_sdl, tail_sdl);	// sprite_pointers are set by the renderer functions above!
		}
	}
	xemu_update_screen();
}


/* --- SNAPSHOT RELATED --- */


#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_VIC4_BLOCK_VERSION	2
#define SNAPSHOT_VIC4_BLOCK_SIZE	0x400

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
	for (a = 0; a < 0x300; a++)
		vic3_write_palette_reg(a, buffer[a + 0x100]);	// TODO: save VIC4 style stuffs, but it doesn't exist yet ...
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
	memcpy(buffer + 0x100, vic3_palette_nibbles, 0x300);	// $300 bytes
	buffer[0] = vic_iomode;
	U32_AS_BE(buffer + 1, interrupt_status);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
