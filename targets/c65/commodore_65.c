/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 emulator.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include "emutools.h"
#include "commodore_65.h"
#include "cpu65c02.h"
#include "cia6526.h"
#include "f011_core.h"
#include "c65_d81_image.h"
#include "c65dma.h"
#include "emutools_hid.h"
#include "vic3.h"
#include "sid.h"
#include "cbmhostfs.h"
#include "c64_kbd_mapping.h"
#include "emutools_config.h"
#include "c65_snapshot.h"



static SDL_AudioDeviceID audio = 0;

Uint8 memory[0x100000];			// 65CE02 MAP'able address space
struct Cia6526 cia1, cia2;		// CIA emulation structures for the two CIAs
static struct SidEmulation sids[2];	// the two SIDs

// We re-map I/O requests to a high address space does not exist for real. cpu_read() and cpu_write() should handle this as an IO space request
// It must be high enough not to collide with the 1Mbyte address space + almost-64K "overflow" area and mapping should not cause to alter lower 12 bits of the addresses,
// (that is, the constant should have three '0' hex digits at its end)
// though the exact value does not matter too much over the mentioned requirements above.
#define IO_REMAP_VIRTUAL	0x110000
// Other re-mapping addresses
// Re-mapping for VIC3 reg $30
#define ROM_C000_REMAP		 0x20000
#define ROM_8000_REMAP		 0x30000
#define ROM_A000_REMAP		 0x30000
#define ROM_E000_REMAP		 0x30000
// Re-mapping for "CPU-port" related stuffs
#define ROM_C64_CHR_REMAP	 0x20000
#define ROM_C64_KERNAL_REMAP	 0x20000
#define ROM_C64_BASIC_REMAP	 0x20000

static int addr_trans_rd[16];		// address translating offsets for READ operation (it can be added to the CPU address simply, selected by the high 4 bits of the CPU address)
static int addr_trans_wr[16];		// address translating offsets for WRITE operation (it can be added to the CPU address simply, selected by the high 4 bits of the CPU address)
static int map_mask;			// MAP mask, should be filled at the MAP opcode, *before* calling apply_memory_config() then
static int map_offset_low;		// MAP low offset, should be filled at the MAP opcode, *before* calling apply_memory_config() then
static int map_offset_high;		// MAP high offset, should be filled at the MAP opcode, *before* calling apply_memory_config() then





