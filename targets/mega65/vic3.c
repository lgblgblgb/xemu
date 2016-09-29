/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the VIC3 "emulation". Currently it does one-frame-at-once
   kind of horrible work, and only a subset of VIC2 and VIC3 knowledge
   is implemented. Some of the missing features: hardware attributes,
   DAT, sprites, screen positioning, H1280 mode, V400 mode, interlace,
   chroma killer, VIC2 MCM, ECM, 38/24 columns mode, border.

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


#include "emutools.h"
#include "mega65.h"
#include "cpu65c02.h"
#include "vic3.h"
#include "emutools.h"

#define RGB(r,g,b) rgb_palette[((r) << 8) | ((g) << 4) | (b)]

static const char *iomode_names[4] = { "VIC2", "VIC3", "BAD!", "VIC4" };

static Uint32 rgb_palette[4096];	// all the C65 palette, 4096 colours (SDL pixel format related form)
static Uint32 vic3_palette[0x100];	// VIC3 palette in SDL pixel format related form (can be written into the texture directly to be rendered)
static Uint32 vic3_rom_palette[0x100];	// the "ROM" palette, for C64 colours (with some ticks, ie colours above 15 are the same as the "normal" programmable palette)
static Uint32 *palette;			// the selected palette ...
static Uint8 vic3_palette_nibbles[0x300];
Uint8 vic3_registers[0x80];		// VIC-3 registers. It seems $47 is the last register. But to allow address the full VIC3 reg I/O space, we use $80 here
int vic_iomode;				// VIC2/VIC3/VIC4 mode
int scanline;				// current scan line number
int clock_divider7_hack;
static int compare_raster;		// raster compare (9 bits width) data
static int interrupt_status;		// Interrupt status of VIC
int vic2_16k_bank;			// VIC-2 modes' 16K BANK address within 64K (NOT the traditional naming of banks with 0,1,2,3)
static Uint8 *sprite_pointers;		// Pointer to sprite pointers :)
static Uint8 *sprite_bank;
int vic3_blink_phase;			// blinking attribute helper, state.
static Uint8 raster_colours[512];

static int warn_sprites = 0, warn_ctrl_b_lo = 1;


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






void vic3_init ( void )
{
	int r, g, b, i;
	// *** Init 4096 element palette with RGB components for faster access later on palette register changes (avoid SDL calls to convert)
	for (r = 0, i = 0; r < 16; r++)
		for (g = 0; g < 16; g++)
			for (b = 0; b < 16; b++)
				rgb_palette[i++] = SDL_MapRGBA(sdl_pix_fmt, r * 17, g * 17, b * 17, 0xFF); // 15*17=255, last arg 0xFF: alpha channel for SDL
	SDL_FreeFormat(sdl_pix_fmt);	// thanks, we don't need this anymore from SDL
	// *** Init VIC3 registers and palette
	vic2_16k_bank = 0;
	vic_iomode = VIC2_IOMODE;
	interrupt_status = 0;
	palette = vic3_rom_palette;
	scanline = 0;
	compare_raster = 0;
	clock_divider7_hack = 7;
	for (i = 0; i < 0x100; i++) {	// Initiailize all palette registers to zero, initially, to have something ...
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
	DEBUG("VIC3: has been initialized." NL);
}



static void vic3_interrupt_checker ( void )
{
	int vic_irq_old = cpu_irqLevel & 2;
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
			cpu_irqLevel |= 2;
		else
			cpu_irqLevel &= ~2;
	}
}



void vic3_check_raster_interrupt ( void )
{
	raster_colours[scanline] = vic3_registers[0x21];	// ugly hack to make some kind of raster-bars visible :-/
	// I'm lame even with VIC2 knowledge it seems
	// C65 seems to use raster interrupt to generate the usual periodic IRQ
	// (which was done with CIA on C64) in raster line 511. However as
	// raster line 511 can never be true, I really don't know what to do.
	// To be able C65 ROM to work, I assume that raster 511 is raster 0.
	// It's possible that this is an NTSC/PAL issue, as raster can be "negative"
	// according to the specification in case of NTSC. I really don't know ...
	if (
		(scanline == compare_raster)
	//	|| ((compare_raster == 511 || compare_raster == 319) && scanline == 0)
	) {
		interrupt_status |= 1;
	} else
		interrupt_status &= 0xFE;
	//interrupt_status &= vic3_registers[0x1A];
	vic3_interrupt_checker();
}



