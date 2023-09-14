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

// 512K is the max "main" RAM. Currently only 384K is used by M65
Uint8 main_ram[512 << 10];

// 32K of colour RAM. VIC-IV can see this as for colour information only. The first 2K can be seen at the last 2K of
// the chip-RAM. Also, the first 1 or 2K can be seen in the C64-style I/O area too, at $D800
Uint8 colour_ram[0x8000];
// Write-Only memory (WOM) for character fetch when it would be the ROM (on C64 eg)
// FUN FACT: WOM is not write-only any more :) But for "historical purposes" I containue to name it as "WOM" anyway ;)
Uint8 char_wom[0x2000];
// 16K of hypervisor RAM, can be only seen in hypervisor mode.
Uint8 hypervisor_ram[0x4000];
// I2C registers
Uint8 i2c_regs[0x1000];
// This is the 8Mbyte of "slow RAM", more commonly called "hyperRAM" (not to be confused with hypervisor RAM!) or "attic RAM"
Uint8 slow_ram[8 << 20];


#define PARANOID_MEMORY
//#define LAZY_MEMORY


typedef Uint8(*mem_rd_fnc_t)(unsigned int addr);
typedef void (*mem_wr_fnc_t)(unsigned int addr, Uint8 data);


struct mem_map_st {
	unsigned int	begin;
	unsigned int	end;
	Uint8		*mem_rd_ptr;
	Uint8		*mem_wr_ptr;
	mem_rd_fnc_t	mem_rd_fnc;
	mem_wr_fnc_t	mem_wr_fnc;
};

Uint8		*mem_rd_ptrs[MAX_MEM_SLOTS];
mem_rd_fnc_t	mem_rd_fnc[MAX_MEM_SLOTS];
Uint8		*mem_wr_ptrs[MAX_MEM_SLOTS];
mem_wr_fnc_t	mem_wr_fnc[MAX_MEM_SLOTS];


static XEMU_INLINE Uint8 cpu_read_byte ( Uint16 addr )
{
	const register unsigned int page = addr >> 8;
	const register Uint8 *p = mem_rd_ptrs[page];
	return XEMU_LIKELY(p) ? *(p + addr) : mem_rd_fncs[page](map_offsets[page] + addr);
}



#define DEFINE_LINEAR_BYTE_READER_CALLBACK(name,slot) \
	Uint8 name ( unsigned int addr ) \
	{ \
		if (XEMU_UNLIKELY(map_linears[slot] != (addr & 0xFFFF00U))) \
			phys_addr_decoder_for_linear_addressing(slot, addr); \
		return XEMU_LIKELY(mem_rd_ptrs[slot]) ? *(mem_rd_ptrs[slot] + (addr & 0xFF)) : mem_rd_fncs[slot](map_offsets[slot] + (addr & 0xFF)); \
	}



/* RULES:
   * The table must have zero for "begin" at the very first line, and UINT_MAX as the very last line
   * All entries must begin at an address with zero and end with $FF as the least significent byte
   * being and end addresses must be continuous with the next entry exactly
   * if rd or wr data ptr is NULL, then rd or wr function pointer is used (so then, they are cannot be NULL!)
   * if rd or wr data ptr is not NULL, then the function pointers will be ignored (can be NULL)
   * some special cases breaks the previous two rules above, see the comments with "**SPECIAL POLICY**"
*/
static const struct mem_map_st mem_map[] = {
	//BEGIN      END        READ-DATA-PTR       WRITE-DATA-PTR      READ-FUNC-PTR       WRITE-FUNC-PTR
	//---------- ---------- ------------------- ------------------- ------------------- ------------------
	{ 0x0000000, 0x001F7FF, main_ram,           main_ram,           NULL,               NULL               },
	// colour RAM _writer_ must be a function as it needs to update main_ram and colour_ram as well!
	{ 0x001F800, 0x001FFFF, main_ram + 0x1F800, NULL,               NULL,               colour_ram_writer  }, // colour RAM writing needs function ptr as it updates colour and "normal" RAMs as well!
	{ 0x0020000, 0x003FFFF, main_ram + 0x20000, main_ram + 0x20000, NULL,               ignore_rom_writer  }, // ROM, **SPECIAL POLICY**, needs writer func ptr as well!
	{ 0x0040000, 0x005FFFF, main_ram + 0x40000, main_ram + 0x40000, NULL,               NULL               },
	{ 0x0060000, 0x00FFFFF, NULL,               NULL,               dummy_reader,       dummy_writer       }, // C65/MEGA65 ROMs seems to try to access this area, so define it as "dummy"
	{ 0x4000000, 0x7FFFFFF, NULL,               NULL,               slowdev_reader,     slowdev_writer     }, // slow device memory area ~ cartridge
	{ 0x8000000, 0x87FFFFF, slow_ram,           slow_ram,           NULL,               NULL               }, // "slow RAM" aka "hyper RAM" aka "attic RAM" (8Mbyte)
	{ 0xFF7E000, 0xFF7FFFF, char_wom,           char_wom,           NULL,               NULL               },
	{ 0xFFD0000, 0xFFD3FFF, NULL,               NULL,               m65_io_reader,      m65_io_writer      },
	{ 0xFFF8000, 0xFFFBFFF, hypervisor_ram,     hypervisor_ram,     dummy_reader,       dummy_writer       }, // Hypervisor mem, **SPECIAL POLICY** needs reader & writer func ptrs as well!
	{ 0x10000000U, UINT_MAX, NULL,              NULL,               fatal_error_reader, fatal_error_writer }  // must be the last entry, DO NOT modify this!
};