/* You *MUST* call this every time, when *any* of these events applies:
   * MAP 4510 opcode is issued, map_offset_low, map_offset_high, map_mask are modified
   * "CPU port" data or DDR register has been written, witn cpu_port[0 or 1] modified
   * VIC3 register $30 is written, with vic3_registers[0x30] modified
   The reason of this madness: do the ugly work here, as memory configuration change is
   less frequent than memory usage (read/write). Thus do more work here, but simplier
   work when doing actual memory read/writes, with a simple addition and shift, or such.
   The tables are 4K in steps, 4510 would require only 8K steps, but there are other
   reasons (ie, I/O area is only 4K long, mapping is not done by the CPU).
   More advanced technique can be used not to handle *everything* here, but it's better
   for the initial steps, to have all address translating logic at once.
*/
void apply_memory_config ( void )
{
	// FIXME: what happens if VIC-3 reg $30 mapped ROM is tried to be written? Ignored, or RAM is used to write to, as with the CPU port mapping?
	// About the produced signals on the "CPU port"
	int cp = (memory[1] | (~memory[0]));
	// Simple ones, only CPU MAP may apply not other factors
	// Also, these are the "lower" blocks, needs the offset for the "lower" area in case of CPU MAP'ed state
	addr_trans_wr[0] = addr_trans_rd[0] = addr_trans_wr[1] = addr_trans_rd[1] = (map_mask & 1) ? map_offset_low : 0;	// $0XXX + $1XXX, MAP block 0 [mask 1]
	addr_trans_wr[2] = addr_trans_rd[2] = addr_trans_wr[3] = addr_trans_rd[3] = (map_mask & 2) ? map_offset_low : 0;	// $2XXX + $3XXX, MAP block 1 [mask 2]
	addr_trans_wr[4] = addr_trans_rd[4] = addr_trans_wr[5] = addr_trans_rd[5] = (map_mask & 4) ? map_offset_low : 0;	// $4XXX + $5XXX, MAP block 2 [mask 4]
	addr_trans_wr[6] = addr_trans_rd[6] = addr_trans_wr[7] = addr_trans_rd[7] = (map_mask & 8) ? map_offset_low : 0;	// $6XXX + $7XXX, MAP block 3 [mask 8]
	// From this point, we must use the "high" area offset if it's CPU MAP'ed
	// $8XXX and $9XXX, MAP block 4 [mask 16]
	if (vic3_registers[0x30] & 8)
		addr_trans_wr[8] = addr_trans_rd[8] = addr_trans_wr[9] = addr_trans_rd[9] = ROM_8000_REMAP;
	else if (map_mask & 16)
		addr_trans_wr[8] = addr_trans_rd[8] = addr_trans_wr[9] = addr_trans_rd[9] = map_offset_high;
	else
		addr_trans_wr[8] = addr_trans_rd[8] = addr_trans_wr[9] = addr_trans_rd[9] = 0;
	// $AXXX and $BXXX, MAP block 5 [mask 32]
	if (vic3_registers[0x30] & 16)
		addr_trans_wr[0xA] = addr_trans_rd[0xA] = addr_trans_wr[0xB] = addr_trans_rd[0xB] = ROM_A000_REMAP;
	else if ((map_mask & 32))
		addr_trans_wr[0xA] = addr_trans_rd[0xA] = addr_trans_wr[0xB] = addr_trans_rd[0xB] = map_offset_high;
	else {
		addr_trans_wr[0xA] = addr_trans_wr[0xB] = 0;
		addr_trans_rd[0xA] = addr_trans_rd[0xB] = ((cp & 3) == 3) ? ROM_C64_BASIC_REMAP : 0;
	}
	// $CXXX, MAP block 6 [mask 64]
	// Warning: all VIC3 reg $30 related ROM maps are for 8K size, *except* of '@C000' (interface ROM) which is only 4K! Also this is in another ROM bank than the others
	if (vic3_registers[0x30] & 32)
		addr_trans_wr[0xC] = addr_trans_rd[0xC] = ROM_C000_REMAP;
	else
		addr_trans_wr[0xC] = addr_trans_rd[0xC] = (map_mask & 64) ? map_offset_high : 0;
	// $DXXX, *still* MAP block 6 [mask 64]
	if (map_mask & 64)
		addr_trans_wr[0xD] = addr_trans_rd[0xD] = map_offset_high;
	else {
		if ((cp & 7) > 4) {
			addr_trans_wr[0xD] = addr_trans_rd[0xD] = IO_REMAP_VIRTUAL;
		} else {
			addr_trans_wr[0xD] = 0;
			addr_trans_rd[0xD] = (cp & 3) ? ROM_C64_CHR_REMAP : 0;
		}
	}
	// $EXXX and $FXXX, MAP block 7 [mask 128]
	if (vic3_registers[0x30] & 128)
		addr_trans_wr[0xE] = addr_trans_rd[0xE] = addr_trans_wr[0xF] = addr_trans_rd[0xF] = ROM_E000_REMAP;
	else if (map_mask & 128)
		addr_trans_wr[0xE] = addr_trans_rd[0xE] = addr_trans_wr[0xF] = addr_trans_rd[0xF] = map_offset_high;
	else {
		addr_trans_wr[0xE] = addr_trans_wr[0xF] = 0;
		addr_trans_rd[0xE] = addr_trans_rd[0xF] = ((cp & 3) > 1) ? ROM_C64_KERNAL_REMAP : 0;
	}
}



