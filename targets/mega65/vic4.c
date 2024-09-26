/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2024 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Copyright (C)2020-2022 Hernán Di Pietro <hernan.di.pietro@gmail.com>

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
#include "vic4_palette.h"
#include "memory_mapper.h"
#include "hypervisor.h"
#include "configdb.h"
#include "xemu/f011_core.h"
#include "xemu/emutools_files.h"
#include "xemu/basic_text.h"
#include "io_mapper.h"


#define SPRITE_SPRITE_COLLISION
#define SPRITE_FG_COLLISION
#define SPRITE_COORD_LATCHING


const char *iomode_names[4] = { "VIC2", "VIC3", "VIC4ETH", "VIC4" };
const Uint8 iomode_hexdigitids[4] = { 2, 3, 0xE, 4 };	// identifier of IO modes uses with %X (hex digit print format), will result "E" when IO mode is VIC4-ETH, "E" meaning "Ethernet"

// (SDL) target texture rendering pointers
static Uint32 *current_pixel;					// current_pixel pointer to the rendering target (one current_pixel: 32 bit)
static Uint32 *pixel_start;					// points to the end and start of the buffer
static Uint32 *pixel_raster_start;				// first pixel of current raster
Uint8 vic_registers[0x80];					// VIC4 registers
static int compare_raster;					// raster compare (9 bits width) data
static int logical_raster = 0;
static int interrupt_status;					// Interrupt status of VIC
static int blink_phase = 0;					// blinking attribute helper, state.
Uint8 c128_d030_reg;						// C128-like register can be only accessed in VIC2 mode but not in others, quite special!
static Uint8 reg_d018_screen_addr = 0;				// Legacy VIC2 $D018 screen address register
static int vic_hotreg_touched = 0;				// If any "legacy" registers were touched
static int vic4_sideborder_touched = 0;				// If side-border register were touched
static int border_x_left= 0;			 		// Side border left
static int border_x_right= 0;			 		// Side border right
static int xcounter = 0, ycounter = 0;				// video counters
static int char_row = 0, display_row = 0;
static Uint8 draw_mask;						// Normally $FF, if RRB asks for specific ROW MASK, draw_mask will be set to FF/00 for the right char_row according to the ROW MASK
// FIXME: really, it's 2048 now, since in H320, GOTOX value is multiplied with 2 and may overflow this array even if it's not so much used this way, we want avoid crash ...
// FIXME: should be rethought!!!!
static Uint8 is_fg[2048];					// this cache helps in sprite rendering, zero means background state, other value: foreground
#ifdef SPRITE_SPRITE_COLLISION
static Uint8 is_sprite[1024];
#endif
static float char_x_step = 0.0;
static int enable_bg_paint = 1;
//static int display_row_count = 0;
#define display_row_count vic_registers[0x7B]
static int max_rasters = PHYSICAL_RASTERS_DEFAULT;
static int visible_area_height = SCREEN_HEIGHT_VISIBLE_DEFAULT;
static int vicii_first_raster = 7;				// Default for NTSC
static Uint8 *bitplane_bank_p = main_ram;
static Uint8 *bitplane_p[8];
static Uint32 red_colour, black_colour;				// used by "drive LED", and cross-hair (only the red) for debug pixel read
static Uint8 vic_pixel_readback_result[4];
static Uint8 vic_color_register_mask;
static Uint32 *used_palette;					// normally the same value as "palette" from vic4_palette.c but GOTOX RRB token can modify this! So this should be used
static int EFFECTIVE_V400;
#ifdef SPRITE_COORD_LATCHING
static Uint8 sprite_is_being_rendered[8];
#warning "Sprite coordinate latching is an experimental feature (SPRITE_COORD_LATCHING is defined)!"
#endif
static Uint8 bug_compat_vic_iii_d016_delta = 2;
static bool  bug_compat_char_attr = true;

// --- these things are altered by vic4_open_frame_access() ONLY at every fame ONLY based on PAL or NTSC selection
Uint8 videostd_id = 0xFF;			// 0=PAL, 1=NTSC [give some insane value by default to force the change at the fist frame after starting Xemu]
const char *videostd_name = "<UNDEF>";		// PAL or NTSC, however initially is not yet set
int videostd_frametime = NTSC_FRAME_TIME;	// time in microseconds for a frame to produce
float videostd_1mhz_cycles_per_scanline = 32.0;	// have some value to jumpstart emulation, it will be overriden sooner or later XXX FIXME: why it does not work with zero value when it's overriden anyway?!
int videostd_changed = 0;
static const char NTSC_STD_NAME[] = "NTSC";
static const char PAL_STD_NAME[] = "PAL";
int vic_readjust_sdl_viewport = 0;
int vic4_disallow_videostd_change = 0;		// Disallows programs to change video std via register D06F, bit 7 (emulator internally writing that bit still can change video std though!)
int vic4_registered_screenshot_request = 0;
unsigned int vic_frame_counter, vic_frame_counter_since_boot;


// VIC4 Modeline Parameters
// ----------------------------------------------------
#define DISPLAY_HEIGHT			((max_rasters-1)-20)
#define TEXT_HEIGHT_200			400
#define TEXT_HEIGHT_400			400
#define CHARGEN_Y_SCALE_200		2
#define CHARGEN_Y_SCALE_400		1
#define chargen_y_pixels 		0
#define TOP_BORDERS_HEIGHT_200		(DISPLAY_HEIGHT - TEXT_HEIGHT_200)
#define TOP_BORDERS_HEIGHT_400		(DISPLAY_HEIGHT - TEXT_HEIGHT_400)
#define SINGLE_TOP_BORDER_200		(TOP_BORDERS_HEIGHT_200 >> 1)
#define SINGLE_TOP_BORDER_400		(TOP_BORDERS_HEIGHT_400 >> 1)
// TODO: move as many things as possible from vic4.h to here which is only used by vic4.c, to avoid confusion ...


static const Uint8 reverse_byte_table[] = {
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,	//0
	0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,	//8
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,	//16
	0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,	//24
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,	//32
	0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,	//40
	0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,	//48
	0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,	//56
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,	//64
	0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,	//72
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,	//80
	0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,	//88
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,	//96
	0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,	//104
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,	//112
	0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,	//120
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,	//128
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


void vic_reset ( void )
{
	vic_frame_counter = 0;
	vic_frame_counter_since_boot = 0;
	memory_set_io_mode(VIC2_IOMODE);
	vic_color_register_mask = 0x0F;
	interrupt_status = 0;
	compare_raster = 0;
	vic_hotreg_touched = 0;
	vic4_sideborder_touched = 0;
	vic_registers[0x5D] |= 0x80;	// set hotregs by default
	// *** Just a check to try all possible regs (in VIC2,VIC3 and VIC4 modes), it should not panic ...
	// It may also sets/initializes some internal variables sets by register writes, which would cause a crash on screen rendering without prior setup!
	for (int i = 0; i < 0x140; i++) {
		vic_write_reg(i, 0);
		(void)vic_read_reg(i);
	}
	vic_registers[0x5D] |= 0x80;	// set hotregs by default (again)
	// to deactivate the pixel readback crosshair by default, ie X/Y pos that never meet
	vic_registers[0x7D] = 0xFF;
	vic_registers[0x7E] = 0xFF;
	vic_registers[0x7F] = 0xFF;
	// turn off possible remained sprite collision info
	vic_registers[0x1E] = 0;
	vic_registers[0x1F] = 0;
}


static inline void vic4_reset_display_counters ( void )
{
	xcounter = 0;
	display_row = 0;
	char_row = 0;
	ycounter = 0;
}


void vic_init ( void )
{
	vic_pixel_readback_result[0] = 0xFF;	// "hyperram access count" or what, not so much emulated
	// Needed to render "drive LED" feature + debug pixel-read back cross-hair (only the red colour)
	red_colour   = SDL_MapRGBA(sdl_pix_fmt, 0xFF, 0x00, 0x00, 0xFF);
	black_colour = SDL_MapRGBA(sdl_pix_fmt, 0x00, 0x00, 0x00, 0xFF);
	// Init VIC4 stuffs
	vic4_init_palette();
	vic_reset();
	c128_d030_reg = 0;	// make sure to set it to zero, FIXME: should we move this into vic_reset() which is also called from vic_init() but then would function only calling reset as well?!
	machine_set_speed(0);
	vic4_reset_display_counters();
	DEBUG("VIC4: has been initialized." NL);
}


// Debug pixel-read back feature of MEGA65
static XEMU_INLINE void pixel_readback ( void )
{
	// FIXME: this is surely wrong, and we should not use texture coords directly. We must fix this somehow (offsets?)
	const int pix_readback_x = (vic_registers[0x7D] | ((vic_registers[0x7F] & 0x0F) << 8)) - 8 + 2;
	const int pix_readback_y = vic_registers[0x7E] | ((vic_registers[0x7F] & 0xF0) << 4);
	if (XEMU_UNLIKELY(pix_readback_y >= 0 && pix_readback_x >= 0 && pix_readback_y < max_rasters && pix_readback_x < TEXTURE_WIDTH)) {
		const Uint32 texpixcol = xemu_frame_pixel_access_p[TEXTURE_WIDTH * pix_readback_y + pix_readback_x];
		// the array will be used on reading $D70D indexed by the top two bits of $D07C, element "0" is "hyperram access count" and not handled here
		// FIXME Warning: this code assumes that R/G/B SDL components are exactly 8 bit
		vic_pixel_readback_result[1] = (texpixcol >> sdl_pix_fmt->Rshift) & 0xFF;	// red channel
		vic_pixel_readback_result[2] = (texpixcol >> sdl_pix_fmt->Gshift) & 0xFF;	// green channel
		vic_pixel_readback_result[3] = (texpixcol >> sdl_pix_fmt->Bshift) & 0xFF;	// blue channel
		// draw red coloured cross-hair
		Uint32 *p = xemu_frame_pixel_access_p + TEXTURE_WIDTH * pix_readback_y;
		for (int a = 0; a < TEXTURE_WIDTH; a++)
			*p++ = red_colour;
		p = xemu_frame_pixel_access_p + pix_readback_x;
		for (int a = 0; a < max_rasters; a++) {
			*p = red_colour;
			p+= TEXTURE_WIDTH;
		}
	}
}


// Pair of vic4_open_frame_access() and the place when screen is updated at SDL level, finally.
// Do NOT call this function from vic4.c! It must be used by the emulator's main loop!
void vic4_close_frame_access ( void )
{
	DEBUG("FRAME CLOSED" NL);
#ifdef	SPRITE_COORD_LATCHING
	// To avoid the problem when sprite rendering is not finished (at the very bottom of the screen, does not "fit"),
	// thus the "end" condition in active rendering is never reached: that would be remain latched for the next frame then!
	memset(sprite_is_being_rendered, 0, sizeof sprite_is_being_rendered);
#endif
	// Debug pixel-read back feature of MEGA65
	pixel_readback();
#ifdef	XEMU_FILES_SCREENSHOT_SUPPORT
	// Screenshot
	if (XEMU_UNLIKELY(vic4_registered_screenshot_request)) {
		unsigned int x1, y1, x2, y2;
		xemu_get_viewport(&x1, &y1, &x2, &y2);
		vic4_registered_screenshot_request = 0;
		if (!xemu_screenshot_png(
			NULL, configdb.screenshot_and_exit,
			1, 1,		// no ratio/zoom correction is applied
			pixel_start + y1 * TEXTURE_WIDTH + x1,	// pixel pointer corresponding to the top left corner of the viewport
			x2 - x1 + 1,	// width
			y2 - y1 + 1,	// height
			TEXTURE_WIDTH	// full width (ie, width of the texture)
		)) {
			const char *p = strrchr(xemu_screenshot_full_path, DIRSEP_CHR);
			if (p)
				OSD(-1, -1, "%s", p + 1);
		}
		if (configdb.screenshot_and_exit) {
			DEBUGPRINT("VIC4: exiting on 'exit-on-screenshot' feature." NL);
			XEMUEXIT(0);
		}
	}
#endif
	// Render "drive LED" if it was requested at all
	if (configdb.show_drive_led && fdc_get_led_state(16)) {
		unsigned int x_origin, y_origin;	// top right corner of the viewport
		xemu_get_viewport(NULL, &y_origin, &x_origin, NULL);
		for (unsigned int y = 0; y < 8; y++)
			for (unsigned int x = 0; x < 8; x++)
				*(pixel_start + x_origin - 10 + x + (y + 2 + y_origin) * (TEXTURE_WIDTH)) = (x > 1 && x < 7 && y > 1 && y < 7) ? red_colour : black_colour;
	}
	// FINALLY ....
	xemu_update_screen();
	vic_frame_counter++;
	vic_frame_counter_since_boot++;
}

// The hardware allows a sideborder value of 16383 as a remnant of old MEGA65 design.
// In practical terms, any sideborder exceeding display_width / 2 will cover the entire
// character generator (effective 400 since since dw is fixed to 800px wide). Since our
// scanline renderer takes borders into account and any bizarre value will crash emulator
// due to offlimits pixel buffer access, we clamp the maximum practical sideborder value
// to TEXTURE_WIDTH/2, even when program code can set any of the 13-bit value range through
// the $D05C/$D05D registers.
static inline unsigned int vic4_single_side_border_clamped( void )
{
	return (SINGLE_SIDE_BORDER > (TEXTURE_WIDTH / 2)) ? (TEXTURE_WIDTH / 2) : SINGLE_SIDE_BORDER;
}


static void vic4_update_sideborder_dimensions ( void )
{
	if (REG_CSEL) {	// 40-columns?
		border_x_left = FRAME_H_FRONT + vic4_single_side_border_clamped();
		if (!REG_H640)
			border_x_right = FRAME_H_FRONT + TEXTURE_WIDTH - vic4_single_side_border_clamped() - 1;
		else	// 80-col mode
			border_x_right = FRAME_H_FRONT + TEXTURE_WIDTH - vic4_single_side_border_clamped();
	} else {	// 38-columns
		border_x_right = FRAME_H_FRONT + TEXTURE_WIDTH - vic4_single_side_border_clamped() - 18;
		if (!REG_H640)
			border_x_left = FRAME_H_FRONT + vic4_single_side_border_clamped() + 14;
		else	// 78-col mode
			border_x_left = FRAME_H_FRONT + vic4_single_side_border_clamped() + 15;
	}
	DEBUG("VIC4: set border left=%d, right=%d, textxpos=%d" NL, border_x_left, border_x_right, CHARGEN_X_START);
}


static void vic4_update_vertical_borders ( void )
{
	// FIXME: it seems we need this line here! Otherwise EFFECTIVE_V400 may not reflect what
	// it should be, if just updated in vic4_open_frame_access(). This seems to fix the OpenROMs
	// issue that the bottom half of the screen is invisible, since the wrong condition below
	// is taken for setting display_row_count based on V400 from EFFECTIVE_V400 ...
	EFFECTIVE_V400 = (REG_V400 && REG_CHRYSCL == 0 && (vic_registers[0x51] & 0x40)) ? 0 : !!REG_V400;
	if (REG_CSEL) {	// 40-columns?
		if (!REG_H640)
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL));
		else	// 80-col mode
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL) - bug_compat_vic_iii_d016_delta);
	} else {	// 38-columns
		if (!REG_H640)
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL));
		else	// 78-col mode
			SET_CHARGEN_X_START(FRAME_H_FRONT + SINGLE_SIDE_BORDER + (2 * REG_VIC2_XSCROLL) - bug_compat_vic_iii_d016_delta);
	}
	if (!EFFECTIVE_V400) {	// Standard mode (200-lines)
		display_row_count = 24;
		if (REG_RSEL) {	// 25-row
			SET_BORDER_Y_TOP(RASTER_CORRECTION + SINGLE_TOP_BORDER_200 - (2 * vicii_first_raster));
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + DISPLAY_HEIGHT - SINGLE_TOP_BORDER_200 - (2 * vicii_first_raster) - 1);
		} else {
			SET_BORDER_Y_TOP(RASTER_CORRECTION + SINGLE_TOP_BORDER_200 - (2 * vicii_first_raster) + 8);
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + DISPLAY_HEIGHT - (2 * vicii_first_raster) - SINGLE_TOP_BORDER_200 - 7);
		}
		SET_CHARGEN_Y_START(RASTER_CORRECTION + SINGLE_TOP_BORDER_200 - (2 * vicii_first_raster) - 6 + REG_VIC2_YSCROLL * 2);
	} else {		// V400
		display_row_count = 49;
		if (REG_RSEL) {	// 25-line+V400
			SET_BORDER_Y_TOP(RASTER_CORRECTION + SINGLE_TOP_BORDER_400 - (2 * vicii_first_raster));
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + DISPLAY_HEIGHT - SINGLE_TOP_BORDER_400 - (2 * vicii_first_raster) - 1);
		} else {
			SET_BORDER_Y_TOP(RASTER_CORRECTION + SINGLE_TOP_BORDER_400 - (2 * vicii_first_raster) + 8);
			SET_BORDER_Y_BOTTOM(RASTER_CORRECTION + DISPLAY_HEIGHT - (2 * vicii_first_raster) - SINGLE_TOP_BORDER_200 - 7);
		}
		SET_CHARGEN_Y_START(RASTER_CORRECTION + SINGLE_TOP_BORDER_400 - (2 * vicii_first_raster) - 6 + (REG_VIC2_YSCROLL * 2));
	}
	// This offset is present in recent versions of VIC4 VHDL
	SET_CHARGEN_X_START(CHARGEN_X_START - 1);
	DEBUG("VIC4: set border top=%d, bottom=%d, textypos=%d, display_row_count=%d vic_ii_first_raster=%d EFFECTIVE_V400=%d REG_V400=%d" NL, BORDER_Y_TOP, BORDER_Y_BOTTOM,
		CHARGEN_Y_START, display_row_count, vicii_first_raster, EFFECTIVE_V400, REG_V400);
}


