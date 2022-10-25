/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the Commander X16 emulation. Note: the source is overcrowded with comments by intent :)
   That it can useful for other people as well, or someone wants to contribute, etc ...

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
#include "vera.h"
#include "xemu/cpu65.h"
#include "sdcard.h"
#include "commander_x16.h"


#define DEBUGVERA DEBUGPRINT
//#define DEBUGVERA DEBUG
//#define DEBUGVERA(...)

#define SCANLINES_TOTAL		525
#define SCANLINE_START_VSYNC	492
#define SCANLINE_STOP_VSYNC	495

#define MODE_NTSC_COMPOSITE	2
// 4 bits of interrupt info
#define INTERRUPT_REGISTER_MASK	15
//#define VRAM_SIZE		0x20000
//#define UART_CLOCK		25000000
//#define UART_DEFAULT_BAUD_RATE	1000000

#define	VSYNC_IRQ		1
#define	RASTER_IRQ		2
#define	SPRITE_IRQ		4
#define	AFLOW_IRQ		8


static struct dataport_st {	// current_port variable selects one of these
	int	addr;
	int	inc;
	Uint8	inc_reg_raw;
} dataport[2];
static const int dataport_increment_values[32] = { 0, 0, 1, -1, 2, -2, 4, -4, 8, -8, 16, -16, 32, -32, 64, -64, 128, -128, 256, -256, 512, -512, 40, -40, 80, -80, 160, -160, 320, -320, 640, -640 };
static int	current_port;	// current dataport in use for CPU VERA registers 0/1/2
static int	dcsel;		// 0/1, selects the function of VERA registers $9-$C

// the exact integer values are very important!
enum layer_modes {
	LAYER_MODE_TEXT_16C = 0,
	LAYER_MODE_TILE_4C = 1,
	LAYER_MODE_TILE_16C = 2,
	LAYER_MODE_TILE_256C = 3,
	LAYER_MODE_BITMAP_2C = 4,
	LAYER_MODE_BITMAP_4C = 5,
	LAYER_MODE_BITMAP_16C = 6,
	LAYER_MODE_BITMAP_256C = 7,
	LAYER_MODE_TEXT_256C = 8,
};

static struct layer_st {
	Uint8	regspace[7];
	int	enabled;
	int	map_base, tile_base;
	//int	colour_depth;
	//int	bitmap_mode;
	//int	t256c;
	int	map_width;
	int	map_height;
	int	tile_width16;
	int	tile_height16;
	int	hscroll;
	int	vscroll;
	int	palette_offset;
	enum layer_modes mode;
} layer[2];

static struct {
	int	video_mode;
	int	chroma_kill;
	int	sprites_enabled;
	Uint8	regspace[8];
	int	hscale,vscale;
	int	hstart,vstart;
	int	hstop,vstop;
	Uint8	border;
} dc;

static struct {
	Uint32	colours[0x100];
	Uint32	border_colour;
	Uint32	all_rgb[4096];
	Uint32	all_mono[4096];
	Uint32	*all;
	Uint8	regspace[0x200];
} palette;

static struct {
	Uint8	received_data;
	int	select;
	//int	busy;		// not so much used currently ...
	int	slow_clock;
} spi;

static Uint8	ien_reg, isr_reg;
static int	irqline;
static Uint8	vram[0x20000];		// the 128K of video-ram ("VRAM")