static void cia_setint_cb ( int level )
{
	DEBUG("%s: IRQ level changed to %d" NL, cia1.name, level);
	if (level)
		cpu_irqLevel |= 1;
	else
		cpu_irqLevel &= ~1;
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
	vic3_select_bank((~(data | (~cia2.DDRA))) & 3);
	//vic2_16k_bank = ((~(data | (~cia2.DDRA))) & 3) << 14;
	//DEBUG("VIC2: 16K BANK is set to $%04X (CIA mask=$%02X)" NL, vic2_16k_bank, cia2.DDRA);
}



// Just for easier test to have a given port value for CIA input ports
static Uint8 cia_port_in_dummy ( void )
{
	return 0xFF;
}



static void audio_callback(void *userdata, Uint8 *stream, int len)
{
	DEBUG("AUDIO: audio callback, wants %d samples" NL, len);
	// We use the trick, to render boths SIDs with step of 2, with a byte offset
	// to get a stereo stream, wanted by SDL.
	sid_render(&sids[1], ((short *)(stream)) + 0, len >> 1, 2);	// SID @ left
	sid_render(&sids[0], ((short *)(stream)) + 1, len >> 1, 2);	// SID @ right
}



static void c65_init ( int sid_cycles_per_sec, int sound_mix_freq )
{
	const char *p;
	SDL_AudioSpec audio_want, audio_got;
	hid_init(
		c64_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// joy HID events enabled
	);
	joystick_emu = 1;
	// *** host-FS
	p = emucfg_get_str("hostfsdir");
	if (p)
		hostfs_init(p, NULL);
	else
		hostfs_init(sdl_pref_dir, "hostfs");
	// *** Init memory space
	memset(memory, 0xFF, sizeof memory);
	// *** Load ROM image
	p = emucfg_get_str("rom");
	if (emu_load_file(p, memory + 0x20000, 0x20001) != 0x20000)
		FATAL("Cannot load C65 system ROM (%s) or invalid size!", p);
	// *** Initialize VIC3
	vic3_init();
	// *** Memory configuration
	memory[0] = memory[1] = 0xFF;		// the "CPU I/O port" on 6510/C64, implemented by VIC3 for real in C65!
	map_mask = 0;				// as all 8K blocks are unmapped, we don't need to worry about the low/high offset to set here
	apply_memory_config();			// VIC3 $30 reg is already filled, so it's OK to call this now
	// *** CIAs
	cia_init(&cia1, "CIA-1",
		NULL,			// callback: OUTA
		NULL,			// callback: OUTB
		NULL,			// callback: OUTSR
		cia1_in_a,		// callback: INA ~ joy#2
		cia1_in_b,		// callback: INB ~ keyboard
		NULL,			// callback: INSR
		cia_setint_cb		// callback: SETINT
	);
	cia_init(&cia2, "CIA-2",
		cia2_out_a,		// callback: OUTA ~ eg VIC-II bank
		NULL,			// callback: OUTB
		NULL,			// callback: OUTSR
		cia_port_in_dummy,	// callback: INA
		NULL,			// callback: INB
		NULL,			// callback: INSR
		NULL			// callback: SETINT ~ that would be NMI in our case
	);
	// *** Initialize DMA
	dma_init();
	// Initialize FDC
	fdc_init();
	c65_d81_init(emucfg_get_str("8"));
	// SIDs, plus SDL audio
	sid_init(&sids[0], sid_cycles_per_sec, sound_mix_freq);
	sid_init(&sids[1], sid_cycles_per_sec, sound_mix_freq);
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
	// *** RESET CPU, also fetches the RESET vector into PC
	cpu_reset();
	DEBUG("INIT: end of initialization!" NL);
	// *** Snapshot init and loading etc should be the LAST!!!!
#ifdef XEMU_SNAPSHOT_SUPPORT
	if (!c65snapshot_init(emucfg_get_str("snapload"), emucfg_get_str("snapsave"))) {
		printf("SNAP: ok, snapshot loaded, doing post-load steps." NL);
		apply_memory_config();
	}
#endif
}



