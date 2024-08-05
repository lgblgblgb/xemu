/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2017-2024 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_MEMORY_MAPPER_H_INCLUDED
#define XEMU_MEGA65_MEMORY_MAPPER_H_INCLUDED

extern void memory_init ( void );
extern void memory_reset_unwritten_debug_stat ( void );
extern void memory_set_rom_protection ( const bool protect );
extern void memory_reconfigure (
	const Uint8 d030_value, const Uint8 new_io_mode, const Uint8 new_cpu_port0, const Uint8 new_cpu_port1,
	const Uint32 new_map_mb_lo, const Uint32 new_map_ofs_lo,
	const Uint32 new_map_mb_hi, const Uint32 new_map_ofs_hi,
	const Uint8 new_map_mask,
	const bool new_in_hypervisor
);
extern Uint8 memory_get_cpu_io_port ( const Uint8 addr );
extern void memory_set_io_mode ( const Uint8 new_io_mode );
extern void memory_write_d030 ( const Uint8 data );

extern int    memory_cpu_addr_to_desc   ( const Uint16 cpu_addr, char *p, const unsigned int n );
extern Uint32 memory_cpu_addr_to_linear ( const Uint16 cpu_addr, Uint32 *wr_addr_p );

// Non- CPU or DMA emulator memory acceses ("debug" read/write CPU/linear memory bytes)
extern Uint8 debug_read_linear_byte   ( const Uint32 addr32 );
extern Uint8 sdebug_read_linear_byte  ( const Uint32 addr32 );
extern void  debug_write_linear_byte  ( const Uint32 addr32, const Uint8 data );
extern void  sdebug_write_linear_byte ( const Uint32 addr32, const Uint8 data );
// debug read/write CPU address functions: other than hardware emulation, these must be used for debug purposes (monitor/debugger, etc)
extern Uint8 debug_read_cpu_byte  ( const Uint16 addr16 );
extern void  debug_write_cpu_byte ( const Uint16 addr16, const Uint8 data );

// DMA implementation related, used by dma65.c:
extern Uint8 memory_dma_source_mreader ( const Uint32 addr32 );
extern void  memory_dma_source_mwriter ( const Uint32 addr32, const Uint8 data );
extern Uint8 memory_dma_target_mreader ( const Uint32 addr32 );
extern void  memory_dma_target_mwriter ( const Uint32 addr32, const Uint8 data );
extern Uint8 memory_dma_list_reader    ( const Uint32 addr32 );

// MAP related variables, do not change these values directly!
extern Uint32 map_offset_low, map_offset_high, map_megabyte_low, map_megabyte_high;
extern Uint8  map_mask;

extern Uint8 main_ram[512 << 10], colour_ram[0x8000], char_ram[0x2000], hypervisor_ram[0x4000];
extern Uint32 main_ram_size;
#define SLOW_RAM_SIZE (8 << 20)
extern Uint8 attic_ram[SLOW_RAM_SIZE];

extern Uint8 io_mode;			// "VIC" I/O mode: do not change this value directly!

#define I2C_UUID_OFFSET		0x100
#define I2C_UUID_SIZE		8
#define I2C_RTC_OFFSET		0x110
#define I2C_RTC_SIZE		7
#define I2C_NVRAM_OFFSET	0x140
#define I2C_NVRAM_SIZE		64
extern Uint8 i2c_regs[0x1000];

extern int skip_unhandled_mem;
extern int cpu_rmw_old_data;

#endif
