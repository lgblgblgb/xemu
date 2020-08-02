/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 emulator.
   Copyright (C)2016,2017,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the VIC3 "emulation". Currently it does scanline based rendering,
   though sprites are totally incorrect and rendered "at-once" after the
   frame (very ugly! no sprite bg priority, no other tricks etc). Other
   limitations: no DAT emulation, hardware attributes works only in col-80
   (normal) text mode, no screen pisitioning, no H1280 mode, no V400 and
   interlace mode, no chrome killer, no X/Y scroll and 28/24 column mode
   (VIC-II stuff), no border is emulated. Timing is totally bad, but hey,
   VIC-III wouldn't be compatible too much with VIC-II either for the
   exact timing, so ...

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
#include "commodore_65.h"
#include "xemu/cpu65.h"
#include "vic3.h"



#define RGB(r,g,b)		rgb_palette[((r) << 8) | ((g) << 4) | (b)]
#define COLMEMPTR		(memory + 0x1F800)
#define IS_H640			(vic3_registers[0x31] & 0x80)
#define BLINK_COUNTER_INIT	25

#ifdef VIC_CACHE_COLOURS
#define VIC_REG_COLOUR(n)	vic_reg_palette_cache[(n) - 0x20]
#else
#define	VIC_REG_COLOUR(n)	palette[vic3_registers[n]]
#endif


#ifdef VIC_CACHE_COLOURS
#warning "VIC_CACHE_COLOURS feature has not been implemented, yet!"
static Uint32 vic_reg_palette_cache[15];
#endif
static Uint32 rgb_palette[4096];	// all the C65 palette, 4096 colours (SDL pixel format related form)
static Uint32 vic3_palette[0x100];	// VIC3 palette in SDL pixel format related form (can be written into the texture directly to be rendered)
static Uint32 vic3_rom_palette[0x100];	// the "ROM" palette, for C64 colours (with some ticks, ie colours above 15 are the same as the "normal" programmable palette)
static Uint32 *palette;			// the selected palette ...
static Uint8 vic3_palette_nibbles[0x300];
Uint8 vic3_registers[0x80];		// VIC-3 registers. It seems $47 is the last register. But to allow address the full VIC3 reg I/O space, we use $80 here
int vic_new_mode;			// VIC3 "newVic" IO mode is activated flag
static int scanline;			// current scan line number
int cpu_cycles_per_scanline;
static int compare_raster;		// raster compare (9 bits width) data
static int interrupt_status;		// Interrupt status of VIC
static int blink_phase;			// blinking attribute helper: on/off phase of blink
static int blink_counter;		// blinking attribute helper: counter for blink_phase change
static Uint8 attributes;		// hardware attribute mode ($F0 ON, $00 OFF). NOTE: currently implemented for normal text mode only, which is not so correct!

// (SDL) target texture rendering pointers
static Uint32 *pixel;			// pixel pointer to the rendering target (one pixel: 32 bit)
static Uint32 *pixel_end, *pixel_start;	// points to the end and start of the buffer

int frameskip;				// if non-zero: frameskip mode, do NOT render the frame, but maintain scanline counter
static int video_counter;		// video counter ("VC") 0...1000 or 0...2000 (in H640 mode) Currently only the BEGINNING of lines, as it's scanline based emulation ...
static int video_counter_inc;		// 40 or 80 based on H640 bit
static int row_counter;			// character row counter ("RC") 0...7 (0=badline)
static int vic2_bank_number = 0;	// vic-2 bank number written by CIA
static void (*renderer_func)(void);	// the selected scanline renderer, modified on mode register writes automatically

// Pointers of VIC base addresses. Not always used in all modes, of course.
// They're updated on register writes. So we can save re-calculation addresses all the time
static Uint8 *vicptr_video_40;		// pointer to video matrix, only used in !H640 modes
static Uint8 *vicptr_video_80;		// pointer to video matrix, only used in H640 modes
static Uint8 *vicptr_chargen;		// pointer to chargen, can even point into the ROM! Used only in text modes
static Uint8 *vicptr_bank16k;		// pointer to VIC-II 16K bank start address
static Uint8 *vicptr_bank32k;		// pointer to VIC-III 32K bank start address (only used for H640 bitmap mode)
static Uint8 *vicptr_bitmap_320;	// pointer to bitmap data, only used in !H640 bitmap modes
static Uint8 *vicptr_bitmap_640;	// pointer to bitmap data, only used in H640 bitmap modes (the only one which is calculated from 32K bank!)
static Uint8 *vicptr_idlefetch_p;	// IDLE-fetch pointer
static Uint8 *sprite_pointers;		// points to the actual memory bytes where currently sprite pointers are
// For bitplanes we don't use pointers, since there are just too many sources (max of 8 bitplanes)
// Instead we use integer values, as linear memory addresses.
static int bitplane_addr_320[8];
static int bitplane_addr_640[8];

static int warn_sprites = 0, warn_ctrl_b_lo = 1;

char scanline_render_debug_info[320];




void vic3_open_frame_access ( void )
{
	int tail_sdl;
	pixel = pixel_start = xemu_start_pixel_buffer_access(&tail_sdl);
	pixel_end = pixel + (SCREEN_WIDTH * SCREEN_HEIGHT);
	if (tail_sdl)
		FATAL("tail_sdl is not zero!");
}