#if !defined(XEMU_OFFICIAL_BUILD) || defined(PARANOID_MEMORY)
static inline void mem_map_consistency_check ( void )
{
	for (const struct mem_map_st *p = mem_map, *q = mem_map + (sizeof(mem_map) / sizeof(struct mem_map_st)) - 1, *l = NULL; !(p == q && p->end == UINT_MAX); l = p++)
		if ((p == q && p->end != UINT_MAX) || (l && p->begin != l->end + 1) || (!l && p->begin) || (p->begin & 0xFF) || ((p->end + 1) & 0xFF) || (b->begin >= p->end) || (p->begin < 0x10000000U && p->end >= 0x10000000U) || (p->begin >= 0x10000000U && (p->begin != 0x10000000U || p->end != UINT_MAX)))
			FATAL_ERROR("%(): bad mem_map near <0x%X...0x%X> definition", p->begin, p->end);
}
#endif


void memory_init ( void )
{
#if	!defined(XEMU_OFFICIAL_BUILD) || defined(PARANOID_MEMORY)
	mem_map_consistency_check();
#endif
}


static XEMU_INLINE const struct mem_map_st *_phys_addr_decoder_search ( const struct mem_map_st *p, const unsigned int addr )
{
	for (;;) {
		if (addr > p->end)
			p++;
		else if (addr < p->begin)
			p--;
		else
			return p;
	}
}


static XEMU_INLINE void _phys_addr_decoder_common ( const struct mem_map_st *p, const unsigned int minus_offset )
{
	mem_rd_ptrs[slot] = p->mem_rd_ptr - minus_offset;
	mem_wr_ptrs[slot] = p->mem_wr_ptr - minus_offset;
	mem_rd_fncs[slot] = p->mem_rd_fnc;
	mem_wr_fncs[slot] = p->mem_wr_fnc;
	mem_linears[slot] = addr;
	mem_offsets[slot] = addr - p->begin - minus_offset;
	mem_structs[slot] = p;
	if (p->begin == 0x20000 && rom_protect) {
		// in case of write protection on ROM, set wr data ptr to NULL, the table MUST contain a writer func ptr which takes effect then
		mem_wr_ptrs[slot] = NULL;
		mem_policies[slot] = 1;
	} else if (p->begin == 0xFFF8000 && !in_hypervisor) {
		// in case of hypervisor memory accessed from user mode, set rd&wr data ptrs to NULL, the table MUST contain reader&writer func ptrs which take effect then
		mem_rd_ptrs[slot] = NULL;
		mem_wr_ptrs[slot] = NULL;
		mem_policies[slot] = 2;
	} else {
		mem_policies[slot] = 0;
	}
}


// For CPU addressing: slot < 0x100
static void phys_addr_decoder_for_cpu_addressing ( const unsigned int slot, const unsigned int addr )
{
#ifdef	PARANOID_MEMORY
	if ((addr & ~0xFFFF00U))
		FATAL("%s(): invalid address $%X", addr);
#endif
	static const struct mem_map_st *p = mem_map;
	p = _phys_addr_decoder_search(p, addr);
	_phys_addr_decoder_common(p, slot << 8);
}


// For linear addressing: slot >= 0x100
static void phys_addr_decoder_for_linear_addressing ( const unsigned int slot, const unsigned int addr )
{
#ifdef	PARANOID_MEMORY
	if ((addr & ~0xFFFF00U))
		FATAL("%s(): invalid address $%X", addr);
#endif
	static const struct mem_map_st *p = mem_map;
	p = _phys_addr_decoder_search(p, addr);
	_phys_addr_decoder_common(p, 0);
}


