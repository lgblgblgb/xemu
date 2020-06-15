/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Copyright (C)2020 Hernán Di Pietro <hernan.di.pietro@gmail.com>

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

#ifndef __XEMU_MEGA65_VIC4_H_INCLUDED
#define __XEMU_MEGA65_VIC4_H_INCLUDED
#include <SDL_types.h>

#define VIC2_IOMODE	0
#define VIC3_IOMODE	1
#define VIC_BAD_IOMODE	2
#define VIC4_IOMODE	3

#define SCREEN_WIDTH		      800
#define SCREEN_HEIGHT_NTSC    506
#define SCREEN_HEIGHT_PAL     603
#define SCREEN_HEIGHT	      SCREEN_HEIGHT_PAL
#define NTSC_PHYSICAL_RASTERS 526
#define PAL_PHYSICAL_RASTERS  624
#define FRAME_H_FRONT         0
#define RASTER_CORRECTION     3
#define DISPLAY_FETCH_START   719
#define VIC4_BLINK_INTERVAL   25

// Register defines 
// 
// Ref: 
// https://github.com/MEGA65/mega65-core/blob/138-hdmi-audio-27mhz/iomap.txt
// ----------------------------------------------------
// _Un  suffix indicates upper n bits of register
//
#define REG_EBM             (vic_registers[0x11] & 0x40)
#define REG_MCM             (vic_registers[0x16] & 0x10)
#define REG_BMM             (vic_registers[0x11] & 0x20)
#define REG_SPRITE_ENABLE   vic_registers[0x15]
#define REG_BORDER_COLOR    vic_registers[0x20]
#define REG_SCREEN_COLOR    vic_registers[0x21]
#define REG_MULTICOLOR_1    vic_registers[0x22]
#define REG_MULTICOLOR_2    vic_registers[0x23]
#define REG_MULTICOLOR_3    vic_registers[0x24]
#define REG_H640            (vic_registers[0x31] & 128)
#define REG_V400            (vic_registers[0x31] & 8)
#define REG_VICIII_ATTRIBS  (vic_registers[0x31] & 0x20)
#define REG_RSEL            (vic_registers[0x16] & 8)
#define REG_CSEL            (vic_registers[0x11] & 8)
#define REG_DISPLAYENABLE   (vic_registers[0x11] & 0x10)
#define REG_VIC2_XSCROLL    (vic_registers[0x16] & 7)
#define REG_VIC2_YSCROLL    (vic_registers[0x11] & 7)
#define REG_CRAM2K          (vic_registers[0x30] & 1)
#define REG_TBRDPOS         (vic_registers[0x48])
#define REG_TBRDPOS_U4      (vic_registers[0x49] & 0xF)
#define REG_BBRDPOS         (vic_registers[0x4A])
#define REG_BBRDPOS_U4      (vic_registers[0x4B] & 0xF)
#define REG_TEXTXPOS        (vic_registers[0x4C])
#define REG_TEXTXPOS_U4     (vic_registers[0x4D] & 0xF)
#define REG_TEXTYPOS        (vic_registers[0x4E])
#define REG_TEXTYPOS_U4     (vic_registers[0x4F] & 0xF)
#define REG_16BITCHARSET    (vic_registers[0x54] & 1)
#define REG_FCLRLO          (vic_registers[0x54] & 2)
#define REG_FCLRHI          (vic_registers[0x54] & 4)
#define REG_CHRXSCL         (vic_registers[0x5A])
#define REG_CHRYSCL         (vic_registers[0x5B])
#define REG_SIDBDRWD        (vic_registers[0x5C])
#define REG_SIDBDRWD_U5     (vic_registers[0x5D] & 0x3F)
#define REG_HOTREG          (vic_registers[0x5D] & 0x80)
#define REG_CHARSTEP        vic_registers[0x58]
#define REG_CHARSTEP_U8     vic_registers[0x59]
#define REG_CHARXSCALE      vic_registers[0x5A]
#define REG_CHRCOUNT        vic_registers[0x5E]
#define REG_SCRNPTR_B0      vic_registers[0x60]
#define REG_SCRNPTR_B1      vic_registers[0x61]
#define REG_SCRNPTR_B2      vic_registers[0x62]
#define REG_SCRNPTR_B3      vic_registers[0x63]
#define REG_COLPTR          vic_registers[0x64]
#define REG_COLPTR_MSB      vic_registers[0x65]
#define REG_CHARPTR_B0      vic_registers[0x68]
#define REG_CHARPTR_B1      vic_registers[0x69]
#define REG_CHARPTR_B2      vic_registers[0x6A]
#define REG_SPRPTR_B0       vic_registers[0x6C]
#define REG_SPRPTR_B1       vic_registers[0x6D]
#define REG_SPRPTR_B2       vic_registers[0x6E]
#define REG_SCREEN_ROWS     vic_registers[0x7B]
#define REG_PALNTSC         (vic_registers[0x6f] & 0x80)
#define REG_PAL_RED_BASE    (vic_registers[0x100])
#define REG_PAL_GREEN_BASE  (vic_registers[0x200])
#define REG_PAL_BLUE_BASE   (vic_registers[0x300])