// *** Implements the MAP opcode of 4510, called by the 65CE02 emulator
void cpu_do_aug ( void )
{
	cpu_inhibit_interrupts = 1;	// disable interrupts to the next "EOM" (ie: NOP) opcode
	DEBUG("CPU: MAP opcode, input A=$%02X X=$%02X Y=$%02X Z=$%02X" NL, cpu_a, cpu_x, cpu_y, cpu_z);
	map_offset_low  = (cpu_a << 8) | ((cpu_x & 15) << 16);	// offset of lower half (blocks 0-3)
	map_offset_high = (cpu_y << 8) | ((cpu_z & 15) << 16);	// offset of higher half (blocks 4-7)
	map_mask        = (cpu_z & 0xF0) | (cpu_x >> 4);	// "is mapped" mask for blocks (1 bit for each)
	DEBUG("MEM: applying new memory configuration because of MAP CPU opcode" NL);
	DEBUG("LOW -OFFSET = $%X" NL, map_offset_low);
	DEBUG("HIGH-OFFSET = $%X" NL, map_offset_high);
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
		DEBUG("CPU: NOP in not treated as EOM (no MAP before)" NL);
}


static inline Uint8 read_some_sid_register ( int addr )
{
	// currently we don't support reading SID registers at all (1351 mouse emulation may need POT-X and POT-Y in the future though, TODO)
	// addr &= 0x1F;
	return 0xFF;
}


static inline void write_some_sid_register ( int addr, Uint8 data )
{
	int instance = (addr >> 6) & 1; // Selects left/right SID based on address
	DEBUG("SID%d: writing register $%04X ($%04X) with data $%02X @ PC=$%04X" NL, ((addr >> 6) & 1) + 1, addr & 0x1F, addr + 0xD000, data, cpu_pc);
	sid_write_reg(&sids[instance], addr & 0x1F, data);
}