// Called from set_hw_errata_level() in io_mapper.c
void  vic4_set_errata_level ( const Uint8 level )
{
	static Uint8 old_bug_compat_vic_iii_d016_delta = 2;
	bug_compat_vic_iii_d016_delta = level > 0 ? 0 : 2;	// if level > 0, no delta, if level == 0 then delta = 2 (VIC-III/C65 "buggy" default)
	bug_compat_char_attr = level > 1 ? false : true;
	DEBUGPRINT("VIC: errata level is set %u: d016_delta=%u, char_attr_buggy=%u" NL, level, bug_compat_vic_iii_d016_delta, bug_compat_char_attr);
	if (bug_compat_vic_iii_d016_delta != old_bug_compat_vic_iii_d016_delta) {
		old_bug_compat_vic_iii_d016_delta = bug_compat_vic_iii_d016_delta;
		if (REG_HOTREG)
			vic4_update_vertical_borders();	// so "bug_compat_vic_iii_d016_delta" will have effect ... However maybe this is FIXME in case of hot-register issue (?)
	}
}


static void vic4_interpret_legacy_mode_registers ( void )
{
	// See https://github.com/MEGA65/mega65-core/blob/257d78aa6a21638cb0120fd34bc0e6ab11adfd7c/src/vhdl/viciv.vhdl#L1277
	vic4_update_sideborder_dimensions();
	vic4_update_vertical_borders();

	Uint8 width = REG_H640 ? 80 : 40;

	// Set all 10 bits of ChrCount
	vic_registers[0x5e] = width;
	vic_registers[0x63] &= 0xCF; // clearing bits 4 and 5, being MSB of CHRCOUNT

	SET_LINESTEP_BYTES(width);	// * (REG_16BITCHARSET ? 2 : 1));

	REG_SCRNPTR_B0 = 0;
	REG_SCRNPTR_B1 &= 0xC0;
	REG_SCRNPTR_B1 |= REG_H640 ? ((reg_d018_screen_addr & 14) << 2) : (reg_d018_screen_addr << 2);
	REG_SCRNPTR_B2 = 0;
	vic_registers[0x63] &= 0b11110000;	// clear VIC4 precise screen addr bits 31-24 (from post bits 3-0) as it does not make sense; MEGA65 does not have enough fast RAM to use it

	REG_SPRPTR_B0 = 0xF8;
	REG_SPRPTR_B1 = (reg_d018_screen_addr << 2) | 0x3;
	if (REG_H640 | EFFECTIVE_V400)
		REG_SPRPTR_B1 |= 4;
	vic_registers[0x6E] &= 128;		// hmmm, clearing VIC4 sprite pointer bits 22-16 (bits 0-6 of this reg)

	REG_SPRPTR_B1  = (~last_dd00_bits << 6) | (REG_SPRPTR_B1 & 0x3F);
	REG_SCRNPTR_B1 = (~last_dd00_bits << 6) | (REG_SCRNPTR_B1 & 0x3F);
	REG_CHARPTR_B1 = (~last_dd00_bits << 6) | (REG_CHARPTR_B1 & 0x3F);

	SET_COLORRAM_BASE(0);
	DEBUG("VIC4: 16bit=%d, chrcount=%d, linestep=%d bytes, charxscale=%d, ras_src=%d "
		"screen_ram=$%06x, charset/bitmap=$%06x, sprite=$%06x" NL,
		REG_16BITCHARSET, REG_CHRCOUNT, LINESTEP_BYTES, REG_CHARXSCALE,
		REG_FNRST, SCREEN_ADDR, CHARSET_ADDR, SPRITE_POINTER_ADDR);
}


// Must be called before using the texture at all, otherwise crash will happen, or nothing at all.
// Access must be closed with vic4_close_frame_access().
// Do NOT call this function from vic4.c! It must be used by the emulator's main loop!
void vic4_open_frame_access ( void )
{
	int tail_sdl;
	current_pixel = pixel_start = xemu_start_pixel_buffer_access(&tail_sdl);
	if (XEMU_UNLIKELY(tail_sdl))
		FATAL("tail_sdl is not zero!");
	// The V400 hack ...
	// V400 + Yscale=0 + Bit6 of $D051 is handled as V200 ...
	EFFECTIVE_V400 = (REG_V400 && REG_CHRYSCL == 0 && (vic_registers[0x51] & 0x40)) ? 0 : !!REG_V400;
	// Now check the video mode: NTSC or PAL
	// Though it can be changed any time, this kind of information really only can be applied
	// at frame level. Thus we check here, if during the previous frame there was change
	// and apply the video mode set for our just started new frame.
	const Uint8 new_mode = !!(vic_registers[0x6F] & 0x80);
	if (XEMU_UNLIKELY(new_mode != videostd_id)) {
		// We have video mode change!
		videostd_id = new_mode;
		// signal that video standard has been changed, it's handled in the main emulation stuff then (reacted with recalculated timing change, and such)
		// ... including the task of resetting this signal variable!
		videostd_changed = 1;
		const char *new_name;
		if (videostd_id) {
			// --- NTSC ---
			new_name = NTSC_STD_NAME;
			videostd_frametime = NTSC_FRAME_TIME;
			videostd_1mhz_cycles_per_scanline = 1000000.0 / (float)(NTSC_LINE_FREQ);
			max_rasters = PHYSICAL_RASTERS_NTSC;
			visible_area_height = SCREEN_HEIGHT_VISIBLE_NTSC;
			vicii_first_raster = 7;
			REG_SPRITE_Y_ADJUST = 24;
		} else {
			// --- PAL ---
			new_name = PAL_STD_NAME;
			videostd_frametime = PAL_FRAME_TIME;
			videostd_1mhz_cycles_per_scanline = 1000000.0 / (float)(PAL_LINE_FREQ);
			max_rasters = PHYSICAL_RASTERS_PAL;
			visible_area_height = SCREEN_HEIGHT_VISIBLE_PAL;
			vicii_first_raster = 0;
			REG_SPRITE_Y_ADJUST = 0;
		}
		DEBUGPRINT("VIC4: switching video standard from %s to %s (1MHz line cycle count is %f, frame time is %dusec, max raster is %d, visible area height is %d)" NL, videostd_name, new_name, videostd_1mhz_cycles_per_scanline, videostd_frametime, max_rasters, visible_area_height);
		videostd_name = new_name;
		vic_readjust_sdl_viewport = 1;
		vicii_first_raster = vic_registers[0x6F] & 0x1F;
		if (!in_hypervisor) {
			// FIXME: later it can be a problem that a very brief transition to hypervisor mode and back (ie a quick trap) may be triggered within a single frame without
			// ever hitting this point (?!)
			vic4_update_sideborder_dimensions();
			vic4_update_vertical_borders();
		}
	}
	// handle this via vic_readjust_sdl_viewport variable (not directly above) so external stuff (like UI) can also
	// force to adjust viewport, not just the PAL/NTSC change itself (ie: fullborder/clipped border change)
	if (XEMU_UNLIKELY(vic_readjust_sdl_viewport)) {
		vic_readjust_sdl_viewport = 0;
		if (configdb.fullborders)	// XXX FIXME what should be the correct values for full borders and without that?!
			xemu_set_viewport(0, 0, TEXTURE_WIDTH - 1, max_rasters - 1, XEMU_VIEWPORT_ADJUST_LOGICAL_SIZE);
		else
			xemu_set_viewport(48, 0, TEXTURE_WIDTH - 48, visible_area_height - 1, XEMU_VIEWPORT_ADJUST_LOGICAL_SIZE);
	}
}