// Helper macros for accessing multi-byte registers
// and other similar functionality for convenience
// -----------------------------------------------------
#define PHYS_RASTER_COUNT   (REG_PALNTSC ? NTSC_PHYSICAL_RASTERS : PAL_PHYSICAL_RASTERS)
#define SINGLE_SIDE_BORDER  (((Uint16)REG_SIDBDRWD) | (REG_SIDBDRWD_U5) << 8)
#define BORDER_Y_TOP        (((Uint16)REG_TBRDPOS) | (REG_TBRDPOS_U4) << 8)
#define BORDER_Y_BOTTOM     (((Uint16)REG_BBRDPOS) | (REG_BBRDPOS_U4) << 8)
#define CHARGEN_Y_START     (((Uint16)REG_TEXTYPOS) | (REG_TEXTYPOS_U4) << 8)
#define CHARGEN_X_START     (((Uint16)REG_TEXTXPOS) | (REG_TEXTXPOS_U4) << 8)
#define SCREEN_RAM_ADDR_VIC  (REG_SCREEN_ADDR * 1024)
#define SCREEN_ADDR          ((Uint32)REG_SCRNPTR_B0 | (REG_SCRNPTR_B1<<8) | (REG_SCRNPTR_B2 <<16))
#define CHARSET_ADDR         ((Uint32)REG_CHARPTR_B0 | (REG_CHARPTR_B1<<8) | (REG_CHARPTR_B2 <<16))
#define VIC2_BITMAP_ADDR     ((Uint32)REG_CHARPTR_B0 | (REG_CHARPTR_B1<<8) | (REG_CHARPTR_B2 <<16))
#define SPRITE_POINTER_ADDR  ((Uint32)REG_SPRPTR_B0  | (REG_SPRPTR_B1<<8)  | (REG_SPRPTR_B2 <<16))
#define COLOR_RAM_ADDR       ((((Uint16)REG_COLPTR) | (REG_COLPTR_MSB) << 8) + 0xFF80000)
#define IS_PAL_MODE          (REG_PALNTSC ^ 0x80)
#define SCREEN_STEP          (((Uint16)REG_CHARSTEP) | (REG_CHARSTEP_U8) << 8)
#define SPRITE_POS_Y(n)      (vic_registers[1 + (n)*2])
#define SPRITE_POS_X(n)      (((Uint16)vic_registers[(n)*2]) | ( (vic_registers[0x10] & (1 << (n)) ? 0x100 : 0)))
#define SPRITE_COLOR(n)      (vic_registers[0x27+(n)] & 0xF)
#define SPRITE_MULTICOLOR_1  (vic_registers[0x25] & 0xF)
#define SPRITE_MULTICOLOR_2  (vic_registers[0x26] & 0xF)
#define SPRITE_IS_BACK(n)    (vic_registers[0x1B] & (1 << (n)))
#define SPRITE_HORZ_2X(n)    (vic_registers[0x1D] & (1 << (n)))
#define SPRITE_VERT_2X(n)    (vic_registers[0x17] & (1 << (n)))
#define SPRITE_MULTICOLOR(n) (vic_registers[0x1C] & (1 << (n)))
#define TEXT_MODE            (!REG_BMM)
#define HIRES_BITMAP_MODE    (REG_BMM & !REG_MCM & !REG_EBM)
#define MULTICOLOR_BITMAP_MODE (REG_BMM & REG_MCM & !REG_EBM)
#define VIC3_ATTR_BLINK(c)       ((c) & 0x1)
#define VIC3_ATTR_REVERSE(c)     ((c) & 0x2)     
#define VIC3_ATTR_BOLD(c)        ((c) & 0x4)
#define VIC3_ATTR_UNDERLINE(c)   ((c) & 0x8)
#define CHAR_IS256_COLOR(ch)     (REG_FCLRLO && (ch) < 0x100) || (REG_FCLRHI && (ch) > 0x0FF)

