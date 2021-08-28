/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Copyright (C)2020-2021 Hernán Di Pietro <hernan.di.pietro@gmail.com>

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

#ifndef XEMU_MEGA65_VIC4_H_INCLUDED
#define XEMU_MEGA65_VIC4_H_INCLUDED

#define VIC2_IOMODE	0
#define VIC3_IOMODE	1
#define VIC_BAD_IOMODE	2
#define VIC4_IOMODE	3

// Horizontal sync frequencies (in Hertz) for NTSC and PAL video output of MEGA65. Must be float.
#define PAL_LINE_FREQ	31250.0
#define NTSC_LINE_FREQ	31468.5
// Frame times (in microseconds) for NTSC and PAL video output of MEGA65. Must be integer.
#define PAL_FRAME_TIME	20000
#define NTSC_FRAME_TIME	((int)(16683.35))

// Output window is fixed at 800x600 to support MegaPHONE, PAL-MEGA65
// and NTSC_MEGA65 modes. Internally, the VIC-IV chip draws 800-pixel
// wide buffers and traverses up to 526/624 physical rasters, as we do here.
// The user can select a clipped borders view (called "normal borders") which shows
// the real visible resolution of PAL (720x576) or NSTC(720x480).

#define TEXTURE_WIDTH			800
#define TEXTURE_HEIGHT			625

#define PHYSICAL_RASTERS_DEFAULT	PHYSICAL_RASTERS_NTSC
#define SCREEN_HEIGHT_VISIBLE_DEFAULT	SCREEN_HEIGHT_VISIBLE_NTSC
#define SCREEN_HEIGHT_VISIBLE_NTSC	480
#define SCREEN_HEIGHT_VISIBLE_PAL	576
#define PHYSICAL_RASTERS_NTSC		526
#define PHYSICAL_RASTERS_PAL		624
#define FRAME_H_FRONT			0
#define RASTER_CORRECTION		3
#define VIC4_BLINK_INTERVAL		30

// Register defines
//
// Ref:
// https://github.com/MEGA65/mega65-core/blob/138-hdmi-audio-27mhz/iomap.txt
// ----------------------------------------------------
// _Un  suffix indicates upper n bits of register

