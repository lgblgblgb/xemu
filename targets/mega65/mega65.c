/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
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


#include "xemu/emutools.h"
#include "mega65.h"
#include "xemu/cpu65c02.h"
#include "xemu/cia6526.h"
#include "xemu/f011_core.h"
#include "xemu/f018_core.h"
#include "xemu/emutools_hid.h"
#include "vic3.h"
#include "xemu/sid.h"
#include "sdcard.h"
#include "uart_monitor.h"
#include "hypervisor.h"
#include "xemu/c64_kbd_mapping.h"
#include "xemu/emutools_config.h"
#include "m65_snapshot.h"

#define kicked_hypervisor gs_regs[0x67E]

static SDL_AudioDeviceID audio = 0;

Uint8 memory[0x100000];			// "Normal" max memory space of C65 (1Mbyte). Special Mega65 cases are handled differently in the current implementation
Uint8 colour_ram[0x10000];
Uint8 character_rom[0x1000];		// the "WOM"-like character ROM of VIC-IV
Uint8 slow_ram[127 << 20];		// 127Mbytes of slowRAM, heh ...
//Uint8 cpu_port[2];			// CPU I/O port at 0/1 (implemented by the VIC3 for real, on C65 but for the usual - C64/6510 - name, it's the "CPU port")
struct Cia6526 cia1, cia2;		// CIA emulation structures for the two CIAs
struct SidEmulation sid1, sid2;		// the two SIDs
int cpu_linear_memory_addressing_is_enabled = 0;
static int nmi_level;			// please read the comment at nmi_set() below


// We re-map I/O requests to a high address space does not exist for real. cpu_read() and cpu_write() should handle this as an IO space request
// This is *still* not Mega65 compatible at implementation level, but it will work, unless az M65 software accesses the I/O space at the high
// memory address and not in a compatible way (ie, CPU address $DXXX)
#define IO_REMAP_ADD		(0xD0000 - 0xD000)
#define IO_REMAP_MEGABYTE	0xFF
// Other re-mapping addresses
// Re-mapping for VIC3 reg $30
#define ROM_C000_REMAP		0x20000
#define ROM_8000_REMAP		0x30000
#define ROM_A000_REMAP		0x30000
#define ROM_E000_REMAP		0x30000
// Re-mapping for "CPU-port" related stuffs
#define ROM_C64_CHR_REMAP	0x20000
#define ROM_C64_KERNAL_REMAP	0x20000
#define ROM_C64_BASIC_REMAP	0x20000

#define TRAP_RESET	0x40
#define IO_REMAPPED	(((IO_REMAP_MEGABYTE) << 20) | ((0xD000 + (IO_REMAP_ADD)) & 0xFFFFF))

static int addr_trans_rd[16];		// address translating offsets for READ operation (it can be added to the CPU address simply, selected by the high 4 bits of the CPU address)
static int addr_trans_wr[16];		// address translating offsets for WRITE operation (it can be added to the CPU address simply, selected by the high 4 bits of the CPU address)
static int addr_trans_rd_megabyte[16];	// Mega65 extension
static int addr_trans_wr_megabyte[16];	// Mega65 extension
int map_mask;			// MAP mask, should be filled at the MAP opcode, *before* calling apply_memory_config() then
// WARNING: map_offset_low and map_offset_high must be used FROM bit-8 only, the lower 8 bits must be zero always!
int map_offset_low;		// MAP low offset, should be filled at the MAP opcode, *before* calling apply_memory_config() then
int map_offset_high;		// MAP high offset, should be filled at the MAP opcode, *before* calling apply_memory_config() then
int map_megabyte_low;		// Mega65 extension: selects the "MegaByte range" (MB) for the mappings on mapped blocks 0...3, NOTE: shifted to Mbyte position!
int map_megabyte_high;		// Mega65 extension: selects the "MegaByte range" (MB) for the mappings on mapped blocks 4...7, NOTE: shifted to Mbyte position!
int io_at_d000;
int skip_unhandled_mem;

int disallow_turbo;

static int frame_counter;

static int   paused = 0, paused_old = 0;
static int   breakpoint_pc = -1;
static int   trace_step_trigger = 0;
static void (*m65mon_callback)(void) = NULL;
static const char emulator_paused_title[] = "TRACE/PAUSE";
char emulator_speed_title[] = "????MHz";

Uint8 gs_regs[0x1000];			// mega65 specific I/O registers, currently an ugly way, as only some bytes are used, ie not VIC3/4, etc etc ...
static int rom_protect;			// C65 system ROM write protection
static int fpga_switches = 0;		// State of FPGA board switches (bits 0 - 15), set switch 12 (hypervisor serial output)