Uint8 io_read ( int addr )
{
	addr &= 0xFFF;	// Internally, we use I/O addresses $0000-$0FFF only!
	switch ((addr >> 8) | vic_new_mode) {	// "addr >> 8" is from 0...F,  vic_new_mode can be $10 or 0, that is $00-$1F for all cases
		/* --- I/O read in old VIC I/O mode --- */
		case 0x00:	// $D000-$D0FF
		case 0x01:	// $D100-$D1FF, C65 VIC-III palette, red components   (in old VIC mode, cannot be accessed)
		case 0x02:	// $D200-$D2FF, C65 VIC-III palette, green components (in old VIC mode, cannot be accessed)
		case 0x03:	// $D300-$D3FF, C65 VIC-III palette, blue components  (in old VIC mode, cannot be accessed)
			// In old VIC-II mode, according to c65manual.txt and C64 "standard", VIC-II registers can be seen at every 64 bytes in I/O range $D000-$D3FF
			return vic3_read_reg(addr & 0x3F);
		case 0x04:	// $D400-$D4FF, SID stuffs
		case 0x05:	// $D500-$D5FF
		case 0x06:	// $D600-$D6FF, would be C65 UART, in old I/O mode: SID images still
		case 0x07:	// $D700-$D7FF, would be C65 DMA, in old I/O mode; SID images still
			return read_some_sid_register(addr);
		case 0x08:	// $D800-$D8FF, colour SRAM
		case 0x09:	// $D900-$D9FF, colour SRAM
		case 0x0A:	// $DA00-$DAFF, colour SRAM
		case 0x0B:	// $DB00-$DBFF, colour SRAM
			return memory[0x1F000 + addr];	// 0x1F800 would be the colour RAM on C65, but since the offset in I/O address range is already $800, we use 0x1F000
		case 0x0C:	// $DC00-$DCFF, CIA-1 or colour SRAM second half
			// TODO: check if "extended colour SRAM" would work at all in old I/O mode this way!!!!
			return vic3_registers[0x30] & 1 ? memory[0x1F000 + addr] : cia_read(&cia1, addr & 0xF);
		case 0x0D:	// $DD00-$DDFF, CIA-2 or colour SRAM second half
			return vic3_registers[0x30] & 1 ? memory[0x1F000 + addr] : cia_read(&cia2, addr & 0xF);
		case 0x0E:	// $DE00-$DEFF, I/O-1 or colour SRAM second half on C65
		case 0x0F:	// $DF00-$DFFF, I/O-2 or colour SRAM second half on C65
			return vic3_registers[0x30] & 1 ? memory[0x1F000 + addr] : 0xFF;	// I/O exp area is not emulated by Xemu, gives $FF on reads
		/* --- I/O read in new VIC I/O mode --- */
		case 0x10:	// $D000-$D0FF
			if (likely(addr < 0x80))
				return vic3_read_reg(addr);
			if (addr < 0xA0)
				return fdc_read_reg(addr & 0xF);
			if (addr == 0xFE)
				return hostfs_read_reg0();
			if (addr == 0xFF)
				return hostfs_read_reg1();
			return 0xFF;	// RAM expansion controller (not emulated by Xemu)
		case 0x11:	// $D100-$D1FF, C65 VIC-III palette, red components
		case 0x12:	// $D200-$D2FF, C65 VIC-III palette, green components
		case 0x13:	// $D300-$D3FF, C65 VIC-III palette, blue components
			return 0xFF;	// palette registers are write-only on C65????
		case 0x14:	// $D400-$D4FF, SID stuffs
		case 0x15:	// $D500-$D5FF
			return read_some_sid_register(addr);
		case 0x16:	// $D600-$D6FF, C65 UART
			return 0xFF;			// not emulated by Xemu, yet, TODO
		case 0x17:	// $D700-$D7FF, C65 DMA
			return dma_read_reg(addr & 15);
		case 0x18:	// $D800-$D8FF, colour SRAM
		case 0x19:	// $D900-$D9FF, colour SRAM
		case 0x1A:	// $DA00-$DAFF, colour SRAM
		case 0x1B:	// $DB00-$DBFF, colour SRAM
			return memory[0x1F000 + addr];  // 0x1F800 would be the colour RAM on C65, but since the offset in I/O address range is already $800, we use 0x1F000
		case 0x1C:
			return vic3_registers[0x30] & 1 ? memory[0x1F000 + addr] : cia_read(&cia1, addr & 0xF);
		case 0x1D:
			return vic3_registers[0x30] & 1 ? memory[0x1F000 + addr] : cia_read(&cia2, addr & 0xF);
		case 0x1E:	// $DE00-$DEFF, I/O-1 or colour SRAM second half on C65
		case 0x1F:	// $DF00-$DFFF, I/O-2 or colour SRAM second half on C65
			return vic3_registers[0x30] & 1 ? memory[0x1F000 + addr] : 0xFF;	// I/O exp area is not emulated by Xemu, gives $FF on reads
		default:
			FATAL("Invalid switch case in io_read(%d)!! CASE=%X, vic_new_mode=%d", addr, (addr >> 8) | vic_new_mode, vic_new_mode);
			break;
	}
	return 0xFF;	// just make gcc happy ...
}