#define REG_EBM				(vic_registers[0x11] & 0x40)
#define REG_MCM				(vic_registers[0x16] & 0x10)
#define REG_BMM				(vic_registers[0x11] & 0x20)
#define REG_SPRITE_ENABLE		vic_registers[0x15]
#define REG_BORDER_COLOR		(vic_registers[0x20] & vic_color_register_mask)
#define REG_SCREEN_COLOR		(vic_registers[0x21] & vic_color_register_mask)
#define REG_MULTICOLOR_1		(vic_registers[0x22] & vic_color_register_mask)
#define REG_MULTICOLOR_2		(vic_registers[0x23] & vic_color_register_mask)
//#define REG_MULTICOLOR_3		(vic_registers[0x24] & vic_color_register_mask)
#define REG_H640			(vic_registers[0x31] & 128)
#define REG_V400			(vic_registers[0x31] & 8)
#define REG_VICIII_ATTRIBS		(vic_registers[0x31] & 0x20)
#define REG_RSEL			(vic_registers[0x11] & 8)
#define REG_CSEL			(vic_registers[0x16] & 8)
#define REG_DISPLAYENABLE		(vic_registers[0x11] & 0x10)
#define REG_VIC2_XSCROLL		(vic_registers[0x16] & 7)
#define REG_VIC2_YSCROLL		(vic_registers[0x11] & 7)
//#define REG_CRAM2K			(vic_registers[0x30] & 1)
#define REG_TBRDPOS			(vic_registers[0x48])
#define REG_SPRBPMEN_0_3		(vic_registers[0x49] >> 4)
#define REG_SPRBPMEN_4_7		(vic_registers[0x4B] >> 4)
#define REG_TBRDPOS_U4			(vic_registers[0x49] & 0xF)
#define REG_BBRDPOS			(vic_registers[0x4A])
#define REG_BBRDPOS_U4			(vic_registers[0x4B] & 0xF)
#define REG_TEXTXPOS			(vic_registers[0x4C])
#define REG_TEXTXPOS_U4			(vic_registers[0x4D] & 0xF)
#define REG_SPRTILEN			((vic_registers[0x4D] >> 4) | (vic_registers[0x4F] & 0xF0))
#define REG_TEXTYPOS			(vic_registers[0x4E])
#define REG_TEXTYPOS_U4			(vic_registers[0x4F] & 0xF)
//#define REG_XPOS			(vic_registers[0x51])
//#define REG_XPOS_U6			(vic_registers[0x50] & 0x3F)
#define REG_FNRST			(vic_registers[0x53] & 0x80)
#define REG_16BITCHARSET		(vic_registers[0x54] & 1)
#define REG_FCLRLO			(vic_registers[0x54] & 2)
#define REG_FCLRHI			(vic_registers[0x54] & 4)
#define REG_SPR640			(vic_registers[0x54] & 0x10)
#define REG_SPRHGHT			(vic_registers[0x56])
//#define REG_CHRXSCL			(vic_registers[0x5A])
#define REG_CHRYSCL			(vic_registers[0x5B])
#define REG_SIDBDRWD			(vic_registers[0x5C])
#define REG_SIDBDRWD_U5			(vic_registers[0x5D] & 0x3F)
#define REG_HOTREG			(vic_registers[0x5D] & 0x80)
#define REG_CHARSTEP			vic_registers[0x58]
#define REG_CHARSTEP_U8			vic_registers[0x59]
#define REG_CHARXSCALE			vic_registers[0x5A]
#define REG_CHRCOUNT			vic_registers[0x5E]
#define REG_SCRNPTR_B0			vic_registers[0x60]
#define REG_SCRNPTR_B1			vic_registers[0x61]
#define REG_SCRNPTR_B2			vic_registers[0x62]
#define REG_SCRNPTR_B3			(vic_registers[0x63] & 0xF)
#define REG_COLPTR			vic_registers[0x64]
#define REG_COLPTR_MSB			vic_registers[0x65]
#define REG_CHARPTR_B0			vic_registers[0x68]
#define REG_CHARPTR_B1			vic_registers[0x69]
#define REG_CHARPTR_B2			vic_registers[0x6A]
#define REG_SPRPTR_B0			vic_registers[0x6C]
#define REG_SPRPTR_B1			vic_registers[0x6D]
#define REG_SPRPTR_B2			(vic_registers[0x6E] & 0x7F)
//#define REG_SCREEN_ROWS		vic_registers[0x7B]
//#define REG_PAL_RED_BASE		(vic_registers[0x100])
//#define REG_PAL_GREEN_BASE		(vic_registers[0x200])
//#define REG_PAL_BLUE_BASE		(vic_registers[0x300])

// Helper macros for accessing multi-byte registers
// and other similar functionality for convenience