/* You *MUST* call this every time, when *any* of these events applies:
   * MAP 4510 opcode is issued, map_offset_low, map_offset_high, map_mask are modified
   * "CPU port" data or direction (though this one does not have too much meaning for C65/M65, but still must be handled
     because of the special case if switched as "input") register has been written, witn cpu_port[0 or 1] modified
   * VIC3 register $30 is written, with vic3_registers[0x30] modified 
   The reason of this madness: do the ugly work here, as memory configuration change is
   less frequent than memory usage (read/write). Thus do more work here, but simplier
   work when doing actual memory read/writes, with a simple addition and shift, or such.
   The tables are 4K in steps, 4510 would require only 8K steps, but there are other
   reasons (ie, I/O area is only 4K long, mapping is not done by the CPU).
   More advanced technique can be used not to handle *everything* here, but it's better
   for the initial steps, to have all address translating logic at once.
   AND YEAH: I HAVE QUITE WIDE MONITOR FOR THESE LINES TO FIT :) :)
   This looks awfully complicated, but in fact:
   * it's only called on mem config change (see above)
   * most of this terrible looking stuff compiles into only some assembly directives to load a register and store at one or more places, etc
*/
void apply_memory_config ( void )
{
	// FIXME: what happens if VIC-3 reg $30 mapped ROM is tried to be written? Ignored, or RAM is used to write to, as with the CPU port mapping?
	// About the produced signals on the "CPU port"
	int cp = (CPU_PORT(1) | (~CPU_PORT(0)));
	DEBUG("MEGA65: MMU: applying new memory config (PC=$%04X,hyper=%d,CP=%d,ML=$%02X,MH=$%02X,MM=$%02X,MBL=$%02X,MBH=$%02X)" NL,
		cpu_pc, in_hypervisor, cp & 7, map_offset_low >> 8, map_offset_high >> 8, map_mask, map_megabyte_low >> 20, map_megabyte_high >> 20
	);
	// Simple ones, only CPU MAP may apply not other factors
	// Also, these are the "lower" blocks, needs the offset for the "lower" area in case of CPU MAP'ed state
	if (map_mask & 1) {	// $0XXX + $1XXX, MAP block 0 [mask 1]
		addr_trans_wr         [0x0] = addr_trans_rd         [0x0] = addr_trans_wr         [0x1] = addr_trans_rd         [0x1] = map_offset_low;
		addr_trans_wr_megabyte[0x0] = addr_trans_rd_megabyte[0x0] = addr_trans_wr_megabyte[0x1] = addr_trans_rd_megabyte[0x1] = map_megabyte_low;
	} else {
		addr_trans_wr         [0x0] = addr_trans_rd         [0x0] = addr_trans_wr         [0x1] = addr_trans_rd         [0x1] = 0;
		addr_trans_wr_megabyte[0x0] = addr_trans_rd_megabyte[0x0] = addr_trans_wr_megabyte[0x1] = addr_trans_rd_megabyte[0x1] = 0;
	}
	if (map_mask & 2) {	// $2XXX + $3XXX, MAP block 1 [mask 2]
		addr_trans_wr         [0x2] = addr_trans_rd         [0x2] = addr_trans_wr         [0x3] = addr_trans_rd         [0x3] = map_offset_low;
		addr_trans_wr_megabyte[0x2] = addr_trans_rd_megabyte[0x2] = addr_trans_wr_megabyte[0x3] = addr_trans_rd_megabyte[0x3] = map_megabyte_low;
	} else {
		addr_trans_wr         [0x2] = addr_trans_rd         [0x2] = addr_trans_wr         [0x3] = addr_trans_rd         [0x3] = 0;
		addr_trans_wr_megabyte[0x2] = addr_trans_rd_megabyte[0x2] = addr_trans_wr_megabyte[0x3] = addr_trans_rd_megabyte[0x3] = 0;
	}
	if (map_mask & 4) {	// $4XXX + $5XXX, MAP block 2 [mask 4]
		addr_trans_wr         [0x4] = addr_trans_rd         [0x4] = addr_trans_wr         [0x5] = addr_trans_rd         [0x5] = map_offset_low;
		addr_trans_wr_megabyte[0x4] = addr_trans_rd_megabyte[0x4] = addr_trans_wr_megabyte[0x5] = addr_trans_rd_megabyte[0x5] = map_megabyte_low;
	} else {
		addr_trans_wr         [0x4] = addr_trans_rd         [0x4] = addr_trans_wr         [0x5] = addr_trans_rd         [0x5] = 0;
		addr_trans_wr_megabyte[0x4] = addr_trans_rd_megabyte[0x4] = addr_trans_wr_megabyte[0x5] = addr_trans_rd_megabyte[0x5] = 0;
	}
	if (map_mask & 8) {	// $6XXX + $7XXX, MAP block 3 [mask 8]
		addr_trans_wr         [0x6] = addr_trans_rd         [0x6] = addr_trans_wr         [0x7] = addr_trans_rd         [0x7] = map_offset_low;
		addr_trans_wr_megabyte[0x6] = addr_trans_rd_megabyte[0x6] = addr_trans_wr_megabyte[0x7] = addr_trans_rd_megabyte[0x7] = map_megabyte_low;
	} else {
		addr_trans_wr         [0x6] = addr_trans_rd         [0x6] = addr_trans_wr         [0x7] = addr_trans_rd         [0x7] = 0;
		addr_trans_wr_megabyte[0x6] = addr_trans_rd_megabyte[0x6] = addr_trans_wr_megabyte[0x7] = addr_trans_rd_megabyte[0x7] = 0;
	}
	// *** !!!! From this point, we must use the "high" area offset if it's CPU MAP'ed !!!! ****
	// $8XXX and $9XXX, MAP block 4 [mask 16]
	if (vic3_registers[0x30] & 8) {
		addr_trans_wr         [0x8] = addr_trans_rd         [0x8] = addr_trans_wr         [0x9] = addr_trans_rd         [0x9] = ROM_8000_REMAP;
		addr_trans_wr_megabyte[0x8] = addr_trans_rd_megabyte[0x8] = addr_trans_wr_megabyte[0x9] = addr_trans_rd_megabyte[0x9] = 0;
	} else if (map_mask & 16) {
		addr_trans_wr         [0x8] = addr_trans_rd         [0x8] = addr_trans_wr         [0x9] = addr_trans_rd         [0x9] = map_offset_high;
		addr_trans_wr_megabyte[0x8] = addr_trans_rd_megabyte[0x8] = addr_trans_wr_megabyte[0x9] = addr_trans_rd_megabyte[0x9] = map_megabyte_high;
	} else {
		addr_trans_wr         [0x8] = addr_trans_rd         [0x8] = addr_trans_wr         [0x9] = addr_trans_rd         [0x9] = 0;
		addr_trans_wr_megabyte[0x8] = addr_trans_rd_megabyte[0x8] = addr_trans_wr_megabyte[0x9] = addr_trans_rd_megabyte[0x9] = 0;
	}
	// $AXXX and $BXXX, MAP block 5 [mask 32]
	if (vic3_registers[0x30] & 16) {
		addr_trans_wr         [0xA] = addr_trans_rd         [0xA] = addr_trans_wr         [0xB] = addr_trans_rd         [0xB] = ROM_A000_REMAP;
		addr_trans_wr_megabyte[0xA] = addr_trans_rd_megabyte[0xA] = addr_trans_wr_megabyte[0xB] = addr_trans_rd_megabyte[0xB] = 0;
	} else if ((map_mask & 32)) {
		addr_trans_wr         [0xA] = addr_trans_rd         [0xA] = addr_trans_wr         [0xB] = addr_trans_rd         [0xB] = map_offset_high;
		addr_trans_wr_megabyte[0xA] = addr_trans_rd_megabyte[0xA] = addr_trans_wr_megabyte[0xB] = addr_trans_rd_megabyte[0xB] = map_megabyte_high;
	} else {
		addr_trans_wr[0xA] = addr_trans_wr[0xB] = 0;
		addr_trans_rd[0xA] = addr_trans_rd[0xB] = ((cp & 3) == 3) ? ROM_C64_BASIC_REMAP : 0;
		addr_trans_wr_megabyte[0xA] = addr_trans_rd_megabyte[0xA] = addr_trans_wr_megabyte[0xB] = addr_trans_rd_megabyte[0xB] = 0;
	}
	// $CXXX, MAP block 6 [mask 64]
	// Warning: all VIC3 reg $30 related ROM maps are for 8K size, *except* of '@C000' (interface ROM) which is only 4K! Also this is in another ROM bank than the others
	if (vic3_registers[0x30] & 32) {
		addr_trans_wr         [0xC] = addr_trans_rd         [0xC] = ROM_C000_REMAP;
		addr_trans_wr_megabyte[0xC] = addr_trans_rd_megabyte[0xC] = 0;
	} else if (map_mask & 64) {
		addr_trans_wr         [0xC] = addr_trans_rd         [0xC] = map_offset_high;
		addr_trans_wr_megabyte[0xC] = addr_trans_rd_megabyte[0xC] = map_megabyte_high;
	} else {
		addr_trans_wr         [0xC] = addr_trans_rd         [0xC] = 0;
		addr_trans_wr_megabyte[0xC] = addr_trans_rd_megabyte[0xC] = 0;
	}
	// $DXXX, *still* MAP block 6 [mask 64], "classic" I/O area is here, only 4K size
	// We remap I/O to a higher physical address (>1Mbyte), though it's *NOT* M65 fully compatible yet, as it has multiple I/O areas based on the VIC I/O mode.
	if (map_mask & 64) {
		addr_trans_wr         [0xD] = addr_trans_rd         [0xD] = map_offset_high;
		addr_trans_wr_megabyte[0xD] = addr_trans_rd_megabyte[0xD] = map_megabyte_high;
		io_at_d000 = 0;
	} else {
		if ((cp & 7) > 4) {
			addr_trans_wr         [0xD] = addr_trans_rd         [0xD] = IO_REMAP_ADD;
			addr_trans_wr_megabyte[0xD] = addr_trans_rd_megabyte[0xD] = IO_REMAP_MEGABYTE << 20;
			io_at_d000 = 1;
		} else {
			addr_trans_wr[0xD] = 0;
			addr_trans_rd[0xD] = (cp & 3) ? ROM_C64_CHR_REMAP : 0;
			addr_trans_wr_megabyte[0xD] = addr_trans_rd_megabyte[0xD] = 0;
			io_at_d000 = 0;
		}
	}
	// $EXXX and $FXXX, MAP block 7 [mask 128]
	if (vic3_registers[0x30] & 128) {
		addr_trans_wr         [0xE] = addr_trans_rd         [0xE] = addr_trans_wr         [0xF] = addr_trans_rd         [0xF] = ROM_E000_REMAP;
		addr_trans_wr_megabyte[0xE] = addr_trans_rd_megabyte[0xE] = addr_trans_wr_megabyte[0xF] = addr_trans_rd_megabyte[0xF] = 0;
	} else if (map_mask & 128) {
		addr_trans_wr         [0xE] = addr_trans_rd         [0xE] = addr_trans_wr         [0xF] = addr_trans_rd         [0xF] = map_offset_high;
		addr_trans_wr_megabyte[0xE] = addr_trans_rd_megabyte[0xE] = addr_trans_wr_megabyte[0xF] = addr_trans_rd_megabyte[0xF] = map_megabyte_high;
	} else {
		addr_trans_wr[0xE] = addr_trans_wr[0xF] = 0;
		addr_trans_rd[0xE] = addr_trans_rd[0xF] = ((cp & 3) > 1) ? ROM_C64_KERNAL_REMAP : 0;
		addr_trans_wr_megabyte[0xE] = addr_trans_rd_megabyte[0xE] = addr_trans_wr_megabyte[0xF] = addr_trans_rd_megabyte[0xF] = 0;
	}
}



static void cia1_setint_cb ( int level )
{
	DEBUG("%s: IRQ level changed to %d" NL, cia1.name, level);
	if (level)
		cpu_irqLevel |= 1;
	else
		cpu_irqLevel &= ~1;
}