// I admit, I got this table from Mist's X16 emulator, since I had no idea what the default palette _exactly_ was ...
static const Uint16 x16paldata[] = {
	0x000, 0xFFF, 0x800, 0xAFE, 0xC4C, 0x0C5, 0x00A, 0xEE7, 0xD85, 0x640, 0xF77, 0x333, 0x777, 0xAF6, 0x08F, 0xBBB,
	0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0x888, 0x999, 0xAAA, 0xBBB, 0xCCC, 0xDDD, 0xEEE, 0xFFF,
	0x211, 0x433, 0x644, 0x866, 0xA88, 0xC99, 0xFBB, 0x211, 0x422, 0x633, 0x844, 0xA55, 0xC66, 0xF77, 0x200, 0x411,
	0x611, 0x822, 0xA22, 0xC33, 0xF33, 0x200, 0x400, 0x600, 0x800, 0xA00, 0xC00, 0xF00, 0x221, 0x443, 0x664, 0x886,
	0xAA8, 0xCC9, 0xFEB, 0x211, 0x432, 0x653, 0x874, 0xA95, 0xCB6, 0xFD7, 0x210, 0x431, 0x651, 0x862, 0xA82, 0xCA3,
	0xFC3, 0x210, 0x430, 0x640, 0x860, 0xA80, 0xC90, 0xFB0, 0x121, 0x343, 0x564, 0x786, 0x9A8, 0xBC9, 0xDFB, 0x121,
	0x342, 0x463, 0x684, 0x8A5, 0x9C6, 0xBF7, 0x120, 0x241, 0x461, 0x582, 0x6A2, 0x8C3, 0x9F3, 0x120, 0x240, 0x360,
	0x480, 0x5A0, 0x6C0, 0x7F0, 0x121, 0x343, 0x465, 0x686, 0x8A8, 0x9CA, 0xBFC, 0x121, 0x242, 0x364, 0x485, 0x5A6,
	0x6C8, 0x7F9, 0x020, 0x141, 0x162, 0x283, 0x2A4, 0x3C5, 0x3F6, 0x020, 0x041, 0x061, 0x082, 0x0A2, 0x0C3, 0x0F3,
	0x122, 0x344, 0x466, 0x688, 0x8AA, 0x9CC, 0xBFF, 0x122, 0x244, 0x366, 0x488, 0x5AA, 0x6CC, 0x7FF, 0x022, 0x144,
	0x166, 0x288, 0x2AA, 0x3CC, 0x3FF, 0x022, 0x044, 0x066, 0x088, 0x0AA, 0x0CC, 0x0FF, 0x112, 0x334, 0x456, 0x668,
	0x88A, 0x9AC, 0xBCF, 0x112, 0x224, 0x346, 0x458, 0x56A, 0x68C, 0x79F, 0x002, 0x114, 0x126, 0x238, 0x24A, 0x35C,
	0x36F, 0x002, 0x014, 0x016, 0x028, 0x02A, 0x03C, 0x03F, 0x112, 0x334, 0x546, 0x768, 0x98A, 0xB9C, 0xDBF, 0x112,
	0x324, 0x436, 0x648, 0x85A, 0x96C, 0xB7F, 0x102, 0x214, 0x416, 0x528, 0x62A, 0x83C, 0x93F, 0x102, 0x204, 0x306,
	0x408, 0x50A, 0x60C, 0x70F, 0x212, 0x434, 0x646, 0x868, 0xA8A, 0xC9C, 0xFBE, 0x211, 0x423, 0x635, 0x847, 0xA59,
	0xC6B, 0xF7D, 0x201, 0x413, 0x615, 0x826, 0xA28, 0xC3A, 0xF3C, 0x201, 0x403, 0x604, 0x806, 0xA08, 0xC09, 0xF0B
};




static XEMU_INLINE void UPDATE_IRQ ( void )
{
	// leave any upper bits as-is, used by other IRQ sources than VERA, in the emulation of VIAs for example
	// This works at the 65XX emulator level, that is a single irqLevel variable is though to be boolean as zero (no IRQ) or non-ZERO (IRQ request),
	// but, on other component level I use it as bit fields from various IRQ sources. That is, I save emulator time to create an 'OR" logic for
	// various interrupt sources.
	cpu65.irqLevel = (cpu65.irqLevel & ~INTERRUPT_REGISTER_MASK) | (ien_reg & isr_reg);
}

#if 0
static XEMU_INLINE void SET_IRQ ( int irq_type )
{
	isr_reg |= irq_type;
	UPDATE_IRQ();
}

static XEMU_INLINE void CLEAR_IRQ ( int irq_type )
{
	isr_reg &= ~irq_type;
	UPDATE_IRQ();
}
#endif