// This works even if vic4_disallow_videostd_change is true!
void vic4_set_videostd ( const int mode, const char *comment )
{
	// other values are ignored, since the caller may have the policy that -1 means leave it as it was, etc
	if (mode == 0 || mode == 1) {
		vic_registers[0x6F] = (vic_registers[0x6F] & 0x7F) | (mode << 7);
		DEBUGPRINT("VIC4: setting %s mode (%s)" NL, mode ? "NTSC" : "PAL", comment ? comment : EMPTY_STR);
	}
}


static void interrupt_checker ( void )
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
		DEBUG("VIC4: interrupt change %s -> %s" NL, vic_irq_old ? "active" : "inactive", vic_irq_new ? "active" : "inactive");
		if (vic_irq_new)
			cpu65.irqLevel |= 2;
		else
			cpu65.irqLevel &= ~2;
	}
}


static inline void check_raster_interrupt ( int nraster )
{
	if (nraster == compare_raster)
		interrupt_status |= 1;
	else
		interrupt_status &= 0xFE;
	interrupt_checker();
}


static inline void calculate_char_x_step ( void )
{
	char_x_step = (REG_CHARXSCALE / 120.0f) / (REG_H640 ? 1 : 2);
}


// FIXME: preliminary DAT support. For real, these should be mostly calculated at writing
// DAT X/Y registers, bitplane selection registers etc (also true for the actual renderer!),
// would give much better emulator performace. Though for now, that's a naive preliminary
// way to support DAT at all!
static XEMU_INLINE Uint8 *get_dat_addr ( unsigned int bpn )
{
	unsigned int x = vic_registers[0x3C];
	unsigned int y = vic_registers[0x3D] + ((x << 1) & 0x100);
	unsigned int h640 = (vic_registers[0x31] & 128);
	unsigned int and_mask, bit_shifter;
	x &= 0x7F;
	//DEBUGPRINT("VIC4: DAT: accessing DAT for bitplane #%u at X,Y of %u,%u in H%u mode" NL, bpn, x, y, h640 ? 640 : 320);
	// In V400 modes, odd/even scanlines should be considered as well!
	if (EFFECTIVE_V400) {
		if ((y & 1)) {
			and_mask = h640 ? 12 << 4 : 14 << 4;
			bit_shifter = 12 - 4;
		} else {
			and_mask = h640 ? 12      : 14     ;
			bit_shifter = 12;
		}
		y >>= 1;
	} else {
		and_mask = h640 ? 12 : 14;
		bit_shifter = 12;
	}
	return
		bitplane_bank_p +						// MEGA65 feature to support relocatable bitplane bank by the DAT! (this is a pointer, not an integer!)
		((vic_registers[0x33 + bpn] & and_mask) << bit_shifter) +	// bitplane address
		((bpn & 1) ? 0x10000 : 0) +					// odd/even bitplane selection
		(((y >> 3) * (h640 ? 640 : 320)) + (x << 3) + (y & 7))		// position within the bitplane given by the X/Y info
	;
}


/* DESIGN of vic_read_reg() and vic_write_reg() functions:
   addr = 00-7F, VIC4 registers 00-7F (ALWAYS, regardless of current I/O mode!)
   addr = 80-FF, VIC3 registers 00-7F (ALWAYS, regardless of current I/O mode!) [though for VIC3, many registers are ignored after the last one]
   addr = 100-13F, VIC2 registers 00-3F (ALWAYS, regardless of current I/O mode!)
   NOTES:
	* on a real VIC2 last used register is $2E. However we need the KEY register ($2F) and the C128-style 2MHz mode ($30) on MEGA65 too.
	* ALL cases must be handled!! from 000-13F for both of reading/writing funcs, otherwise Xemu will panic! this is a safety stuff
	* on write, later an MEGA65-alike solution is needed: ie "hot registers" for VIC2,VIC3 also writes VIC4 specific registers then
	* currently MANY things are not handled, it will be the task of "move to VIC4 internals" project ...
	* the purpose of ugly "tons of case" implementation that it should compile into a simple jump-table, which cannot be done faster too much ...
	* do not confuse these "vic reg mode" ranges with the vic_iomode variable, not so much direct connection between them! vic_iomode referred
	  for the I/O mode used on the "classic $D000 area" and DMA I/O access only
*/

static const char vic_registers_internal_mode_names[] = {'4', '3', '2'};

#define CASE_VIC_2(n)	case n+0x100
#define CASE_VIC_3(n)	case n+0x080
#define CASE_VIC_4(n)	case n
#define CASE_VIC_ALL(n)	CASE_VIC_2(n): CASE_VIC_3(n): CASE_VIC_4(n)
#define CASE_VIC_3_4(n)	CASE_VIC_3(n): CASE_VIC_4(n)