void io_write ( int addr, Uint8 data )
{
	addr &= 0xFFF;	// Internally, we use I/O addresses $0000-$0FFF only!
	switch ((addr >> 8) | vic_new_mode) {	// "addr >> 8" is from 0...F,  vic_new_mode can be $10 or 0, that is $00-$1F for all cases
		/* --- I/O write in old VIC I/O mode --- */
		case 0x00:	// $D000-$D0FF
		case 0x01:	// $D100-$D1FF, C65 VIC-III palette, red components   (in old VIC mode, cannot be accessed)
		case 0x02:	// $D200-$D2FF, C65 VIC-III palette, green components (in old VIC mode, cannot be accessed)
		case 0x03:	// $D300-$D3FF, C65 VIC-III palette, blue components  (in old VIC mode, cannot be accessed)
			// In old VIC-II mode, according to c65manual.txt and C64 "standard", VIC-II registers can be seen at every 64 bytes in I/O range $D000-$D3FF
			vic3_write_reg(addr & 0x3F, data);
			return;
		case 0x04:	// $D400-$D4FF, SID stuffs
		case 0x05:	// $D500-$D5FF
		case 0x06:	// $D600-$D6FF, would be C65 UART, in old I/O mode: SID images still
		case 0x07:	// $D700-$D7FF, would be C65 DMA, in old I/O mode; SID images still
			write_some_sid_register(addr, data);
			return;
		case 0x08:	// $D800-$D8FF, colour SRAM
		case 0x09:	// $D900-$D9FF, colour SRAM
		case 0x0A:	// $DA00-$DAFF, colour SRAM
		case 0x0B:	// $DB00-$DBFF, colour SRAM
			memory[0x1F000 + addr] = data;	// 0x1F800 would be the colour RAM on C65, but since the offset in I/O address range is already $800, we use 0x1F000
			return;
		case 0x0C:	// $DC00-$DCFF, CIA-1 or colour SRAM second half
			// TODO: check if "extended colour SRAM" would work at all in old I/O mode this way!!!!
			if (vic3_registers[0x30] & 1)
				memory[0x1F000 + addr] = data;
			else
				cia_write(&cia1, addr & 0xF, data);
			return;
		case 0x0D:	// $DD00-$DDFF, CIA-2 or colour SRAM second half
			if (vic3_registers[0x30] & 1)
				memory[0x1F000 + addr] = data;
			else
				cia_write(&cia2, addr & 0xF, data);
			return;
		case 0x0E:	// $DE00-$DEFF, I/O-1 or colour SRAM second half on C65
		case 0x0F:	// $DF00-$DFFF, I/O-2 or colour SRAM second half on C65
			if (vic3_registers[0x30] & 1)
				memory[0x1F000 + addr] = data;
			return;
		/* --- I/O write in new VIC I/O mode --- */
		case 0x10:	// $D000-$D0FF
			if (likely(addr < 0x80)) {
				vic3_write_reg(addr, data);
				return;
			}
			if (addr < 0xA0) {
				fdc_write_reg(addr & 0xF, data);
				return;
			}
			if (addr == 0xFE) {
				hostfs_write_reg0(data);
				return;
			}
			if (addr == 0xFF) {
				hostfs_write_reg1(data);
				return;
			}
			return;	// Note: RAM expansion controller (not emulated by Xemu)
		case 0x11:	// $D100-$D1FF, C65 VIC-III palette, red components
		case 0x12:	// $D200-$D2FF, C65 VIC-III palette, green components
		case 0x13:	// $D300-$D3FF, C65 VIC-III palette, blue components
			vic3_write_palette_reg(addr - 0x100, data);
			return;
		case 0x14:	// $D400-$D4FF, SID stuffs
		case 0x15:	// $D500-$D5FF
			write_some_sid_register(addr, data);
			return;
		case 0x16:	// $D600-$D6FF, C65 UART
			return;				// not emulated by Xemu, yet, TODO
		case 0x17:	// $D700-$D7FF, C65 DMA
			dma_write_reg(addr & 15, data);
			return;
		case 0x18:	// $D800-$D8FF, colour SRAM
		case 0x19:	// $D900-$D9FF, colour SRAM
		case 0x1A:	// $DA00-$DAFF, colour SRAM
		case 0x1B:	// $DB00-$DBFF, colour SRAM
			memory[0x1F000 + addr] = data;  // 0x1F800 would be the colour RAM on C65, but since the offset in I/O address range is already $800, we use 0x1F000
			return ;
		case 0x1C:
			if (vic3_registers[0x30] & 1)
				memory[0x1F000 + addr] = data;
			else
				cia_write(&cia1, addr & 0xF, data);
			return;
		case 0x1D:
			if (vic3_registers[0x30] & 1)
				memory[0x1F000 + addr] = data;
			else
				cia_write(&cia2, addr & 0xF, data);
			return;
		case 0x1E:	// $DE00-$DEFF, I/O-1 or colour SRAM second half on C65
		case 0x1F:	// $DF00-$DFFF, I/O-2 or colour SRAM second half on C65
			if (vic3_registers[0x30] & 1)
				memory[0x1F000 + addr] = data;
			return;
		default:
			FATAL("Invalid switch case in io_write(%d)!! CASE=%X, vic_new_mode=%d", addr, (addr >> 8) | vic_new_mode, vic_new_mode);
			return;
	}
}