#define CPU_MEM_MAP_UNKNOWN	0x80000000U
#define CPU_MEM_MAP_POLICY_RAM	0x80000001U
#define CPU_MEM_MAP_POLICY_ROM	0x80000002U
#define CPU_MEM_MAP_POLICY_IO	0x80000003U
static unsigned int policy_per_4k[16];	// yes, per 4K, technically MAP works on 8K basis, but the ROM mapping at $C000 and I/O at $D000 is only 4K





static void cpu_addr_decoder ( const unsigned int addr )
{
#ifdef	PARANOID_MEMORY
	if (addr > 0xFFFF)
		FATAL("%(): bad addr $%X", __func__, addr);
#endif
	const unsigned int slot = addr >> 8;
	const unsigned int policy = policy_per_4k[addr >> 12];
	switch (policy) {
		default:
			if (XEMU_LIKELY(policy < 0x10000000U))
				phys_addr_decoder_for_cpu_addressing(slot, (policy & 0xFF00000) + ((policy + addr) & 0xFFF00));
			else
				FATAL("%s(): unknown policy (%u) encountered at CPU address $%04X", __func__, policy, addr);
			break;
		// the rest is for UNMAP'ed regions!
		case CPU_MEM_MAP_POLICY_RAM:
			if (XEMU_UNLIKELY(!slot)) {
				// Needs special care! Since the CPU I/O port is here, we need special function pointers
				mem_rd_ptrs[0] = NULL;
				mem_wr_ptrs[0] = NULL;
				mem_rd_fncs[0] = zero_page_reader;
				mem_wr_fncs[0] = zero_page_writer;
			} else {
				mem_rd_ptrs[slot] = main_ram;
				mem_wr_ptrs[slot] = main_ram;
			}
			mem_linears[slot] = addr & 0xFF00;
			break;
		case CPU_MEM_MAP_POLICY_ROM:
			mem_rd_ptrs[slot] = main_ram + 0x20000;
			mem_wr_ptrs[slot] = main_ram;			// write RAM "behind" ROM!
			mem_linears[slot] = (addr & 0xFF00) + 0x20000;
			break;
		case CPU_MEM_MAP_POLICY_IO:
			mem_rd_ptrs[slot] = NULL;
			mem_wr_ptrs[slot] = NULL;
			mem_rd_fncs[slot] = legacy_io_reader;
			mem_wr_fncs[slot] = legacy_io_writer;
			mem_linears[slot] = 0xD000;
			break;
	}
}


static Uint8 resolve_via_cpu_read ( unsigned int addr )
{
#ifdef	PARANOID_MEMORY
	if (addr > 0xFFFF)
		FATAL("%s(): address $%X is bigger than $FFFF", __func__, addr);
#endif
	cpu_addr_decoder(addr);
	return cpu_read_byte(addr);
}


static void resolve_via_cpu_write ( unsigned int addr, Uint8 data )
{
#ifdef	PARANOID_MEMORY
	if (addr > 0xFFFF)
		FATAL("%s(): address $%X is bigger than $FFFF", __func__, addr);
#endif
	cpu_addr_decoder(addr);
	cpu_write_byte(addr, data);
}


static void invalidate_slots ( unsigned int slot, unsigned int n )
{
	while (n--) {
#ifdef		LAZY_MEMORY
		mem_rd_ptrs[slot] = NULL;
		mem_wr_ptrs[slot] = NULL;
		mem_rd_fncs[slot] = resolve_via_cpu_read;	// Only called for slots < 0x100, should be OK to always set
		mem_wr_fncs[slot] = resolve_via_cpu_write;	// -- "" --
		mem_linears[slot] = UINT_MAX;
		mem_offsets[slot] = 0;
		mem_policies[slot] = 0;
		slot++;
#else
		if (slot < 0x100) {
			cpu_addr_decoder(slot++ << 8);
		} else {
			mem_linears[slot] = UINT_MAX;	// FIXME:
		}
#endif
	}
}


static void invalidate_all_slots_by_policy ( int policy )
{
	for (int n = 0; n < MAX_MEM_SLOTS; n++) {
		if (mem_policies[n] == policy)
			invalidate_slots(n, 1);
	}
}