/* - If HOTREG register is enabled, VIC4 will trigger recalculation of border and such on next raster,
     on any "legacy" register write. For the VIC4 such "hot" registers are:

	  -- @IO:C64 $D011 VIC2 control register
	  -- @IO:C64 $D016 VIC2 control register
	  -- @IO:C64 $D018 VIC2 RAM addresses
	  -- @IO:C65 $D031 VIC3 Control Register B
*/
void vic_write_reg ( unsigned int addr, Uint8 data )
{
#if 0
	if (addr == 0x7D || addr == 0x7E || addr == 0x7F)
		DEBUGPRINT("VIC4: crosshair reg $%02X was written with value $%03X at PC=$%04X" NL, addr, data, cpu65.old_pc);
#endif
	//DEBUGPRINT("VIC4: write VIC%c reg $%02X (internally $%03X) with data $%02X" NL, XEMU_LIKELY(addr < 0x180) ? vic_registers_internal_mode_names[addr >> 7] : '?', addr & 0x7F, addr, data);
	// IMPORTANT NOTE: writing of vic_registers[] happens only *AFTER* this switch/case construct! This means if you need to do this before, you must do it manually at the right "case"!!!!
	// if you do so, you can even use "return" instead of "break" to save the then-redundant write of the register
	switch (addr) {
		CASE_VIC_ALL(0x00): CASE_VIC_ALL(0x01): CASE_VIC_ALL(0x02): CASE_VIC_ALL(0x03): CASE_VIC_ALL(0x04): CASE_VIC_ALL(0x05): CASE_VIC_ALL(0x06): CASE_VIC_ALL(0x07):
		CASE_VIC_ALL(0x08): CASE_VIC_ALL(0x09): CASE_VIC_ALL(0x0A): CASE_VIC_ALL(0x0B): CASE_VIC_ALL(0x0C): CASE_VIC_ALL(0x0D): CASE_VIC_ALL(0x0E): CASE_VIC_ALL(0x0F):
		CASE_VIC_ALL(0x10):
			break;		// Sprite coordinates: simple write the VIC reg in all I/O modes.
		CASE_VIC_ALL(0x11):
			if (vic_registers[0x11] ^ data)
				vic_hotreg_touched = 1;
			compare_raster = (compare_raster & 0xFF) | ((data & 0x80) << 1);
			DEBUG("VIC4: compare raster is now %d" NL, compare_raster);
			break;
		CASE_VIC_ALL(0x12):
			compare_raster = (compare_raster & 0xFF00) | data;
			DEBUG("VIC4: compare raster is now %d" NL, compare_raster);
			break;
		CASE_VIC_ALL(0x13): CASE_VIC_ALL(0x14):
			return;		// FIXME: writing light-pen registers?????
		CASE_VIC_ALL(0x15):	// sprite enabled
			break;
		CASE_VIC_ALL(0x16):	// control-reg#2, we allow write even if non-used bits here
			if (vic_registers[0x16] ^ data)
				vic_hotreg_touched = 1;
			break;
		CASE_VIC_ALL(0x17):	// sprite-Y expansion
			break;
		CASE_VIC_ALL(0x18):	// memory pointers.
			// (See vic4_interpret_legacy_mode_registers () for later REG_SCRNPTR_ adjustments)
			// Reads are mapped to extended registers.
			// So we just store the D018 Legacy Screen Address to be referenced elsewhere.
			REG_CHARPTR_B2 = 0;
			REG_CHARPTR_B1 = (data & 14) << 2;
			REG_CHARPTR_B0 = 0;
			REG_SCRNPTR_B2 &= 0xF0;
			reg_d018_screen_addr = (data & 0xF0) >> 4;
			vic_hotreg_touched = 1;
			//DEBUGPRINT("VIC4: $D018 is set to $%02X @ PC=$%04X" NL, data, cpu65.pc);
			break;
		CASE_VIC_ALL(0x19):
			interrupt_status = interrupt_status & (~data) & 0xF;
			interrupt_checker();
			break;
		CASE_VIC_ALL(0x1A):
			data &= 0xF;
			break;
		CASE_VIC_ALL(0x1B):	// sprite data priority
		CASE_VIC_ALL(0x1C):	// sprite multicolour
		CASE_VIC_ALL(0x1D):	// sprite-X expansion
			break;
		CASE_VIC_ALL(0x1E):	// sprite-sprite collision
			vic_registers[0x1E] = 0;
			interrupt_status &= 255 - 4;
			interrupt_checker();
			return;
		CASE_VIC_ALL(0x1F):	// sprite-data collision
			vic_registers[0x1F] = 0;
			interrupt_status &= 255 - 2;
			interrupt_checker();
			return;
		CASE_VIC_2(0x20): CASE_VIC_2(0x21): CASE_VIC_2(0x22): CASE_VIC_2(0x23): CASE_VIC_2(0x24): CASE_VIC_2(0x25): CASE_VIC_2(0x26): CASE_VIC_2(0x27):
		CASE_VIC_2(0x28): CASE_VIC_2(0x29): CASE_VIC_2(0x2A): CASE_VIC_2(0x2B): CASE_VIC_2(0x2C): CASE_VIC_2(0x2D): CASE_VIC_2(0x2E):
			data &= 0xF;	// colour-related registers are 4 bit only for VIC2
			break;
		// Colour registers of VIC seems to be always 8 in VIC3. It's may be in conflict with c65manual.txt
		// But anyway, this seems to be MEGA65's way.
		// Previously this was "gated" with D031.5, however that was not correct!
		CASE_VIC_3(0x20): CASE_VIC_3(0x21): CASE_VIC_3(0x22): CASE_VIC_3(0x23): CASE_VIC_3(0x24): CASE_VIC_3(0x25): CASE_VIC_3(0x26): CASE_VIC_3(0x27):
		CASE_VIC_3(0x28): CASE_VIC_3(0x29): CASE_VIC_3(0x2A): CASE_VIC_3(0x2B): CASE_VIC_3(0x2C): CASE_VIC_3(0x2D): CASE_VIC_3(0x2E):
		CASE_VIC_4(0x20): CASE_VIC_4(0x21): CASE_VIC_4(0x22): CASE_VIC_4(0x23): CASE_VIC_4(0x24): CASE_VIC_4(0x25): CASE_VIC_4(0x26): CASE_VIC_4(0x27):
		CASE_VIC_4(0x28): CASE_VIC_4(0x29): CASE_VIC_4(0x2A): CASE_VIC_4(0x2B): CASE_VIC_4(0x2C): CASE_VIC_4(0x2D): CASE_VIC_4(0x2E):
			break;		// colour-related registers are full 8 bit for VIC4 and VIC3
		CASE_VIC_ALL(0x2F):	// the KEY register, it must be handled in ALL VIC modes, to be able to set VIC I/O mode
			// FIXME? in hypervisor mode, it's not possible to alter I/O mode?? Thus I just ignore write in that case.
			// This seems to make freezer actually starting in Xemu, first time ever :-O
			if (!in_hypervisor) {
				int vic_new_iomode;
				switch ((vic_registers[0x2F] << 8) | data) {
					default:     vic_new_iomode = VIC2_IOMODE;    break;
					case 0xA596: vic_new_iomode = VIC3_IOMODE;    break;
					case 0x4554: vic_new_iomode = VIC4ETH_IOMODE; break;
					case 0x4753: vic_new_iomode = VIC4_IOMODE;    break;
				}
				if (vic_new_iomode != io_mode) {
					static const Uint8 color_register_masks[4] = { 0x0F, 0xFF, 0xFF, 0xFF };
					DEBUG("VIC4: changing I/O mode %d(%s) -> %d(%s)" NL, io_mode, iomode_names[io_mode], vic_new_iomode, iomode_names[vic_new_iomode]);
					memory_set_io_mode(vic_new_iomode);
					vic_color_register_mask = color_register_masks[io_mode];
				}
			} else
				DEBUGPRINT("VIC4: warning: I/O mode KEY $D02F register wanted to be written (with $%02X) in hypervisor mode! PC=$%04X" NL, data, cpu65.old_pc);
			break;
		CASE_VIC_2(0x30):	// this register is _SPECIAL_, and exists only in VIC2 (C64) I/O mode: C128-style "2MHz fast" mode ...
			// NOTE: in theory it's NOT possible to write this reg in hypervisor mode anymore, as then **always** VIC4 I/O mode is assumed, if I'm right!
			DEBUGPRINT("VIC4: writing $D030 in VIC2 I/O mode with data $%02x @ PC=$%04X (hypervisor mode: %d)" NL, data, cpu65.old_pc, !!in_hypervisor);
			data &= 1;	// use only bit0
			c128_d030_reg = data;
			machine_set_speed(0);
			return;		// it IS important to have return here, since it's not a "real" VIC4 mode register's view in another mode!!
		/* --- NO MORE VIC2 REGS FROM HERE --- */
		CASE_VIC_3_4(0x30):
			memory_write_d030(data);
			check_if_rom_palette(!(data & 4));
			break;
		CASE_VIC_3_4(0x31):
			// (!) NOTE:
			// According to Paul, speed change should trigger "HOTREG" touched notification but no VIC legacy register "interpret"
			// So probably we need a separate (cpu_speed_hotreg) var?
			// NOTE/FIXME: it's seems this regsiter is always hotreg now! See mega65-core change 0f1a8b37186d17b6a8d6f89d8fb95d166704fcd4
			// Also this may invalidate the first (old!) "(!) NOTE"
			//if ((vic_registers[0x31] & 0xBF) ^ (data & 0xBF))
			//    vic_hotreg_touched = 1;
			vic_hotreg_touched = 1;
			vic_registers[0x31] = data;	// we need this work-around, since reg-write happens _after_ this switch statement, but machine_set_speed above needs it ...
			machine_set_speed(0);
			calculate_char_x_step();
			break;				// we did the write, but we need to trigger vichot_reg if should

		CASE_VIC_3_4(0x32): CASE_VIC_3_4(0x33): CASE_VIC_3_4(0x34): CASE_VIC_3_4(0x35): CASE_VIC_3_4(0x36): CASE_VIC_3_4(0x37): CASE_VIC_3_4(0x38):
		CASE_VIC_3_4(0x39): CASE_VIC_3_4(0x3A): CASE_VIC_3_4(0x3B): CASE_VIC_3_4(0x3C): CASE_VIC_3_4(0x3D): CASE_VIC_3_4(0x3E): CASE_VIC_3_4(0x3F):
			break;
		// DAT read/write bitplanes port
		CASE_VIC_3_4(0x40): CASE_VIC_3_4(0x41): CASE_VIC_3_4(0x42): CASE_VIC_3_4(0x43): CASE_VIC_3_4(0x44): CASE_VIC_3_4(0x45): CASE_VIC_3_4(0x46):
		CASE_VIC_3_4(0x47):
			*get_dat_addr(addr & 7) = data;	// write pixels via the DAT!
			break;
		/* --- NO MORE VIC3 REGS FROM HERE --- */
		CASE_VIC_4(0x48): CASE_VIC_4(0x49): CASE_VIC_4(0x4A): CASE_VIC_4(0x4B):
		CASE_VIC_4(0x4C): CASE_VIC_4(0x4D): CASE_VIC_4(0x4E): CASE_VIC_4(0x4F):
			break;
		CASE_VIC_4(0x50):
			// Writing to XPOS register is no-op
			return;
		CASE_VIC_4(0x51):
			// Writing to XPOS register (high bits) is no-op, BUT the two top bits are writable!
			vic_registers[0x51] = (data & 0xC0) | (vic_registers[0x51] & 0x3F);
			return;
		CASE_VIC_4(0x52): CASE_VIC_4(0x53):
			break;
		CASE_VIC_4(0x54):
			vic_registers[0x54] = data;	// we need this work-around, since reg-write happens _after_ this switch statement, but machine_set_speed above needs it ...
			machine_set_speed(0);
			if (configdb.allow_scanlines && !in_hypervisor)	// FIXME: this is "do now allow to alter show-scanline setting by hypervisor"
				configdb.show_scanlines = !!(data & 32);
			return;				// since we DID the write, it's OK to return here and not using "break"
		CASE_VIC_4(0x55): CASE_VIC_4(0x56): CASE_VIC_4(0x57): break;
		CASE_VIC_4(0x58): CASE_VIC_4(0x59):
			DEBUG("VIC4: writing $%04X LINESTEP: $%02X" NL, addr, data);
			break;
		CASE_VIC_4(0x5A):
			//DEBUGPRINT("VIC4: WRITE $%04x CHARXSCALE: $%02x" NL, addr, data);
			vic_registers[0x5A] = data;	// write now and calculate step
			calculate_char_x_step();
			return;
		CASE_VIC_4(0x5B):
			break;
		CASE_VIC_4(0x5C):
			vic4_sideborder_touched = 1;
			break;

		CASE_VIC_4(0x5D):
			DEBUG("VIC4: writing $%04X SIDEBORDER/HOTREG: $%02X" NL, addr, data);
			if ((vic_registers[0x5D] & 0x1F) ^ (data & 0x1F))	// sideborder MSB (0..5) modified?
				vic4_sideborder_touched = 1;
			if (!(data & 0x80))	// writing bit 7 as zero (hotreg disable) also clears any possible remembered "trigger"
				vic_hotreg_touched = 0;
			// NOTE: if bit 7 is one, hotreg=enabled feature will be set after the switch/case block, thus indeed, the
			// hotreg event will be triggered then (also in this function, below) as "should be"
			break;

		CASE_VIC_4(0x5E):
			DEBUG("VIC4: writing $%04X CHARCOUNT: $%02X" NL, addr, data);
			break;
		CASE_VIC_4(0x5F):
			break;
		CASE_VIC_4(0x60): CASE_VIC_4(0x61): CASE_VIC_4(0x62): CASE_VIC_4(0x63):
			DEBUG("VIC4: writing SCREENADDR byte $D0%02X: $%02X" NL, addr, data);
			break;
		CASE_VIC_4(0x64):
		CASE_VIC_4(0x65): CASE_VIC_4(0x66): CASE_VIC_4(0x67): /*CASE_VIC_4(0x68): CASE_VIC_4(0x69): CASE_VIC_4(0x6A):*/ CASE_VIC_4(0x6B): /*CASE_VIC_4(0x6C):
		CASE_VIC_4(0x6D): CASE_VIC_4(0x6E):*//*CASE_VIC_4(0x70):*/ CASE_VIC_4(0x71): CASE_VIC_4(0x72): CASE_VIC_4(0x73): CASE_VIC_4(0x74):
		CASE_VIC_4(0x75): CASE_VIC_4(0x76): CASE_VIC_4(0x77): CASE_VIC_4(0x78): CASE_VIC_4(0x79): /*CASE_VIC_4(0x7A):*/ CASE_VIC_4(0x7B): /*CASE_VIC_4(0x7C):*/
		CASE_VIC_4(0x7D): CASE_VIC_4(0x7E): CASE_VIC_4(0x7F):
			break;

		CASE_VIC_4(0x68): CASE_VIC_4(0x69): CASE_VIC_4(0x6A):
			break;
		CASE_VIC_4(0x6C): CASE_VIC_4(0x6D): CASE_VIC_4(0x6E):
			vic_registers[addr & 0x7F] = data;
			break;
		CASE_VIC_4(0x6F):
			// If video standard change was disallowed, we keep bit7 as is, regardless of the write
			if (vic4_disallow_videostd_change)
				data = (vic_registers[0x6F] & 0x80) | (data & 0x7F);
			// We trigger video setup at next frame automatically, no need do anything further here
			break;

		CASE_VIC_4(0x70):	// VIC4 palette selection register
			altpalette	= ((data & 0x03) << 8) + vic_palettes;
			spritepalette	= ((data & 0x0C) << 6) + vic_palettes;
			palette		= ((data & 0x30) << 4) + vic_palettes;
			palregaccofs	= ((data & 0xC0) << 2);
			check_if_rom_palette(!(vic_registers[0x30] & 4));
			break;
		CASE_VIC_4(0x7A):
			// GS $D07A.5 VIC-IV:NOBUGCOMPAT Disables VIC-III / C65 Bug Compatibility Mode if set
			if ((vic_registers[0x7A] ^ data) & 32)
				set_hw_errata_level(data & 32 ? HW_ERRATA_MAX_LEVEL : 0, "D07A.5 change");
			break;
		CASE_VIC_4(0x7C):
			if ((data & 7) <= 2) {
				// The lower 3 bits of $7C set's the number of "128K slice" of the main RAM to be used with bitplanes
				bitplane_bank_p = main_ram + ((data & 7) << 17);
				DEBUG("VIC4: bitmap bank offset is $%X" NL, (unsigned int)(bitplane_bank_p - main_ram));
			} else
				DEBUGPRINT("VIC4: bitplane selection 128K-bank tried to set over 2. Refused to do so." NL);
			break;
		/* --- NON-EXISTING REGISTERS --- */
		CASE_VIC_2(0x31): CASE_VIC_2(0x32): CASE_VIC_2(0x33): CASE_VIC_2(0x34): CASE_VIC_2(0x35): CASE_VIC_2(0x36): CASE_VIC_2(0x37): CASE_VIC_2(0x38):
		CASE_VIC_2(0x39): CASE_VIC_2(0x3A): CASE_VIC_2(0x3B): CASE_VIC_2(0x3C): CASE_VIC_2(0x3D): CASE_VIC_2(0x3E): CASE_VIC_2(0x3F):
			DEBUG("VIC4: this VIC2 register does not exist for this mode, ignoring write." NL);
			return;		// not existing VIC2 registers, do not write!
		CASE_VIC_3(0x48): CASE_VIC_3(0x49): CASE_VIC_3(0x4A): CASE_VIC_3(0x4B): CASE_VIC_3(0x4C): CASE_VIC_3(0x4D): CASE_VIC_3(0x4E): CASE_VIC_3(0x4F):
		CASE_VIC_3(0x50): CASE_VIC_3(0x51): CASE_VIC_3(0x52): CASE_VIC_3(0x53): CASE_VIC_3(0x54): CASE_VIC_3(0x55): CASE_VIC_3(0x56): CASE_VIC_3(0x57):
		CASE_VIC_3(0x58): CASE_VIC_3(0x59): CASE_VIC_3(0x5A): CASE_VIC_3(0x5B): CASE_VIC_3(0x5C): CASE_VIC_3(0x5D): CASE_VIC_3(0x5E): CASE_VIC_3(0x5F):
		CASE_VIC_3(0x60): CASE_VIC_3(0x61): CASE_VIC_3(0x62): CASE_VIC_3(0x63): CASE_VIC_3(0x64): CASE_VIC_3(0x65): CASE_VIC_3(0x66): CASE_VIC_3(0x67):
		CASE_VIC_3(0x68): CASE_VIC_3(0x69): CASE_VIC_3(0x6A): CASE_VIC_3(0x6B): CASE_VIC_3(0x6C): CASE_VIC_3(0x6D): CASE_VIC_3(0x6E): CASE_VIC_3(0x6F):
		CASE_VIC_3(0x70): CASE_VIC_3(0x71): CASE_VIC_3(0x72): CASE_VIC_3(0x73): CASE_VIC_3(0x74): CASE_VIC_3(0x75): CASE_VIC_3(0x76): CASE_VIC_3(0x77):
		CASE_VIC_3(0x78): CASE_VIC_3(0x79): CASE_VIC_3(0x7A): CASE_VIC_3(0x7B): CASE_VIC_3(0x7C): CASE_VIC_3(0x7D): CASE_VIC_3(0x7E): CASE_VIC_3(0x7F):
			DEBUG("VIC4: this VIC3 register does not exist for this mode, ignoring write." NL);
			return;		// not existing VIC3 registers, do not write!
		/* --- FINALLY, IF THIS IS HIT, IT MEANS A MISTAKE SOMEWHERE IN MY CODE --- */
		default:
			FATAL("Xemu: invalid VIC internal register numbering on write: $%X", addr);
	}
	vic_registers[addr & 0x7F] = data;	// if addr == 0x5D and data bit 7 is one, REG_HOTREG just below will be already true (REG_HOTREG actually tests reg $5D bit 7)
	if (vic_hotreg_touched && REG_HOTREG) {
		//DEBUGPRINT("VIC4: vic_hotreg_touched triggered (WRITE $D0%02x, $%02x)" NL, addr & 0x7F, data );
		vic4_interpret_legacy_mode_registers();	// this also calls vic4_update_sideborder_dimensions(), thus we reset vic4_sideborder_touched to avoid calling it again below
		vic4_sideborder_touched = 0;
		vic_hotreg_touched = 0;
	}
	if (vic4_sideborder_touched) {
		//DEBUGPRINT("VIC4: vic4_sideborder_touched triggered (WRITE $D0%02x, $%02x)" NL, addr & 0x7F, data );
		vic4_update_sideborder_dimensions();
		vic4_sideborder_touched = 0;
	}
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
			// Always mapped to VIC4 extended "precise" registers according to the VHDL!
			// That is, reading D018 does not read back what D018 was written with before, at least
			// NOT always, if someone alters the "precise" registers (REG_*PTR_*) then
			// not, even not when hotregs are disabled it seems!!
			result = ((REG_SCRNPTR_B1 << 2) & 0xF0) | ((REG_CHARPTR_B1 >> 2) & 0x0F);
			//DEBUGPRINT("VIC4: $D018 is read as $%02X @ PC=$%04X" NL, result, cpu65.pc);
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
			vic_registers[0x1E] = 0;
			interrupt_status &= 255 - 4;
			interrupt_checker();
			break;
		CASE_VIC_ALL(0x1F):	// sprite-data collision
			vic_registers[0x1F] = 0;
			interrupt_status &= 255 - 2;
			interrupt_checker();
			break;
		CASE_VIC_2(0x20): CASE_VIC_2(0x21): CASE_VIC_2(0x22): CASE_VIC_2(0x23): CASE_VIC_2(0x24): CASE_VIC_2(0x25): CASE_VIC_2(0x26): CASE_VIC_2(0x27):
		CASE_VIC_2(0x28): CASE_VIC_2(0x29): CASE_VIC_2(0x2A): CASE_VIC_2(0x2B): CASE_VIC_2(0x2C): CASE_VIC_2(0x2D): CASE_VIC_2(0x2E):
			// XXX check this!!! I'm not sure if MEGA65 really does this, even though on C64, unused top 4 bits are read as '1'!
			result |= 0xF0;	// colour-related registers are 4 bit only for VIC2
			break;
		CASE_VIC_3(0x20): CASE_VIC_3(0x21): CASE_VIC_3(0x22): CASE_VIC_3(0x23): CASE_VIC_3(0x24): CASE_VIC_3(0x25): CASE_VIC_3(0x26): CASE_VIC_3(0x27):
		CASE_VIC_3(0x28): CASE_VIC_3(0x29): CASE_VIC_3(0x2A): CASE_VIC_3(0x2B): CASE_VIC_3(0x2C): CASE_VIC_3(0x2D): CASE_VIC_3(0x2E):
		CASE_VIC_4(0x20): CASE_VIC_4(0x21): CASE_VIC_4(0x22): CASE_VIC_4(0x23): CASE_VIC_4(0x24): CASE_VIC_4(0x25): CASE_VIC_4(0x26): CASE_VIC_4(0x27):
		CASE_VIC_4(0x28): CASE_VIC_4(0x29): CASE_VIC_4(0x2A): CASE_VIC_4(0x2B): CASE_VIC_4(0x2C): CASE_VIC_4(0x2D): CASE_VIC_4(0x2E):
			break;		// colour-related registers are full 8 bit for VIC4 and VIC3
		CASE_VIC_ALL(0x2F):	// the KEY register
			break;
		CASE_VIC_2(0x30):	// this register is _SPECIAL_, and exists only in VIC2 (C64) I/O mode: C128-style "2MHz fast" mode ...
			result = c128_d030_reg;	// ... so we override "result" read before the "switch" statement!
			break;
		/* --- NO MORE VIC2 REGS FROM HERE --- */
		CASE_VIC_3_4(0x30):
			break;
		CASE_VIC_3_4(0x31):
			break;
		CASE_VIC_3_4(0x32): CASE_VIC_3_4(0x33): CASE_VIC_3_4(0x34): CASE_VIC_3_4(0x35): CASE_VIC_3_4(0x36): CASE_VIC_3_4(0x37): CASE_VIC_3_4(0x38):
		CASE_VIC_3_4(0x39): CASE_VIC_3_4(0x3A): CASE_VIC_3_4(0x3B): CASE_VIC_3_4(0x3C): CASE_VIC_3_4(0x3D): CASE_VIC_3_4(0x3E): CASE_VIC_3_4(0x3F):
			break;
		// DAT read/write bitplanes port
		CASE_VIC_3_4(0x40): CASE_VIC_3_4(0x41): CASE_VIC_3_4(0x42): CASE_VIC_3_4(0x43): CASE_VIC_3_4(0x44): CASE_VIC_3_4(0x45): CASE_VIC_3_4(0x46):
		CASE_VIC_3_4(0x47):
			result = *get_dat_addr(addr & 7);	// read pixels via the DAT!
			break;
		/* --- NO MORE VIC3 REGS FROM HERE --- */
		CASE_VIC_4(0x48): CASE_VIC_4(0x49): CASE_VIC_4(0x4A): CASE_VIC_4(0x4B): CASE_VIC_4(0x4C): CASE_VIC_4(0x4D): CASE_VIC_4(0x4E): CASE_VIC_4(0x4F):
		CASE_VIC_4(0x50):
			// XPOS low byte
			break;
		CASE_VIC_4(0x51):
			// XPOS high bits + others
			// FIXME XXX super ugly hack to have something XPOS register changing. (some programs wait that to be changed)
			// Note, that bit 6 and 7 is different and not part of the XPOS info.
			result = (result & 0xC0) | ((result + 1) & 0x3F);
			vic_registers[0x51] = result;
			break;
		CASE_VIC_4(0x52): CASE_VIC_4(0x53):
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
			break;
		CASE_VIC_4(0x7D):
			result = vic_pixel_readback_result[vic_registers[0x7C] >> 6];
			break;
		CASE_VIC_4(0x7E): CASE_VIC_4(0x7F):
			break;
		/* --- NON-EXISTING REGISTERS --- */
		CASE_VIC_2(0x31): CASE_VIC_2(0x32): CASE_VIC_2(0x33): CASE_VIC_2(0x34): CASE_VIC_2(0x35): CASE_VIC_2(0x36): CASE_VIC_2(0x37): CASE_VIC_2(0x38):
		CASE_VIC_2(0x39): CASE_VIC_2(0x3A): CASE_VIC_2(0x3B): CASE_VIC_2(0x3C): CASE_VIC_2(0x3D): CASE_VIC_2(0x3E): CASE_VIC_2(0x3F):
			DEBUG("VIC4: this VIC2 register does not exist for this mode, $FF for read answer." NL);
			result = 0xFF;		// not existing VIC2 registers
			break;
		CASE_VIC_3(0x48): CASE_VIC_3(0x49): CASE_VIC_3(0x4A): CASE_VIC_3(0x4B): CASE_VIC_3(0x4C): CASE_VIC_3(0x4D): CASE_VIC_3(0x4E): CASE_VIC_3(0x4F):
		CASE_VIC_3(0x50): CASE_VIC_3(0x51): CASE_VIC_3(0x52): CASE_VIC_3(0x53): CASE_VIC_3(0x54): CASE_VIC_3(0x55): CASE_VIC_3(0x56): CASE_VIC_3(0x57):
		CASE_VIC_3(0x58): CASE_VIC_3(0x59): CASE_VIC_3(0x5A): CASE_VIC_3(0x5B): CASE_VIC_3(0x5C): CASE_VIC_3(0x5D): CASE_VIC_3(0x5E): CASE_VIC_3(0x5F):
		CASE_VIC_3(0x60): CASE_VIC_3(0x61): CASE_VIC_3(0x62): CASE_VIC_3(0x63): CASE_VIC_3(0x64): CASE_VIC_3(0x65): CASE_VIC_3(0x66): CASE_VIC_3(0x67):
		CASE_VIC_3(0x68): CASE_VIC_3(0x69): CASE_VIC_3(0x6A): CASE_VIC_3(0x6B): CASE_VIC_3(0x6C): CASE_VIC_3(0x6D): CASE_VIC_3(0x6E): CASE_VIC_3(0x6F):
		CASE_VIC_3(0x70): CASE_VIC_3(0x71): CASE_VIC_3(0x72): CASE_VIC_3(0x73): CASE_VIC_3(0x74): CASE_VIC_3(0x75): CASE_VIC_3(0x76): CASE_VIC_3(0x77):
		CASE_VIC_3(0x78): CASE_VIC_3(0x79): CASE_VIC_3(0x7A): CASE_VIC_3(0x7B): CASE_VIC_3(0x7C): CASE_VIC_3(0x7D): CASE_VIC_3(0x7E): CASE_VIC_3(0x7F):
			DEBUG("VIC4: this VIC3 register does not exist for this mode, $FF for read answer." NL);
			result = 0xFF;
			break;			// not existing VIC3 registers
		/* --- FINALLY, IF THIS IS HIT, IT MEANS A MISTAKE SOMEWHERE IN MY CODE --- */
		default:
			FATAL("Xemu: invalid VIC internal register numbering on read: $%X", addr);
	}
	DEBUG("VIC4: read VIC%c reg $%02X (internally $%03X) with result $%02X" NL, XEMU_LIKELY(addr < 0x180) ? vic_registers_internal_mode_names[addr >> 7] : '?', addr & 0x7F, addr, result);
	return result;
}