void write_phys_mem ( int addr, Uint8 data )
{
	addr &= 0xFFFFF;
	if (unlikely(addr < 2)) {	// "CPU port" at memory addr 0/1
		if ((memory[addr] & 7) != (data & 7)) {
			memory[addr] = data;
			DEBUG("MEM: applying new memory configuration because of CPU port writing" NL);
			apply_memory_config();
		} else
			memory[addr] = data;
	} else if (
		(likely(addr < 0x20000))
#if defined(ALLOW_256K_RAMEXP) && defined(ALLOW_512K_RAMEXP)
		|| (addr >= 0x40000)
#else
#	ifdef ALLOW_256K_RAMEXP
		|| (addr >= 0x40000 && addr < 0x80000)
#	endif
#	ifdef ALLOW_512K_RAMEXP
		|| (addr >= 0x80000)
#	endif
#endif
	)
		memory[addr] = data;
}



Uint8 read_phys_mem ( int addr )
{
	return memory[addr & 0xFFFFF];
}



// This function is called by the 65CE02 emulator in case of reading a byte (regardless of data or code)
Uint8 cpu_read ( Uint16 addr )
{
	register int phys_addr = addr_trans_rd[addr >> 12] + addr;	// translating address with the READ table created by apply_memory_config()
	if (likely(phys_addr < 0x10FF00))
		return memory[phys_addr & 0xFFFFF];	// light optimization, do not call read_phys_mem for this single stuff :)
	else
		return io_read(phys_addr);
}



// This function is called by the 65CE02 emulator in case of writing a byte
void cpu_write ( Uint16 addr, Uint8 data )
{
	register int phys_addr = addr_trans_wr[addr >> 12] + addr;	// translating address with the WRITE table created by apply_memory_config()
	if (likely(phys_addr < 0x10FF00))
		write_phys_mem(phys_addr, data);
	else
		io_write(phys_addr, data);
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
	int phys_addr = addr_trans_wr[addr >> 12] + addr;	// translating address with the WRITE table created by apply_memory_config()
	if (likely(phys_addr < 0x10FF00))
		write_phys_mem(phys_addr, new_data);	// "normal" memory, just write once, no need to emulate the behaviour
	else {
		DEBUG("CPU: RMW opcode is used on I/O area for $%04X" NL, addr);
		io_write(phys_addr, old_data);		// first write back the old data ...
		io_write(phys_addr, new_data);		// ... then the new
	}
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
	// Dump memory, so some can inspect the result (low 128K, RAM only)
	f = fopen(MEMDUMP_FILE, "wb");
	if (f) {
		fwrite(memory, 1, 0x20000, f);
		fclose(f);
		DEBUG("Memory is dumped into " MEMDUMP_FILE NL);
	}
#endif
	printf("Scanline render info = \"%s\"" NL, scanline_render_debug_info);
	DEBUG("Execution has been stopped at PC=$%04X [$%05X]" NL, cpu_pc, addr_trans_rd[cpu_pc >> 12] + cpu_pc);
}



// Called by emutools_hid!!! to handle special private keys assigned to this emulator
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	if (pressed) {
		if (key == SDL_SCANCODE_F10) {	// reset
			memory[0] = memory[1] = 0xFF;
			map_mask = 0;
			vic3_registers[0x30] = 0;
			apply_memory_config();
			cpu_reset();
			DEBUG("RESET!" NL);
		} else if (key == SDL_SCANCODE_KP_ENTER)
			c64_toggle_joy_emu();
	}
	return 0;
}