// Updates a palette enty based on the "addr" in palette-memory
// Also gives "monochrome" version if it's needed
// Also, updates the border colour if the changed x16 colour index is used for the border
static void update_palette_entry ( int addr )
{
	addr &= 0x1FE;
	const Uint32 colour = palette.all[palette.regspace[addr] + ((palette.regspace[addr + 1] & 0xF) << 8)];
	addr >>= 1;
	palette.colours[addr] = colour;
	if (addr == dc.border)
		palette.border_colour = colour;
}


static XEMU_INLINE void update_all_palette_entries ( void )
{
	for (int a = 0; a < 0x200; a += 2)
		update_palette_entry(a);
}


static void write_layer_register ( struct layer_st *lp, const int reg, const Uint8 data )
{
	static const int map_dimensions[] = { 32, 64, 128, 256 };
	switch (reg) {
		case 0x0:
			//layer[ln].colour_depth = data & 3;
			//layer[ln].bitmap_mode = data & 4;
			//layer[ln].t256c = data & 8;
			lp->map_width  = map_dimensions[(data >> 4) & 2];
			lp->map_height = map_dimensions[(data >> 6) & 2];
			lp->mode = (data & 15) == 8 ? LAYER_MODE_TEXT_256C : (enum layer_modes)(data & 7);
			break;
		case 0x1:
			lp->map_base = data << 9;
			break;
		case 0x2:
			lp->tile_base = (data & 0xFC) << (11 - 2);
			lp->tile_width16 = data & 1;
			lp->tile_height16 = data & 2;
			break;
		case 0x3:
			lp->hscroll = (lp->hscroll & 0xFF00) + data;
			break;
		case 0x4:
			lp->hscroll = (lp->hscroll & 0x00FF) + ((data & 0xF) << 8);
			// the very same info is also used as "palette offset" in some situation, so let's propogate here, as it has different storage method as for the data as "hscroll"
			lp->palette_offset = (data & 0xF) << 4;
			break;
		case 0x5:
			lp->vscroll = (lp->vscroll & 0xFF00) + data;
			break;
		case 0x6:
			lp->vscroll = (lp->vscroll & 0x00FF) + ((data & 0xF) << 8);
			break;
	}
	lp->regspace[reg] = data;
}


static void write_vram_through_dataport ( struct dataport_st *dp, Uint8 data )
{
	const int addr = dp->addr;	// current addr
	dp->addr = (addr + dp->inc) & 0x1FFFF;	// increment for the next access
	DEBUGVERA("VERA: writing VMEM at $%05X with data $%02X" NL, addr, data);
	vram[addr] = data;	// writing always write the 128K VRAM even for the $1F9C0-$1FFFF part, which are various registers are not "real" VRAM in the common sense
	// $00000 - $1F9BF: "normal" video RAM address, no special other meaning
	if (addr < 0x1F9C0)
		return;
	// $1F9C0 - $1F9FF: PSG registers
	if (addr < 0x1FA00)
		return;	// FIXME: implement this!
	// $1FA00 - $1FBFF: palette
	if (addr < 0x1FC00) {
		palette.regspace[addr & 0x1FF] = data;
		update_palette_entry(addr);
		return;
	}
	// $1FC00 - $1FFFF: sprite attributes
	return;	// FIXME: implement these!
}