void mem_set_rom_protection ( int on )
{
	on = !!on;
	if (XEMU_LIKELY(on == rom_protect))
		return;
	if (XEMU_UNLIKELY(!in_hypervisor))
		FATAL("%s(): called with !in_hypervisor");
	rom_protect = on;
	invalidate_all_slots_by_policy(MEM_ROM_POLICY);

}



Uint8 cpu_io_port[2];




// VIC-3 based ROM banking bit fields (register $D030):
#define ROM_AT_8000	0x08
#define ROM_AT_A000	0x10
#define ROM_AT_C000	0x20
#define ROM_AT_E000	0x80
// NOT from VIC-3 ROM banking (but from C64-style baking!) however we fake those here to have a uniform scheme:
#define ROM_AT_D000	0x01
#define IOR_AT_D000	0x02


// This table allows us to quickly translate C64-style banking into our "faked" format, which is mix of C65-style D030 values and two additonal extra ones (see ROM_AT_* definitions above)
static const Uint8 c64_memcfg_to_c65[8] = {
	0           | 0           | 0           , // 0: all RAM
	0           | ROM_AT_D000 | 0           , // 1: CHAR ROM
	ROM_AT_E000 | ROM_AT_D000 | 0           , // 2: KERNAL ROM + CHAR ROM
	ROM_AT_E000 | ROM_AT_D000 | ROM_AT_A000 , // 3: KERNAL ROM + CHAR ROM + BASIC ROM
	0           | 0           | 0           , // 4: all RAM
	0           | IOR_AT_D000 | 0           , // 5: I/O
	ROM_AT_E000 | IOR_AT_D000 | 0           , // 6: KERNAL ROM + I/O
	ROM_AT_E000 | IOR_AT_D000 | ROM_AT_A000   // 7: KERNAL ROM + I/O + BASIC ROM
};


static inline Uint8 get_banking_config ( void )
{
	// Note: VIC-3 based "ROM banking" does not have any effect if we're in hypervisor mode
	return c64_memcfg_to_c65[(cpu_io_port[1] | (~cpu_io_port[0])) & 7] | (XEMU_UNLIKELY(in_hypervisor) ? 0 : (vic_registers[0x30] & (ROM_AT_8000|ROM_AT_A000|ROM_AT_C000|ROM_AT_E000)));
}


static Uint8 bank_cfg = 0xFF;

static unsigned int map_lo = 0;
static unsigned int map_hi = 0;
static Uint8 map_mask = 0;


static void mem_apply_config ( void )
{
	static unsigned int new_policy[16];
	new_policy[0x0] = new_policy[0x1] = (map_mask & 0x01) ? map_lo : CPU_MEM_MAP_POLICY_RAM;
	new_policy[0x2] = new_policy[0x3] = (map_mask & 0x02) ? map_lo : CPU_MEM_MAP_POLICY_RAM;
	new_policy[0x4] = new_policy[0x5] = (map_mask & 0x04) ? map_lo : CPU_MEM_MAP_POLICY_RAM;
	new_policy[0x6] = new_policy[0x7] = (map_mask & 0x08) ? map_lo : CPU_MEM_MAP_POLICY_RAM;
	new_policy[0x8] = new_policy[0x9] = (map_mask & 0x10) ? map_hi : ((bank_cfg & ROM_AT_8000) ? CPU_MEM_MAP_POLICY_ROM : CPU_MEM_MAP_POLICY_RAM);
	new_policy[0xA] = new_policy[0xB] = (map_mask & 0x20) ? map_hi : ((bank_cfg & ROM_AT_A000) ? CPU_MEM_MAP_POLICY_ROM : CPU_MEM_MAP_POLICY_RAM);
	new_policy[0xC] =                   (map_mask & 0x40) ? map_hi : ((bank_cfg & ROM_AT_C000) ? CPU_MEM_MAP_POLICY_ROM : CPU_MEM_MAP_POLICY_RAM);
	new_policy[0xD] =                   (map_mask & 0x40) ? map_hi : ((bank_cfg & IOR_AT_D000) ? CPU_MEM_MAP_POLICY_IO  : (
	                                                                  (bank_cfg & ROM_AT_D000) ? CPU_MEM_MAP_POLICY_ROM : CPU_MEM_MAP_POLICY_RAM));
	new_policy[0xE] = new_policy[0xF] = (map_mask & 0x80) ? map_hi : ((bank_cfg & ROM_AT_E000) ? CPU_MEM_MAP_POLICY_ROM : CPU_MEM_MAP_POLICY_RAM);
	for (unsigned int i = 0; i < 0x10; i++)
		if (policy_per_4k[i] != new_policy[i]) {
			policy_per_4k[i] = new_policy[i];
			invalidate_slots(i << 4, 0x10);
		}
}