static inline void nmi_set ( int level, int mask )
{
	// NMI is a low active _EDGE_ triggered 65xx input ... In my emulator though, the signal
	// is "high active", and also we must form the "edge" ourselves from "level". NMI level is
	// set as a 2bit number, on bit 0, CIA2, on bit 1, keyboard RESTORE key. Thus having zero
	// value for level means (in the emu!) that not RESTORE key is pressed neither CIA2 has active
	// IRQ output, non-zero value means some activation. Well, if I am not confused enough here,
	// this should mean that nmi_level zero->non-zero transit should produce the edge (which should
	// be the falling edge in the real hardware anyway ... but the rising here. heh, I should follow
	// the signal level of the hardware in my emulator, honestly ...)
	int nmi_new_level;
	if (level)
		nmi_new_level = nmi_level | mask;
	else
		nmi_new_level = nmi_level & (~mask);
	if ((!nmi_level) && nmi_new_level) {
		DEBUG("NMI edge is emulated towards the CPU (%d->%d)" NL, nmi_level, nmi_new_level);
		cpu_nmiEdge = 1;	// the "NMI edge" trigger is deleted by the CPU emulator itself (it's not a level trigger)
	}
	nmi_level = nmi_new_level;
}



static void cia2_setint_cb ( int level )
{
       nmi_set(level, 1);
}


void clear_emu_events ( void )
{
	hid_reset_events(1);
}


#define KBSEL cia1.PRA


static Uint8 cia1_in_b ( void )
{
	return
		((KBSEL &   1) ? 0xFF : kbd_matrix[0]) &
		((KBSEL &   2) ? 0xFF : kbd_matrix[1]) &
		((KBSEL &   4) ? 0xFF : kbd_matrix[2]) &
		((KBSEL &   8) ? 0xFF : kbd_matrix[3]) &
		((KBSEL &  16) ? 0xFF : kbd_matrix[4]) &
		((KBSEL &  32) ? 0xFF : kbd_matrix[5]) &
		((KBSEL &  64) ? 0xFF : kbd_matrix[6]) &
		((KBSEL & 128) ? 0xFF : kbd_matrix[7]) &
		(joystick_emu == 1 ? c64_get_joy_state() : 0xFF)
	;
}


static Uint8 cia1_in_a ( void )
{
	return joystick_emu == 2 ? c64_get_joy_state() : 0xFF;
}


static void cia2_out_a ( Uint8 data )
{
	vic2_16k_bank = ((~(data | (~cia2.DDRA))) & 3) << 14;
	DEBUG("VIC2: 16K BANK is set to $%04X (CIA mask=$%02X)" NL, vic2_16k_bank, cia2.DDRA);
}



// Just for easier test to have a given port value for CIA input ports
static Uint8 cia_port_in_dummy ( void )
{
	return 0xFF;
}



static void audio_callback(void *userdata, Uint8 *stream, int len)
{
	//DEBUG("AUDIO: audio callback, wants %d samples" NL, len);
	// We use the trick, to render boths SIDs with step of 2, with a byte offset
	// to get a stereo stream, wanted by SDL.
	sid_render(&sid2, ((short *)(stream)) + 0, len >> 1, 2);	// SID @ left
	sid_render(&sid1, ((short *)(stream)) + 1, len >> 1, 2);	// SID @ right
}


#include "initial_charset.c"
static const Uint8 initial_kickup[] = {
#include "../../rom/kickup.cdata"
};



#ifdef XEMU_SNAPSHOT_SUPPORT
static const char *m65_snapshot_saver_filename = NULL;
static void m65_snapshot_saver_on_exit_callback ( void )
{
	if (!m65_snapshot_saver_filename)
		return;
	if (xemusnap_save(m65_snapshot_saver_filename))
		ERROR_WINDOW("Could not save snapshot \"%s\": %s", m65_snapshot_saver_filename, xemusnap_error_buffer);
	else
		INFO_WINDOW("Snapshot has been saved to \"%s\".", m65_snapshot_saver_filename);
}
#endif



static void mega65_init ( int sid_cycles_per_sec, int sound_mix_freq )
{
	const char *p;
#ifdef AUDIO_EMULATION
	SDL_AudioSpec audio_want, audio_got;
#endif
	hypervisor_debug_init(emucfg_get_str("kickuplist"), emucfg_get_bool("hyperdebug"));
	hid_init(
		c64_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// joy HID events enabled
	);
	joystick_emu = 1;
	nmi_level = 0;
	// *** FPGA switches ...
	do {
		int switches[16], r = emucfg_integer_list_from_string(emucfg_get_str("fpga"), switches, 16, ",");
		if (r < 0)
			FATAL("Too many FPGA switches specified for option 'fpga'");
		while (r--) {
			DEBUGPRINT("FPGA switch is turned on: #%d" NL, switches[r]);
			if (switches[r] < 0 || switches[r] > 15)
				FATAL("Invalid switch sepcifictation for option 'fpga': %d", switches[r]);
			fpga_switches |= 1 << (switches[r]);
		}
	} while (0);
	// *** Init memory space
	memset(memory, 0xFF, sizeof memory);
	memcpy(character_rom, initial_charset, sizeof initial_charset); // pre-initialize charrom "WOM" with an initial charset
	memset(slow_ram, 0xFF, sizeof slow_ram);
	memset(colour_ram, 0xFF, sizeof colour_ram);
	in_hypervisor = 0;
	memset(gs_regs, 0, sizeof gs_regs);
	rom_protect = 1;
	kicked_hypervisor = emucfg_get_num("kicked");
	DEBUG("MEGA65: I/O is remapped to $%X" NL, IO_REMAPPED);
	// *** Trying to load kickstart image
	p = emucfg_get_str("kickup");
	if (emu_load_file(p, hypervisor_memory, 0x4001) == 0x4000) {
		DEBUG("MEGA65: %s loaded into hypervisor memory." NL, p);
	} else {
		WARNING_WINDOW("Kickstart %s cannot be found. Using the default (maybe outdated!) built-in version", p);
		if (sizeof initial_kickup != 0x4000)
			FATAL("Internal error: initial kickup is not 16K!");
		memcpy(hypervisor_memory, initial_kickup, 0x4000);
		hypervisor_debug_invalidate("no kickup could be loaded, built-in one does not have debug info");
	}
	// *** Image file for SDCARD support
	if (sdcard_init(emucfg_get_str("sdimg"), emucfg_get_str("8")) < 0)
		FATAL("Cannot find SD-card image (which is a must for Mega65 emulation): %s", emucfg_get_str("sdimg"));
	// *** Initialize VIC3
	vic3_init();
	// *** Memory configuration (later override will happen for mega65 mode though, this is only the default)
	CPU_PORT(0) = CPU_PORT(1) = 0xFF;	// the "CPU I/O port" on 6510/C64, implemented by VIC3 for real in C65!
	map_mask = 0;				// as all 8K blocks are unmapped, we don't need to worry about the low/high offset to set here
	map_megabyte_low = 0;
	map_megabyte_high = 0;
	map_offset_low = 0;
	map_offset_high = 0;
	apply_memory_config();			// VIC3 $30 reg is already filled, so it's OK to call this now
	// *** CIAs
	cia_init(&cia1, "CIA-1",
		NULL,			// callback: OUTA
		NULL,			// callback: OUTB
		NULL,			// callback: OUTSR
		cia1_in_a,		// callback: INA ~ joy#2
		cia1_in_b,		// callback: INB ~ keyboard
		NULL,			// callback: INSR
		cia1_setint_cb		// callback: SETINT
	);
	cia_init(&cia2, "CIA-2",
		cia2_out_a,		// callback: OUTA
		NULL,			// callback: OUTB
		NULL,			// callback: OUTSR
		cia_port_in_dummy,	// callback: INA
		NULL,			// callback: INB
		NULL,			// callback: INSR
		cia2_setint_cb		// callback: SETINT ~ that would be NMI in our case
	);
	// *** Initialize DMA
	dma_init(
		emucfg_get_num("dmarev"),
		read_phys_mem,  // dma_reader_cb_t set_source_mreader ,
		write_phys_mem, // dma_writer_cb_t set_source_mwriter ,
		read_phys_mem,  // dma_reader_cb_t set_target_mreader ,
		write_phys_mem, // dma_writer_cb_t set_target_mwriter,
		io_read,        // dma_reader_cb_t set_source_ioreader,
		io_write,       // dma_writer_cb_t set_source_iowriter,
		io_read,        // dma_reader_cb_t set_target_ioreader,
		io_write,       // dma_writer_cb_t set_target_iowriter,
		read_phys_mem   // dma_reader_cb_t set_list_reader
	);
	dma_set_phys_io_offset(0xD000);	// FIXME: currently Mega65 uses D000 based I/O decoding, so we need this here ...
	// Initialize FDC
	fdc_init();
	// SIDs, plus SDL audio
	sid_init(&sid1, sid_cycles_per_sec, sound_mix_freq);
	sid_init(&sid2, sid_cycles_per_sec, sound_mix_freq);
#ifdef AUDIO_EMULATION
	SDL_memset(&audio_want, 0, sizeof(audio_want));
	audio_want.freq = sound_mix_freq;
	audio_want.format = AUDIO_S16SYS;	// used format by SID emulation (ie: signed short)
	audio_want.channels = 2;		// that is: stereo, for the two SIDs
	audio_want.samples = 1024;		// Sample size suggested (?) for the callback to render once
	audio_want.callback = audio_callback;	// Audio render callback function, called periodically by SDL on demand
	audio_want.userdata = NULL;		// Not used, "userdata" parameter passed to the callback by SDL
	audio = SDL_OpenAudioDevice(NULL, 0, &audio_want, &audio_got, 0);
	if (audio) {
		int i;
		for (i = 0; i < SDL_GetNumAudioDevices(0); i++)
			DEBUG("AUDIO: audio device is #%d: %s" NL, i, SDL_GetAudioDeviceName(i, 0));
		// Sanity check that we really got the same audio specification we wanted
		if (audio_want.freq != audio_got.freq || audio_want.format != audio_got.format || audio_want.channels != audio_got.channels) {
			SDL_CloseAudioDevice(audio);	// forget audio, if it's not our expected format :(
			audio = 0;
			ERROR_WINDOW("Audio parameter mismatches.");
		}
		DEBUG("AUDIO: initialized (#%d), %d Hz, %d channels, %d buffer sample size." NL, audio, audio_got.freq, audio_got.channels, audio_got.samples);
	} else
		ERROR_WINDOW("Cannot open audio device!");
#endif
	//
#ifdef UARTMON_SOCKET
	uartmon_init(UARTMON_SOCKET);
#endif
	// *** RESET CPU, also fetches the RESET vector into PC
	cpu_reset();
	rom_protect = 0;
	cpu_linear_memory_addressing_is_enabled = 1;
	hypervisor_enter(TRAP_RESET);
	DEBUG("INIT: end of initialization!" NL);
#ifdef XEMU_SNAPSHOT_SUPPORT
	xemusnap_init(m65_snapshot_definition);
	p = emucfg_get_str("snapload");
	if (p) {
		if (xemusnap_load(p))
			FATAL("Couldn't load snapshot \"%s\": %s", p, xemusnap_error_buffer);
	}
	m65_snapshot_saver_filename = emucfg_get_str("snapsave");
	atexit(m65_snapshot_saver_on_exit_callback);
#endif
}