#undef CASE_VIC_2
#undef CASE_VIC_3
#undef CASE_VIC_4
#undef CASE_VIC_ALL
#undef CASE_VIC_3_4


// A very interesting thing happening here. If I want to check only if is_sprite[pos] is zero,
// I found, that the sprite can collide with itself ... Looks like it sees it's "own data"
// somehow which should be impossible as "is_sprite" is zeroed after each scanline. No idea,
// maybe some non-integer stepping make this? Anyway, I had to use another algorithm because of
// this problem. - LGB
#ifdef	SPRITE_SPRITE_COLLISION
#	warning "Sprite-sprite collision is an experimental feature (SPRITE_SPRITE_COLLISION is defined)!"
#	define DO_SPRITE_SPRITE_COLLISION(pos,cond) do {		\
		if (cond) {						\
			const Uint8 sp = is_sprite[pos] | sprbmask;	\
			is_sprite[pos] = sp;				\
			if (XEMU_UNLIKELY(sp != sprbmask))		\
				vic_registers[0x1E] |= sp;		\
		}							\
	} while (0)
#ifndef	SPRITE_ANY_COLLISION
#define	SPRITE_ANY_COLLISION
#endif
#else
	// dummy macro for the case when SPRITE_SPRITE_COLLISION was not requested
#	define DO_SPRITE_SPRITE_COLLISION(pos,cond)
#endif


#ifdef	SPRITE_FG_COLLISION
#	warning "Sprite-foreground collision is an experimental feature (SPRITE_FG_COLLISION is defined)!"
#	define DO_SPRITE_FG_COLLISION(pos,cond) do {		\
		if (is_fg[pos] && (cond))			\
			vic_registers[0x1F] |= sprbmask;	\
	} while (0)
#ifndef	SPRITE_ANY_COLLISION
#define	SPRITE_ANY_COLLISION
#endif
#else
	// dummy macro for the case when SPRITE_FG_COLLISION was not requested
#	define DO_SPRITE_FG_COLLISION(pos,cond)
#endif