void vera_write_cpu_register ( int reg, Uint8 data )
{
	reg &= 0x1F;
	DEBUGVERA("VERA: writing register %d with data $%02X" NL, reg, data);
	switch (reg) {
		case 0x00:
			dataport[current_port].addr = (dataport[current_port].addr & 0x1FF00) | data;
			break;
		case 0x01:
			dataport[current_port].addr = (dataport[current_port].addr & 0x100FF) | (data << 8);
			break;
		case 0x02:
			dataport[current_port].inc_reg_raw = data & 0xF8;
			dataport[current_port].inc  = dataport_increment_values[data >> 3];
			dataport[current_port].addr = (dataport[current_port].addr & 0x0FFFF) | ((data & 1) << 16);
			break;
		case 0x03:
			write_vram_through_dataport(&dataport[0], data);
			break;
		case 0x04:
			write_vram_through_dataport(&dataport[1], data);
			break;
		case 0x05:
			if (XEMU_UNLIKELY(data & 0x80)) {
				vera_reset();
			} else {
				current_port = data & 1;
				dcsel = !!(data & 2);
			}
			break;
		case 0x06:
			irqline = (irqline & 0x0FF) + ((data & 0x80) << 1);
			ien_reg = data & INTERRUPT_REGISTER_MASK;
			UPDATE_IRQ();
			break;
		case 0x07:
			isr_reg &= (~data) & INTERRUPT_REGISTER_MASK;
			UPDATE_IRQ();
			break;
		case 0x08:
			irqline = (irqline & 0x100) + data;
			break;
		case 0x09: case 0x0A: case 0x0B: case 0x0C:
			reg = reg - 0x09 + (!dcsel ? 0 : 4);
			switch (reg) {
				case 0:
					dc.video_mode = data & 3;
					dc.chroma_kill = data & 4;
					const Uint32 *pal_old = palette.all;
					palette.all = XEMU_UNLIKELY(dc.video_mode == 2 && dc.chroma_kill) ? palette.all_mono : palette.all_rgb;
					if (pal_old != palette.all)
						update_all_palette_entries();
					layer[0].enabled = data & 16;
					layer[1].enabled = data & 32;
					dc.sprites_enabled = data & 64;
					//FIXME: implement this!
					//if ((dc.regspace[0] & 3) != (data & 3))
					//	change_video_mode(data & 3);
					break;
				case 1:
					dc.hscale = data;
					break;
				case 2:
					dc.vscale = data;
					break;
				case 3:
					dc.border = data;
					palette.border_colour = palette.all[dc.border];
					break;
				case 4:
					dc.hstart = data << 2;
					break;
				case 5:
					dc.hstop = data << 2;
					break;
				case 6:
					dc.vstart = data << 1;
					break;
				case 7:
					dc.vstop = data << 1;
					break;
			}
			dc.regspace[reg] = data;
			break;
		case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13:
			write_layer_register(&layer[0], reg - 0x0D, data);
			break;
		case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A:
			write_layer_register(&layer[1], reg - 0x14, data);
			break;
		case 0x1E:
			spi.received_data = sdcard_spi_transfer(data);
			break;
		case 0x1F:
			data &= 3;
			spi.select = data & 1;
			sdcard_spi_select(spi.select);
			spi.slow_clock = !!(data & 2);
			break;


	}
}


static XEMU_INLINE void increment_17bit_int ( int *p, const int inc_val )
{
	*p = (*p + inc_val) & 0x1FFFF;
}


Uint8 vera_read_cpu_register ( int reg )
{
	Uint8 data;
	reg &= 0x1F;
	switch (reg) {
		case 0x00:
			data = dataport[current_port].addr & 0xFF;
			break;
		case 0x01:
			data = (dataport[current_port].addr >> 8) & 0xFF;
			break;
		case 0x02:
			data = dataport[current_port].inc_reg_raw + ((dataport[current_port].addr >> 16) & 1);
			break;
		case 0x03:
			data = vram[dataport[0].addr];
			increment_17bit_int(&dataport[0].addr, dataport[0].inc);
			break;
		case 0x04:
			data = vram[dataport[1].addr];
			increment_17bit_int(&dataport[1].addr, dataport[1].inc);
			break;
		case 0x05:
			data = current_port + (dcsel ? 2 : 0);
			break;
		case 0x06:
			data = (ien_reg & 0x7F) | ((irqline & 512) >> 1);
			break;
		case 0x07:
			data = isr_reg & ien_reg;	// it seems from the doc, that only the 'effective' bits are set, ie if also that source is enabled
			break;
		case 0x08:
			data = irqline & 0xFF;
			break;
		case 0x09: case 0x0A: case 0x0B: case 0x0C:
			data = dc.regspace[reg - 0x09 + (!dcsel ? 0 : 4)];
			break;
		case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13:
			data = layer[0].regspace[reg - 0x0D];
			break;
		case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A:
			data = layer[1].regspace[reg - 0x14];
			break;
		case 0x1B:
		case 0x1C:
		case 0x1D:
			data = 0x00;	// PCM audio stuff, TODO!
			break;
		case 0x1E:
			data = spi.received_data;
			break;
		case 0x1F:
			data = spi.select + (spi.slow_clock ? 2 : 0);
			break;
	}
	return data;
}