//#define PHYS_RASTER_COUNT		(videostd_id ? NTSC_PHYSICAL_RASTERS : PAL_PHYSICAL_RASTERS)
#define SINGLE_SIDE_BORDER		(((Uint16)REG_SIDBDRWD) | (REG_SIDBDRWD_U5) << 8)
#define BORDER_Y_TOP			(((Uint16)REG_TBRDPOS) | (REG_TBRDPOS_U4) << 8)
#define BORDER_Y_BOTTOM			(((Uint16)REG_BBRDPOS) | (REG_BBRDPOS_U4) << 8)
#define CHARGEN_Y_START			(((Uint16)REG_TEXTYPOS) | (REG_TEXTYPOS_U4) << 8)
#define CHARGEN_X_START			(((Uint16)REG_TEXTXPOS) | (REG_TEXTXPOS_U4) << 8)
#define CHARSTEP_BYTES			(((Uint16)REG_CHARSTEP) | (REG_CHARSTEP_U8) << 8)
//#define SCREEN_RAM_ADDR_VIC		(REG_SCREEN_ADDR * 1024)
#define SCREEN_ADDR			((Uint32)REG_SCRNPTR_B0 | (REG_SCRNPTR_B1<<8) | (REG_SCRNPTR_B2 <<16) | (REG_SCRNPTR_B3 << 24))
#define CHARSET_ADDR			((Uint32)REG_CHARPTR_B0 | (REG_CHARPTR_B1<<8) | (REG_CHARPTR_B2 <<16))
#define VIC2_BITMAP_ADDR		((CHARSET_ADDR) & 0xFFE000)
#define SPRITE_POINTER_ADDR		((Uint32)REG_SPRPTR_B0  | (REG_SPRPTR_B1<<8)  | (REG_SPRPTR_B2 <<16))
#define COLOUR_RAM_OFFSET		((((Uint16)REG_COLPTR) | (REG_COLPTR_MSB) << 8))
//#define IS_NTSC_MODE			(videostd_id)
//#define SCREEN_STEP			(((Uint16)REG_CHARSTEP) | (REG_CHARSTEP_U8) << 8)
#define SPRITE_POS_Y(n)			(vic_registers[1 + (n)*2])
#define SPRITE_POS_X(n)			(((Uint16)vic_registers[(n)*2]) | ( (vic_registers[0x10] & (1 << (n)) ? 0x100 : 0)))
#define SPRITE_COLOR(n)			(vic_registers[0x27+(n)] & vic_color_register_mask)
#define SPRITE_COLOR_4BIT(n)		(vic_registers[0x27+(n)] & 0xF)
#define SPRITE_MULTICOLOR_1		(vic_registers[0x25] & vic_color_register_mask)
#define SPRITE_MULTICOLOR_2		(vic_registers[0x26] & vic_color_register_mask)
#define SPRITE_IS_BACK(n)		(vic_registers[0x1B] & (1 << (n)))
#define SPRITE_HORZ_2X(n)		(vic_registers[0x1D] & (1 << (n)))
#define SPRITE_VERT_2X(n)		(vic_registers[0x17] & (1 << (n)))
#define SPRITE_MULTICOLOR(n)		(vic_registers[0x1C] & (1 << (n)))
#define SPRITE_16COLOR(n)		(vic_registers[0x6B] & (1 << (n)))
#define SPRITE_EXTWIDTH(n)		(SPRITE_16COLOR(n) | (vic_registers[0x57] & (1 << (n))))
#define SPRITE_EXTHEIGHT(n)		(vic_registers[0x55] & (1 << (n)))
#define SPRITE_BITPLANE_ENABLE(n)	(((REG_SPRBPMEN_4_7) << 4 | REG_SPRBPMEN_0_3) & (1 << (n)))
#define SPRITE_16BITPOINTER		(vic_registers[0x6E] & 0x80)
//#define TEXT_MODE			(!REG_BMM)
//#define HIRES_BITMAP_MODE		(REG_BMM & !REG_MCM & !REG_EBM)
//#define MULTICOLOR_BITMAP_MODE	(REG_BMM & REG_MCM & !REG_EBM)
#define VIC3_ATTR_BLINK(c)		((c) & 0x1)
#define VIC3_ATTR_REVERSE(c)		((c) & 0x2)
#define VIC3_ATTR_BOLD(c)		((c) & 0x4)
#define VIC3_ATTR_UNDERLINE(c)		((c) & 0x8)
#define CHAR_IS256_COLOR(ch)		(REG_FCLRLO && (ch) < 0x100) || (REG_FCLRHI && (ch) > 0x0FF)

// "Super-Extended character attributes" (see https://github.com/MEGA65/mega65-core/blob/master/docs/viciv-modes.md)
// cw is color-word (16-bit from Color RAM). sw is screen-ram-word (16bit from Screen RAM)

#define SXA_TRIM_RIGHT_BITS012(sw)	((sw) >> 13)
#define SXA_VERTICAL_FLIP(cw)		((cw) & 0x8000)
#define SXA_HORIZONTAL_FLIP(cw)		((cw) & 0x4000)
//#define SXA_ALPHA_BLEND(cw)		((cw) & 0x2000)
#define SXA_GOTO_X(cw)			((cw) & 0x1000)
#define SXA_4BIT_PER_PIXEL(cw)		((cw) & 0x0800)
#define SXA_TRIM_RIGHT_BIT3(cw)		((cw) & 0x0400)
#define SXA_ATTR_BOLD(cw)		((cw) & 0x0040)
#define SXA_ATTR_REVERSE(cw)		((cw) & 0x0020)
//#define SXA_TRIM_TOP_BOTTOM(cw)	(((cw) & 0x0300) >> 8)