static XEMU_INLINE void vic4_draw_sprite_row_16color ( const int sprnum, int x_display_pos, const Uint8* row_data_ptr, const int xscale, const int do_tiling )
{
	const int totalBytes = SPRITE_EXTWIDTH(sprnum) ? 8 : 3;
	//const int palindexbase = sprnum * 16 + 128 * (SPRITE_BITPLANE_ENABLE(sprnum) >> sprnum);
	// pal16 is a pointer corrected by "palindexbase" already, so ready to be indexed with the 4 bit (16) colour
	const Uint32 *pal16 = spritepalette + (sprnum * 16 + 128 * (SPRITE_BITPLANE_ENABLE(sprnum) >> sprnum));
	// in 16 colour sprite mode, sprite colour register gives the transparent colour index
	// We always use the lower 4 bit only at this very specific case, that's the reason for SPRITE_COLOR_4BIT() macro and not SPRITE_COLOR() [which can be 4/8 bit depending on curretn VIC mode)
	const Uint8 transparency_palette_index = SPRITE_COLOR_4BIT(sprnum);
#	ifdef SPRITE_ANY_COLLISION
	const Uint8 sprbmask = 1 << sprnum;
#	endif
	do {
		for (int byte = 0; byte < totalBytes; byte++) {
			const Uint8 c0 = (*(row_data_ptr + byte)) >> 4;
			const Uint8 c1 = (*(row_data_ptr + byte)) & 0xF;
			for (int p = 0; p < xscale && x_display_pos < border_x_right; p++, x_display_pos++) {
				if (c0 != transparency_palette_index && x_display_pos >= border_x_left && (
					!SPRITE_IS_BACK(sprnum) || (SPRITE_IS_BACK(sprnum) && !is_fg[x_display_pos])
				)) {
					*(pixel_raster_start + x_display_pos) = pal16[c0];
					DO_SPRITE_SPRITE_COLLISION(x_display_pos, 1);
					DO_SPRITE_FG_COLLISION(x_display_pos, 1);
				}
			}
			for (int p = 0; p < xscale && x_display_pos < border_x_right; p++, x_display_pos++) {
				if (c1 != transparency_palette_index && x_display_pos >= border_x_left && (
					!SPRITE_IS_BACK(sprnum) || (SPRITE_IS_BACK(sprnum) && !is_fg[x_display_pos])
				)) {
					*(pixel_raster_start + x_display_pos) = pal16[c1];
					DO_SPRITE_SPRITE_COLLISION(x_display_pos, 1);
					DO_SPRITE_FG_COLLISION(x_display_pos, 1);
				}
			}
		}
	} while (XEMU_UNLIKELY(do_tiling && x_display_pos < border_x_right));
}


static XEMU_INLINE void vic4_draw_sprite_row_multicolor ( const int sprnum, int x_display_pos, const Uint8* row_data_ptr, const int xscale, const int do_tiling )
{
	const int totalBytes = SPRITE_EXTWIDTH(sprnum) ? 8 : 3;
	const Uint8 mcm_spr_pal_indices[4] = { 0, SPRITE_MULTICOLOR_1, SPRITE_COLOR(sprnum), SPRITE_MULTICOLOR_2 };	// entry zero is not used
#	ifdef SPRITE_ANY_COLLISION
	const Uint8 sprbmask = 1 << sprnum;
#	endif
	do {
		for (int byte = 0; byte < totalBytes; byte++) {
			const Uint8 row_data = *row_data_ptr++;
			for (int shift = 6; shift >= 0; shift -= 2) {
				const int mcm_pixel_value = (row_data >> shift) & 3;
				const Uint32 sdl_pixel = spritepalette[mcm_spr_pal_indices[mcm_pixel_value]];
				for (int p = 0; p < xscale && x_display_pos < border_x_right; p++, x_display_pos += 2) {
					if (mcm_pixel_value) {
						if (x_display_pos >= border_x_left && (
							!SPRITE_IS_BACK(sprnum) || (SPRITE_IS_BACK(sprnum) && !is_fg[x_display_pos])
						)) {
							*(pixel_raster_start + x_display_pos) = sdl_pixel;
							DO_SPRITE_SPRITE_COLLISION(x_display_pos, mcm_pixel_value & 2);
							DO_SPRITE_FG_COLLISION(x_display_pos, mcm_pixel_value & 2);
						}
						if (x_display_pos + 1 >= border_x_left && (
							!SPRITE_IS_BACK(sprnum) || (SPRITE_IS_BACK(sprnum) && !is_fg[x_display_pos + 1])
						)) {
							*(pixel_raster_start + x_display_pos + 1) = sdl_pixel;
							DO_SPRITE_SPRITE_COLLISION(x_display_pos + 1, mcm_pixel_value & 2);
							DO_SPRITE_FG_COLLISION(x_display_pos + 1, mcm_pixel_value & 2);
						}
					}
				}
			}
		}
	} while (XEMU_UNLIKELY(do_tiling && x_display_pos < border_x_right));
}


static XEMU_INLINE void vic4_draw_sprite_row_mono ( const int sprnum, int x_display_pos, const Uint8 *row_data_ptr, const int xscale, const int do_tiling )
{
	const int totalBytes = SPRITE_EXTWIDTH(sprnum) ? 8 : 3;
	const Uint32 sdl_pixel = spritepalette[SPRITE_COLOR(sprnum)];
#	ifdef SPRITE_ANY_COLLISION
	const Uint8 sprbmask = 1 << sprnum;
#	endif
	do {
		for (int byte = 0; byte < totalBytes; byte++) {
			for (int xbit = 0; xbit < 8; xbit++) {
				const Uint8 sprite_bit = *row_data_ptr & (0x80 >> xbit);
				for (int p = 0; p < xscale && x_display_pos < border_x_right; p++, x_display_pos++) {
					if (x_display_pos >= border_x_left && sprite_bit && (
						!SPRITE_IS_BACK(sprnum) ||
						(SPRITE_IS_BACK(sprnum) && !is_fg[x_display_pos])
					)) {
						*(pixel_raster_start + x_display_pos) = sdl_pixel;
						DO_SPRITE_SPRITE_COLLISION(x_display_pos, 1);
						DO_SPRITE_FG_COLLISION(x_display_pos, 1);
					}
				}
			}
			row_data_ptr++;
		}
	} while (XEMU_UNLIKELY(do_tiling && x_display_pos < border_x_right));
}


static XEMU_INLINE void vic4_do_sprites ( void )
{
	// Fetch and sequence sprites.
	//
	// NOTE about Text/Bitmap Graphics Background/foreground semantics:
	// In multicolor mode (MCM=1), the bit combinations "00" and "01" belong to the background
	// and "10" and "11" to the foreground whereas in standard mode (MCM=0),
	// cleared pixels belong to the background and set pixels to the foreground.
#ifdef	SPRITE_COORD_LATCHING
	static int sprite_x_latch[8], sprite_y_latch[8];
#endif
	const int reg_tiling = REG_SPRTILEN;
	for (int sprnum = 7; sprnum >= 0; sprnum--) {
		if (REG_SPRITE_ENABLE & (1 << sprnum)) {
			const int spriteHeight = SPRITE_EXTHEIGHT(sprnum) ? REG_SPRHGHT : 21;
			const int y_display_pos =
#ifdef				SPRITE_COORD_LATCHING
				sprite_is_being_rendered[sprnum] ? sprite_y_latch[sprnum] :
#endif
				((SPRITE_V400(sprnum) ? 1 : 2) * (SPRITE_POS_Y(sprnum) - (SPRITE_V400(sprnum) ? 0 : (REG_SPRITE_Y_ADJUST - 2))));
			int sprite_row_in_raster = ycounter - y_display_pos;
			if (!SPRITE_V400(sprnum))
				sprite_row_in_raster = sprite_row_in_raster >> 1;
			if (SPRITE_VERT_2X(sprnum))
				sprite_row_in_raster = sprite_row_in_raster >> 1;
			if (sprite_row_in_raster >= 0 && sprite_row_in_raster < spriteHeight) {
				// FIXME: it's currently unknown if X coordinate is latched as well, now I assume it is ...
				const int x_display_pos =
#ifdef					SPRITE_COORD_LATCHING
					sprite_is_being_rendered[sprnum] ? sprite_x_latch[sprnum] :
#endif
					((REG_SPR640 ? 1 : 2) * SPRITE_POS_X(sprnum) + (REG_SPR640 ? 1 : SPRITE_FIRST_X));
#ifdef				SPRITE_COORD_LATCHING
				if (sprite_row_in_raster == spriteHeight - 1) {
					// the last line of sprite, turn off the latched signal
					sprite_is_being_rendered[sprnum] = 0;
				} else if (!sprite_is_being_rendered[sprnum]) {
					// first - detected - render event for the sprite, let's latch it
					sprite_is_being_rendered[sprnum] = 1;
					sprite_x_latch[sprnum] = x_display_pos;
					sprite_y_latch[sprnum] = y_display_pos;
				}
#endif
				const int widthBytes = SPRITE_EXTWIDTH(sprnum) ? 8 : 3;
				// Mask-out bits 0-3, 23-19 if SPRPTR16 enabled
				const Uint32 sprite_pointer_addr = SPRITE_16BITPOINTER ? (SPRITE_POINTER_ADDR & 0x8FFFF0) : SPRITE_POINTER_ADDR;
				const Uint8 *sprite_data_pointer = main_ram + sprite_pointer_addr + sprnum * ((SPRITE_16BITPOINTER >> 7) + 1);
				const Uint32 sprite_data_addr = SPRITE_16BITPOINTER ?
					64 * ((*(sprite_data_pointer + 1) << 8) | (*sprite_data_pointer))
					: ((64 * (*sprite_data_pointer)) | (SPRITE_POINTER_ADDR & 0xC000)); // Use bits 14-15 (this can be set from $DD00 if HOTREG is ENABLED)
				//DEBUGPRINT("VIC4: Sprite %d data at $%08X " NL, sprnum, sprite_data_addr);
				const Uint8 *sprite_data = main_ram + sprite_data_addr;
				const Uint8 *row_data = sprite_data + widthBytes * sprite_row_in_raster;
				const int xscale = (REG_SPR640 ? 1 : 2) * (SPRITE_HORZ_2X(sprnum) ? 2 : 1);
				const int do_tiling = reg_tiling & (1 << sprnum);
				if (SPRITE_MULTICOLOR(sprnum))
					vic4_draw_sprite_row_multicolor(sprnum, x_display_pos, row_data, xscale, do_tiling);
				else if (SPRITE_16COLOR(sprnum))
					vic4_draw_sprite_row_16color(sprnum, x_display_pos, row_data, xscale, do_tiling);
				else
					vic4_draw_sprite_row_mono(sprnum, x_display_pos, row_data, xscale, do_tiling);
			}
		} else {
#ifdef			SPRITE_COORD_LATCHING
			// To avoid the problem when sprite gets disabled during its rendering so remains latched for the whole rest of the frame,
			// since the "end" condition is never hit above on its - would be - active rendering region (by its height).
			sprite_is_being_rendered[sprnum] = 0;
#endif
		}
	}
}


// Render a monochrome character cell row
// flip = 00 Dont flip, 01 = flip vertical, 10 = flip horizontal, 11 = flip both
static XEMU_INLINE void vic4_render_mono_char_row ( Uint8 char_byte, const int glyph_width, const Uint8 bg_color, Uint8 fg_color, Uint8 vic3attr )
{
	const Uint32 *palette_now = used_palette;
	if (XEMU_UNLIKELY(vic3attr)) {
		if(!VIC3_ATTR_BLINK(vic3attr) || blink_phase) {
			if (XEMU_UNLIKELY(VIC3_ATTR_BOLD(vic3attr) && VIC3_ATTR_REVERSE(vic3attr)))
				palette_now = altpalette;
			else if (VIC3_ATTR_REVERSE(vic3attr))
				char_byte = ~char_byte;
			if (VIC3_ATTR_BOLD(vic3attr))
				fg_color |= 0x10;
			if (char_row == 7 && VIC3_ATTR_UNDERLINE(vic3attr))
				char_byte = 0xFF;
		} else if (VIC3_ATTR_BLINK(vic3attr) && vic3attr == 0x1) {
			char_byte = 0;
		}
	}
	char_byte &= draw_mask;
	const Uint32 sdl_fg_color = palette_now[fg_color];
	if (XEMU_LIKELY(enable_bg_paint)) {
		const Uint32 sdl_bg_color = palette_now[bg_color];
		for (float cx = 0; cx < glyph_width && xcounter < border_x_right; cx += char_x_step) {
			const Uint8 char_pixel = (char_byte & (0x80 >> (int)cx));
			*(current_pixel++) = char_pixel ? sdl_fg_color : sdl_bg_color;
			is_fg[xcounter++] = char_pixel;
		}
	} else {
		for (float cx = 0; cx < glyph_width && xcounter < border_x_right; cx += char_x_step) {
			const Uint8 char_pixel = (char_byte & (0x80 >> (int)cx));
			if (char_pixel)
				*current_pixel = sdl_fg_color;
			current_pixel++;
			is_fg[xcounter++] = char_pixel;
		}
	}
}


