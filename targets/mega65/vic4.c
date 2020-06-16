/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Copyright (C)2020 Hernán Di Pietro <hernan.di.pietro@gmail.com>

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
#include <assert.h>

extern int in_hypervisor;

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
static int logical_raster = 0;
static int interrupt_status;			// Interrupt status of VIC
static int vic4_blink_phase = 0;		// blinking attribute helper, state.
Uint8 c128_d030_reg;					// C128-like register can be only accessed in VIC-II mode but not in others, quite special!
static Uint8 reg_d018_screen_addr = 0;     // Legacy VIC-II $D018 screen address register
static int vic_hotreg_touched = 0; 		// If any "legacy" registers were touched
static int vic4_sideborder_touched = 0;  // If side-border register were touched
static int border_x_left= 0;			 // Side border left 
static int border_x_right= 0;			 // Side border right
static int xcounter = 0, ycounter = 0;   // video counters
static int frame_counter = 0;
static int vicii_first_raster = 0;
static int char_row = 0, display_row = 0;
static Uint8 bg_pixel_state[1024]; // See FOREGROUND_PIXEL and BACKGROUND_PIXEL constants
static Uint8* screen_ram_current_ptr = NULL;
static Uint8* colour_ram_current_ptr = NULL;
extern int user_scanlines_setting;
float char_x_step = 0.0;
static int warn_ctrl_b_lo = 1;

// VIC-IV Modeline Parameters
// ----------------------------------------------------
#define TEXT_HEIGHT_200  		400
#define TEXT_HEIGHT_400  		400
#define CHARGEN_Y_SCALE_200 	2
#define CHARGEN_Y_SCALE_400 	1
#define chargen_y_pixels 		0
#define TOP_BORDERS_HEIGHT_200 	(SCREEN_HEIGHT - TEXT_HEIGHT_200)
#define TOP_BORDERS_HEIGHT_400 	(SCREEN_HEIGHT - TEXT_HEIGHT_400)
#define SINGLE_TOP_BORDER_200 	(TOP_BORDERS_HEIGHT_200 >> 1)
#define SINGLE_TOP_BORDER_400 	(TOP_BORDERS_HEIGHT_400 >> 1)

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

// Lookup-based bit reversal.  (TODO: Move this to a proper file)

static inline Uint8 reverse_byte(unsigned char x)
{
    static const Uint8 table[] = {
        0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, //0
        0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0, //8
        0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, //16
        0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8, //24
        0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, //32
        0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4, //40
        0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec, //48
        0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc, //56
        0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, //64
        0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2, //72
        0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea, //80
        0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa, //88
        0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6, //96
        0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6, //104
        0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, //112
        0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe, //120
        0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1, //128
        0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
        0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
        0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
        0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
        0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
        0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
        0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
        0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
        0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
        0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
        0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
        0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
        0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
        0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
        0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
    };
    return table[x];
}


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

static void vic4_interrupt_checker ( void )
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

static void vic4_check_raster_interrupt ( void )
{
	if (logical_raster == compare_raster)
		interrupt_status |= 1;
	else
		interrupt_status &= 0xFE;
	vic4_interrupt_checker();
}

inline static void calculate_char_x_step()
{
	char_x_step = (REG_CHARXSCALE / 120.0f) / (REG_H640 ? 1 : 2);
}

