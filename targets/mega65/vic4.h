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
// _Un  suffix indicates upper n bits of register
//
#define REG_D018_SCREEN_ADDR (vic_registers[0x18] >> 4)
#define REG_H640            (vic_registers[0x31] & 128)
#define REG_V400            (vic_registers[0x31] & 8)
#define REG_RSEL            (vic_registers[0x16] & 8)
#define REG_CSEL            (vic_registers[0x11] & 8)
#define REG_VIC2_XSCROLL    (vic_registers[0x16] & 7)
#define REG_VIC2_YSCROLL    (vic_registers[0x11] & 7)
#define REG_TBRDPOS         (vic_registers[0x48])
#define REG_TBRDPOS_U4      (vic_registers[0x49] & 0xF)
#define REG_BBRDPOS         (vic_registers[0x4A])
#define REG_BBRDPOS_U4      (vic_registers[0x4B] & 0xF)
#define REG_TEXTXPOS        (vic_registers[0x4C])
#define REG_TEXTXPOS_U4     (vic_registers[0x4D] & 0xF)
#define REG_TEXTYPOS        (vic_registers[0x4E])
#define REG_TEXTYPOS_U4     (vic_registers[0x4F] & 0xF)
#define REG_CHRXSCL         (vic_registers[0x5A])
#define REG_CHRYSCL         (vic_registers[0x5B])
#define REG_SIDBDRWD        (vic_registers[0x5C])
#define REG_SIDBDRWD_U5     (vic_registers[0x5D] & 0x3F)
#define REG_HOTREG          (vic_registers[0x5D] & 0x80)
#define REG_CHARSTEP        vic_registers[0x58]
#define REG_CHARSTEP_U8     vic_registers[0x59]
#define REG_CHRCOUNT        vic_registers[0x5E]
#define REG_SCRNPTR_BYTE0   (vic_registers[0x60])
#define REG_SCRNPTR_BYTE1   (vic_registers[0x61])
#define REG_SCRNPTR_BYTE2   (vic_registers[0x62])
#define REG_SCRNPTR_BYTE3   (vic_registers[0x63])
#define REG_COLPTR          (vic_registers[0x64])
#define REG_COLPTR_MSB      (vic_registers[0x65])
#define REG_VIC2_SPRPTRADR_BYTE0 (vic_registers[0x6C])
#define REG_VIC2_SPRPTRADR_BYTE1 (vic_registers[0x6D])
#define REG_VIC2_SPRPTRADR_BYTE2 (vic_registers[0x6E])
#define REG_PALNTSC         (vic_registers[0x6f] & 0x80)

// Helper macros for accessing multi-byte registers
// and other similar functionality for convenience
// -----------------------------------------------------
#define PHYS_RASTER_COUNT   (REG_PALNTSC ? NTSC_PHYSICAL_RASTERS : PAL_PHYSICAL_RASTERS)
#define SINGLE_SIDE_BORDER  (((Uint16)REG_SIDBDRWD) | (REG_SIDBDRWD_U5) << 8)
#define BORDER_Y_TOP        (((Uint16)REG_TBRDPOS) | (REG_TBRDPOS_U4) << 8)
#define BORDER_Y_BOTTOM     (((Uint16)REG_BBRDPOS) | (REG_BBRDPOS_U4) << 8)
#define CHARGEN_Y_START     (((Uint16)REG_TEXTYPOS) | (REG_TEXTYPOS_U4) << 8)
#define CHARGEN_X_START     (((Uint16)REG_TEXTXPOS) | (REG_TEXTXPOS_U4) << 8)

// Multi-byte register write helpers
// ---------------------------------------------------

#define SET_12BIT_REG(basereg,x) vic_registers[(basereg+1)] |= (Uint8) ((((Uint16)(x)) & 0xF00) >> 8); \
                                 vic_registers[(basereg)] = (Uint8) ((Uint16)(x)) & 0x00FF;
#define SET_16BIT_REG(basereg,x) vic_registers[(basereg+1)] |= ((Uint16)(x)) & 0xFF00; \
                                 vic_registers[(basereg)]= ((Uint16)(x)) & 0x00FF;
// 12-bit registers

                                 
#define SET_BORDER_Y_TOP(x)    SET_12BIT_REG(0x48, (x))
#define SET_BORDER_Y_BOTTOM(x) SET_12BIT_REG(0x4A, (x))
#define SET_CHARGEN_X_START(x) SET_12BIT_REG(0x4C, (x))
#define SET_CHARGEN_Y_START(x) SET_12BIT_REG(0x4E, (x))

//16-bit registers

#define SET_COLORRAM_BASE(x)       SET_16BIT_REG(REG_COLPTR,(x))
#define SET_VIRTUAL_ROW_WIDTH(x)   SET_16BIT_REG(REG_CHARSTEP,(x))

// 24-bit registers                               

#define SET_VIC2_SPRPTRADR(x)  REG_VIC2_SPRPTRADR_BYTE2 = ((Uint32)(x)) & 0xFF0000; \
                               REG_VIC2_SPRPTRADR_BYTE1 = ((Uint32)(x)) & 0xFF00; \
                               REG_VIC2_SPRPTRADR_BYTE0 = ((Uint32)(x)) & 0xFF;



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
extern int   vic4_render_scanline ( void );
extern void  vic3_check_raster_interrupt ( void );
extern void  vic4_open_frame_access();

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_snapshot.h"
extern int vic4_snapshot_load_state ( const struct xemu_snapshot_definition_st *def , struct xemu_snapshot_block_st *block );
extern int vic4_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