// *** Implements the MAP opcode of 4510, called by the 65CE02 emulator (which knows
// only "AUG" generic opcode and use cpu_do_aug() callback then.
// FIXME: for M65, we should handle the "MB selecting" stuff here ...
void cpu_do_aug ( void )
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
	+-------+-------+-------+-------+-------+-------+-------+-------+ */
/*
  -- C65GS extension: Set the MegaByte register for low and high mobies
      -- so that we can address all 256MB of RAM.
      if reg_x = x"0f" then
        reg_mb_low <= reg_a;
      end if;
      if reg_z = x"0f" then
        reg_mb_high <= reg_y;
      end if;*/
	cpu_inhibit_interrupts = 1;	// disable interrupts till the next "EOM" (ie: NOP) opcode
	DEBUG("CPU: MAP opcode, input A=$%02X X=$%02X Y=$%02X Z=$%02X" NL, cpu_a, cpu_x, cpu_y, cpu_z);
	map_offset_low  = (cpu_a << 8) | ((cpu_x & 15) << 16);	// offset of lower half (blocks 0-3)
	map_offset_high = (cpu_y << 8) | ((cpu_z & 15) << 16);	// offset of higher half (blocks 4-7)
	map_mask        = (cpu_z & 0xF0) | (cpu_x >> 4);	// "is mapped" mask for blocks (1 bit for each)
	// M65 specific "MB" (megabyte) selector "mode":
	if (cpu_x == 0x0F)
		map_megabyte_low  = (int)cpu_a << 20;
	if (cpu_z == 0x0F)
		map_megabyte_high = (int)cpu_y << 20;
	DEBUG("MEM: applying new memory configuration because of MAP CPU opcode" NL);
	DEBUG("LOW -OFFSET = $%03X, MB = $%02X" NL, map_offset_low , map_megabyte_low  >> 20);
	DEBUG("HIGH-OFFSET = $%03X, MB = $%02X" NL, map_offset_high, map_megabyte_high >> 20);
	DEBUG("MASK        = $%02X" NL, map_mask);
	apply_memory_config();
}



// *** Implements the EOM opcode of 4510, called by the 65CE02 emulator
void cpu_do_nop ( void )
{
	if (cpu_inhibit_interrupts) {
		cpu_inhibit_interrupts = 0;
		DEBUG("CPU: EOM, interrupts were disabled because of MAP till the EOM" NL);
	} else
		DEBUG("CPU: NOP not treated as EOM (no MAP before)" NL);
}