static XEMU_INLINE void vic4_render_multicolor_char_row ( Uint8 char_byte, const int glyph_width, const Uint8 color_source[4] )
{
	char_byte &= draw_mask;
	for (float cx = 0; cx < glyph_width && xcounter < border_x_right; cx += char_x_step) {
		const Uint8 bitsel = 2 * (int)(cx / 2);
		const Uint8 bit_pair = (char_byte & (0x80 >> bitsel)) >> (6-bitsel) | (char_byte & (0x40 >> bitsel)) >> (6-bitsel);
		if (XEMU_LIKELY(bit_pair || enable_bg_paint))
			*current_pixel = used_palette[color_source[bit_pair]];
		current_pixel++;
		is_fg[xcounter++] = (bit_pair & 2);
	}
}


// 8-bytes per row
static XEMU_INLINE void vic4_render_fullcolor_char_row ( const Uint8* char_row, const int glyph_width, const Uint32 bg_sdl_color, const Uint32 fg_sdl_color, const int hflip, const Uint32 *palette_now )
{
	for (float cx = 0; cx < glyph_width && xcounter < border_x_right; cx += char_x_step) {
		const Uint8 char_data = draw_mask & char_row[XEMU_LIKELY(!hflip) ? (int)cx : glyph_width - 1 - (int)cx];
		if (char_data == 0xFF)
			*current_pixel = fg_sdl_color;
		else if (XEMU_LIKELY(char_data))
			*current_pixel = palette_now[char_data];
		else if (XEMU_LIKELY(enable_bg_paint))
			*current_pixel = bg_sdl_color;
		current_pixel++;
		is_fg[xcounter++] = char_data;
	}
}


// 16-color (Nybl) mode (4-bit per pixel / 16 pixel wide characters)
static XEMU_INLINE void vic4_render_16color_char_row ( const Uint8* char_row, const int glyph_width, const Uint32 bg_sdl_color, const Uint32 fg_sdl_color, const Uint32 *palette16, const int hflip )
{
	for (float cx = 0; cx < glyph_width && xcounter < border_x_right; cx += char_x_step) {
		Uint8 char_data;
		if (XEMU_LIKELY(!hflip)) {
			char_data = char_row[((int)cx) / 2];
			if (((int)cx) & 1)
				char_data >>= 4;
			else
				char_data &= 0xf;
		} else {
			char_data = char_row[glyph_width / 2 - 1 - (((int)cx) / 2)];
			if (((int)cx) & 1)
				char_data &= 0xf;
			else
				char_data >>= 4;
		}
		char_data &= draw_mask;
		is_fg[xcounter++] = char_data;
		if (char_data)
			*current_pixel = (char_data != 15) ? palette16[char_data] : fg_sdl_color;
		else if (enable_bg_paint)
			*current_pixel = bg_sdl_color;
		current_pixel++;
	}
}


static XEMU_INLINE void set_bitplane_pointers ( void )
{
	// Get Bitplane source addresses
	/* TODO: Cache the following reads & EA calculation */
	int and_mask, bit_shifter;
	if (EFFECTIVE_V400) {
		if (!(ycounter & 1)) {
			and_mask = (REG_H640 ? 12 : 14);
			bit_shifter = 12;
		} else {
			and_mask = (REG_H640 ? 12 << 4 : 14 << 4);
			bit_shifter = 12 - 4;
		}
	} else {
		and_mask = (REG_H640 ? 12 : 14);
		bit_shifter = 12;
	}
	bitplane_p[0] = bitplane_bank_p + ((vic_registers[0x33] & and_mask) << bit_shifter);
	bitplane_p[1] = bitplane_bank_p + ((vic_registers[0x34] & and_mask) << bit_shifter) + 0x10000;
	bitplane_p[2] = bitplane_bank_p + ((vic_registers[0x35] & and_mask) << bit_shifter);
	bitplane_p[3] = bitplane_bank_p + ((vic_registers[0x36] & and_mask) << bit_shifter) + 0x10000;
	bitplane_p[4] = bitplane_bank_p + ((vic_registers[0x37] & and_mask) << bit_shifter);
	bitplane_p[5] = bitplane_bank_p + ((vic_registers[0x38] & and_mask) << bit_shifter) + 0x10000;
	bitplane_p[6] = bitplane_bank_p + ((vic_registers[0x39] & and_mask) << bit_shifter);
	bitplane_p[7] = bitplane_bank_p + ((vic_registers[0x3A] & and_mask) << bit_shifter) + 0x10000;
}


// Render a bitplane-mode character cell row
static XEMU_INLINE void vic4_render_bitplane_char_row ( const Uint32 offset, const int glyph_width )
{
	const Uint8 bpe_mask = vic_registers[0x32] & (REG_H640 ? 15 : 255);
	for (float cx = 0; cx < glyph_width && xcounter < border_x_right; cx += char_x_step) {
		const Uint8 bitsel = 0x80 >> ((int)cx);
		*(current_pixel++) = palette[((			// Do not try this at home ...
			((*(bitplane_p[0] + offset) & bitsel) ?   1 : 0) |
			((*(bitplane_p[1] + offset) & bitsel) ?   2 : 0) |
			((*(bitplane_p[2] + offset) & bitsel) ?   4 : 0) |
			((*(bitplane_p[3] + offset) & bitsel) ?   8 : 0) |
			((*(bitplane_p[4] + offset) & bitsel) ?  16 : 0) |
			((*(bitplane_p[5] + offset) & bitsel) ?  32 : 0) |
			((*(bitplane_p[6] + offset) & bitsel) ?  64 : 0) |
			((*(bitplane_p[7] + offset) & bitsel) ? 128 : 0)
			) & bpe_mask) ^ vic_registers[0x3B]
		];
		is_fg[xcounter++] = (*(bitplane_p[2] + offset) & bitsel);
	}
}


static XEMU_INLINE void vic4_render_bitplane_raster ( void )
{
	// FIXME: do not call this function here, but from actual register writes only
	// which can affect the result of this function!!
	set_bitplane_pointers();
	Uint32 offset = display_row * REG_CHRCOUNT * 8 + char_row;
	int line_char_index = 0;
	while (line_char_index < REG_CHRCOUNT) {
		vic4_render_bitplane_char_row(offset, 8);
		offset += 8;
		line_char_index++;
	}
	if (!EFFECTIVE_V400 || (ycounter  & 1)) {
		if (++char_row > 7) {
			char_row = 0;
			display_row++;
		}
	}
	while (xcounter++ < border_x_right)
		*current_pixel++ = palette[REG_SCREEN_COLOR];
}


// TODO: make this register-write time event rather than calling by the scanline renderer again and again ...
static XEMU_INLINE Uint8 *get_charset_effective_addr ( void )
{
	//const Uint8 *row_data_base_addr = main_ram + (REG_BMM ? VIC2_BITMAP_ADDR : get_charset_effective_addr());
	int addr = REG_BMM ? VIC2_BITMAP_ADDR : CHARSET_ADDR;
	// Note: in theory on C65 there is a bit for choose between two charsets (rather than only lower/upper case)
	// See: https://github.com/lgblgblgb/xemu/issues/213
	// However it seems even MEGA65 does not support this.
	// FIXME: how we can be sure, there won't be any out-of-bound access for the relative small WOM then?
	if (!REG_BMM && (addr == 0x1000 || addr == 0x9000 || addr == 0x1800 || addr == 0x9800))
		return char_ram + (addr & 0xFFF);
	// FIXME XXX this is a fixed constant for checking.
	if (XEMU_UNLIKELY(addr > 0x60000))	// this is valid since we still have got some extra unused RAM left to go beyond actual RAM while bulding the frame
		return main_ram + 0x60000;	// give some unused ram array of emulaton, thus whatever high value set by user as ADDR, won't overflow during the frame
	else
		return main_ram + addr;
}