void mem_apply_config_after_banking ( void )
{
	const Uint8 new_bank_cfg = get_banking_config();
	if (bank_cfg != new_bank_cfg) {
		bank_cfg = new_bank_cfg;
		mem_apply_config();
	}
}


static void mem_apply_config_after_map ( void )
{
	map_lo = map_megabyte_low  + map_offset_low;
	map_hi = map_megabyte_high + map_offset_high;
	mem_apply_config();
}


void mem_apply_config_after_combined_event ( void )
{
	bank_cfg = get_banking_config();
	mem_apply_config_after_map();
}


// This implements the MAP opcode, ie "AUG" in case of 65CE02, which was re-defined to "MAP" in C65's CPU
// M65's extension to select "MB" (ie: megabyte slice, which wraps within!) is supported as well
void cpu65_do_aug_callback ( void )
{
	/*   7       6       5       4       3       2       1       0    BIT
	+-------+-------+-------+-------+-------+-------+-------+-------+
	| LOWER | LOWER | LOWER | LOWER | LOWER | LOWER | LOWER | LOWER | A
	| OFF15 | OFF14 | OFF13 | OFF12 | OFF11 | OFF10 | OFF9  | OFF8  |
	+-------+-------+-------+-------+-------+-------+-------+-------+
	| MAP   | MAP   | MAP   | MAP   | LOWER | LOWER | LOWER | LOWER | X
	| BLK3  | BLK2  | BLK1  | BLK0  | OFF19 | OFF18 | OFF17 | OFF16 |
	+-------+-------+-------+-------+-------+-------+-------+-------+
	| UPPER | UPPER | UPPER | UPPER | UPPER | UPPER | UPPER | UPPER | Y
	| OFF15 | OFF14 | OFF13 | OFF12 | OFF11 | OFF10 | OFF9  | OFF8  |
	+-------+-------+-------+-------+-------+-------+-------+-------+
	| MAP   | MAP   | MAP   | MAP   | UPPER | UPPER | UPPER | UPPER | Z
	| BLK7  | BLK6  | BLK5  | BLK4  | OFF19 | OFF18 | OFF17 | OFF16 |
	+-------+-------+-------+-------+-------+-------+-------+-------+
	-- C65GS extension: Set the MegaByte register for low and high mobies
	-- so that we can address all 256MB of RAM.
	if reg_x = x"0f" then
		reg_mb_low <= reg_a;
	end if;
	if reg_z = x"0f" then
		reg_mb_high <= reg_y;
	end if; */
	cpu65.cpu_inhibit_interrupts = 1;	// disable interrupts till the next "EOM" (ie: NOP) opcode
	DEBUG("CPU: MAP opcode, input A=$%02X X=$%02X Y=$%02X Z=$%02X" NL, cpu65.a, cpu65.x, cpu65.y, cpu65.z);
	map_offset_low  = (cpu65.a <<   8) | ((cpu65.x & 15) << 16);	// offset of lower half (blocks 0-3)
	map_offset_high = (cpu65.y <<   8) | ((cpu65.z & 15) << 16);	// offset of higher half (blocks 4-7)
	map_mask        = (cpu65.z & 0xF0) | ( cpu65.x >> 4);		// "is mapped" mask for blocks (1 bit for each)
	// M65 specific "MB" (megabyte) selector "mode":
	if (cpu65.x == 0x0F)
		map_megabyte_low  = (unsigned int)cpu65.a << 20;
	if (cpu65.z == 0x0F)
		map_megabyte_high = (unsigned int)cpu65.y << 20;
	DEBUG("MEM: applying new memory configuration because of MAP CPU opcode" NL);
	DEBUG("LOW -OFFSET = $%03X, MB = $%02X" NL, map_offset_low , map_megabyte_low  >> 20);
	DEBUG("HIGH-OFFSET = $%03X, MB = $%02X" NL, map_offset_high, map_megabyte_high >> 20);
	DEBUG("MASK        = $%02X" NL, map_mask);
	mem_apply_config_after_map();
}


// *** Implements the EOM opcode of 4510, called by the 65CE02 emulator
void cpu65_do_nop_callback ( void )
{
	if (cpu65.cpu_inhibit_interrupts) {
		cpu65.cpu_inhibit_interrupts = 0;
		DEBUG("CPU: EOM, interrupts were disabled because of MAP till the EOM" NL);
	} else
		DEBUG("CPU: NOP not treated as EOM (no MAP before)" NL);
}