void vic3_write_reg ( int addr, Uint8 data )
{
	Uint8 old_data;
	addr &= vic_iomode ? 0x7F : 0x3F;
	old_data = vic3_registers[addr];
	DEBUG("VIC3: write reg $%02X with data $%02X" NL, addr, data);
	if (addr == 0x2F) {
		int vic_new_iomode;
		if (data == 0x96 && old_data == 0xA5)
			vic_new_iomode = VIC3_IOMODE;
		else if (data == 0x53 && old_data == 0x47 && mega65_capable)
			vic_new_iomode = VIC4_IOMODE;
		else
			vic_new_iomode = VIC2_IOMODE;
		if (vic_new_iomode != vic_iomode) {
			DEBUG("VIC: changing I/O mode %d(%s) -> %d(%s)" NL, vic_iomode, iomode_names[vic_iomode], vic_new_iomode, iomode_names[vic_new_iomode]);
			vic_iomode = vic_new_iomode;
		}
	}
	if (vic_iomode == VIC2_IOMODE && addr > 0x2F) {
		DEBUG("VIC3: ignoring writing register $%02X (with data $%02X) because of old I/O access mode selected" NL, addr, data);
		return;
	}
	if (vic_iomode != VIC4_IOMODE && addr > 0x3D) {
		DEBUG("VIC4: ignoring writing register $%02X (with data $%02X) because of non-Mega65 I/O access mode selected" NL, addr, data);
		return;
	}
	if (addr > 0x3D)
		DEBUG("VIC4: writing VIC4 register $%02X in Mega65 I/O mode (data=$%02X)" NL, addr, data);
	vic3_registers[addr] = data;
	switch (addr) {
		case 0x11:
			compare_raster = (compare_raster & 0xFF) | ((data & 128) ? 0x100 : 0);
			DEBUG("VIC3: compare raster is now %d" NL, compare_raster);
			break;
		case 0x12:
			compare_raster = (compare_raster & 0xFF00) | data;
			DEBUG("VIC3: compare raster is now %d" NL, compare_raster);
			break;
		case 0x19:
			interrupt_status = interrupt_status & (~data) & 15;
			vic3_interrupt_checker();
			break;
		case 0x1A:
			vic3_registers[0x1A] &= 15;
			break;
		case 0x21:	// TODO: it seems we have to do about the same with various registers later, FIXME not only this one ... FIXME #2: on reading though high bits should be '1' with C64 I/O mode
			if (!vic_iomode)
				vic3_registers[0x21] &= 15;
			break;
		case 0x30:
			// Save some un-needed memory translating table rebuilds, if there is no important bits (of us) changed.
			// CRAM@DC00 is not handled by the translator directly, so bit0 does not apply here!
			if (
				(data & 0xF8) != (old_data & 0xF8)
			) {
				DEBUG("MEM: applying new memory configuration because of VIC3 $30 is written" NL);
				apply_memory_config();
			} else
				DEBUG("MEM: no need for new memory configuration (because of VIC3 $30 is written): same ROM bit values are set" NL);
			palette = (data & 4) ? vic3_palette : vic3_rom_palette;
			break;
		case 0x31:
			clock_divider7_hack = (data & 64) ? 7 : 2;
			DEBUG("VIC3: clock_divider7_hack = %d" NL, clock_divider7_hack);
			if ((data & 15) && warn_ctrl_b_lo) {
				INFO_WINDOW("VIC3 control-B register V400, H1280, MONO and INT features are not emulated yet!");
				warn_ctrl_b_lo = 0;
			}
			break;
#if 0
		case 0x15:
			if (data && warn_sprites) {
				INFO_WINDOW("VIC2 sprites are not emulated yet! [enabled: $%02X]", data);
				warn_sprites = 0;
			}
			break;
#endif
	}
}	