// The character rendering engine. Most features are shared between
// all graphic modes. Basically, the VIC4 supports the following character
// color modes:
//
// - Monochrome (Bg/Fg)
// - VIC2 Multicolor
// - 16-color
// - 256-color
//
// It's interesting to see that the four modes can be selected in
// bitmap or text modes.
//
// VIC3 Extended attributes are applied to characters if properly set,
// except in Multicolor modes.
static XEMU_INLINE void vic4_render_char_raster ( void )
{
	int line_char_index = 0;
	enable_bg_paint = 1;
	draw_mask = 0xFF;	// initialize draw mask being $FF initially (glyph row is not masked out)
	const Uint8 *row_data_base_addr = get_charset_effective_addr();	// FIXME: is it OK that I moved here, before the loop?
	// If this line is inside the vertical borders, mark all pixels as border color
	if (ycounter < BORDER_Y_TOP || ycounter >= BORDER_Y_BOTTOM) {
		for (int i = 0; i < TEXTURE_WIDTH; i++)
			*(current_pixel++) = palette[REG_BORDER_COLOR];
	}
	else if (display_row <= display_row_count) {
		Uint32 colour_ram_current_addr = COLOUR_RAM_OFFSET + (display_row * LINESTEP_BYTES);
		Uint32 screen_ram_current_addr = SCREEN_ADDR + (display_row * LINESTEP_BYTES);
		// Account for Chargen X-displacement
		for (Uint32 *p = current_pixel; p < current_pixel + (CHARGEN_X_START - border_x_left); p++)
			*p = palette[REG_SCREEN_COLOR];
		current_pixel += (CHARGEN_X_START - border_x_left);
		xcounter += (CHARGEN_X_START - border_x_left);
		const int xcounter_start = xcounter;
		Sint8 char_fetch_offset = 0;
		// Chargen starts here.
		while (line_char_index < REG_CHRCOUNT) {
			Uint16 color_data = colour_ram[(colour_ram_current_addr++) & 0x07FFF];
			Uint16 char_value = main_ram[(screen_ram_current_addr++) & 0x7FFFF];
			if (REG_16BITCHARSET) {
				color_data = (color_data << 8) | colour_ram[(colour_ram_current_addr++) & 0x07FFF];
				char_value = char_value | (main_ram[(screen_ram_current_addr++) & 0x7FFFF] << 8);
				if (XEMU_UNLIKELY(SXA_GOTO_X(color_data))) {
					// ---- Start of the GOTOX re-positioning functionality implementation, tricky one ----
					xcounter = (char_value & 0x3FF);	// first, extract the goto 'X' value as an usigned number
					// Check the given value as "signed" as well, decide if it's "negative" or not
					if (REG_H640) {
						// Interpret as a "negative" value compared to xcounter_start if it would fit into the real range of 0-xcounter_start,
						// otherwise interpret that as a positive offset compared to xcounter_start
						if (0x400 - xcounter <= xcounter_start)
							xcounter = xcounter_start - (0x400 - xcounter);
						else
							xcounter += xcounter_start;
					} else {
						xcounter <<= 1;	// multiply by 2, if !H640 (as the pixel is double width for lower resolution)
						if (0x800 - xcounter <= xcounter_start)
							xcounter = xcounter_start - (0x800 - xcounter);
						else
							xcounter += xcounter_start;
					}
					// The ugly: too large goto X values may cause out-of-bound access on eg is_fg buffer. Thus, if the result is larger than
					// the width of the SDL texture, it won't be seen anyway, so we "clamp" it for the NEXT raster as an ugly solution, which
					// will be overwritten anyway on rendering in the next raster. This way we don't need checking of out-of-bound access (faster
					// code) _everywhere_ ...
					if (xcounter > TEXTURE_WIDTH)
						xcounter = TEXTURE_WIDTH;
					// Align current_pixel pointer according the calculated xcounter "horror show" above
					current_pixel = pixel_raster_start + xcounter;
					// ---- End of the GOTOX re-positioning functionality implementation ----
					line_char_index++;
					char_fetch_offset = (char_value >> 13) & 7;
					// If ScreenRAMByte1 bit 4 is set then the char_fetch_offset should be subtracted and not added
					if (char_value & (1 << 12))
						char_fetch_offset = -char_fetch_offset;
					if (SXA_VERTICAL_FLIP(color_data))
						enable_bg_paint = 0;
					else
						enable_bg_paint = 1;
					if (SXA_ATTR_BOLD(color_data) && SXA_ATTR_REVERSE(color_data) && !REG_VICIII_ATTRIBS)
						used_palette = altpalette;	// use the alternate palette from now in the scanline
					else
						used_palette = palette;		// we do this as well, since there can be "double GOTOX" so we want back to "original" palette ...
					if (SXA_4BIT_PER_PIXEL(color_data)) 	// this signals for rowmask [the rowmask itself is color_data & 0xFF]
						draw_mask = (color_data & (1 << char_row)) ? 0xFF : 0x00;	// draw_mask is $FF (not masked) _or_ $00 (masked) ~ for the current char_row!
					else
						draw_mask = 0xFF;
					continue;
				}
			}
			// Background and foreground colors
			//const Uint8 char_fgcolor = color_data & 0xF;	// FIXME: remove this! commented out since "&0xF" causes problems, replaced any char_fgcolor refs later with color_data refs
			const Uint16 char_id = REG_EBM ? (char_value & 0x3f) : char_value & 0x1fff; // up to 8192 characters (13-bit)
			const Uint8 char_bgcolor = REG_EBM ? vic_registers[0x21 + ((char_value >> 6) & 3)] : REG_SCREEN_COLOR;
			// FIXME: the commented line below seems not to work in a way as MEGA65 does, there is some disturbance in the force even on the MEGA65 it seems [?]
			//        This change seems to fix MegaPoly intro to allow it to work on Xemu as well. Suggested by Mirage_BD (the author of MegaPoly)
			// const Uint8 glyph_trim = SXA_TRIM_RIGHT_BITS012(char_value); // + (SXA_TRIM_RIGHT_BIT3(color_data) ? 8 : 0);
			const Uint8 glyph_trim = SXA_TRIM_RIGHT_BITS012(char_value) + ((SXA_TRIM_RIGHT_BIT3(color_data) & (SXA_4BIT_PER_PIXEL(color_data)>>1)) ? 8 : 0);
			// Default fetch from char mode.
			const int sel_char_row = (XEMU_UNLIKELY(SXA_VERTICAL_FLIP(color_data)) ? 7 - char_row : char_row);
			// Render character cell row
			if (SXA_4BIT_PER_PIXEL(color_data)) {	// 16-color character
				vic4_render_16color_char_row(
					main_ram + (((char_id * 64) + ((sel_char_row + char_fetch_offset) * 8)) & 0x7FFFF),
					16 - glyph_trim,
					used_palette[char_bgcolor],		// bg SDL colour
					used_palette[color_data & 0xFF],	// fg SDL colour
					used_palette + (color_data & 0xF0),	// palette(16) pointer
					SXA_HORIZONTAL_FLIP(color_data)		// hflip?
				);
			} else if (CHAR_IS256_COLOR(char_id)) {	// 256-color character
				// fgcolor in case of FCM should mean colour index $FF
				// FIXME: check if the passed palette[color_data & 0xFF] is correct or another index should be used for that $FF colour stuff
				const Uint32 *palette_now = ((REG_VICIII_ATTRIBS) && SXA_ATTR_ALTPALETTE(color_data)) ? altpalette : used_palette;
				vic4_render_fullcolor_char_row(
					main_ram + (((char_id * 64) + ((sel_char_row + char_fetch_offset) * 8)) & 0x7FFFF),
					8 - glyph_trim,
					palette_now[char_bgcolor],		// bg SDL colour
					palette_now[color_data & 0xFF],		// fg SDL colour
					SXA_HORIZONTAL_FLIP(color_data),	// hflip?
					palette_now
				);
			} else if ((REG_MCM && (color_data & 8)) || (REG_MCM && REG_BMM)) {	// Multicolor character
				// using static vars: faster in a rapid loop like this, no need to re-adjust stack pointer all the time to allocate space and this way using constant memory address
				// also, as an optimization, later, some value can be re-used and not always initialized here, when in reality VIC
				// registers in current Xemu cannot change within a scanline anyway (ie, scanline precision based emulation/rendering)
				static Uint8 color_source_mcm[4];
				Uint8 char_byte;
				color_source_mcm[0] = REG_SCREEN_COLOR;
				if (REG_BMM) {
					// value 00 is common /w or w/o BMM so not initialized here
					color_source_mcm[1] = char_value >> 4;	// 01
					color_source_mcm[2] = char_value & 0xF;	// 10
					color_source_mcm[3] = color_data & 0xF;	// 11
					char_byte = *(row_data_base_addr + display_row * (LINESTEP_BYTES * 8) + 8 * line_char_index + sel_char_row);
				} else {
					// value 00 is common /w or w/o BMM so not initialized here
					color_source_mcm[1] = REG_MULTICOLOR_1;	// 01
					color_source_mcm[2] = REG_MULTICOLOR_2;	// 10
					color_source_mcm[3] = color_data & 7;	// 11
					char_byte = *(row_data_base_addr + (char_id * 8) + sel_char_row);
				}
				// FIXME: is this really a thing to have FLIP in bitmap mode AS WELL?!
				// FIXME: also this is WRONG, MCM data cannot be reversed with this table!!
				if (XEMU_UNLIKELY(SXA_HORIZONTAL_FLIP(color_data)))
					char_byte = reverse_byte_table[char_byte];
				vic4_render_multicolor_char_row(
					char_byte,
					8 - glyph_trim, // glyph_width
					color_source_mcm			// 4 element (legacy) MCM colour index table
				);
			} else {	// Single color character
				Uint8 char_byte, char_bgcolor_now, char_fgcolor_now;
				if (!REG_BMM) {
					char_bgcolor_now = char_bgcolor;
					char_fgcolor_now = color_data & 0xF;	// FIXME: is the "&" mask OK as being 0xF?
					char_byte = *(row_data_base_addr + (char_id * 8) + sel_char_row);
				} else {
					char_bgcolor_now = char_value & 0xF;
					char_fgcolor_now = char_value >> 4;
					char_byte = *(row_data_base_addr + display_row * (LINESTEP_BYTES * 8) + 8 * line_char_index + sel_char_row);
				}
				// FIXME: is this really a thing to have FLIP in bitmap mode AS WELL?!
				if (XEMU_UNLIKELY(SXA_HORIZONTAL_FLIP(color_data)))
					char_byte = reverse_byte_table[char_byte];
				// FIXME: do vic3 attributes work with bitmap mode as well???
				vic4_render_mono_char_row(
					char_byte,
					8 - glyph_trim,	// glyph_width
					char_bgcolor_now,			// bg colour index
					char_fgcolor_now,			// fg colour index
					(REG_VICIII_ATTRIBS && !REG_MCM) ? (color_data >> 4) : 0	// VIC3 hardware attribute info
				);
			}
			line_char_index++;
		}
	}
	if (++char_row > 7) {
		char_row = 0;
		display_row++;
	}
	// Fill screen color after chargen phase
	while (xcounter++ < border_x_right)
		*current_pixel++ = palette[REG_SCREEN_COLOR];
}


int vic4_render_scanline ( void )
{
	// Work this first. DO NOT OPTIMIZE EARLY.

	used_palette = palette;	// may be overriden later by GOTOX token!
	xcounter = 0;
	current_pixel = pixel_start + ycounter * TEXTURE_WIDTH;
	pixel_raster_start = current_pixel;

	SET_PHYSICAL_RASTER(ycounter);
	logical_raster = ycounter >> (EFFECTIVE_V400 ? 0 : 1);

	// FIXME: this is probably a bad fix ... Trying to remedy that in V400, no raster interrupts seems to work ... XXX
	if (!(ycounter & 1) || EFFECTIVE_V400) // VIC2 raster source: shall we check FNRST?
		check_raster_interrupt(logical_raster);
	// "Double-scan hack"
	// FIXME: is this really correct? ie even sprites cannot be set to Y pos finer than V200 or ...
	// ... having resolution finer than V200 with some "VIC4 magic"?
	if (!EFFECTIVE_V400 && (ycounter & 1)) {
		if (XEMU_UNLIKELY(configdb.show_scanlines)) {
			for (int i = 0; i < TEXTURE_WIDTH; i++, current_pixel++)
				*current_pixel = ((*(current_pixel - TEXTURE_WIDTH) >> 1) & 0x7F7F7F7FU) | black_colour;	// "| black_colour" is used to correct the messed-up alpha channel to $FF
		} else {
			memcpy(current_pixel, current_pixel - TEXTURE_WIDTH, TEXTURE_WIDTH * 4);
			current_pixel += TEXTURE_WIDTH;
		}
	} else {
		// Top and bottom borders
		if (ycounter < BORDER_Y_TOP || ycounter >= BORDER_Y_BOTTOM || !REG_DISPLAYENABLE) {
			for (int i = 0; i < TEXTURE_WIDTH; i++)
				*(current_pixel++) = palette[REG_BORDER_COLOR];
		}
		if (ycounter >= CHARGEN_Y_START && ycounter < BORDER_Y_BOTTOM) {
			// Render chargen area and render side-borders later to cover X-displaced
			// character generator if needed.  Chargen area maybe covered by top/bottom
			// borders also if y-offset applies.
			xcounter += border_x_left;
			current_pixel += border_x_left;
			if (XEMU_LIKELY(!(vic_registers[0x31] & 0x10)))
				vic4_render_char_raster();
			else
				vic4_render_bitplane_raster();
#			ifdef SPRITE_SPRITE_COLLISION
			memset(is_sprite, 0, sizeof is_sprite);
#			endif
		}
		// Paint screen color if positive y-offset (CHARGEN_Y_START > BORDER_Y_TOP)
		// FIXME: in case of changed palette by GOTOX, maybe this must be dependent on bg_paint to use the new palette or the old??
		if (ycounter >= BORDER_Y_TOP) {
			if (ycounter < CHARGEN_Y_START)
				while (xcounter++ < border_x_right)
					*current_pixel++ = palette[REG_SCREEN_COLOR];
		}
		for (Uint32 *p = pixel_raster_start; p < pixel_raster_start + border_x_left; p++)
			*p = palette[REG_BORDER_COLOR];
		for (Uint32 *p = current_pixel; p < current_pixel + border_x_right; p++)
			*p = palette[REG_BORDER_COLOR];
	}
	// Sprites can be displayed on V200/V400 independently of char generator, so
	// this must be outside of the main loop to avoid being affected by double-scan

	if (XEMU_LIKELY(REG_DISPLAYENABLE) && (ycounter >= BORDER_Y_TOP && ycounter < BORDER_Y_BOTTOM)) {
		vic4_do_sprites();
		if (vic_registers[0x1E])		// sprite-sprite collision
			interrupt_status |= 4;
		else
			interrupt_status &= 255 - 4;
		if (vic_registers[0x1F])		// sprite-foreground collision
			interrupt_status |= 2;
		else
			interrupt_status &= 255 - 2;
		// I don't call interrupt_checker() as it will be on the next call of the current function.
		// That check then is part of function check_raster_interrupt. Yes a bit confusing and messy ... - LGB
	}

	ycounter++;
	// End of frame?
	if (ycounter == max_rasters) {
		vic4_reset_display_counters();
		static int blink_frame_counter = 0;
		blink_frame_counter++;
		if (blink_frame_counter == VIC4_BLINK_INTERVAL) {
			blink_frame_counter = 0;
			blink_phase = !blink_phase;
		}
		return 1;
	}
	return 0;
}


/* --- AUX FUNCTIONS FOR NON-ESSENTIAL THINGS (query current text screen parameters for other components, put/get screen content as ASCII) --- */


int vic4_query_screen_width ( void )
{
	return REG_H640 ? 80 : 40;
}


int vic4_query_screen_height ( void )
{
	return EFFECTIVE_V400 ? 50 : 25;
}


Uint8 *vic4_query_screen_address ( void )
{
	return main_ram + (SCREEN_ADDR & 0x7FFFF);
}


Uint8 *vic4_query_colour_address ( void )
{
	return colour_ram + COLOUR_RAM_OFFSET;
}


char *vic4_textshot ( void )
{
	char text[8192];
	char *result = xemu_cbm_screen_to_text(
		text,
		sizeof text,
		vic4_query_screen_address(),
		vic4_query_screen_width(),
		vic4_query_screen_height(),
		(vic_registers[0x18] & 2)	// lowercase font? try to auto-detect by checking selected address chargen addr, LSB
	);
	return result ? xemu_strdup(result) : NULL;
}


int vic4_textinsert ( const char *text )
{
	return xemu_cbm_text_to_screen(
		vic4_query_screen_address(),
		vic4_query_screen_width(),
		vic4_query_screen_height(),
		text,				// text buffer as input
		(vic_registers[0x18] & 2)	// lowercase font? try to auto-detect by checking selected address chargen addr, LSB
	);
}


void vic4_set_emulation_colour_effect ( int val )
{
	if (configdb.colour_effect != val) {
		if (val < 0)
			val = -val;	// negative value: to allow to set anyway, even if it was the previous one
		DEBUGPRINT("VIC4: setting XEMU-specific colour effect to %d" NL, val);
		configdb.colour_effect = val;
		vic4_revalidate_all_palette();
	}
}