static void vic3_interrupt_checker ( void )
{
	int vic_irq_old = cpu65.irqLevel & 2;
	int vic_irq_new;
	if ((interrupt_status & vic3_registers[0x1A])) {
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
	if (scanline == compare_raster)
		interrupt_status |= 1;
	else
		interrupt_status &= 0xFE;
	vic3_interrupt_checker();
}


#define STATIC_COLOUR_RENDERER(colour, size) do { \
	register int a = size; \
	while (a--) \
		*(pixel++) = colour; \
} while(0)


static void renderer_disabled_screen ( void )
{
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), SCREEN_WIDTH);
}


static void renderer_invalid_mode ( void )
{
	STATIC_COLOUR_RENDERER(rgb_palette[0], SCREEN_WIDTH);	// invalid modes renders only blackness
}



// FIXME: hardware attributes are supported now for only 40/80-col text mode!
// FIXME: for real, it's supported for everything, _even_ for bitplane mode .......... (surprisingly ...)
#define VIC3_ADJUST_BY_HARDWARE_ATTRIBUTES( has_attrib_condition, colour_var, vdata_var ) do {								\
	if (has_attrib_condition) {															\
		if ((colour_var & 0xF0) == 0x10) {	/* only the blink bit for the character is set */						\
			if (blink_phase)														\
				vdata_var = 0;	/* blinking character, in one phase, the character "disappears", ie blinking */				\
			colour_var &= 15;														\
		} else if ((!(colour_var & 0x10)) || blink_phase) {											\
			if ((colour_var & 0x80) && (row_counter == 7))	/* underline (must be before reverse, as underline can be reversed as well!) */	\
				vdata_var = 0xFF;	/* the underline */										\
			if (colour_var & 0x20)	/* reverse bit for char */										\
				vdata_var ^= 0xFF;													\
			if (colour_var & 0x40)	/* highlight, this must be the LAST, since it sets the low nibble of coldata ... */			\
				colour_var = 0x10 | (colour_var & 15);											\
			else																\
				colour_var &= 15;													\
		} else																	\
			colour_var &= 15;														\
	} else																		\
		colour_var &= 15;															\
} while (0)