// Multi-byte register write helpers

#define SET_11BIT_REG(basereg,x) do { \
		vic_registers[((basereg)+1)] = (Uint8) ((((Uint16)(x)) & 0x700) >> 8); \
		vic_registers[(basereg)] = (Uint8) ((Uint16)(x)) & 0x00FF; \
	} while(0)
#define SET_12BIT_REG(basereg,x) do { \
		vic_registers[((basereg)+1)] = (Uint8) ((((Uint16)(x)) & 0xF00) >> 8); \
		vic_registers[(basereg)] = (Uint8) ((Uint16)(x)) & 0x00FF; \
	} while(0)
#define SET_14BIT_REG(basereg,x) do { \
		vic_registers[((basereg)+1)] = (Uint8) ((((Uint16)(x)) & 0x3F00) >> 8); \
		vic_registers[(basereg)] = (Uint8) ((Uint16)(x)) & 0x00FF; \
	} while(0)
/* #define SET_14BIT_REGI(basereg,x) do { \
		vic_registers[(basereg)] = (Uint8) ((((Uint16)(x)) & 0x3F00) >> 8); \
		vic_registers[((basereg)+1)] = (Uint8) ((Uint16)(x)) & 0x00FF; \
	} while(0) */
#define SET_16BIT_REG(basereg,x) do { \
		vic_registers[((basereg) + 1)] = (Uint8) ((((Uint16)(x)) & 0xFF00) >> 8); \
		vic_registers[(basereg)]= ((Uint16)(x)) & 0x00FF; \
	} while(0)

// 11-bit registers

#define SET_PHYSICAL_RASTER(x)		SET_11BIT_REG(0x52, (x))
//#define SET_RASTER_XPOS(x)		SET_14BIT_REGI(0x50, (x))

// 12-bit registers

#define SET_BORDER_Y_TOP(x)		SET_12BIT_REG(0x48, (x))
#define SET_BORDER_Y_BOTTOM(x)		SET_12BIT_REG(0x4A, (x))
#define SET_CHARGEN_X_START(x)		SET_12BIT_REG(0x4C, (x))
#define SET_CHARGEN_Y_START(x)		SET_12BIT_REG(0x4E, (x))

//16-bit registers

#define SET_COLORRAM_BASE(x)		SET_16BIT_REG(0x64,(x))
#define SET_CHARSTEP_BYTES(x)		SET_16BIT_REG(0x58,(x))

// Review this! (VIC-II values)

#define SPRITE_X_BASE_COORD		24
#define SPRITE_Y_BASE_COORD		50
//#define SPRITE_X_UPPER_COORD		250
//#define SPRITE_Y_UPPER_COORD		344

// Current state

extern int   vic_iomode;
//extern int   scanline;
extern Uint8 vic_registers[];
extern Uint8 c128_d030_reg;

extern const char *videostd_name;
extern int   videostd_frametime;
extern int   videostd_changed;
extern Uint8 videostd_id;
extern float videostd_1mhz_cycles_per_scanline;
extern int   vic_readjust_sdl_viewport;

extern int   vic_vidp_legacy, vic_chrp_legacy, vic_sprp_legacy;

extern const char *iomode_names[4];

extern void  vic_init ( void );
extern void  vic_reset ( void );
extern void  vic_write_reg ( unsigned int addr, Uint8 data );
extern Uint8 vic_read_reg  ( unsigned int addr );
extern int   vic4_render_scanline ( void );
extern void  vic4_open_frame_access ( void );
extern void  vic4_close_frame_access (void );

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_snapshot.h"
extern int vic4_snapshot_load_state ( const struct xemu_snapshot_definition_st *def , struct xemu_snapshot_block_st *block );
extern int vic4_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
