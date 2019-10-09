/* The Xemu project.
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


static struct {
	int	inc;
	int	addr;
	Uint8	r[3];
} dataport[2];
static int    current_port;
static Uint8  ien_reg, isr_reg;
static Uint32 palette[0x100];	// target texture format!
static Uint8  border_index;	// colour of border with index
static Uint32 border_colour;	// colour of border in texture format
// VRAM is actually only 128K. However this is a kind of optimization:
// We can read from "vmem" (which actually means here the entries Vera decoded address space including "gaps") with no further decoding.
// And it's much easier to handle things like "is the character set now in ROM or RAM?" since we need only offset in VMEM, no matter RAM/ROM or even setting to undecoded area, etc ...
static Uint8  vmem[0x41000];

#define CHRROMS	(vmem + 0x20000)
#define L1REGS	(vmem + 0x40000)
#define L2REGS	(vmem + 0x40010)
#define SPRREGS	(vmem + 0x40020)
#define DCREGS	(vmem + 0x40040)
#define PALMEM	(vmem + 0x40200)
#define SPRMEM	(vmem + 0x40800)

// This table is taken from Mist's X16 emulator, since I have no idea what the default palette is ...
static const Uint16 default_palette[] = {
	0x0000,0xfff,0x800,0xafe,0xc4c,0x0c5,0x00a,0xee7,0xd85,0x640,0xf77,0x333,0x777,0xaf6,0x08f,0xbbb,0x000,0x111,0x222,0x333,0x444,0x555,0x666,0x777,0x888,0x999,0xaaa,0xbbb,0xccc,0xddd,0xeee,0xfff,0x211,0x433,0x644,0x866,0xa88,0xc99,0xfbb,0x211,0x422,0x633,0x844,0xa55,0xc66,0xf77,0x200,0x411,0x611,0x822,0xa22,0xc33,0xf33,0x200,0x400,0x600,0x800,0xa00,0xc00,0xf00,0x221,0x443,0x664,0x886,0xaa8,0xcc9,0xfeb,0x211,0x432,0x653,0x874,0xa95,0xcb6,0xfd7,0x210,0x431,0x651,0x862,0xa82,0xca3,0xfc3,0x210,0x430,0x640,0x860,0xa80,0xc90,0xfb0,0x121,0x343,0x564,0x786,0x9a8,0xbc9,0xdfb,0x121,0x342,0x463,0x684,0x8a5,0x9c6,0xbf7,0x120,0x241,0x461,0x582,0x6a2,0x8c3,0x9f3,0x120,0x240,0x360,0x480,0x5a0,0x6c0,0x7f0,0x121,0x343,0x465,0x686,0x8a8,0x9ca,0xbfc,0x121,0x242,0x364,0x485,0x5a6,0x6c8,0x7f9,0x020,0x141,0x162,0x283,0x2a4,0x3c5,0x3f6,0x020,0x041,0x061,0x082,0x0a2,0x0c3,0x0f3,0x122,0x344,0x466,0x688,0x8aa,0x9cc,0xbff,0x122,0x244,0x366,0x488,0x5aa,0x6cc,0x7ff,0x022,0x144,0x166,0x288,0x2aa,0x3cc,0x3ff,0x022,0x044,0x066,0x088,0x0aa,0x0cc,0x0ff,0x112,0x334,0x456,0x668,0x88a,0x9ac,0xbcf,0x112,0x224,0x346,0x458,0x56a,0x68c,0x79f,0x002,0x114,0x126,0x238,0x24a,0x35c,0x36f,0x002,0x014,0x016,0x028,0x02a,0x03c,0x03f,0x112,0x334,0x546,0x768,0x98a,0xb9c,0xdbf,0x112,0x324,0x436,0x648,0x85a,0x96c,0xb7f,0x102,0x214,0x416,0x528,0x62a,0x83c,0x93f,0x102,0x204,0x306,0x408,0x50a,0x60c,0x70f,0x212,0x434,0x646,0x868,0xa8a,0xc9c,0xfbe,0x211,0x423,0x635,0x847,0xa59,0xc6b,0xf7d,0x201,0x413,0x615,0x826,0xa28,0xc3a,0xf3c,0x201,0x403,0x604,0x806,0xa08,0xc09,0xf0b
};



static XEMU_INLINE void CHECK_IRQ ( void )
{
	cpu65.irqLevel =
		(cpu65.irqLevel & 0xF8) |	// leave any upper bits as-is, used by other IRQ sources than VERA, in the emulation
		(ien_reg & isr_reg & 7);
}



static XEMU_INLINE void set_border ( Uint8 data )
{
	border_index = data;
	border_colour = palette[data];
}

static void update_palette_entry ( int addr )
{
	addr &= 0x1FF;
	palette[addr >> 1] = SDL_MapRGBA(
		sdl_pix_fmt,
		(PALMEM[addr |  1] & 0x0F) << 4,	// RED component
		(PALMEM[addr & ~1] & 0xF0),		// GREEN component
		(PALMEM[addr & ~1] & 0x0F) << 4,	// BLUE component
		0xFF					// alpha channel
	);
	border_colour = palette[border_index];	// to be sure border colour is updated (actually probbaly faster than with an 'if' to see if border is involved at all)
}



void vera_reset ( void )
{
	current_port = 0;
	memset(dataport, 0, sizeof dataport);
	ien_reg = 0;
	isr_reg = 0;
	CHECK_IRQ();
	border_index = 0;
	for (int a = 0, b = 0; a < 0x100;) {
		PALMEM[b++] = default_palette[a  ] & 0xFF;
		PALMEM[b  ] = default_palette[a++] >> 8;
		update_palette_entry(b++);
	}
}


void vera_init ( void )
{
	memset(vmem, 0xFF, sizeof vmem);
	vera_reset();
}



int vera_load_rom ( const char *fn )
{
	return xemu_load_file(fn, CHRROMS, 0x1000, 0x1000, "Character ROM") != 0x1000;
}




static void write_vmem_through_dataport ( int port, Uint8 data )
{
	// FIXME: what is the wrapping point on incrementation? Ie, data port can address 24 bit, but VERA may not have such an addressing space
	int addr = dataport[port].addr;
	dataport[port].addr = (addr + dataport[port].inc) & 0xFFFFF;
	// OK, let's see, what is about to be written ...
	DEBUGPRINT("VERA: writing VMEM at $%05X with data $%02X" NL, addr, data);
	if (XEMU_LIKELY(addr < 0x20000)) {
		vmem[addr] = data;
		return;
	}
	if (XEMU_UNLIKELY(addr < 0x40000 || addr > 0x40FFF))
		return;	// for write, there is nothing between $20000 - $3FFFF, as the char ROM is not writable, neither the "gap". Also ignore if the write op refeers a too big, undecoded address
	// FIXME: it still allows to write later "gaps" which is probably not the case with the "real" thing?
	vmem[addr] = data;
	if (addr >= 0x40200 && addr < 0x40400)
		update_palette_entry(data);	// writing palette registers: as addr is masked in the update function, we don't need to do here
}


static XEMU_INLINE Uint8 read_vmem_through_dataport ( int port )
{
	// FIXME: what is the wrapping point on incrementation? Ie, data port can address 24 bit, but VERA may not have such an addressing space
	int addr = dataport[port].addr;
	dataport[port].addr = (addr + dataport[port].inc) & 0xFFFFF;
	return XEMU_LIKELY(addr < 0x41000) ? vmem[addr] : 0xFF;
}


void vera_write_reg_by_cpu ( int reg, Uint8 data )
{
	DEBUGPRINT("VERA: writing register %d with data $%02X" NL, reg & 7, data);
	switch (reg & 7) {
		case 0:
			dataport[current_port].r[0] = data;
			dataport[current_port].inc  = data >> 4;
			dataport[current_port].addr = (dataport[current_port].addr & 0x0FFFF) | ((data & 0xF) << 16);
			break;
		case 1:
			dataport[current_port].r[1] = data;
			dataport[current_port].addr = (dataport[current_port].addr & 0xF00FF) | (data << 8);
			break;
		case 2:
			dataport[current_port].r[2] = data;
			dataport[current_port].addr = (dataport[current_port].addr & 0xFFF00) | data;
			break;
		case 3:
			write_vmem_through_dataport(0, data);
			break;
		case 4:
			write_vmem_through_dataport(1, data);
			break;
		case 5:
			if (XEMU_UNLIKELY(data & 0x80))
				vera_reset();
			else
				current_port = data & 1;
			break;
		case 6:
			ien_reg = data & 7;
			CHECK_IRQ();
			break;
		case 7:
			isr_reg &= (~data) & 7;
			CHECK_IRQ();
			break;
	}
}


Uint8 vera_read_reg_by_cpu ( int reg )
{
	switch (reg & 7) {
		case 0:
			return dataport[current_port].r[0];
		case 1:
			return dataport[current_port].r[1];
		case 2:
			return dataport[current_port].r[2];
		case 3:
			return read_vmem_through_dataport(0);
		case 4:
			return read_vmem_through_dataport(1);
		case 5:
			return current_port;
		case 6:
			return ien_reg;
		case 7:
			return isr_reg & ien_reg;	// it seems from the doc, that only the 'effective' bits are set, ie if also that source is enabled
	}
	return 0xFF;	// some C compilers cannot figure out, that all possible cases ARE handled above, and gives a warning without this ... :(
}


static Uint32 *pixel;
static int scanline;
static int map_base, tile_base;
static int tile_row;

static int hacky_shit = 0;


static void hacky_shitter ( void )
{
	FILE *f = fopen("vmem.dump", "w");
	if (f) {
		fwrite(vmem, sizeof vmem, 1, f);
		fclose(f);
	}
}


void vera_vsync ( void )
{
	int tail;
	pixel = xemu_start_pixel_buffer_access(&tail);
	scanline = 0;
	map_base = (L1REGS[2] << 2) | (L1REGS[3] << 10);
	tile_base = (L1REGS[4] << 2) | (L1REGS[5] << 10);
	tile_row = 0;
	fprintf(stderr, "L1: EN=%d MODE=%d MAP_BASE=%d TILE_BASE=%d TILEH=%d TILEW=%d MAPH=%d MAPW=%d "
			"HSTOP=%d HSTART=%d VSTOP=%d VSTART=%d OUT_MODE=%d "
			"IEN=%d\n",
			L1REGS[0] & 1, L1REGS[0] >> 5, map_base, tile_base,
			(L1REGS[1] >> 5) & 1,	// TILEH
			(L1REGS[1] >> 4) & 1,	// TILEW
			(L1REGS[1] >> 2) & 3,	// MAPH
			L1REGS[1] & 3,		// MAPW
			DCREGS[5] | (((DCREGS[8] >> 2) & 3) << 8), // HSTOP
			DCREGS[4] | ((DCREGS[8] & 3) << 8), // HSTART
			DCREGS[7] | (((DCREGS[8] >> 5) & 1) << 8), // VSTOP
			DCREGS[6] | (((DCREGS[8] >> 4) & 1) << 8),  // VSTART
			DCREGS[0] & 3,
			ien_reg
	);
	if (!hacky_shit) {
		hacky_shit = 1;
		atexit(hacky_shitter);
	}
	isr_reg |= 1;
	CHECK_IRQ();
}


int vera_render_line ( void )
{
	for (int x = 0; x < 80; x++) {
		Uint8 chr = vmem[map_base++];
		Uint32 fg = palette[vmem[map_base  ] & 0xF];
		Uint32 bg = palette[vmem[map_base++] >> 4 ];
		//fg = 0xFFFFFFFFU;
		//bg = 0x88888888U;
		chr = vmem[tile_base + (chr << 3) + tile_row];
		// HACK
		//chr = CHRROMS[8 + tile_row];
		//chr = 0x55;
		//chr = hackrom[8 + tile_row];
		for (int b = 128; b; b >>= 1) {
			*pixel++ = (chr & b) ? fg : bg;
		}
	}
	if (isr_reg & 1) {
		isr_reg &= ~1;
		CHECK_IRQ();
	}
	scanline++;
	if (tile_row == 7) {
		tile_row = 0;
		map_base += 256 - 80*2;
		return (scanline >= 480);
	} else {
		tile_row++;
		map_base -= 80 * 2;
		return 0;
	}
}