// "Super-Extended character attributes" (see https://github.com/MEGA65/mega65-core/blob/master/docs/viciv-modes.md)
// cw is color-word (16-bit from Color RAM). chw is character-word (16bit from Screen RAM)
#define SXA_TRIM_RIGHT_BITS012(chw) ((chw) >> 13)
#define SXA_TRIM_RIGHT_BIT3(cw)    ((cw) & 0x0400)
#define SXA_4BIT_PER_PIXEL(cw)     ((cw) & 0x0800)


// Multi-byte register write helpers
// ---------------------------------------------------
#define SET_11BIT_REG(basereg,x) vic_registers[(basereg+1)] = (Uint8) ((((Uint16)(x)) & 0x700) >> 8); \
                                 vic_registers[(basereg)] = (Uint8) ((Uint16)(x)) & 0x00FF;
#define SET_12BIT_REG(basereg,x) vic_registers[(basereg+1)] = (Uint8) ((((Uint16)(x)) & 0xF00) >> 8); \
                                 vic_registers[(basereg)] = (Uint8) ((Uint16)(x)) & 0x00FF;
#define SET_16BIT_REG(basereg,x) vic_registers[(basereg+1)] = ((Uint16)(x)) & 0xFF00; \
                                 vic_registers[(basereg)]= ((Uint16)(x)) & 0x00FF;

// 11-bit registers

#define SET_PHYSICAL_RASTER(x) SET_11BIT_REG(0x52, (x))

// 12-bit registers

                                 
#define SET_BORDER_Y_TOP(x)    SET_12BIT_REG(0x48, (x))
#define SET_BORDER_Y_BOTTOM(x) SET_12BIT_REG(0x4A, (x))
#define SET_CHARGEN_X_START(x) SET_12BIT_REG(0x4C, (x))
#define SET_CHARGEN_Y_START(x) SET_12BIT_REG(0x4E, (x))



//16-bit registers

#define SET_COLORRAM_BASE(x)       SET_16BIT_REG(REG_COLPTR,(x))
#define SET_VIRTUAL_ROW_WIDTH(x)   SET_16BIT_REG(REG_CHARSTEP,(x))

// Pixel foreground/background indicator for aiding in sprite rendering
#define FOREGROUND_PIXEL 1
#define BACKGROUND_PIXEL 0

// Review this! (VIC-II values)
#define SPRITE_X_BASE_COORD   24
#define SPRITE_Y_BASE_COORD   50
#define SPRITE_X_UPPER_COORD  250
#define SPRITE_Y_UPPER_COORD  344

// Current state
// -------------

extern int   vic_iomode;
extern int   scanline;
extern Uint8 vic_registers[];
extern int   cpu_cycles_per_scanline;
extern int   vic2_16k_bank;
extern int   force_fast;
extern Uint8 c128_d030_reg;

extern int   vic_vidp_legacy, vic_chrp_legacy, vic_sprp_legacy;

extern void  vic_init ( void );
extern void  vic_write_reg ( unsigned int addr, Uint8 data );
extern Uint8 vic_read_reg  ( unsigned int addr );
extern void  vic3_write_palette_reg ( int num, Uint8 data );
extern void  vic4_write_palette_reg ( int num, Uint8 data );
//extern void  vic_render_screen ( void );
extern int   vic4_render_scanline ( void );
extern void  vic3_check_raster_interrupt ( void );
extern void  vic4_open_frame_access();

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_snapshot.h"
extern int vic4_snapshot_load_state ( const struct xemu_snapshot_definition_st *def , struct xemu_snapshot_block_st *block );
extern int vic4_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