#define RETURN_ON_IO_READ_NOT_IMPLEMENTED(func, fb) \
	do { DEBUG("IO: NOT IMPLEMENTED read (emulator lacks feature), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_READ_NO_NEW_VIC_MODE(func, fb) \
	do { DEBUG("IO: ignored read (not new VIC mode), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_WRITE_NOT_IMPLEMENTED(func) \
	do { DEBUG("IO: NOT IMPLEMENTED write (emulator lacks feature), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)
#define RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE(func) \
	do { DEBUG("IO: ignored write (not new VIC mode), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)
#define WARN_IO_MODE_WR(func) \
	DEBUG("IO: write operation defaults (not new VIC mode) to VIC-2 registers, though it would be: \"%s\" (a=$%04X, d=$%02X)" NL, func, addr, data)
#define WARN_IO_MODE_RD(func) \
	DEBUG("IO: read operation defaults (not new VIC mode) to VIC-2 registers, though it would be: \"%s\" (a=$%04X)" NL, func, addr)


// Call this ONLY with addresses between $D000-$DFFF
// Ranges marked with (*) needs "vic_new_mode"
Uint8 io_read ( int addr )
{
	// FIXME: sanity check ...
	if (addr < 0xD000 || addr > 0xDFFF)
		FATAL("io_read() decoding problem addr $%X is not in range of $D000...$DFFF", addr);
	// Future stuff: instead of slow tons of IFs, use the >> 5 maybe
	// that can have new device at every 0x20 dividible addresses,
	// that is: switch ((addr >> 5) & 127)
	// Other idea: array of function pointers, maybe separated for new/old
	// VIC modes as well so no need to check each time that either ...
	if (addr < 0xD080)	// $D000 - $D07F:	VIC3
		return vic3_read_reg(addr);
	if (addr < 0xD0A0) {	// $D080 - $D09F	DISK controller (*)
		if (vic_iomode)
			return fdc_read_reg(addr & 0xF);
		else {
			WARN_IO_MODE_RD("DISK controller");
			return vic3_read_reg(addr);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
	}
	if (addr < 0xD100) {	// $D0A0 - $D0FF	RAM expansion controller (*)
		if (vic_iomode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("RAM expansion controller", 0xFF);
		else {
			WARN_IO_MODE_RD("RAM expansion controller");
			return vic3_read_reg(addr);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
	}
	if (addr < 0xD400) {	// $D100 - $D3FF	palette red/green/blue nibbles (*)
		if (vic_iomode)
			return 0xFF; // NOT READABLE ON VIC3!
		else {
			WARN_IO_MODE_RD("palette reg/green/blue nibbles");
			return vic3_read_reg(addr);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
	}
	if (addr < 0xD440) {	// $D400 - $D43F	SID, right
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("right SID", 0xFF);
	}
	if (addr < 0xD600) {	// $D440 - $D5FF	SID, left
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("left SID", 0xFF);
	}
	if (addr < 0xD700) {	// $D600 - $D6FF	UART (*)
		if (vic_iomode == VIC4_IOMODE && addr >= 0xD609) {	// D609 - D6FF: Mega65 suffs
			if (addr >= 0xD680 && addr <= 0xD693)		// SDcard controller etc of Mega65
				return sdcard_read_register(addr - 0xD680);
			switch (addr) {
				case 0xD67C:
					return 0;	// emulate the "UART is ready" situation (used by newer kickstarts around from v0.11 or so)
				case 0xD67E:				// upgraded hypervisor signal
					if (kicked_hypervisor == 0x80)	// 0x80 means for Xemu (not for a real M65!): ask the user!
						kicked_hypervisor = QUESTION_WINDOW(
							"Not upgraded yet, it can do it|Already upgraded, I test kicked state",
							"Kickstart asks hypervisor upgrade state. What do you want Xemu to answer?\n"
							"(don't worry, it won't be asked again without RESET)"
						) ? 0xFF : 0;
					return kicked_hypervisor;
				case 0xD6F0:
					return fpga_switches & 0xFF;
				case 0xD6F1:
					return (fpga_switches >> 8) & 0xFF;
				default:
					DEBUG("MEGA65: reading Mega65 specific I/O @ $%04X result is $%02X" NL, addr, gs_regs[addr & 0xFFF]);
					return gs_regs[addr & 0xFFF];
			}
		} else if (vic_iomode)
			RETURN_ON_IO_READ_NOT_IMPLEMENTED("UART", 0xFF);
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("UART", 0xFF);
	}
	if (addr < 0xD800) {	// $D700 - $D7FF	DMA (*)
		if (vic_iomode)
			return dma_read_reg(addr & 0xF);
		else
			RETURN_ON_IO_READ_NO_NEW_VIC_MODE("DMA controller", 0xFF);
	}
	if (addr < ((vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00)) {	// $D800-$DC00/$E000	COLOUR NIBBLES, mapped to $1F800 in BANK1
		DEBUG("IO: reading colour RAM at offset $%04X" NL, addr - 0xD800);
		return colour_ram[addr - 0xD800];
	}
	if (addr < 0xDD00) {	// $DC00 - $DCFF	CIA-1
		Uint8 result = cia_read(&cia1, addr & 0xF);
		//RETURN_ON_IO_READ_NOT_IMPLEMENTED("CIA-1", 0xFF);
		DEBUG("%s: reading register $%X result is $%02X" NL, cia1.name, addr & 15, result);
		return result;
	}
	if (addr < 0xDE00) {	// $DD00 - $DDFF	CIA-2
		Uint8 result = cia_read(&cia2, addr & 0xF);
		//RETURN_ON_IO_READ_NOT_IMPLEMENTED("CIA-2", 0xFF);
		DEBUG("%s: reading register $%X result is $%02X" NL, cia2.name, addr & 15, result);
		return result;
	}
	// Only IO-1 and IO-2 areas left, if SD-card buffer is mapped for Mega65, this is our only case left!
	do {
		int result = sdcard_read_buffer(addr - 0xDE00);	// try to read SD buffer
		if (result >= 0) {	// if non-negative number got, answer is really the SD card (mapped buffer)
			DEBUG("SDCARD: BUFFER: reading SD-card buffer at offset $%03X with result $%02X PC=$%04X" NL, addr - 0xDE00, result, cpu_pc);
			return result;
		} else
			DEBUG("SDCARD: BUFFER: *NOT* mapped SD-card buffer is read, can it be a bug?? PC=$%04X" NL, cpu_pc);
	} while (0);
	if (addr < 0xDF00) {	// $DE00 - $DEFF	IO-1 external
		RETURN_ON_IO_READ_NOT_IMPLEMENTED("IO-1 external select", 0xFF);
	}
	// The rest: IO-2 external
	RETURN_ON_IO_READ_NOT_IMPLEMENTED("IO-2 external select", 0xFF);
}




// Call this ONLY with addresses between $D000-$DFFF
// Ranges marked with (*) needs "vic_new_mode"
void io_write ( int addr, Uint8 data )
{
	// FIXME: sanity check ...
	if (addr < 0xD000 || addr > 0xDFFF)
		FATAL("io_read() decoding problem addr $%X is not in range of $D000...$DFFF", addr);
	if (addr < 0xD080) {	// $D000 - $D07F:	VIC3
		vic3_write_reg(addr, data);
		return;
	}
	if (addr < 0xD0A0) {	// $D080 - $D09F	DISK controller (*)
		if (vic_iomode)
			fdc_write_reg(addr & 0xF, data);
		else {
			WARN_IO_MODE_WR("DISK controller");
			vic3_write_reg(addr, data);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
		return;
	}
	if (addr < 0xD100) {	// $D0A0 - $D0FF	RAM expansion controller (*)
		if (vic_iomode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("RAM expansion controller");
		else {
			WARN_IO_MODE_WR("RAM expansion controller");
			vic3_write_reg(addr, data);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
		return;
	}
	if (addr < 0xD400) {	// $D100 - $D3FF	palette red/green/blue nibbles (*)
		if (vic_iomode)
			vic3_write_palette_reg(addr - 0xD100, data);
		else {
			WARN_IO_MODE_WR("palette red/green/blue nibbles");
			vic3_write_reg(addr, data);	// if I understand correctly, without newVIC mode, $D000-$D3FF will mean legacy VIC-2 everywhere [?]
		}
		return;
	}
	if (addr < 0xD440) {	// $D400 - $D43F	SID, right
		sid_write_reg(&sid1, addr & 31, data);
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("right SID");
		return;
	}
	if (addr < 0xD600) {	// $D440 - $D5FF	SID, left
		sid_write_reg(&sid2, addr & 31, data);
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("left SID");
		return;
	}
	if (addr < 0xD700) {	// $D600 - $D6FF	UART (*)
		if (vic_iomode == VIC4_IOMODE && addr >= 0xD609) {	// D609 - D6FF: Mega65 suffs
			gs_regs[addr & 0xFFF] = data;
			DEBUG("MEGA65: writing Mega65 specific I/O range @ $%04X with $%02X" NL, addr, data);
			if (!in_hypervisor && addr >= 0xD640 && addr <= 0xD67F) {
				// In user mode, writing to $D640-$D67F (in VIC4 iomode) causes to enter hypervisor mode with
				// the trap number given by the offset in this range
				hypervisor_enter(addr & 0x3F);
				return;
			}
			if (addr >= 0xD680 && addr <= 0xD693) {
				sdcard_write_register(addr - 0xD680, data);
				return;
			}
			switch (addr) {
				case 0xD67C:	// hypervisor serial monitor port
					hypervisor_serial_monitor_push_char(data);
					break;
				case 0xD67D:
					DEBUG("MEGA65: features set as $%02X" NL, data);
					if ((data & 4) != rom_protect) {
						fprintf(stderr, "MEGA65: ROM protection has been turned %s." NL, data & 4 ? "ON" : "OFF");
						rom_protect = data & 4;
					}
					break;
				case 0xD67E:	// it seems any write (?) here marks the byte as non-zero?! FIXME TODO
					kicked_hypervisor = 0xFF;
					fprintf(stderr, "Writing already-kicked register $%04X!" NL, addr);
					hypervisor_debug_invalidate("$D67E was written, maybe new kickstart will boot!");
					break;
				case 0xD67F:	// hypervisor leave
					hypervisor_leave();
					break;
				default:
					DEBUG("MEGA65: this I/O port is not emulated in Xemu yet: $%04X" NL, addr);
					break;
			}
                        return;
		} else if (vic_iomode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("UART");
		else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("UART");
	}
	if (addr < 0xD800) {	// $D700 - $D7FF	DMA (*)
		DEBUG("DMA: writing register $%04X (data = $%02X)" NL, addr, data);
		if (vic_iomode) {
			dma_write_reg(addr & 0xF, data);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("DMA controller");
	}
	if (addr < ((vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00)) {	// $D800-$DC00/$E000	COLOUR NIBBLES, mapped to $1F800 in BANK1
		memory[0x1F800 + addr - 0xD800] = data;
		colour_ram[addr - 0xD800] = data;
		DEBUG("IO: writing colour RAM at offset $%04X" NL, addr - 0xD800);
		return;
	}
	if (addr < 0xDD00) {	// $DC00 - $DCFF	CIA-1
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("CIA-1");
		DEBUG("%s: writing register $%X with data $%02X" NL, cia1.name, addr & 15, data);
		cia_write(&cia1, addr & 0xF, data);
		return;
	}
	if (addr < 0xDE00) {	// $DD00 - $DDFF	CIA-2
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("CIA-2");
		DEBUG("%s: writing register $%X with data $%02X" NL, cia2.name, addr & 15, data);
		cia_write(&cia2, addr & 0xF, data);
		return;
	}
	// Only IO-1 and IO-2 areas left, if SD-card buffer is mapped for Mega65, this is our only case left!
	if (sdcard_write_buffer(addr - 0xDE00, data) >= 0)
		return;	// if return value is non-negative, buffer was mapped and written!
	if (addr < 0xDF00) {	// $DE00 - $DEFF	IO-1 external
		RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("IO-1 external select");
	}
	// The rest: IO-2 external
	RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("IO-2 external select");
}



static inline int cpu_get_flat_addressing_mode_address ( void )
{
	register int addr = cpu_read(cpu_pc++);	// fetch base page address
	// FIXME: really, BP/ZP is wrapped around in case of linear addressing and eg BP addr of $FF got??????
	return ((
		 cpu_read(cpu_bphi |   addr             )        |
		(cpu_read(cpu_bphi | ((addr + 1) & 0xFF)) <<  8) |
		(cpu_read(cpu_bphi | ((addr + 2) & 0xFF)) << 16) |
		(cpu_read(cpu_bphi | ((addr + 3) & 0xFF)) << 24)
	) + cpu_z) & 0xFFFFFFF;	// FIXME: check if it's really apply here: warps around at 256Mbyte, for address bus of Mega65
}


Uint8 cpu_read_linear_opcode ( void )
{
	int addr = cpu_get_flat_addressing_mode_address();
	Uint8  data = read_phys_mem(addr);
	DEBUG("MEGA65: reading LINEAR memory [PC=$%04X/OPC=$%02X] @ $%X with result $%02X" NL, cpu_old_pc, cpu_op, addr, data);
	return data;
}



void cpu_write_linear_opcode ( Uint8 data )
{
	int addr = cpu_get_flat_addressing_mode_address();
	DEBUG("MEGA65: writing LINEAR memory [PC=$%04X/OPC=$%02X] @ $%X with data $%02X" NL, cpu_old_pc, cpu_op, addr, data);
	write_phys_mem(addr, data);
}



void write_phys_mem ( int addr, Uint8 data )
{
	// NOTE: this function assumes that address within the valid 256Mbyte addressing range.
	// Normal MAP stuffs does this, since it wraps within a single 1Mbyte "MB" already
	// However this can be an issue for other users, ie DMA
	// FIXME: check that at DMAgic, also the situation that DMAgic can/should/etc wrap at all on MB ranges, and on 256Mbyte too!
	addr &= 0xFFFFFFF;		// warps around at 256Mbyte, for address bus of Mega65
	if (addr < 0x000002) {
		// FIXME: handle the magic M65 access: POKEing addr 0 with 64 means force_fast<='0' and 65 force_fast<='1'
		// forcefast==1 _seems_ to be override all speed settings and force to 48Mhz ...
		if (unlikely((addr == 0) && ((data & 0xFE) == 0x40))) {
			data &= 1;
			if (force_fast != data) {
				force_fast = data;
				machine_set_speed(0);
			}
		} else {
			if ((CPU_PORT(addr) & 7) != (data & 7)) {
				CPU_PORT(addr) = data;
				DEBUG("MEM: applying new memory configuration because of CPU port writing." NL);
				apply_memory_config();
			} else
				CPU_PORT(addr) = data;
		}
		return;
	}
	if (addr < 0x01F800) {		// accessing RAM @ 2 ... 128-2K.
		memory[addr] = data;
		return;
	}
	if (addr < 0x020000) {		// the last 2K of the mentioned 128K is the mega65 mapped colour RAM (126K ... 128K)
		memory[addr] = data; 	// also update the "legacy 2K C65 colour-RAM @ 126K" so read func won't have a different case for this!
		colour_ram[addr & 0x7FF] = data;
		return;
	}
	if (addr < 0x040000) {		// ROM area (128K ... 256K)
		if (!rom_protect)
			memory[addr] = data;
		return;
	}
	if (addr < 0x100000)		// unused space (256K ... 1M)
		return;
	// No other memory accessible components/space on C65. The following areas on M65 currently decoded with masks:
	if (addr >= 0x8000000 && addr < 0x8000000 + sizeof(slow_ram)) {
		DEBUG("MEGA65: writing slow RAM at $%X with value of $%02X" NL, addr, data);
		slow_ram[addr - 0x8000000] = data;
		// FIXME: That would be something I don't understand: shadow of the ROM of C65 or something? Hmmm. But it's the DDR RAM!
		// $8000000-$FEFFFFF, and also
		// $0020000-$003FFFF
		if (addr >= 0x8020000 && addr <= 0x803FFFF)
			memory[addr - 0x8000000] = data;
		return;
	}
	if ((addr & 0xFFF0000) == 0xFF80000) {
		colour_ram[addr & 0xFFFF] = data;
		if (addr < 0xFF80800)
			memory[addr - 0xFF60800] = data;
		return;
	}
	if ((addr & 0xFFFF000) == 0xFF7E000) {
		character_rom[addr & 0xFFF] = data;
		return;
	}
	if ((addr & 0xFFFF000) == IO_REMAPPED) {		// I/O stuffs (remapped from standard $D000 location as found on C64 or C65 too)
		io_write((addr & 0xFFF) | 0xD000, data);	// TODO/FIXME: later we can save using D000, if io_read/io_write internally uses 0-FFF range only!
		return;
	}
	if ((addr & 0xFFFC000) == 0xFFF8000) {			// accessing of hypervisor memory
		if (in_hypervisor)	// hypervisor memory is unavailable from "user mode", FIXME: do we need to do trap/whatever if someone tries this?
			hypervisor_memory[addr & 0x3FFF] = data;
		return;
	}
	if (skip_unhandled_mem)
		DEBUGPRINT("WARNING: Unhandled memory write operation for linear address $%X data = $%02X (PC=$%04X)" NL, addr, data, cpu_pc);
	else
		FATAL("Unhandled memory write operation for linear address $%X data = $%02X (PC=$%04X)" NL, addr, data, cpu_pc);
#if 0
	addr &= 0xFFFFFFF;	// warps around at 256Mbyte, for address bus of Mega65
	// !!!! The following line was for C65 to make it secure, only access 1Mbyte of memory ...
	//addr &= 0xFFFFF;
	if (addr < 2) {
		if ((cpu_port[addr] & 7) != (data & 7)) {
			cpu_port[addr] = data;
			DEBUG("MEM: applying new memory configuration because of CPU port writing." NL);
			apply_memory_config();
		} else
			cpu_port[addr] = data;
	} else if (
		(addr < 0x20000)
#if defined(ALLOW_256K_RAMEXP) && defined(ALLOW_512K_RAMEXP)
		|| (addr >= (rom_protect ? 0x40000 : 0x20000))
#else
#	ifdef ALLOW_256K_RAMEXP
		|| (addr >= (rom_protect ? 0x40000 : 0x20000) && addr < 0x80000)
#	endif
#	ifdef ALLOW_512K_RAMEXP
		|| (addr >= 0x80000)
#	endif
#endif
	) {
		if ((addr & 0xFFFF000) == 0xFF7E000) {	// FIXME: temporary hack to allow non-existing VIC-IV charrom writes :-/
			DEBUG("LINEAR: VIC-IV charrom writes are ignored for now in Xemu" NL);
		} else {
		if (addr > sizeof memory)
			FATAL("Invalid physical memory write at $%X" NL, addr);
		if (addr == HYPERVISOR_MEM_REMAP_VIRTUAL + 0x8000)
			FATAL("Somebody EVIL writes hypervisor memory!!! PC=$%04X" NL, cpu_pc);
		memory[addr] = data;
		}
	} else
		DEBUG("MMU: this _physical_ address is not writable: $%X (data=$%02X)" NL, addr, data);
#endif
}



Uint8 read_phys_mem ( int addr )
{
	addr &= 0xFFFFFFF;		// warps around at 256Mbyte, for address bus of Mega65
	//Check for < 2 not needed anymore, as CPU port is really the memory, though it can be a problem if DMA sees this issue differently?!
	//if (addr < 0x000002)
	//	return CPU_PORT(addr);
	if (addr < 0x040000)		// accessing C65 RAM+ROM @ 0 ... 256K
		return memory[addr];
//	if (addr < 0x020000)		// the last 2K of the mentioned 128K is the mega65 mapped colour RAM (126K ... 128K)
//		return colour_ram[addr & 0x7FF]; 	// FIXME: currently it's not mapped for real at the last Mbyte of 256MBytes, as it should be!
//	if (addr < 0x040000)		// ROM area (128K ... 256K)
//		return memory[addr];
	if (addr < 0x100000)		// unused space (256K ... 1M)
		return 0xFF;
	// No other memory accessible components/space on C65. The following areas on M65 currently decoded with masks:
	if (addr >= 0x8000000 && addr < 0x8000000 + sizeof(slow_ram))
		return slow_ram[addr - 0x8000000];
	if ((addr & 0xFFF0000) == 0xFF80000)
		return colour_ram[addr & 0xFFFF];
	if ((addr & 0xFFFF000) == IO_REMAPPED)			// I/O stuffs (remapped from standard $D000 location as found on C64 or C65 too)
		return io_read((addr & 0xFFF) | 0xD000);	// TODO/FIXME: later we can save using D000, if io_read/io_write internally uses 0-FFF range only!
	if ((addr & 0xFFFC000) == 0xFFF8000) {			// accessing of hypervisor memory
		if (in_hypervisor)
			return hypervisor_memory[addr & 0x3FFF];
		else
			return 0xFF;	// hypervisor memory is unavailable from "user mode", FIXME: do we need to do trap/whatever if someone tries this?
	}
	if (skip_unhandled_mem) {
		DEBUGPRINT("WARNING: Unhandled memory read operation for linear address $%X (PC=$%04X)" NL, addr, cpu_pc);
		return 0xFF;
	} else
		FATAL("Unhandled memory read operation for linear address $%X (PC=$%04X)" NL, addr, cpu_pc);
}



// This function is called by the 65CE02 emulator in case of reading a byte (regardless of data or code)
Uint8 cpu_read ( Uint16 addr )
{
	register int range4k = addr >> 12;
	return read_phys_mem(addr_trans_rd_megabyte[range4k] | ((addr_trans_rd[range4k] + addr) & 0xFFFFF));
#if 0
	int phys_addr = addr_trans_rd[addr >> 12] + addr;	// translating address with the READ table created by apply_memory_config()
	//if (in_hypervisor)	// DEBUG
	//	DEBUG("MEGA65: cpu_read, addr=%X phys_addr=%X" NL, addr, phys_addr);
	if (phys_addr >= IO_REMAP_VIRTUAL) {
		if ((addr & 0xF000) != 0xD000)
			FATAL("Internal error: IO is not on the IO space!");
		return io_read(addr);	// addr should be in $DXXX range to hit this, hopefully ...
	}
	return read_phys_mem((phys_addr & 0xFFFFF) | addr_trans_rd_megabyte[addr >> 12]);
#endif
}



// This function is called by the 65CE02 emulator in case of writing a byte
void cpu_write ( Uint16 addr, Uint8 data )
{
	register int range4k = addr >> 12;
	write_phys_mem(addr_trans_wr_megabyte[range4k] | ((addr_trans_wr[range4k] + addr) & 0xFFFFF), data);
#if 0
	int phys_addr = addr_trans_wr[addr >> 12] + addr;	// translating address with the WRITE table created by apply_memory_config()
	if (phys_addr >= IO_REMAP_VIRTUAL) {
		if ((addr & 0xF000) != 0xD000)
			FATAL("Internal error: IO is not on the IO space!");
		io_write(addr, data);	// addr should be in $DXXX range to hit this, hopefully ...
		return;
	}
	write_phys_mem((phys_addr & 0xFFFFF) | addr_trans_wr_megabyte[addr >> 12], data);
#endif
}



// Called in case of an RMW (read-modify-write) opcode write access.
// Original NMOS 6502 would write the old_data first, then new_data.
// It has no inpact in case of normal RAM, but it *does* with an I/O register in some cases!
// CMOS line of 65xx (probably 65CE02 as well?) seems not write twice, but read twice.
// However this leads to incompatibilities, as some software used the RMW behavour by intent.
// Thus Mega65 fixed the problem to "restore" the old way of RMW behaviour.
// I also follow this path here, even if it's *NOT* what 65CE02 would do actually!
void cpu_write_rmw ( Uint16 addr, Uint8 old_data, Uint8 new_data )
{
	int phys_addr = addr >> 12;
	phys_addr = addr_trans_wr_megabyte[phys_addr] | ((addr_trans_wr[phys_addr] + addr) & 0xFFFFF);
	if (phys_addr >= 0xff00000)	// Note: it's useless to "emulate" RMW opcode if the destination is memory, however the last MB of M65 addr.space is special, carrying I/O as well, etc!
		write_phys_mem(phys_addr, old_data);
	write_phys_mem(phys_addr, new_data);
#if 0
	int phys_addr = addr_trans_wr[addr >> 12] + addr;	// translating address with the WRITE table created by apply_memory_config()
	if (phys_addr >= IO_REMAP_VIRTUAL) {
		if ((addr & 0xF000) != 0xD000)
			FATAL("Internal error: IO is not on the IO space!");
		if (addr < 0xD800 || addr >= (vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00) {	// though, for only memory areas other than colour RAM (avoids unneeded warnings as well)
			DEBUG("CPU: RMW opcode is used on I/O area for $%04X" NL, addr);
			io_write(addr, old_data);	// first write back the old data ...
		}
		io_write(addr, new_data);	// ... then the new
		return;
	}
	write_phys_mem((phys_addr & 0xFFFFF) | addr_trans_wr_megabyte[addr >> 12], new_data);	// "normal" memory, just write once, no need to emulate the behaviour
#endif
}



static void shutdown_callback ( void )
{
#ifdef MEMDUMP_FILE
	FILE *f;
#endif
	int a;
	for (a = 0; a < 0x40; a++)
		DEBUG("VIC-3 register $%02X is %02X" NL, a, vic3_registers[a]);
	cia_dump_state (&cia1);
	cia_dump_state (&cia2);
#ifdef MEMDUMP_FILE
	// Dump hypervisor memory to a file, so you can check it after exit.
	f = fopen(MEMDUMP_FILE, "wb");
	if (f) {
		fwrite(hypervisor_memory, 1, 0x4000, f);
		fclose(f);
		DEBUG("Hypervisor memory state is dumped into " MEMDUMP_FILE NL);
	}
#endif
#ifdef UARTMON_SOCKET
	uartmon_close();
#endif
	DEBUG("Execution has been stopped at PC=$%04X [$%05X]" NL, cpu_pc, addr_trans_rd[cpu_pc >> 12] + cpu_pc);
}



// Called by emutools_hid!!! to handle special private keys assigned to this emulator
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	// Check for special, emulator-related hot-keys (not C65 key)
	if (pressed) {
		if (key == SDL_SCANCODE_F10) {
			force_fast = 0;	// FIXME: other default speed controls on reset?
			c128_d030_reg = 0xFF;
			machine_set_speed(0);
			CPU_PORT(0) = CPU_PORT(1) = 0xFF;
			map_mask = 0;
			vic3_registers[0x30] = 0;
			in_hypervisor = 0;
			apply_memory_config();
			cpu_reset();
			dma_reset();
			nmi_level = 0;
			kicked_hypervisor = emucfg_get_num("kicked");
			hypervisor_enter(TRAP_RESET);
			DEBUG("RESET!" NL);
		} else if (key == SDL_SCANCODE_KP_ENTER) {
			c64_toggle_joy_emu();
		}
	}
	return 0;
}



static void update_emulator ( void )
{
	hid_handle_all_sdl_events();
	nmi_set(IS_RESTORE_PRESSED(), 2);	// Custom handling of the restore key ...
#ifdef UARTMON_SOCKET
	uartmon_update();
#endif
	// Screen rendering: begin
	vic3_render_screen();
	// Screen rendering: end
	emu_timekeeping_delay(40000);
	// Ugly CIA trick to maintain realtime TOD in CIAs :)
        if (seconds_timer_trigger) {
		struct tm *t = emu_get_localtime();
		cia_ugly_tod_updater(&cia1, t);
		cia_ugly_tod_updater(&cia2, t);
	}
}



void m65mon_show_regs ( void )
{
	umon_printf(
		"PC   A  X  Y  Z  B  SP   MAPL MAPH LAST-OP     P  P-FLAGS   RGP uS IO\r\n"
		"%04X %02X %02X %02X %02X %02X %04X "		// register banned message and things from PC to SP
		"%04X %04X %02X       %02X %02X "		// from MAPL to P
		"%c%c%c%c%c%c%c%c ",				// P-FLAGS
		cpu_pc, cpu_a, cpu_x, cpu_y, cpu_z, cpu_bphi >> 8, cpu_sphi | cpu_sp,
		map_offset_low >> 8, map_offset_high >> 8, cpu_op,
		cpu_get_p(), 0,	// flags
		cpu_pfn ? 'N' : '-',
		cpu_pfv ? 'V' : '-',
		cpu_pfe ? 'E' : '-',
		cpu_pfb ? 'B' : '-',
		cpu_pfd ? 'D' : '-',
		cpu_pfi ? 'I' : '-',
		cpu_pfz ? 'Z' : '-',
		cpu_pfc ? 'C' : '-'
	);
}

void m65mon_dumpmem16 ( Uint16 addr )
{
	int n = 16;
	umon_printf(":000%04X", addr);
	while (n--)
		umon_printf(" %02X", cpu_read(addr++));
}

void m65mon_set_trace ( int m )
{
	paused = m;
}

void m65mon_do_trace ( void )
{
	if (paused) {
		umon_send_ok = 0; // delay command execution!
		m65mon_callback = m65mon_show_regs; // register callback
		trace_step_trigger = 1;	// trigger one step
	} else {
		umon_printf(SYNTAX_ERROR "trace can be used only in trace mode");
	}
}

void m65mon_do_trace_c ( void )
{
	umon_printf(SYNTAX_ERROR "command 'tc' is not implemented yet");
}

void m65mon_empty_command ( void )
{
	if (paused)
		m65mon_do_trace();
}

void m65mon_breakpoint ( int brk )
{
	breakpoint_pc = brk;
}



int main ( int argc, char **argv )
{
	int cycles, frameskip;
	xemu_dump_version(stdout, "The Incomplete Commodore-65/Mega-65 emulator from LGB");
	emucfg_define_str_option("8", NULL, "Path of EXTERNAL D81 disk image (not on/the SD-image)");
	emucfg_define_num_option("dmarev", 0, "Revision of the DMAgic chip  (0=F018A, other=F018B)");
	emucfg_define_str_option("fpga", NULL, "Comma separated list of FPGA-board switches turned ON");
	emucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	emucfg_define_switch_option("hyperdebug", "Crazy, VERY slow and 'spammy' hypervisor debug mode");
	emucfg_define_num_option("kicked", 0x0, "Answer to KickStart upgrade (128=ask user in a pop-up window)");
	emucfg_define_str_option("kickup", KICKSTART_NAME, "Override path of external KickStart to be used");
	emucfg_define_str_option("kickuplist", NULL, "Set path of symbol list file for external KickStart");
	emucfg_define_str_option("sdimg", SDCARD_NAME, "Override path of SD-image to be used");
#ifdef XEMU_SNAPSHOT_SUPPORT
	emucfg_define_str_option("snapload", NULL, "Load a snapshot from the given file");
	emucfg_define_str_option("snapsave", NULL, "Save a snapshot into the given file before Xemu would exit");
#endif
	emucfg_define_switch_option("skipunhandledmem", "Do not panic on unhandled memory access (hides problems!!)");
	emucfg_define_switch_option("c65speed", "Allow emulation of 48MHz (problematic, currently)");
	if (emucfg_parse_commandline(argc, argv, NULL))
		return 1;
	if (xemu_byte_order_test())
		FATAL("Byte order test failed!!");
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	window_title_info_addon = emulator_speed_title;
        if (emu_init_sdl(
		TARGET_DESC APP_DESC_APPEND,	// window title
		APP_ORG, TARGET_NAME,		// app organization and name, used with SDL pref dir formation
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT * 2,// logical size (used with keeping aspect ratio by the SDL render stuffs)
		SCREEN_WIDTH, SCREEN_HEIGHT * 2,// window size
		SCREEN_FORMAT,			// pixel format
		0,				// we have *NO* pre-defined colours as with more simple machines (too many we need). we want to do this ourselves!
		NULL,				// -- "" --
		NULL,				// -- "" --
		RENDER_SCALE_QUALITY,		// render scaling quality
		USE_LOCKED_TEXTURE,		// 1 = locked texture access
		shutdown_callback		// registered shutdown function
	))
		return 1;
	// Initialize Mega65
	mega65_init(
		SID_CYCLES_PER_SEC,		// SID cycles per sec
		AUDIO_SAMPLE_FREQ		// sound mix freq
	);
	// Start!!
	skip_unhandled_mem = emucfg_get_bool("skipunhandledmem");
	disallow_turbo = emucfg_get_bool("c65speed");
	if (disallow_turbo)
		printf("SPEED: WARNING: limitation of max CPU clock to 3.5MHz request is in use!" NL);
	printf("UNHANDLED memory policy: %d" NL, skip_unhandled_mem);
	cycles = 0;
	frameskip = 0;
	frame_counter = 0;
	vic3_blink_phase = 0;
	emu_timekeeping_start();
	if (audio)
		SDL_PauseAudioDevice(audio, 0);
	emu_set_full_screen(emucfg_get_bool("fullscreen"));
	for (;;) {
		while (paused) {	// paused special mode, ie tracing support, or something ...
			if (m65mon_callback) {	// delayed uart monitor command should be finished ...
				m65mon_callback();
				m65mon_callback = NULL;
				uartmon_finish_command();
			}
			// we still need to feed our emulator with update events ... It also slows this pause-busy-loop down to every full frames (~25Hz)
			// note, that it messes timing up a bit here, as there is update_emulator() calls later in the "normal" code as well
			// this can be a bug, but real-time emulation is not so much an issue if you eg doing trace of your code ...
			update_emulator();
			if (trace_step_trigger) {
				// if monitor trigges a step, break the pause loop, however we will get back the control on the next
				// iteration of the infinite "for" loop, as "paused" is not altered
				trace_step_trigger = 0;
				break;	// break the pause loop now
			}
			// Decorate window title about the mode.
			// If "paused" mode is switched off ie by a monitor command (called from update_emulator() above!)
			// then it will resets back the the original state, etc
			window_title_custom_addon = paused ? (char*)emulator_paused_title : NULL;
			if (paused != paused_old) {
				paused_old = paused;
				if (paused)
					fprintf(stderr, "TRACE: entering into trace mode @ $%04X" NL, cpu_pc);
				else
					fprintf(stderr, "TRACE: leaving trace mode @ $%04X" NL, cpu_pc);
			}
		}
		if (in_hypervisor) {
			hypervisor_debug();
		}
		if (breakpoint_pc == cpu_pc) {
			fprintf(stderr, "Breakpoint @ $%04X hit, Xemu moves to trace mode after the execution of this opcode." NL, cpu_pc);
			paused = 1;
		}
		cycles += unlikely(dma_status) ? dma_update() : cpu_step();	// FIXME: this is maybe not correct, that DMA's speed depends on the fast/slow clock as well?
		if (cycles >= cpu_cycles_per_scanline) {
			scanline++;
			//DEBUG("VIC3: new scanline (%d)!" NL, scanline);
			cycles -= cpu_cycles_per_scanline;
			cia_tick(&cia1, 64);
			cia_tick(&cia2, 64);
			if (scanline == 312) {
				//DEBUG("VIC3: new frame!" NL);
				frameskip = !frameskip;
				scanline = 0;
				if (!frameskip)	// well, let's only render every full frames (~ie 25Hz)
					update_emulator();
				sid1.sFrameCount++;
				sid2.sFrameCount++;
				frame_counter++;
				if (frame_counter == 25) {
					frame_counter = 0;
					vic3_blink_phase = !vic3_blink_phase;
				}
			}
			//DEBUG("RASTER=%d COMPARE=%d" NL,scanline,compare_raster);
			//vic_interrupt();
			vic3_check_raster_interrupt();
		}
	}
	return 0;
}

/* --- SNAPSHOT RELATED --- */

#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_M65_BLOCK_VERSION	0
#define SNAPSHOT_M65_BLOCK_SIZE		(0x100 + sizeof(gs_regs))


int m65emu_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_M65_BLOCK_SIZE];
	int a;
	if (block->block_version != SNAPSHOT_M65_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad M65 block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	map_mask = (int)P_AS_BE32(buffer + 0);
	map_offset_low = (int)P_AS_BE32(buffer + 4);
	map_offset_high = (int)P_AS_BE32(buffer + 8);
	cpu_inhibit_interrupts = (int)P_AS_BE32(buffer + 12);
	in_hypervisor = (int)P_AS_BE32(buffer + 16);
	map_megabyte_low = (int)P_AS_BE32(buffer + 20);
	map_megabyte_high = (int)P_AS_BE32(buffer + 24);
	rom_protect = (int)P_AS_BE32(buffer + 28);
	kicked_hypervisor = (int)P_AS_BE32(buffer + 32);
	memcpy(gs_regs, buffer + 0x100, sizeof gs_regs);
	return 0;
}


int m65emu_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_M65_BLOCK_SIZE];
	int a = xemusnap_write_block_header(def->idstr, SNAPSHOT_M65_BLOCK_VERSION);
	if (a) return a;
	memset(buffer, 0xFF, sizeof buffer);
	/* saving state ... */
	U32_AS_BE(buffer +  0, map_mask);
	U32_AS_BE(buffer +  4, map_offset_low);
	U32_AS_BE(buffer +  8, map_offset_high);
	U32_AS_BE(buffer + 12, cpu_inhibit_interrupts);
	U32_AS_BE(buffer + 16, in_hypervisor);
	U32_AS_BE(buffer + 20, map_megabyte_low);
	U32_AS_BE(buffer + 24, map_megabyte_high);
	U32_AS_BE(buffer + 28, rom_protect);
	U32_AS_BE(buffer + 32, kicked_hypervisor);
	memcpy(buffer + 0x100, gs_regs, sizeof gs_regs);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}


int m65emu_snapshot_loading_finalize ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	printf("SNAP: loaded (finalize-callback: begin)" NL);
	apply_memory_config();
	machine_set_speed(1);
	printf("SNAP: loaded (finalize-callback: end)" NL);
	return 0;
}
#endif
