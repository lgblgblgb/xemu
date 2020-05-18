/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#define VIC2_IOMODE	0
#define VIC3_IOMODE	1
#define VIC_BAD_IOMODE	2
#define VIC4_IOMODE	3

#define SCREEN_WIDTH		      800
#define SCREEN_HEIGHT	      600
#define NTSC_PHYSICAL_RASTERS 526
#define PAL_PHYSICAL_RASTERS  624
#define FRAME_H_FRONT         16
#define RASTER_CORRECTION     4

// Register defines 
// ----------------------------------------------------
#define REG_H640            (vic_registers[0x31] & 128)
#define REG_V400            (vic_registers[0x31] & 8)
#define REG_RSEL            (vic_registers[0x16] & 8)
#define REG_CSEL            (vic_registers[0x11] & 8)
#define REG_VIC2_XSCROLL    (vic_registers[0x16] & 7)
#define REG_VIC2_YSCROLL    (vic_registers[0x11] & 7)
#define REG_TBRDPOS         (vic_registers[0x48])
#define REG_TBRDPOS_MSB     (vic_registers[0x49] & 7)
#define REG_BBRDPOS         (vic_registers[0x4A])
#define REG_BBRDPOS_MSB     (vic_registers[0x4B] & 7)
#define REG_TEXTXPOS        (vic_registers[0x4C])
#define REG_TEXTXPOS_MSB    (vic_registers[0x4D] & 7)
#define REG_TEXTYPOS        (vic_registers[0x4E])
#define REG_TEXTYPOS_MSB    (vic_registers[0x4F] & 7)
#define REG_CHRXSCL         (vic_registers[0x5A])
#define REG_CHRYSCL         (vic_registers[0x5B])
#define REG_SIDBDRWD        (vic_registers[0x5C])
#define REG_SIDBDRWD_MSB    (vic_registers[0x5D] & 0x3F)
#define REG_CHARSTEP_MSB    (vic_registers[0x58])
#define REG_CHARSTEP_LSB    (vic_registers[0x59])
#define REG_CHRCOUNT        (vic_registers[0x5E])

// Helper macros
// -----------------------------------------------------
#define PHYS_RASTER_COUNT   (vic_registers[0x6f] & 0x80 ? NSTC_PHYSICAL_RASTERS : PAL_PHYSICAL_RASTERS)
#define SINGLE_SIDE_BORDER  (((int)REG_SIDBDRWD_MSB << 8) | REG_SIDBDRWD)
#define BORDER_Y_TOP        (((int)REG_TBRDPOS_MSB << 8)  | REG_TBRDPOS)
#define BORDER_Y_BOTTOM     (((int)REG_BBRDPOS_MSB << 8)  | REG_BBRDPOS)
#define SET_BORDER_Y_TOP(x)    REG_TBRDPOS_MSB = (x) & 0xFF00; REG_TBRDPOS = (x) & 0x00FF;
#define SET_BORDER_Y_BOTTOM(x) REG_BBRDPOS_MSB = (x) & 0xFF00; REG_BBRDPOS = (x) & 0x00FF;
#define SET_CHARGEN_X_START(x) REG_TEXTXPOS_MSB = (x) & 0xFF00; REG_TEXTXPOS = (x) & 0x00FF;
#define SET_CHARGEN_Y_START(x) REG_TEXTYPOS_MSB = (x) & 0xFF00; REG_TEXTYPOS = (x) & 0x00FF;
#define VIRTUAL_ROW_WIDTH   ((int)REG_CHARSTEP_MSB << 8) | REG_CHARSTEP_LSB;

// VIC-IV Modeline Parameters
// ----------------------------------------------------
extern int     text_height_200;
extern int 	   text_height_400;
extern int  	text_height;
extern int  	chargen_y_scale_200;
extern int  	chargen_y_scale_400;
extern int  	chargen_y_pixels;
extern int  	top_borders_height_200;
extern int  	top_borders_height_400;
extern int  	single_top_border_200;
extern int  	single_top_border_400; 
extern int     border_x_left;
extern int     border_x_right;

// Current state
// -------------

extern int   vic_iomode;
extern int   scanline;
extern Uint8 vic_registers[];
extern int   cpu_cycles_per_scanline;
extern int   vic2_16k_bank;
extern int   vic3_blink_phase;
extern int   force_fast;
extern Uint8 c128_d030_reg;

extern int   vic_vidp_legacy, vic_chrp_legacy, vic_sprp_legacy;

extern void  vic_init ( void );
extern void  vic_write_reg ( unsigned int addr, Uint8 data );
extern Uint8 vic_read_reg  ( unsigned int addr );
extern void  vic3_write_palette_reg ( int num, Uint8 data );
extern void  vic4_write_palette_reg ( int num, Uint8 data );
//extern void  vic_render_screen ( void );
extern int   vic_render_scanline ( void );
extern void  vic3_check_raster_interrupt ( void );

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_snapshot.h"
extern int vic4_snapshot_load_state ( const struct xemu_snapshot_definition_st *def , struct xemu_snapshot_block_st *block );
extern int vic4_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