int vera_dump_vram ( const char *fn )
{
	return dump_stuff(fn, vram, sizeof vram);
}



// CURRENTLY a hard coded case, for 16 colour text mode using layer-1 only (what is the default used one by the KERNAL/BASIC at least)
int vera_render_line ( void )
{
#define LAYER 1
	static Uint32 *pixel;
	static int scanline = 0;
	static int map_addr, tile_line;
	if (XEMU_UNLIKELY(scanline == 0)) {
		// first scanline, that is, beginning of a new frame, it seems.
		int tail;	// not so much used, it should be zero
		pixel = xemu_start_pixel_buffer_access(&tail);
		if (XEMU_UNLIKELY(tail))
			FATAL("Texture TAIL is not zero");
		map_addr = layer[LAYER].map_base;
		tile_line = 0;
	}
	if (XEMU_LIKELY(scanline < 480)) { // actual screen content as VGA signal, can be still border (thus 'inactive'), etc ...
		//DEBUGPRINT("Rendering scanline %d at map_addr $%05X tile_base %05X" NL, scanline, map_addr, layer[LAYER].tile_base);
		for (int x = 0; x < 80; x++) {
			int ch = vram[(layer[LAYER].tile_base + (vram[map_addr + x * 2] & 0x1FFFF) * (layer[LAYER].tile_height16 ? 16 : 8) + tile_line) & 0x1FFFF];
			int co = vram[(map_addr + x * 2 + 1) & 0x1FFFF];
			for (int c = 128; c; c >>= 1) {
				*pixel++ = palette.colours[((co >> (c & ch ? 0 : 4)) & 0xF) + layer[LAYER].palette_offset];
			}

		}
		tile_line++;
		if (tile_line == (layer[LAYER].tile_height16 ? 16 : 8)) {
			tile_line = 0;
			map_addr += layer[LAYER].map_width * 2;
		}
		//*(pixel - 1) = 0xFFFFFFFFU;	// debug purposes, show vertical line at the end of scanlines
	}
	// Manage end of full frame and vsync IRQ handling
	if (XEMU_UNLIKELY(scanline == SCANLINES_TOTAL - 1)) {
		scanline = 0;
	} else {
		scanline++;
		if (XEMU_UNLIKELY(scanline == SCANLINE_START_VSYNC))
			isr_reg |= VSYNC_IRQ;
		if (XEMU_UNLIKELY(scanline == SCANLINE_STOP_VSYNC))
			isr_reg &= ~VSYNC_IRQ;
	}
	// Check and deal with raster IRQ condition [for the next line ... so our line based emulation is more OK]
	if (XEMU_UNLIKELY(scanline == irqline))
		isr_reg |= RASTER_IRQ;
	else
		isr_reg &= ~RASTER_IRQ;
	// Then, update the IRQ status
	UPDATE_IRQ();
	return scanline;
#if 0
	//fprintf(stderr, "TILE BASE is: $%X" NL, layer[0].tile_base);
	//layer[0].tile_base = 0xF800;	// HACK! FIXME
	//DEBUGPRINT("tile base=$%X" NL, layer[0].tile_base);
	if (layer[BITMAP_LAYER].mode == 7) {
		Uint8 *v = vram + layer[BITMAP_LAYER].tile_base + map_counter;
		for (int x = 0; x < 640; x++)
			*pixel++ = palette.colours[*v++];
	} else {
		Uint8 *v = vram + layer[TEXT_LAYER].map_base + map_counter;
		for (int x = 0; x < 80; x++) {
			Uint8 chr = vram[layer[TEXT_LAYER].tile_base + ((*v++) << 3) + tile_row];
			Uint32 fg = palette.colours[*v & 0xF];
			Uint32 bg = palette.colours[(*v++) >> 4 ];
			for (int b = 128; b; b >>= 1) {
				*pixel++ = (chr & b) ? fg : bg;
			}
		}
	}
	if (isr_reg & 1) {
		//isr_reg &= ~1;
		//UPDATE_IRQ();
		CLEAR_IRQ(VSYNC_IRQ);
	}
	scanline++;
	if (layer[BITMAP_LAYER].mode == 7) {
		map_counter += 640;
		return (scanline >= 480);
	} else {
		if (tile_row == 7) {
			tile_row = 0;
	//		map_base += 256 - 80*2;
			map_counter += 256;
			return (scanline >= 480);
		} else {
			tile_row++;
	//		map_base -= 80 * 2;
			return 0;
		}
	}
	// return !scanline;
#endif
}