static void vic4_update_sideborder_dimensions()
{
	if (REG_CSEL) // 40-columns?
	{
		border_x_left = FRAME_H_FRONT + SINGLE_SIDE_BORDER;

		if (!REG_H640)
		{
			border_x_right = FRAME_H_FRONT + SCREEN_WIDTH - SINGLE_SIDE_BORDER - 1;
		}
		else //80-col mode
		{
			border_x_right = FRAME_H_FRONT + SCREEN_WIDTH - SINGLE_SIDE_BORDER;
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

	vic4_update_sideborder_dimensions();

	if (REG_CSEL) // 40-columns? 
	{
		if (!REG_H640) 
		{
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL));
		}
		else //80-col mode
		{
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL) - 2);
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
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL) - 2);
		}
	}

	if (!REG_V400) // Standard mode (200-lines)
	{
		if (REG_RSEL) // 25-row
		{
			SET_BORDER_Y_TOP(RASTER_CORRECTION + SINGLE_TOP_BORDER_200 - (2 * vicii_first_raster));
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + SCREEN_HEIGHT - SINGLE_TOP_BORDER_200 - (2 * vicii_first_raster) - 1);
		}
		else
		{
			SET_BORDER_Y_TOP(RASTER_CORRECTION + SINGLE_TOP_BORDER_200 - (2 * vicii_first_raster) + 8);
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + SCREEN_HEIGHT - (2 * vicii_first_raster) - SINGLE_TOP_BORDER_200 - 7);
		}

		SET_CHARGEN_Y_START(RASTER_CORRECTION + SINGLE_TOP_BORDER_200 + (2 * vicii_first_raster) - 6 + REG_VIC2_YSCROLL * 2);
	}
	else // V400
	{
		if (REG_RSEL) // 25-line+V400
		{
			SET_BORDER_Y_TOP(RASTER_CORRECTION + SINGLE_TOP_BORDER_400 - (2 * vicii_first_raster));
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + SCREEN_HEIGHT - SINGLE_TOP_BORDER_400 - (2 * vicii_first_raster) - 1);
		}
		else
		{
			SET_BORDER_Y_TOP(RASTER_CORRECTION + SINGLE_TOP_BORDER_400 - (2 * vicii_first_raster) + 8);
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + SCREEN_HEIGHT - (2 * vicii_first_raster) - SINGLE_TOP_BORDER_200 - 7);
		}

		SET_CHARGEN_Y_START(RASTER_CORRECTION + SINGLE_TOP_BORDER_400 + (2 * vicii_first_raster) - 6 + (REG_VIC2_YSCROLL * 2));
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
	DEBUGPRINT("VIC4: vic4_interpret_legacy_mode_registers(): vicii_first_raster=%d,chrcount=%d,border yt=%d,yb=%d,xl=%d,xr=%d,textxpos=%d,textypos=%d,"
	          "screen_ram=$%06x,charset/bitmap=$%06x,sprite=$%06x" NL, vicii_first_raster, REG_CHRCOUNT,
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
			DEBUGPRINT("WRITE 0xD011: $%02x" NL, data);
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
			break;
		CASE_VIC_ALL(0x16):	// control-reg#2, we allow write even if non-used bits here
			DEBUGPRINT("WRITE 0xD016: $%02x" NL, data);
			vic_hotreg_touched = 1;
			break;
		CASE_VIC_ALL(0x17):	// sprite-Y expansion
			break;
		CASE_VIC_ALL(0x18):	// memory pointers.
			// Real $D018 does not get written in VIC-IV  here. 
			// (See vic4_interpret_legacy_mode_registers () for later REG_SCRNPTR_ adjustments)
			// Reads are mapped to extended registers.
			// So we just store the D018 Legacy Screen Address to be referenced elsewhere.
			//
			DEBUGPRINT("WRITE 0xD018: $%02x" NL , data);
			REG_CHARPTR_B1 = (data & 14) << 2;
			REG_CHARPTR_B0 = 0;
			REG_SCRNPTR_B2 &= 0xF0;
			reg_d018_screen_addr = (data & 0xF0) >> 4;
			vic_hotreg_touched = 1;  
			return;
		CASE_VIC_ALL(0x19):
			interrupt_status = interrupt_status & (~data) & 0xF;
			vic4_interrupt_checker();
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
			calculate_char_x_step();
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
		CASE_VIC_4(0x55): CASE_VIC_4(0x56): CASE_VIC_4(0x57): CASE_VIC_4(0x58): CASE_VIC_4(0x59): 
			break;
		CASE_VIC_4(0x5A): 
			calculate_char_x_step();
			break;
		CASE_VIC_4(0x5B): 
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
		CASE_VIC_4(0x6C): CASE_VIC_4(0x6D): 
			break;
		CASE_VIC_4(0x6E):
			vicii_first_raster  = data & 0x1F;
			if (!in_hypervisor)
				vic4_sideborder_touched = 1;
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
			result = (result & 0x7F) | ((logical_raster & 0x100) >> 1);
			break;
		CASE_VIC_ALL(0x12):
			result = logical_raster & 0xFF;
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
		CASE_VIC_4(0x6D): 
			break;
		CASE_VIC_4(0x6E): 

			break;
		
		CASE_VIC_4(0x6F): CASE_VIC_4(0x70): CASE_VIC_4(0x71): CASE_VIC_4(0x72): CASE_VIC_4(0x73): CASE_VIC_4(0x74):
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


static void vic4_draw_sprite_row_multicolor(int sprnum, int x_display_pos, Uint8* row_data_ptr, int xscale)
{
	for (int byte = 0; byte < 3; ++byte)
	{
		for (int xbit = 0; xbit < 8; xbit += 2)
		{
			const Uint8 p0 = *row_data_ptr & (0x80 >> xbit);
			const Uint8 p1 = *row_data_ptr & (0x40 >> xbit);
			
			Uint8 pixel = 0; // TODO: See generated code -- use lookup instead of branch?
			if (!p0 && p1) 
				pixel = SPRITE_MULTICOLOR_1;
			else if (p0 && !p1)
				pixel = SPRITE_COLOR(sprnum);
			else if (p0 && p1)
				pixel = SPRITE_MULTICOLOR_2;

			for (int p = 0; p < xscale && x_display_pos < border_x_right; ++p, x_display_pos += 2)
			{
				if (pixel)
				{
					if (x_display_pos >= border_x_left &&
						(!SPRITE_IS_BACK(sprnum) ||
						 (SPRITE_IS_BACK(sprnum) && bg_pixel_state[x_display_pos] != FOREGROUND_PIXEL)))
					{
						*(pixel_raster_start + x_display_pos) = vic3_rom_palette[pixel];
					}

					if (x_display_pos+1 >= border_x_left &&
						(!SPRITE_IS_BACK(sprnum) ||
						 (SPRITE_IS_BACK(sprnum) && bg_pixel_state[x_display_pos + 1] != FOREGROUND_PIXEL)))
					{
						*(pixel_raster_start + x_display_pos + 1) = vic3_rom_palette[pixel];
					}
				}
			}
		}
		row_data_ptr++;
	}
}

static void vic4_draw_sprite_row_mono(int sprnum, int x_display_pos, Uint8 *row_data_ptr, int xscale)
{
	for (int byte = 0; byte < 3; ++byte)
	{
		for (int xbit = 0; xbit < 8; ++xbit)
		{
			const Uint8 pixel = *row_data_ptr & (0x80 >> xbit);
			for (int p = 0; p < xscale && x_display_pos < border_x_right; ++p, ++x_display_pos)
			{
				if (x_display_pos >= border_x_left &&
					pixel &&
					(!SPRITE_IS_BACK(sprnum) ||
					 (SPRITE_IS_BACK(sprnum) && bg_pixel_state[x_display_pos] != FOREGROUND_PIXEL)))
				{
					*(pixel_raster_start + x_display_pos) = vic3_rom_palette[SPRITE_COLOR(sprnum)];
				}
			}
		}
		row_data_ptr++;
	}
}

static void vic4_do_sprites()
{
	// Fetch and sequence sprites.
	// 
	// NOTE about Text/Bitmap Graphics Background/foreground semantics:
	// In multicolor mode (MCM=1), the bit combinations “00” and “01” belong to the background
	// and “10” and “11” to the foreground whereas in standard mode (MCM=0), 
	// cleared pixels belong to the background and set pixels to the foreground.
	//
	for (int sprnum = 7; sprnum >= 0; --sprnum)
	{
		int x_display_pos = border_x_left + ((SPRITE_POS_X(sprnum) - SPRITE_X_BASE_COORD) * (REG_H640 ? 1 : 2)); // in display units
		int y_logical_pos = SPRITE_POS_Y(sprnum) - SPRITE_Y_BASE_COORD +(BORDER_Y_TOP / (REG_V400 ? 1 : 2)); // in logical units

		int sprite_row_in_raster = logical_raster - y_logical_pos;
		if (SPRITE_VERT_2X(sprnum))
			sprite_row_in_raster = sprite_row_in_raster >> 1;

		if ((REG_SPRITE_ENABLE & (1 << sprnum)) &&
			(sprite_row_in_raster >= 0 && sprite_row_in_raster < 21) )
		{
			Uint8 *sprite_data_pointer =  main_ram + SPRITE_POINTER_ADDR + sprnum;
			Uint8 *sprite_data = main_ram + 64 * (*sprite_data_pointer);
			Uint8 *row_data = sprite_data + 3 * sprite_row_in_raster;
			int xscale = (REG_H640 ? 1 : 2) * (SPRITE_HORZ_2X(sprnum) ? 2 : 1);
			if (SPRITE_MULTICOLOR(sprnum))
				vic4_draw_sprite_row_multicolor(sprnum, x_display_pos, row_data, xscale);
			else
				vic4_draw_sprite_row_mono(sprnum, x_display_pos, row_data, xscale);
		}
	}
}

// Render a monochrome character cell row
// flip = 00 Dont flip, 01 = flip vertical, 10 = flip horizontal, 11 = flip both

static void vic4_render_mono_char_row(Uint8 char_byte, int glyph_width, Uint8 bg_color, Uint8 fg_color, Uint8 vic3attr)
{
	if (vic3attr)
	{		
		if (char_row == 7 && VIC3_ATTR_UNDERLINE(vic3attr))
			char_byte = 0xFF;

		if (VIC3_ATTR_REVERSE(vic3attr))
			char_byte = ~char_byte;

		if (VIC3_ATTR_BLINK(vic3attr) && vic4_blink_phase) 
			char_byte = VIC3_ATTR_REVERSE(vic3attr) ? ~char_byte : 0;
		
		if (VIC3_ATTR_BOLD(vic3attr))
		{
			fg_color |= 0x10;
		}
	}
	
	for (float cx = 0; cx < glyph_width && xcounter < border_x_right; cx += char_x_step)
	{
		const Uint8 char_pixel = (char_byte & (0x80 >> (int)cx));
		Uint32 pixel_color = char_pixel ? vic3_rom_palette[fg_color] : vic3_rom_palette[bg_color];
		*(current_pixel++) = pixel_color;
		bg_pixel_state[xcounter++] = char_pixel ? FOREGROUND_PIXEL : BACKGROUND_PIXEL;
	}
}

static void vic4_render_multicolor_char_row(Uint8 char_byte, int glyph_width, const Uint8 color_source[4])
{
	for (float cx = 0; cx < glyph_width && xcounter < border_x_right; cx += char_x_step)
	{
		const Uint8 bitsel =  2 * (int)(cx / 2);
		const Uint8 bit_pair = (char_byte & (0x80 >> bitsel)) >> (6-bitsel) | (char_byte & (0x40 >> bitsel)) >> (6-bitsel);
		
		Uint8 pixel = color_source[bit_pair];
		const Uint8 layer = bit_pair & 2 ? FOREGROUND_PIXEL : BACKGROUND_PIXEL;
		*(current_pixel++) = vic3_rom_palette[pixel];
		bg_pixel_state[xcounter++] = layer;
	}
}

//
//
// The character rendering engine. Most features are shared between
// all graphic modes.  Basically, the VIC-IV supports the following character
// color modes: 
//
// - Monochrome (Bg/Fg)
// - VICII Multicolor
// - 16-color 
// - 256-color 
//
// It's interesting to see that the four modes can be selected in 
// bitmap or text modes.
//
// VIC-III Extended attributes are applied to characters if properly set, 
// except in Multicolor modes.
//
static void vic4_render_char_raster()
{
	int char_x = 0;
	colour_ram_current_ptr = colour_ram + (display_row * REG_CHRCOUNT);
	screen_ram_current_ptr = main_ram + SCREEN_ADDR + (display_row * REG_CHRCOUNT);
	const Uint8* row_data_base_addr = main_ram + (REG_BMM ?  VIC2_BITMAP_ADDR : get_charset_effective_addr());

	// Charset x-displacement

	xcounter += (CHARGEN_X_START - border_x_left);
	
	while (xcounter < border_x_right)
	{
		if (display_row > 25 || char_x >= REG_CHRCOUNT) { // FIX: get display_row from registers.
			(*current_pixel++) = vic3_rom_palette[REG_SCREEN_COLOR];
			xcounter++;
			continue;
		}

		Uint16 color_data = *(colour_ram_current_ptr++);
		Uint16 char_value = *(screen_ram_current_ptr++);

		if (REG_16BITCHARSET)
		{
			color_data = (color_data << 8) | (*(colour_ram_current_ptr++));
			char_value = char_value | (*(screen_ram_current_ptr++) << 8);
		}
		
		// Background and foreground colors

		const Uint8 char_fgcolor = color_data & 0xF; 
		const Uint8 vic3_attr = REG_VICIII_ATTRIBS && !REG_MCM ? (color_data >> 4) : 0;
		const Uint16 char_id = REG_EBM ? (char_value & 0x3f) : char_value & 0x1fff; // up to 8192 characters (13-bit)
		const Uint8 char_bgcolor = REG_EBM ? vic_registers[0x21 + ((char_value >> 6) & 3)] : REG_SCREEN_COLOR;

		// Calculate character-width

		Uint8 glyph_width_deduct = SXA_TRIM_RIGHT_BITS012(char_value) + (SXA_TRIM_RIGHT_BIT3(char_value) ? 8 : 0);
		Uint8 glyph_width = (SXA_4BIT_PER_PIXEL(color_data) ? 16 : 8) - glyph_width_deduct;

		// Default fetch from char mode.
		Uint8 char_byte;
		int sel_char_row = char_row;
		
		if (SXA_VERTICAL_FLIP(color_data))
			sel_char_row = 7 - char_row;

		if (REG_BMM)
		{
			char_byte = *(row_data_base_addr + display_row * 320 + 8 * char_x  + sel_char_row);
		}
		else
		{
			char_byte = *(row_data_base_addr + (char_id * 8) + sel_char_row);
		}
		
		if (SXA_HORIZONTAL_FLIP(color_data))
			char_byte = reverse_byte(char_byte);

		// Render character cell row
				
		if (SXA_4BIT_PER_PIXEL(color_data)) // 16-color character
		{

		}
		else if (CHAR_IS256_COLOR(char_id)) // 256-color character
		{
		}
		else if ((REG_MCM && (char_fgcolor & 8)) || (REG_MCM && REG_BMM)) // Multicolor character
		{
			if (REG_BMM)
			{
				const Uint8 color_source[4] = {
					REG_SCREEN_COLOR,	//00
					char_value >> 4,  	//01
					char_value & 0xF, 	//10
					color_data & 0xF  	//11
				};
				vic4_render_multicolor_char_row(char_byte, glyph_width, color_source);
			}
			else
			{
				const Uint8 color_source[4] = {
					REG_SCREEN_COLOR, //00
					REG_MULTICOLOR_1, //01
					REG_MULTICOLOR_2, //10
					char_fgcolor & 7  //11
				};
				vic4_render_multicolor_char_row(char_byte, glyph_width, color_source);
			}
		}
		else // Single color character
		{
			if (!REG_BMM)
			{
				vic4_render_mono_char_row(char_byte, glyph_width, char_bgcolor, char_fgcolor, vic3_attr);
			}
			else
			{
				vic4_render_mono_char_row(char_byte, glyph_width, char_value & 0xF, char_value >> 4, vic3_attr );
			}
		}

		char_x++;
	}

	if (++char_row > 7)
	{
		char_row = 0;
		display_row++;
	}
}

int vic4_render_scanline() 
{
	// Work this first. DO NOT OPTIMIZE EARLY.

	xcounter = 0;
	pixel_raster_start = current_pixel;

	SET_PHYSICAL_RASTER(ycounter);
	logical_raster = ycounter >> (REG_V400 ? 0 : 1);

	vic4_check_raster_interrupt();

	// "Double-scan hack"
	if (!REG_V400 && (ycounter & 1))
	{
		for (int i = 0; i < SCREEN_WIDTH; i++, current_pixel++)
			*current_pixel = user_scanlines_setting ? 0 : *(current_pixel - SCREEN_WIDTH) ;
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

		// Top and bottom borders

		if (ycounter < BORDER_Y_TOP || ycounter >= BORDER_Y_BOTTOM || !REG_DISPLAYENABLE)
		{
			for (int i = 0; i < SCREEN_WIDTH; ++i)
				*(current_pixel++) = vic3_rom_palette[REG_BORDER_COLOR & 0xF];
		}
		else
		{
			while (xcounter++ < border_x_left)
				*(current_pixel++) = vic3_rom_palette[REG_BORDER_COLOR & 0xF];

			// Cache this, use a function call and avoid branching !

			vic4_render_char_raster();

			while (xcounter++ <= SCREEN_WIDTH)
				*(current_pixel++) = vic3_rom_palette[REG_BORDER_COLOR & 0xF];

			vic4_do_sprites();
		}
	}
	ycounter++;
	
	// End of frame?
	if (ycounter == SCREEN_HEIGHT)
	{
		display_row = 0;
		char_row = 0;
		screen_ram_current_ptr = main_ram + SCREEN_ADDR;
		colour_ram_current_ptr = colour_ram;
		ycounter = 0;
		frame_counter++;
		if (frame_counter == VIC4_BLINK_INTERVAL)
		{
			frame_counter = 0;
			vic4_blink_phase = !vic4_blink_phase;
		}
		return 1;
	}

	return 0;
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