static void update_emulator ( void )
{
	hid_handle_all_sdl_events();
	emu_timekeeping_delay(40000);
	// Ugly CIA trick to maintain realtime TOD in CIAs :)
	if (seconds_timer_trigger) {
		struct tm *t = emu_get_localtime();
		cia_ugly_tod_updater(&cia1, t);
		cia_ugly_tod_updater(&cia2, t);
	}
}





int main ( int argc, char **argv )
{
	int cycles;
	printf("**** The Unusable Commodore 65 emulator from LGB" NL
	"INFO: Texture resolution is %dx%d" NL "%s" NL,
		SCREEN_WIDTH, SCREEN_HEIGHT,
		emulators_disclaimer
	);
	emucfg_define_str_option("8", NULL, "Path of the D81 disk image to be attached");
	emucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	emucfg_define_str_option("hostfsdir", NULL, "Path of the directory to be used as Host-FS base");
	//emucfg_define_switch_option("noaudio", "Disable audio");
	emucfg_define_str_option("rom", "c65-system.rom", "Override system ROM path to be loaded");
#ifdef XEMU_SNAPSHOT_SUPPORT
	emucfg_define_str_option("snapload", NULL, "Load a snapshot from the given file");
	emucfg_define_str_option("snapsave", NULL, "Save a snapshot into the given file before Xemu would exit");
#endif
	if (emucfg_parse_commandline(argc, argv, NULL))
		return 1;
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
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
	// Initialize C65 ...
	c65_init(
		SID_CYCLES_PER_SEC,		// SID cycles per sec
		AUDIO_SAMPLE_FREQ		// sound mix freq
	);
	// Start!!
	cycles = 0;
	if (audio)
		SDL_PauseAudioDevice(audio, 0);
	emu_set_full_screen(emucfg_get_bool("fullscreen"));
	vic3_open_frame_access();
	emu_timekeeping_start();
	for (;;) {
		cycles += cpu_step();
		if (cycles >= cpu_cycles_per_scanline) {
			cia_tick(&cia1, 64);
			cia_tick(&cia2, 64);
			cycles -= cpu_cycles_per_scanline;
			if (vic3_render_scanline()) {
				if (frameskip) {
					frameskip = 0;
					hostfs_flush_all();
				} else {
					frameskip = 1;
					emu_update_screen();
					update_emulator();
					vic3_open_frame_access();
				}
				sids[0].sFrameCount++;
				sids[1].sFrameCount++;
			}
			vic3_check_raster_interrupt();
		}
	}
	return 0;
}

/* --- SNAPSHOT RELATED --- */

#ifdef XEMU_SNAPSHOT_SUPPORT

#include "emutools_snapshot.h"
#include <string.h>

#define SNAPSHOT_C65_BLOCK_VERSION	0
#define SNAPSHOT_C65_BLOCK_SIZE		0x100


int c65emu_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_C65_BLOCK_SIZE];
	int a;
	if (block->block_version != SNAPSHOT_C65_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad C65 block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	map_mask = (int)P_AS_BE32(buffer + 0);
	map_offset_low = (int)P_AS_BE32(buffer + 4);
	map_offset_high = (int)P_AS_BE32(buffer + 8);
	cpu_inhibit_interrupts = (int)P_AS_BE32(buffer + 12);
	return 0;
}


int c65emu_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_C65_BLOCK_SIZE];
	int a = xemusnap_write_block_header(def->idstr, SNAPSHOT_C65_BLOCK_VERSION);
	if (a) return a;
	memset(buffer, 0xFF, sizeof buffer);
	/* saving state ... */
	U32_AS_BE(buffer +  0, map_mask);
	U32_AS_BE(buffer +  4, map_offset_low);
	U32_AS_BE(buffer +  8, map_offset_high);
	U32_AS_BE(buffer + 12, cpu_inhibit_interrupts);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
