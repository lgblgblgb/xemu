/* F018 DMA core emulation for Commodore 65.
   Part of the Xemu project. https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_C65_DMA65_H_INCLUDED
#define XEMU_C65_DMA65_H_INCLUDED

/* Feature bit masks for dma_init(): */

#define DMA_FEATURE_DYNMODESET	0x100
#define DMA_FEATURE_MODULO	0x200

/* Variables */

extern Uint8 dma_status;
extern Uint8 dma_registers[16];
extern int   dma_chip_revision;
extern int   rom_date;

/* Functions: */

extern void  dma_write_reg	( int addr, Uint8 data );
extern Uint8 dma_read_reg	( int reg );
extern void  dma_init		( unsigned int revision );
extern void  dma_init_set_rev	( unsigned int revision, Uint8 *rom_ver_signature );
extern void  dma_reset		( void );
extern int   dma_update		( void );
extern int   dma_update_multi_steps ( int do_for_cycles );

extern void  detect_rom_date    ( Uint8 *p );

/* Things should be provided by the emulator: */

extern Uint8 DMA_SOURCE_IOREADER_FUNC	( int );
extern Uint8 DMA_SOURCE_MEMREADER_FUNC	( int );
extern Uint8 DMA_TARGET_IOREADER_FUNC	( int );
extern Uint8 DMA_TARGET_MEMREADER_FUNC	( int );
extern Uint8 DMA_LIST_READER_FUNC	( int );
extern void  DMA_SOURCE_IOWRITER_FUNC	( int, Uint8 );
extern void  DMA_SOURCE_MEMWRITER_FUNC	( int, Uint8 );
extern void  DMA_TARGET_IOWRITER_FUNC	( int, Uint8 );
extern void  DMA_TARGET_MEMWRITER_FUNC	( int, Uint8 );

/* Snapshot related part: */

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_snapshot.h"
extern int dma_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block );
extern int dma_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