void vera_reset ( void )
{
	current_port = 0;
	dcsel = 0;
	dataport[0].addr = 0;
	dataport[0].inc = 0;
	dataport[1].addr = 0;
	dataport[1].inc = 0;
	irqline = 0;
	// Serial stuffs
	spi.select = 0;
	sdcard_spi_select(spi.select);
	spi.received_data = 0xFF;
	// Make sure, that I clear (maybe there was some during the reset request) + disable interrupts ...
	ien_reg = 0;
	isr_reg = 0;
	UPDATE_IRQ();
	// Populate the default palette for VERA after reset.
	palette.all = palette.all_rgb;
	dc.chroma_kill = 0;
	dc.video_mode = 1;
	dc.border = 0;
	for (int a = 0, b = 0; a < 0x100;) {
		palette.regspace[b++] = x16paldata[a  ];
		palette.regspace[b  ] = x16paldata[a++] >> 8;
		update_palette_entry(b++);
	}
#if 0
	// Clear all registers
	// This is needed, as those write functions decodes the meanings to structs
	// It's more easy,quick to solve this way, rather than to init those decoded stuffs by "hand",
	// also would result in code duplication more or less ...
	for (int a = 0; a < 0x10; a++) {
		write_layer_register(0, a, 0);
		write_layer_register(1, a, 0);
		write_composer_register(a, 0);
	}
#endif
}



// This function must be called only ONCE, before other vera... functions!
void vera_init ( void )
{
	if (!sdl_pix_fmt)
		FATAL("%s() has been called twice or sdl_pix_fmt is NULL", __func__);
	// Generate full system palette [4096 colours]
	// Yeah, it's 4096 * 4 * 2 bytes of memory (*2 because of chroma killed mono version as well), but
	// that it's still "only" 32Kbytes, nothing for a modern PC, but then I have the advantage to get SDL formatted
	// DWORD available for any colour, without any conversion work in run-time!
	palette.all = palette.all_rgb;
	for (int r = 0, index = 0; r < 0x10; r++)
		for (int g = 0; g < 0x10; g++)
			for (int b = 0; b < 0x10; b++, index++) {
				// 0xFF = alpha channel
				// "*17" => 255/15 = 17.0 Thus I can reach full brightness on 4 bit RGB component values
				palette.all_rgb[index] = SDL_MapRGBA(sdl_pix_fmt, r * 17, g * 17, b * 17, 0xFF);
				// About CHROMAKILL:
				//	this is not the right solution too much, but anyway
				//	RGB -> mono conversion would need some gamma correction and whatever black magic still ...
				//	the formula is taken from wikipedia/MSDN, BUT as I noted above, it's maybe incorrect because of the lack of gamma correction
				//	FIXME: I would need to see HDL implmenetation of VERA to be sure what it uses ...
				//int mono = ((0.2125 * r) + (0.7154 * g) + (0.0721 * b)) * 17;
				const Uint8 mono = ((0.299 * (float)r) + (0.587 * (float)g) + (0.114 * (float)b)) * 17.0;
				palette.all_mono[index] = SDL_MapRGBA(sdl_pix_fmt, mono, mono, mono, 0xFF);
			}
	// FIXME: it seems this SDL_free() makes program crashing on _exit_ .... Strange.
	//SDL_free(sdl_pix_fmt);	// I don't need this any more
	sdl_pix_fmt = NULL;
	// Clear all of vram
	memset(vram, 0, sizeof vram);
	// Reset VERA
	vera_reset();
}
