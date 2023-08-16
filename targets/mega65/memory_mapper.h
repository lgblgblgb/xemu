/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2017-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
extern void memory_set_do_map ( void );
extern void memory_set_vic3_rom_mapping ( Uint8 value );
extern void memory_set_cpu_io_port ( int addr, Uint8 value );
extern void memory_set_cpu_io_port_ddr_and_data ( Uint8 p0, Uint8 p1 );
extern Uint8 memory_get_cpu_io_port ( int addr );

extern Uint8 memory_debug_read_phys_addr  ( int addr );
extern void  memory_debug_write_phys_addr ( int addr, Uint8 data );
extern Uint8 memory_debug_read_cpu_addr   ( Uint16 addr );
extern void  memory_debug_write_cpu_addr  ( Uint16 addr, Uint8 data );

// DMA implementation related, used by dma65.c:
extern Uint8 memory_dma_source_mreader ( int addr );
extern void  memory_dma_source_mwriter ( int addr, Uint8 data );
extern Uint8 memory_dma_target_mreader ( int addr );
extern void  memory_dma_target_mwriter ( int addr, Uint8 data );
extern Uint8 memory_dma_list_reader    ( int addr );

extern int map_mask, map_offset_low, map_offset_high, map_megabyte_low, map_megabyte_high;
extern int rom_protect, skip_unhandled_mem;
extern int etherbuffer_is_io_mapped;
extern Uint8 main_ram[512 << 10], colour_ram[0x8000], char_wom[0x2000], hypervisor_ram[0x4000];
#define SLOW_RAM_SIZE (8 << 20)
extern Uint8 slow_ram[SLOW_RAM_SIZE];

#define I2C_UUID_OFFSET		0x100
#define I2C_UUID_SIZE		8
#define I2C_RTC_OFFSET		0x110
#define I2C_RTC_SIZE		7
#define I2C_NVRAM_OFFSET	0x140
#define I2C_NVRAM_SIZE		64
extern Uint8 i2c_regs[0x1000];

extern int cpu_rmw_old_data;

static XEMU_INLINE void write_colour_ram ( const int addr, const Uint8 data )
{
	colour_ram[addr] = data;
	// we also need to update the corresponding part of the main RAM, if it's the first 2K of the colour RAM!
	if (addr < 2048)
		main_ram[addr + 0x1F800] = data;
}

#endif