Uint8 vic3_read_reg ( int addr )
{
	Uint8 result;
	addr &= vic_iomode ? 0x7F : 0x3F;
	if (vic_iomode == VIC2_IOMODE && addr > 0x2F) {
		DEBUG("VIC3: ignoring reading register $%02X because of old I/O access mode selected, answer is $FF" NL, addr);
		return 0xFF;
	}
	switch (addr) {
		case 0x11:
			result =  (vic3_registers[0x11] & 0x7F) | ((scanline & 256) ? 0x80 : 0);
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



static inline Uint8 *vic2_get_chargen_pointer ( void )
{
	int offs = (vic3_registers[0x18] & 14) << 10;	// character generator address address within the current VIC2 bank
	//int crom = vic3_registers[0x30] & 64;
	//DEBUG("VIC2: chargen: BANK=%04X OFS=%04X CROM=%d" NL, vic2_16k_bank, offs, crom);
	if ((vic2_16k_bank == 0x0000 || vic2_16k_bank == 0x8000) && (offs == 0x1000 || offs == 0x1800)) {  // check if chargen info is in ROM
		// In case of Mega65, fetching char-info from ROM means to access the "WOM"
		// FIXME: what should I do with bit 6 of VIC-III register $30 ["CROM"] ?!
		return character_rom + offs - 0x1000;
	} else
		return memory + vic2_16k_bank + offs;
}


//#define BG_FOR_Y(y) vic3_registers[0x21]
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
	int inc_p = (vic3_registers[0x54] & 1) ? 2 : 1;	// VIC-IV (Mega-65) 16 bit text mode?
	int scanline = 0;
	if (vic3_registers[0x31] & 128) { // check H640 bit: 80 column mode?
		xlim = 79;
		ylim = 24;
		// Note: VIC2 sees ROM at some addresses thing is not emulated yet for other thing than chargen memory!
		// Note: according to the specification bit 4 has no effect in 80 columns mode!
		vidp = memory + ((vic3_registers[0x18] & 0xE0) << 6) + vic2_16k_bank;
		sprite_pointers = vidp + 2040;
	} else {
		xlim = 39;
		ylim = 24;
		// Note: VIC2 sees ROM at some addresses thing is not emulated yet for other thing than chargen memory!
		vidp = memory + ((vic3_registers[0x18] & 0xF0) << 6) + vic2_16k_bank;
		sprite_pointers = vidp + 1016;
	}
	// Target SDL pixel related format for the background colour
	bg = palette[BG_FOR_Y(0)];
	PIXEL_POINTER_CHECK_INIT(p, tail, "vic2_render_screen_text");
	for (;;) {
		Uint8 coldata = *colp;
		Uint32 fg;
		if (
			inc_p == 2 && (		// D054 bit 0 controlled stuff (16bit mode)
			(vidp[1] == 0 && (vic3_registers[0x54] & 2)) ||	// enabled for =<$FF chars
			(vidp[1] && (vic3_registers[0x54] & 4))		// enabled for >$FF chars
		)) {
			if (vidp[0] == 0xFF && vidp[1] == 0xFF) {
				// end of line marker, let's use background to fill the rest of the line ...
				// FIXME: however in the current situation we can't do that since of the "fixed" line length for 80 or 40 chars ... :(
				p += xlim == 39 ? 16 : 8;	// so we just ignore ... FIXME !!
			} else {
				int a;
				Uint8 *cp = memory + (((vidp[0] << 6) + (charline << 3) + (vidp[1] << 14)) & 0x1ffff); // and-mask: wrap-around in 128K of chip-RAM
				for (a = 0; a < 8; a++) {
					if (xlim != 79)
						*(p++) = palette[*cp];
					*(p++) = palette[*(cp++)];
				}
			}
		} else {
			Uint8 chrdata = chrg[(*vidp << 3) + charline];
			if (vic3_registers[0x31] & 32) { 	// ATTR bit mode
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
	vidp = memory + ((vic3_registers[0x18] & 0xF0) << 6) + vic2_16k_bank;
	sprite_pointers = vidp + 1016;
	chrp = memory + ((vic3_registers[0x18] & 8) ? 8192 : 0) + vic2_16k_bank;
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
	int xlim, x = 0, y = 0, h640 = (vic3_registers[0x31] & 128);
	Uint8 bpe, *bp[8];
	bp[0] = memory + ((vic3_registers[0x33] & (h640 ? 12 : 14)) << 12);
	bp[1] = memory + ((vic3_registers[0x34] & (h640 ? 12 : 14)) << 12) + 0x10000;
	bp[2] = memory + ((vic3_registers[0x35] & (h640 ? 12 : 14)) << 12);
	bp[3] = memory + ((vic3_registers[0x36] & (h640 ? 12 : 14)) << 12) + 0x10000;
	bp[4] = memory + ((vic3_registers[0x37] & (h640 ? 12 : 14)) << 12);
	bp[5] = memory + ((vic3_registers[0x38] & (h640 ? 12 : 14)) << 12) + 0x10000;
	bp[6] = memory + ((vic3_registers[0x39] & (h640 ? 12 : 14)) << 12);
	bp[7] = memory + ((vic3_registers[0x3A] & (h640 ? 12 : 14)) << 12) + 0x10000;
	bpe = vic3_registers[0x32];	// bit planes enabled mask
	if (h640) {
		bpe &= 15;		// it seems, with H640, only 4 bitplanes can be used (on lower 4 ones)
		xlim = 79;
		sprite_pointers = bp[2] + 0x3FF8;	// FIXME: just guessing
	} else {
		xlim = 39;
		sprite_pointers = bp[2] + 0x1FF8;	// FIXME: just guessing
	}
        DEBUG("VIC3: bitplanes: enable_mask=$%02X comp_mask=$%02X H640=%d" NL,
		bpe, vic3_registers[0x3B], h640 ? 1 : 0
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
			) & bpe) ^ vic3_registers[0x3B]
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
	int sprite_y = vic3_registers[sprite_no * 2 + 1] - SPRITE_Y_START_SCREEN;
	int sprite_x = ((vic3_registers[sprite_no * 2] | ((vic3_registers[16] & sprite_mask) ? 0x100 : 0)) - SPRITE_X_START_SCREEN) * 2;
	Uint32 colour = palette[vic3_registers[39 + sprite_no] & 15];
	int expand_x = vic3_registers[29] & sprite_mask;
	int expand_y = vic3_registers[23] & sprite_mask;
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




/* This is the one-frame-at-once (highly incorrect implementation, that is)
   renderer. It will call legacy VIC2 text mode render (optionally with
   80 columns mode, though, ECM, MCM, hardware attributes are not supported),
   VIC2 legacy HIRES mode (MCM is not supported), or bitplane modes (V400,
   H1280, odd scanning/interlace is not supported). Sprites, screen positioning,
   etc is not supported */
void vic3_render_screen ( void )
{
	int tail_sdl;
	Uint32 *p_sdl = emu_start_pixel_buffer_access(&tail_sdl);
	int sprites = vic3_registers[0x15];
	if (vic3_registers[0x31] & 16) {
	        sprite_bank = memory + ((vic3_registers[0x35] & 12) << 12);	// FIXME: just guessing: sprite bank is bitplane 2 area, always 16K regardless of H640?
		vic3_render_screen_bpm(p_sdl, tail_sdl);
	} else {
		sprite_bank = vic2_16k_bank + memory;				// VIC2 legacy modes uses the VIC2 bank for sure, as the sprite bank too
		if (vic3_registers[0x11] & 32)
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
	emu_update_screen();
}