static void renderer_text_40 ( void )
{
	Uint8 *vp = vicptr_video_40 + video_counter;
	Uint8 *cp = COLMEMPTR + video_counter;
	Uint8 *chargen = vicptr_chargen + row_counter;
	Uint32 bg_colour = VIC_REG_COLOUR(0x21);
	int a;
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 40; a++) {
		Uint8 vdata = chargen[(*(vp++)) << 3];
		Uint32 colour = *(cp++);
		Uint32 fg_colour;
		VIC3_ADJUST_BY_HARDWARE_ATTRIBUTES(XEMU_UNLIKELY(colour & attributes), colour, vdata);
		fg_colour = palette[colour];
		pixel[ 0] = pixel[ 1] = vdata & 0x80 ? fg_colour : bg_colour;
		pixel[ 2] = pixel[ 3] = vdata & 0x40 ? fg_colour : bg_colour;
		pixel[ 4] = pixel[ 5] = vdata & 0x20 ? fg_colour : bg_colour;
		pixel[ 6] = pixel[ 7] = vdata & 0x10 ? fg_colour : bg_colour;
		pixel[ 8] = pixel[ 9] = vdata & 0x08 ? fg_colour : bg_colour;
		pixel[10] = pixel[11] = vdata & 0x04 ? fg_colour : bg_colour;
		pixel[12] = pixel[13] = vdata & 0x02 ? fg_colour : bg_colour;
		pixel[14] = pixel[15] = vdata & 0x01 ? fg_colour : bg_colour;
		pixel += 16;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_text_80 ( void )
{
	Uint8 *vp = vicptr_video_80 + video_counter;
	Uint8 *cp = COLMEMPTR + video_counter;
	Uint8 *chargen = vicptr_chargen + row_counter;
	Uint32 bg_colour = VIC_REG_COLOUR(0x21);
	int a;
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 80; a++) {
		Uint8 vdata = chargen[(*(vp++)) << 3];
		Uint8 colour = *(cp++);
		Uint32 fg_colour;
		VIC3_ADJUST_BY_HARDWARE_ATTRIBUTES(XEMU_UNLIKELY(colour & attributes), colour, vdata);
		fg_colour = palette[colour];
		*(pixel++) = vdata & 0x80 ? fg_colour : bg_colour;
		*(pixel++) = vdata & 0x40 ? fg_colour : bg_colour;
		*(pixel++) = vdata & 0x20 ? fg_colour : bg_colour;
		*(pixel++) = vdata & 0x10 ? fg_colour : bg_colour;
		*(pixel++) = vdata & 0x08 ? fg_colour : bg_colour;
		*(pixel++) = vdata & 0x04 ? fg_colour : bg_colour;
		*(pixel++) = vdata & 0x02 ? fg_colour : bg_colour;
		*(pixel++) = vdata & 0x01 ? fg_colour : bg_colour;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_ecmtext_40 ( void )
{
	Uint8 *vp = vicptr_video_40 + video_counter;
	Uint8 *cp = COLMEMPTR + video_counter;
	Uint8 *chargen = vicptr_chargen + row_counter;
	Uint32 bg_colours[4] = {
		VIC_REG_COLOUR(0x21),
		VIC_REG_COLOUR(0x22),
		VIC_REG_COLOUR(0x23),
		VIC_REG_COLOUR(0x24)
	};
	int a;
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 40; a++) {
		Uint8 vdata = *(vp++);
		Uint8 data = chargen[(vdata & 63) << 3];
		Uint32 fg_colour = palette[*(cp++) & 15];
		vdata >>= 6;
		pixel[ 0] = pixel[ 1] = data & 0x80 ? fg_colour : bg_colours[vdata];
		pixel[ 2] = pixel[ 3] = data & 0x40 ? fg_colour : bg_colours[vdata];
		pixel[ 4] = pixel[ 5] = data & 0x20 ? fg_colour : bg_colours[vdata];
		pixel[ 6] = pixel[ 7] = data & 0x10 ? fg_colour : bg_colours[vdata];
		pixel[ 8] = pixel[ 9] = data & 0x08 ? fg_colour : bg_colours[vdata];
		pixel[10] = pixel[11] = data & 0x04 ? fg_colour : bg_colours[vdata];
		pixel[12] = pixel[13] = data & 0x02 ? fg_colour : bg_colours[vdata];
		pixel[14] = pixel[15] = data & 0x01 ? fg_colour : bg_colours[vdata];
		pixel += 16;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_ecmtext_80 ( void )
{
	Uint8 *vp = vicptr_video_80 + video_counter;
	Uint8 *cp = COLMEMPTR + video_counter;
	Uint8 *chargen = vicptr_chargen + row_counter;
	Uint32 bg_colours[4] = {
		VIC_REG_COLOUR(0x21),
		VIC_REG_COLOUR(0x22),
		VIC_REG_COLOUR(0x23),
		VIC_REG_COLOUR(0x24)
	};
	int a;
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 80; a++) {
		Uint8 vdata = *(vp++);
		Uint8 data = chargen[(vdata & 63) << 3];
		Uint32 fg_colour = palette[*(cp++) & 15];
		vdata >>= 6;
		pixel[0] = data & 0x80 ? fg_colour : bg_colours[vdata];
		pixel[1] = data & 0x40 ? fg_colour : bg_colours[vdata];
		pixel[2] = data & 0x20 ? fg_colour : bg_colours[vdata];
		pixel[3] = data & 0x10 ? fg_colour : bg_colours[vdata];
		pixel[4] = data & 0x08 ? fg_colour : bg_colours[vdata];
		pixel[5] = data & 0x04 ? fg_colour : bg_colours[vdata];
		pixel[6] = data & 0x02 ? fg_colour : bg_colours[vdata];
		pixel[7] = data & 0x01 ? fg_colour : bg_colours[vdata];
		pixel += 8;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_mcmtext_40 ( void )
{
	Uint8 *vp = vicptr_video_40 + video_counter;
	Uint8 *cp = COLMEMPTR + video_counter;
	Uint8 *chargen = vicptr_chargen + row_counter;
	Uint32 colours[4] = {
		VIC_REG_COLOUR(0x21),
		VIC_REG_COLOUR(0x22),
		VIC_REG_COLOUR(0x23),
		0	// to be filled during colour fetch ...
	};
	int a;
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 40; a++) {
		Uint8 vdata = chargen[(*(vp++)) << 3];
		Uint8 coldata = (*(cp++));
		if (coldata & 8) {
			// MCM character
			colours[3] = palette[coldata & 7];
			pixel[ 0] = pixel[ 1] = pixel[ 2] = pixel[ 3] = colours[ vdata >> 6     ];
			pixel[ 4] = pixel[ 5] = pixel[ 6] = pixel[ 7] = colours[(vdata >> 4) & 3];
			pixel[ 8] = pixel[ 9] = pixel[10] = pixel[11] = colours[(vdata >> 2) & 3];
			pixel[12] = pixel[13] = pixel[14] = pixel[15] = colours[ vdata       & 3];
		} else {
			// non-MCM character
			colours[3] = palette[coldata & 7];
			pixel[ 0] = pixel[ 1] = vdata & 0x80 ? colours[3] : colours[0];
			pixel[ 2] = pixel[ 3] = vdata & 0x40 ? colours[3] : colours[0];
			pixel[ 4] = pixel[ 5] = vdata & 0x20 ? colours[3] : colours[0];
			pixel[ 6] = pixel[ 7] = vdata & 0x10 ? colours[3] : colours[0];
			pixel[ 8] = pixel[ 9] = vdata & 0x08 ? colours[3] : colours[0];
			pixel[10] = pixel[11] = vdata & 0x04 ? colours[3] : colours[0];
			pixel[12] = pixel[13] = vdata & 0x02 ? colours[3] : colours[0];
			pixel[14] = pixel[15] = vdata & 0x01 ? colours[3] : colours[0];
		}
		pixel += 16;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_mcmtext_80 ( void )
{
	Uint8 *vp = vicptr_video_80 + video_counter;
	Uint8 *cp = COLMEMPTR + video_counter;
	Uint8 *chargen = vicptr_chargen + row_counter;
	Uint32 colours[4] = {
		VIC_REG_COLOUR(0x21),
		VIC_REG_COLOUR(0x22),
		VIC_REG_COLOUR(0x23),
		0	// to be filled during colour fetch ...
	};
	int a;
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 80; a++) {
		Uint8 vdata = chargen[(*(vp++)) << 3];
		Uint8 coldata = (*(cp++));
		if (coldata & 8) {
			// MCM character
			colours[3] = palette[coldata & 7];
			pixel[0] = pixel[1] = colours[ vdata >> 6     ];
			pixel[2] = pixel[3] = colours[(vdata >> 4) & 3];
			pixel[4] = pixel[5] = colours[(vdata >> 2) & 3];
			pixel[6] = pixel[7] = colours[ vdata       & 3];
		} else {
			// non-MCM character
			colours[3] = palette[coldata & 7];
			pixel[0] = vdata & 0x80 ? colours[3] : colours[0];
			pixel[1] = vdata & 0x40 ? colours[3] : colours[0];
			pixel[2] = vdata & 0x20 ? colours[3] : colours[0];
			pixel[3] = vdata & 0x10 ? colours[3] : colours[0];
			pixel[4] = vdata & 0x08 ? colours[3] : colours[0];
			pixel[5] = vdata & 0x04 ? colours[3] : colours[0];
			pixel[6] = vdata & 0x02 ? colours[3] : colours[0];
			pixel[7] = vdata & 0x01 ? colours[3] : colours[0];
		}
		pixel += 8;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_bitmap_320 ( void )
{
	Uint8 *vp = vicptr_video_40 + video_counter;
	Uint8 *bp = vicptr_bitmap_320 + (video_counter << 3) + row_counter;
	int a;
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 40; a++) {
		Uint8 data = *(vp++);
		Uint32 fg_colour = palette[data >> 4];
		Uint32 bg_colour = palette[data & 15];
		data = *bp;
		pixel[ 0] = pixel[ 1] = data & 0x80 ? fg_colour : bg_colour;
		pixel[ 2] = pixel[ 3] = data & 0x40 ? fg_colour : bg_colour;
		pixel[ 4] = pixel[ 5] = data & 0x20 ? fg_colour : bg_colour;
		pixel[ 6] = pixel[ 7] = data & 0x10 ? fg_colour : bg_colour;
		pixel[ 8] = pixel[ 9] = data & 0x08 ? fg_colour : bg_colour;
		pixel[10] = pixel[11] = data & 0x04 ? fg_colour : bg_colour;
		pixel[12] = pixel[13] = data & 0x02 ? fg_colour : bg_colour;
		pixel[14] = pixel[15] = data & 0x01 ? fg_colour : bg_colour;
		bp += 8;
		pixel += 16;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_bitmap_640 ( void )
{
	Uint8 *vp = vicptr_video_80 + video_counter;
	Uint8 *bp = vicptr_bitmap_640 + (video_counter << 3) + row_counter;
	int a;
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 80; a++) {
		Uint8 data = *(vp++);
		Uint32 fg_colour = palette[data >> 4];
		Uint32 bg_colour = palette[data & 15];
		data = *bp;
		pixel[0] = data & 0x80 ? fg_colour : bg_colour;
		pixel[1] = data & 0x40 ? fg_colour : bg_colour;
		pixel[2] = data & 0x20 ? fg_colour : bg_colour;
		pixel[3] = data & 0x10 ? fg_colour : bg_colour;
		pixel[4] = data & 0x08 ? fg_colour : bg_colour;
		pixel[5] = data & 0x04 ? fg_colour : bg_colour;
		pixel[6] = data & 0x02 ? fg_colour : bg_colour;
		pixel[7] = data & 0x01 ? fg_colour : bg_colour;
		bp += 8;
		pixel += 8;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_mcmbitmap_320 ( void )
{
	Uint8 *vp = vicptr_video_40 + video_counter;
	Uint8 *bp = vicptr_bitmap_320 + (video_counter << 3) + row_counter;
	Uint8 *cp = COLMEMPTR + video_counter;
	Uint32 colours[4];
	int a;
	colours[0] = VIC_REG_COLOUR(0x21);
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 40; a++) {
		Uint8 data = *(vp++);
		colours[1] = palette[data >> 4];
		colours[2] = palette[data & 15];
		colours[3] = palette[(*(cp++)) & 15];
		data = *bp;
		pixel[ 0] = pixel[ 1] = pixel[ 2] = pixel[ 3] = colours[ data >> 6     ];
		pixel[ 4] = pixel[ 5] = pixel[ 6] = pixel[ 7] = colours[(data >> 4) & 3];
		pixel[ 8] = pixel[ 9] = pixel[10] = pixel[11] = colours[(data >> 2) & 3];
		pixel[12] = pixel[13] = pixel[14] = pixel[15] = colours[ data       & 3];
		bp += 8;
		pixel += 16;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_mcmbitmap_640 ( void )
{
	Uint8 *vp = vicptr_video_80 + video_counter;
	Uint8 *bp = vicptr_bitmap_640 + (video_counter << 3) + row_counter;
	Uint8 *cp = COLMEMPTR + video_counter;
	Uint32 colours[4];
	int a;
	colours[0] = VIC_REG_COLOUR(0x21);
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 80; a++) {
		Uint8 data = *(vp++);
		colours[1] = palette[data >> 4];
		colours[2] = palette[data & 15];
		colours[3] = palette[(*(cp++)) & 15];
		data = *bp;
		pixel[0] = pixel[1] = colours[ data >> 6     ];
		pixel[2] = pixel[3] = colours[(data >> 4) & 3];
		pixel[4] = pixel[5] = colours[(data >> 2) & 3];
		pixel[6] = pixel[7] = colours[ data       & 3];
		bp += 8;
		pixel += 8;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_bitplane_320 ( void )
{
	Uint8 *bp = memory + (video_counter << 3) + row_counter;
	int a;
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 40; a++) {
		int bitpos;
		Uint8 fetch[8] = {
			bp[bitplane_addr_320[0]], bp[bitplane_addr_320[1]], bp[bitplane_addr_320[2]], bp[bitplane_addr_320[3]],
			bp[bitplane_addr_320[4]], bp[bitplane_addr_320[5]], bp[bitplane_addr_320[6]], bp[bitplane_addr_320[7]]
		};
		for (bitpos = 128; bitpos; bitpos >>= 1) {
			// Do not try this at home ...
			pixel[0] = pixel[1] = palette[((
				((fetch[0] & bitpos) ?   1 : 0) |
				((fetch[1] & bitpos) ?   2 : 0) |
				((fetch[2] & bitpos) ?   4 : 0) |
				((fetch[3] & bitpos) ?   8 : 0) |
				((fetch[4] & bitpos) ?  16 : 0) |
				((fetch[5] & bitpos) ?  32 : 0) |
				((fetch[6] & bitpos) ?  64 : 0) |
				((fetch[7] & bitpos) ? 128 : 0)
			) & vic3_registers[0x32]) ^ vic3_registers[0x3B]];
			pixel += 2;
		}
		bp += 8;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}


static void renderer_bitplane_640 ( void )
{
	Uint8 *bp = memory + (video_counter << 3) + row_counter;
	int a, enable = vic3_registers[0x32] & 15;	// in H640 bitplane modes, only lower 4 bitplanes can be used (TODO: is that true? check it!)
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), LEFT_BORDER_SIZE);
	for (a = 0; a < 80; a++) {
		int bitpos;
		Uint8 fetch[8] = {
			bp[bitplane_addr_640[0]], bp[bitplane_addr_640[1]], bp[bitplane_addr_640[2]], bp[bitplane_addr_640[3]],
			bp[bitplane_addr_640[4]], bp[bitplane_addr_640[5]], bp[bitplane_addr_640[6]], bp[bitplane_addr_640[7]]
		};
		for (bitpos = 128; bitpos; bitpos >>= 1)
			// Do not try this at home ...
			*(pixel++) = palette[((
				((fetch[0] & bitpos) ?   1 : 0) |
				((fetch[1] & bitpos) ?   2 : 0) |
				((fetch[2] & bitpos) ?   4 : 0) |
				((fetch[3] & bitpos) ?   8 : 0) |
				((fetch[4] & bitpos) ?  16 : 0) |
				((fetch[5] & bitpos) ?  32 : 0) |
				((fetch[6] & bitpos) ?  64 : 0) |
				((fetch[7] & bitpos) ? 128 : 0)
			) & enable) ^ vic3_registers[0x3B]];
		bp += 8;
	}
	STATIC_COLOUR_RENDERER(VIC_REG_COLOUR(0x20), RIGHT_BORDER_SIZE);
}



static void sprite_renderer ( void );


int vic3_render_scanline ( void )
{
	if (scanline < 50 - TOP_BORDER_SIZE) {
		scanline++;
		return 0;
	} else if (scanline < 50) {
		if (!frameskip)
			renderer_disabled_screen();
		scanline++;
		return 0;
	} else if (scanline < 250) {
		if (!frameskip) {
			scanline_render_debug_info[scanline] = (((vic3_registers[0x11] & 0x60) | (vic3_registers[0x16] & 0x10)) >> 4) + 'a';
			renderer_func();	// call the scanline renderer for the current video mode, a function pointer
			if (row_counter == 7) {
				row_counter = 0;
				video_counter += video_counter_inc;
			} else
				row_counter++;
		}
		scanline++;
		return 0;
	} else if (scanline < 250 + BOTTOM_BORDER_SIZE) {
		if (!frameskip)
			renderer_disabled_screen();
		scanline++;
		return 0;
	} else if (scanline == 311) {
		scanline = 0;
		video_counter = 0;
		row_counter = 0;
		if (blink_counter)
			blink_counter--;
		else {
			blink_counter = BLINK_COUNTER_INIT;
			blink_phase = ~blink_phase;
		}
		if (!frameskip) {
			if (XEMU_UNLIKELY(pixel != pixel_end))
				FATAL("Renderer failure: pixel=%p != end=%p (diff=%d) height=%d", pixel, pixel_end, (int)(pixel_end - pixel), SCREEN_HEIGHT);
			// FIXME: Highly incorrect, rendering sprites once *AFTER* the screen content ...
			sprite_renderer();
		} else {
			if (XEMU_UNLIKELY(pixel != pixel_start))
				FATAL("Renderer failure: pixel=%p != start=%p", pixel, pixel_start);
		}
		return 1; // return value non-zero: end-of-frame, emulator should update the SDL rendering context, then call vic3_open_frame_access()
	}
	// else ...
	scanline++;
	return 0;
}



// write register functions should call this on any register change which may affect display mode with settings already stored in vic3_registers[]!
static void select_renderer_func ( void )
{
	// Ugly, but I really don't have idea what happens in VIC-III internally when you
	// switch H640 bit within a frame ... Currently I just limit video counter to be
	// within 1K if H640 is not set. That's certainly incorrect, but it should be tested
	// on a real C65 what happens (eg in text mode) if H640 bit is flipped at the middle
	// of a screen.
	// Also FIXME: where are the sprites in bitplane mode?!
	if (!IS_H640) {
		video_counter &= 1023;
		video_counter_inc = 40;
		sprite_pointers = vicptr_video_40 + 0x3F8;
	} else {
		video_counter_inc = 80;
		sprite_pointers = vicptr_video_80 + 0x7F8;
	}
	// if DEN (Display Enable) bit is zero, then you won't see anything (border colour is rendered!)
	if (!(vic3_registers[0x11] & 0x10)) {
		renderer_func = renderer_disabled_screen;
		return;
	}
	// if BPM bit is set, it's bitplane mode (VIC-III)
	if (vic3_registers[0x31] & 16) {
		renderer_func = IS_H640 ? renderer_bitplane_640 : renderer_bitplane_320;
		return;
	}
	// Otherwise, classic VIC-II modes, select one, from the possible 8 modes (note: some of them are INVALID!)
	// the pattern: ECM - BMM - MCM
	switch (((vic3_registers[0x11] & 0x60) | (vic3_registers[0x16] & 0x10)) >> 4) {
		case 0:	// Standard text mode		(ECM/BMM/MCM=0/0/0)
			renderer_func = IS_H640 ? renderer_text_80       : renderer_text_40;
			break;
		case 1:	// Multicolor text mode		(ECM/BMM/MCM=0/0/1)
			renderer_func = IS_H640 ? renderer_mcmtext_80    : renderer_mcmtext_40;
			break;
		case 2: // Standard bitmap mode		(ECM/BMM/MCM=0/1/0)
			renderer_func = IS_H640 ? renderer_bitmap_640    : renderer_bitmap_320;
			break;
		case 3: // Multicolor bitmap mode	(ECM/BMM/MCM=0/1/1)
			renderer_func = IS_H640 ? renderer_mcmbitmap_640 : renderer_mcmbitmap_320;
			break;
		case 4: // ECM text mode		(ECM/BMM/MCM=1/0/0)
			renderer_func = IS_H640 ? renderer_ecmtext_80    : renderer_ecmtext_40;
			break;
		case 5: // Invalid text mode		(ECM/BMM/MCM=1/0/1)
		case 6: // Invalid bitmap mode 1	(ECM/BMM/MCM=1/1/0)
		case 7: // Invalid bitmap mode 2	(ECM/BMM/MCM=1/1/1)
			// Invalid modes generate only "blackness"
			renderer_func = renderer_invalid_mode;
			break;
	}
}


// Must be called if register 0x18 is written, or bank set, or CROM VIC-III setting changed
static void select_chargen ( void )
{
	// TODO: incorrect implementation. Currently we handle the situation of "char ROM" in certain banks
	// only for character generator info. That's not so correct, as VIC always see that memory in those
	// banks not only for chargen info! However, it's much easier and faster to do this way ...
	if ((!(vic2_bank_number & 1)) && ((vic3_registers[0x18] & 0x0C) == 4)) {
		// FIXME: I am really lost with this CROM bit, and the layout of C65 ROM on charsets, sorry. Maybe this is totally wrong!
		// The problem, for my eye, the two charsets (C64/C65?!) seems to be the very same :-/
		// BUT, it seems, C65 mode *SETS* CROM bit. While for C64 mode it is CLEARED.
		if (vic3_registers[0x30] & 64)	// the CROM bit
			vicptr_chargen = memory + 0x29000 + ((vic3_registers[0x18] & 0x0E) << 10) - 0x1000;	// ... so this should be the C65 charset ...
		else
			vicptr_chargen = memory + 0x2D000 + ((vic3_registers[0x18] & 0x0E) << 10) - 0x1000;	// ... and this should be the C64
	} else
		vicptr_chargen = vicptr_bank16k + ((vic3_registers[0x18] & 0x0E) << 10);	// otherwise, chargen is in RAM
}



// Must be called if any memory ptr is changed (not the bitplane related of VIC-III though!)
// Also, if VIC-II bank is changed (that's done by vic3_select_bank function)
static void select_vic2_memory ( void )
{
	// 24| $d018 |VM13|VM12|VM11|VM10|CB13|CB12|CB11|  - | Memory pointers
	vicptr_video_40 = vicptr_bank16k + ((vic3_registers[0x18] & 0xF0) << 6);
	vicptr_video_80 = vicptr_bank16k + ((vic3_registers[0x18] & 0xE0) << 6);
	sprite_pointers = IS_H640 ? vicptr_video_80 + 0x7F8 : vicptr_video_40 + 0x3F8;
	select_chargen();
	vicptr_bitmap_320 = vicptr_bank16k + ((vic3_registers[0x18] & 8) << 10);
	// Interestingly, VIC-III has a notion of "32K sized bank" as well, over VIC-II but
	// used only for H640 bit set with bitmap modes (the video matrix is still fetched
	// from the 16K bank though). There is no other reason for the bank, other VIC-II
	// modes use 16K bank and VIC-III bitplanes don't use the bank notion at all.
	vicptr_bitmap_640 = vicptr_bank32k + ((vic3_registers[0x18] & 8) << 11);
}


// Called by the emulator on VIC bank selection
// Parameter must be in range of 0-3, with 0 meaning the lowest addressed bank
void vic3_select_bank ( int bank )
{
	bank &= 3;
	// to avoid of pointer rebuilding on all writes, we do this only, if bank is changed
	if (bank != vic2_bank_number) {
		vic2_bank_number = bank;
		bank <<= 14;
		vicptr_bank16k = memory +  bank;
		vicptr_idlefetch_p = vicptr_bank16k + 0x3FFF;
		vicptr_bank32k = memory + (bank & 0x8000);	// VIC-III also has 32K VIC bank if H640 is used with bitmap mode
		select_vic2_memory();
	}
}





// Caller should give only 0-$3F or 0-$7F addresses
void vic3_write_reg ( int addr, Uint8 data )
{
	DEBUG("VIC3: write reg $%02X with data $%02X" NL, addr, data);
	if (addr == 0x2F) {
		if (!vic_new_mode && data == 0x96 && vic3_registers[0x2F] == 0xA5) {
			vic_new_mode = VIC_NEW_MODE;
			DEBUG("VIC3: switched into NEW I/O access mode :)" NL);
		} else if (vic_new_mode) {
			vic_new_mode = 0;
			DEBUG("VIC3: switched into OLD I/O access mode :(" NL);
		}
	}
	if (!vic_new_mode && addr > 0x2F) {
		DEBUG("VIC3: ignoring writing register $%02X (with data $%02X) because of old I/O access mode selected" NL, addr, data);
		return;
	}
	switch (addr) {
		case 0x11:
			vic3_registers[0x11] = data;
			select_renderer_func();
			compare_raster = (compare_raster & 0xFF) | ((data & 0x80) << 1);
			DEBUG("VIC3: compare raster is now %d" NL, compare_raster);
			break;
		case 0x12:
			compare_raster = (compare_raster & 0xFF00) | data;
			DEBUG("VIC3: compare raster is now %d" NL, compare_raster);
			break;
		case 0x16:
			vic3_registers[0x16] = data;
			select_renderer_func();
			break;
		case 0x18:
			vic3_registers[0x18] = data;
			select_vic2_memory();
			break;
		case 0x19:
			interrupt_status = interrupt_status & (~data) & 15;
			vic3_interrupt_checker();
			break;
		case 0x1A:
			data &= 15;
			break;
		case 0x30:
			// Save some un-needed memory translating table rebuilds, if there is important bits (for us) changed.
			// CRAM@DC00 is not handled by the translator directly, so bit0 does not apply here!
			if (
				(data & 0xB8) != (vic3_registers[0x30] & 0xB8)
			) {
				DEBUG("MEM: applying new memory configuration because of VIC3 $30 is written" NL);
				vic3_registers[0x30] = data;	// early write because of apply_memory_config() needs it
				apply_memory_config();
			} else {
				vic3_registers[0x30] = data;	// we need this for select_chargen() below
				DEBUG("MEM: no need for new memory configuration (because of VIC3 $30 is written): same ROM bit values are set" NL);
			}
			select_chargen();
			palette = (data & 4) ? vic3_palette : vic3_rom_palette;
			break;
		case 0x31:
			vic3_registers[0x31] = data;
			attributes = (data & 32) ? 0xF0 : 0x00;	// currently, I use $F0 for on, 00 off, to be able to use as an attrib mask directly for upper 4 bits
			select_renderer_func();
			if ((data & 64)) {
				cpu_cycles_per_scanline = FAST_CPU_CYCLES_PER_SCANLINE;
				cpu65_set_ce_timing(1);
				strcpy(emulator_speed_title, "3.5MHz");
			} else {
				cpu_cycles_per_scanline = SLOW_CPU_CYCLES_PER_SCANLINE;
				cpu65_set_ce_timing(0);
				strcpy(emulator_speed_title, "1MHz");
			}
			if ((data & 15) && warn_ctrl_b_lo) {
				INFO_WINDOW("VIC3 control-B register V400, H1280, MONO and INT features are not emulated yet!\nThere will be no further warnings on this issue.");
				warn_ctrl_b_lo = 0;
			}
			break;
		case 0x33:
		case 0x34:
		case 0x35:
		case 0x36:
		case 0x37:
		case 0x38:
		case 0x39:
		case 0x3A:
			bitplane_addr_320[addr - 0x33] = ((data & 14) << 12) + (addr & 1 ? 0 : 0x10000);
			bitplane_addr_640[addr - 0x33] = ((data & 12) << 12) + (addr & 1 ? 0 : 0x10000);
			break;
		default:
			if (addr >= 0x20 && addr < 0x2F)
				data &= 15;
			break;
	}
	vic3_registers[addr] = data;
}	



// Caller should give only 0-$3F or 0-$7F addresses
Uint8 vic3_read_reg ( int addr )
{
	Uint8 result;
	if (!vic_new_mode && addr > 0x2F) {
		DEBUG("VIC3: ignoring reading register $%02X because of old I/O access mode selected, answer is $FF" NL, addr);
		return 0xFF;
	}
	switch (addr) {
		case 0x11:
			result =  (vic3_registers[0x11] & 0x7F) | ((scanline >> 1) & 0x80);
			break;
		case 0x12:
			result = scanline & 0xFF;
			break;
		case 0x16:
			result = vic3_registers[addr] | (128 + 64);	// unused bits [TODO: also on VIC3?]
			break;
		case 0x19:
			result = interrupt_status | (64 + 32 + 16);	// unused bits [TODO: also on VIC3?]
			break;
		case 0x1A:
			result = vic3_registers[addr] | 0xF0;		// unused bits [TODO: also on VIC3?]
			break;
		case 0x18:
			result = vic3_registers[addr] | 1;		// unused bit [TODO: also on VIC3?]
			break;
		default:
			result = vic3_registers[addr];
			if (addr >= 0x20 && addr < 0x2F)
				result |= 0xF0;				// unused bits [TODO: also on VIC3?]
			break;
	}
	DEBUG("VIC3: read reg $%02X with result $%02X" NL, addr, result);
	return result;
}





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



void vic3_init ( void )
{
	int r, g, b, i;
	// *** Init 4096 element palette with RGB components for faster access later on palette register changes (avoid SDL calls to convert)
	for (r = 0, i = 0; r < 16; r++)
		for (g = 0; g < 16; g++)
			for (b = 0; b < 16; b++)
				rgb_palette[i++] = SDL_MapRGBA(sdl_pix_fmt, r * 17, g * 17, b * 17, 0xFF); // 15*17=255, last arg 0xFF: alpha channel for SDL
	// *** Init VIC3 registers and palette
	memset(scanline_render_debug_info, 0x20, sizeof scanline_render_debug_info);
	scanline_render_debug_info[sizeof(scanline_render_debug_info) - 1] = 0;
	vic_new_mode = 0;
	blink_counter = 0;
	blink_phase = 0;
	attributes = 0;
	interrupt_status = 0;
	palette = vic3_rom_palette;
	frameskip = 0;
	scanline = 0;
	compare_raster = 0;
	cpu_cycles_per_scanline = FAST_CPU_CYCLES_PER_SCANLINE;
	strcpy(emulator_speed_title, "3.5MHz");
	video_counter = 0;
	row_counter = 0;
	for (i = 0; i < 0x100; i++) {	// Initialize all palette registers to zero, initially, to have something ...
		if (i < sizeof vic3_registers)
			vic3_registers[i] = 0;	// Also the VIC3 registers ...
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
	// bitplanes
	bitplane_addr_320[0] = bitplane_addr_320[2] = bitplane_addr_320[4] = bitplane_addr_320[6] =
	bitplane_addr_640[0] = bitplane_addr_640[2] = bitplane_addr_640[4] = bitplane_addr_640[6] = 0;
	bitplane_addr_320[1] = bitplane_addr_320[3] = bitplane_addr_320[5] = bitplane_addr_320[7] =
	bitplane_addr_640[1] = bitplane_addr_640[3] = bitplane_addr_640[5] = bitplane_addr_640[7] = 0x10000;
	// To force bank selection, and pointer re-calculations
	vic2_bank_number = -1;	// invalid, faked one, so vic3_select_bank() will surely not use the "cached" result initially
	vic3_select_bank(0);
	// To force select _some_ renderer
	select_renderer_func();
	DEBUG("VIC3: has been initialized." NL);
}


#define SPRITE_X_START_SCREEN	24
#define SPRITE_Y_START_SCREEN	50


/* Extremely incorrect sprite emulation! BUGS:
   * Sprites cannot be behind the background (sprite priority)
   * No sprite-background collision detection
   * No sprite-sprite collision detection
   * This is a simple, after-the-rendered-frame render-sprites one-by-one algorithm
   * Very ugly, quick&dirty hack, not so optimal either, even without the other mentioned bugs ...
*/
static void render_one_sprite ( int sprite_no, int sprite_mask, Uint8 *data, Uint32 *p )
{
	Uint32 colours[4];
	int sprite_y = vic3_registers[sprite_no * 2 + 1] - SPRITE_Y_START_SCREEN;
	int sprite_x = ((vic3_registers[sprite_no * 2] | ((vic3_registers[16] & sprite_mask) ? 0x100 : 0)) - SPRITE_X_START_SCREEN) * 2;
	int expand_x = vic3_registers[29] & sprite_mask;
	int expand_y = vic3_registers[23] & sprite_mask;
	int lim_y = sprite_y + ((expand_y) ? 42 : 21);
	int mcm = vic3_registers[0x1C] & sprite_mask;
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




/* This is the one-frame-at-once (highly incorrect implementation, that is)
   renderer for sprites. */
static void sprite_renderer ( void )
{
	int sprites = vic3_registers[0x15];
	if (sprites) {	// Render sprites. VERY BAD. We ignore sprite priority as well (cannot be behind the background)
		int a;
		if (warn_sprites) {
			INFO_WINDOW("WARNING: Sprite emulation is really bad! (enabled_mask=$%02X)", sprites);
			warn_sprites = 0;
		}
		for (a = 7; a >= 0; a--) {
			int mask = 1 << a;
			if ((sprites & mask))
				render_one_sprite(a, mask, vicptr_bank16k + (sprite_pointers[a] << 6), pixel_start);	// sprite_pointers are set by the renderer functions above!
		}
	}
}


/* --- SNAPSHOT RELATED --- */


#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_VIC3_BLOCK_VERSION	0
#define SNAPSHOT_VIC3_BLOCK_SIZE	0x400

int vic3_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_VIC3_BLOCK_SIZE];
	int a;
	if (block->block_version != SNAPSHOT_VIC3_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad VIC3 block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	for (a = 0; a < 0x80; a++) {
		vic_new_mode = VIC_NEW_MODE;
		vic3_write_reg(a, buffer[a + 0x80]);
	}
	for (a = 0; a < 0x300; a++)
		vic3_write_palette_reg(a, buffer[a + 0x100]);
	vic_new_mode = buffer[128];
	interrupt_status = (int)P_AS_BE32(buffer + 129);
	return 0;
}


int vic3_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_VIC3_BLOCK_SIZE];
	int a = xemusnap_write_block_header(def->idstr, SNAPSHOT_VIC3_BLOCK_VERSION);
	if (a) return a;
	memset(buffer, 0xFF, sizeof buffer);
	/* saving state ... */
	memcpy(buffer + 0x80,  vic3_registers, 0x80);		//  $80 bytes
	memcpy(buffer + 0x100, vic3_palette_nibbles, 0x300);	// $300 bytes
	buffer[128] = vic_new_mode;
	U32_AS_BE(buffer + 129, interrupt_status);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
