/* Test-case for simple, work-in-progress Commodore 65 emulator.

   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_files.h"
#include "commodore_65.h"
#include "xemu/cpu65.h"
#include "xemu/cia6526.h"
#include "xemu/f011_core.h"
#include "xemu/d81access.h"
#include "dma65.h"
#include "xemu/emutools_hid.h"
#include "vic3.h"
#include "xemu/sid.h"
#include "xemu/cbmhostfs.h"
#include "xemu/c64_kbd_mapping.h"
#include "xemu/emutools_config.h"
#include "c65_snapshot.h"
#include "xemu/emutools_gui.h"
#include "ui.h"
#include "inject.h"
#include "configdb.h"


static SDL_AudioDeviceID audio = 0;

Uint8 memory[0x100000];			// 65CE02 MAP'able address space
struct Cia6526 cia1, cia2;		// CIA emulation structures for the two CIAs
struct SidEmulation sids[2];		// the two SIDs
static int nmi_level;			// please read the comment at nmi_set() below
static int mouse_x = 0;
static int mouse_y = 0;
static int shift_status = 0;

char current_rom_filepath[PATH_MAX];

Uint8 disk_cache[512];			// internal memory of the F011 disk controller


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

char emulator_speed_title[] = "???MHz";



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



static void cia1_setint_cb ( int level )
{
	DEBUG("%s: IRQ level changed to %d" NL, cia1.name, level);
	if (level)
		cpu65.irqLevel |= 1;
	else
		cpu65.irqLevel &= ~1;
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
		cpu65.nmiEdge = 1;	// the "NMI edge" trigger is deleted by the CPU emulator itself (it's not a level trigger)
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


static Uint8 port_d607 = 0xFF;


static Uint8 cia1_in_b ( void )
{
#ifdef FAKE_TYPING_SUPPORT
 	if (XEMU_UNLIKELY(c64_fake_typing_enabled) && (((cia1.PRA | (~cia1.DDRA)) & 0xFF) != 0xFF) && (((cia1.PRB | (~cia1.DDRB)) & 0xFF) == 0xFF))
		c64_handle_fake_typing_internals(cia1.PRA | (~cia1.DDRA));
#endif
	return c64_keyboard_read_on_CIA1_B(
		cia1.PRA | (~cia1.DDRA),
		cia1.PRB | (~cia1.DDRB),
		joystick_emu == 1 ? c64_get_joy_state() : 0xFF, port_d607 & 2
	);
}


static Uint8 cia1_in_a ( void )
{
	return c64_keyboard_read_on_CIA1_A(
		cia1.PRB | (~cia1.DDRB),
		cia1.PRA | (~cia1.DDRA),
		joystick_emu == 2 ? c64_get_joy_state() : 0xFF
	);
}


static void cia2_out_a ( Uint8 data )
{
	vic3_select_bank((~(data | (~cia2.DDRA))) & 3);
	//vic2_16k_bank = ((~(data | (~cia2.DDRA))) & 3) << 14;
	//DEBUG("VIC2: 16K BANK is set to $%04X (CIA mask=$%02X)" NL, vic2_16k_bank, cia2.DDRA);
}


static Uint8 cia2_in_a ( void )
{
	// CIA for real seems to always read their input pins on reading the data
	// register, even if it's output. However VIC bank for example should be
	// readable back this way. Trying to implement here something at least
	// resembling a real situation, also taking account the DATA and CLK lines
	// of the IEC bus has input and output too with inverter gates. Though note,
	// IEC bus otherwise is not emulated by Xemu yet.
	return (cia2.PRA & 0x3F) | ((~cia2.PRA << 2) & 0xC0);
}


static Uint8 cia2_in_b ( void )
{
	// Some kind of ad-hoc stuff, allow to read back data out register if the
	// port bit is output, otherwise (input) give bit '1', by virtually
	// emulation a pull-up as its kind. It seems to be needed, as some C65 ROMs
	// actually has check for some user port lines and doing "interesting"
	// things (mostly crashing ...) when something sensed as grounded.
	// It was a kind of hw debug feature for early C65 ROMs.
	return cia2.PRB | ~cia2.DDRB;
}


static void audio_callback(void *userdata, Uint8 *stream, int len)
{
	DEBUG("AUDIO: audio callback, wants %d samples" NL, len);
	// We use the trick, to render boths SIDs with step of 2, with a byte offset
	// to get a stereo stream, wanted by SDL.
	sid_render(&sids[1], ((short *)(stream)) + 0, len >> 1, 2);	// SID @ left
	sid_render(&sids[0], ((short *)(stream)) + 1, len >> 1, 2);	// SID @ right
}


#ifdef XEMU_SNAPSHOT_SUPPORT
static void c65_snapshot_saver_on_exit_callback ( void )
{
	if (!configdb.snapsave)
		return;
	if (xemusnap_save(configdb.snapsave))
		ERROR_WINDOW("Could not save snapshot \"%s\": %s", configdb.snapsave, xemusnap_error_buffer);
	else
		INFO_WINDOW("Snapshot has been saved to \"%s\".", configdb.snapsave);
}
#endif


// define the callback, d81access call this, we can dispatch the change in FDC config to the F011 core emulation this way, automatically
void d81access_cb_chgmode ( const int which, const int mode ) {
	int have_disk = ((mode & 0xFF) != D81ACCESS_EMPTY);
	int can_write = (!(mode & D81ACCESS_RO));
	if (which < 2)
		DEBUGPRINT("C65FDC: configuring F011 FDC (#%d) with have_disk=%d, can_write=%d" NL, which, have_disk, can_write);
	fdc_set_disk(which, have_disk, can_write);
}
// Here we implement F011 core's callbacks using d81access (and yes, F011 uses 512 bytes long sectors for real)
int fdc_cb_rd_sec ( const int which, Uint8 *buffer, const Uint8 side, const Uint8 track, const Uint8 sector )
{
	const int ret = d81access_read_sect(which, buffer, side, track, sector, 512);
	DEBUG("SDCARD: D81: reading sector at d81_pos=(%d,%d,%d), return value=%d" NL, side, track, sector, ret);
	return ret;
}
int fdc_cb_wr_sec ( const int which, Uint8 *buffer, const Uint8 side, const Uint8 track, const Uint8 sector )
{
	const int ret = d81access_write_sect(which, buffer, side, track, sector, 512);
	DEBUG("SDCARD: D81: writing sector at d81_pos=(%d,%d,%d), return value=%d" NL, side, track, sector, ret);
	return ret;
}


int c65_load_rom ( const char *fn, unsigned int dma_rev )
{
	if (xemu_load_file(fn, memory + 0x20000, 0x20000, 0x20000, strcmp(fn, DEFAULT_ROM_FILE) ? "Cannot load specified C65 ROM" : "Cannot load the default C65 ROM") != 0x20000)
		return -1;
	strcpy(current_rom_filepath, xemu_load_filepath);
	dma_init_set_rev(dma_rev, memory + 0x20000 + 0x16);
	return 0;
}



static void c65_init ( int sid_cycles_per_sec, int sound_mix_freq )
{
	//const char *p;
	SDL_AudioSpec audio_want, audio_got;
	hid_init(
		c64_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// joy HID events enabled
	);
#ifdef HID_KBD_MAP_CFG_SUPPORT
	hid_keymap_from_config_file(configdb.keymap);
#endif
	joystick_emu = 2;		// use port-2 by default
	nmi_level = 0;
	// *** host-FS
	if (configdb.hostfsdir)
		hostfs_init(configdb.hostfsdir, NULL);
	else
		hostfs_init(sdl_pref_dir, "hostfs");
	// *** Init memory space
	memset(memory, 0xFF, sizeof memory);
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
		cia1_setint_cb		// callback: SETINT
	);
	cia_init(&cia2, "CIA-2",
		cia2_out_a,		// callback: OUTA ~ eg VIC-II bank
		NULL,			// callback: OUTB
		NULL,			// callback: OUTSR
		cia2_in_a,		// callback: INA
		cia2_in_b,		// callback: INB
		NULL,			// callback: INSR
		cia2_setint_cb		// callback: SETINT ~ that would be NMI in our case
	);
	// *** Initialize DMA
	dma_init(configdb.dmarev);
	// *** Load ROM image
	if (c65_load_rom(configdb.rom, configdb.dmarev))	// ... but this overrides the DMA revision!
		XEMUEXIT(1);
	// Initialize FDC
	fdc_init(disk_cache);
	// Initialize D81 access abstraction for FDC
	d81access_init();
	atexit(d81access_close_all);
	d81access_attach_fsobj(0, configdb.disk8, D81ACCESS_IMG | D81ACCESS_FAKE64 | D81ACCESS_PRG | D81ACCESS_DIR | D81ACCESS_AUTOCLOSE | (configdb.d81ro ? D81ACCESS_RO : 0));
	d81access_attach_fsobj(1, configdb.disk9, D81ACCESS_IMG | D81ACCESS_FAKE64 | D81ACCESS_PRG | D81ACCESS_DIR | D81ACCESS_AUTOCLOSE | (configdb.d81ro ? D81ACCESS_RO : 0));
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
	cpu65_reset();
	DEBUG("INIT: end of initialization!" NL);
	// *** Snapshot init and loading etc should be the LAST!!!! (at least the load must be last to have initiated machine state, xemusnap_init() can be called earlier too)
#ifdef XEMU_SNAPSHOT_SUPPORT
	xemusnap_init(c65_snapshot_definition);
	if (configdb.snapload) {
		if (xemusnap_load(configdb.snapload))
			FATAL("Couldn't load snapshot \"%s\": %s", configdb.snapload, xemusnap_error_buffer);
	}
	atexit(c65_snapshot_saver_on_exit_callback);
#endif
}







// *** Implements the MAP opcode of 4510, called by the 65CE02 emulator
void cpu65_do_aug_callback ( void )
{
	cpu65.cpu_inhibit_interrupts = 1;	// disable interrupts to the next "EOM" (ie: NOP) opcode
	DEBUG("CPU: MAP opcode, input A=$%02X X=$%02X Y=$%02X Z=$%02X" NL, cpu65.a, cpu65.x, cpu65.y, cpu65.z);
	map_offset_low  = (cpu65.a << 8) | ((cpu65.x & 15) << 16);	// offset of lower half (blocks 0-3)
	map_offset_high = (cpu65.y << 8) | ((cpu65.z & 15) << 16);	// offset of higher half (blocks 4-7)
	map_mask        = (cpu65.z & 0xF0) | (cpu65.x >> 4);	// "is mapped" mask for blocks (1 bit for each)
	DEBUG("MEM: applying new memory configuration because of MAP CPU opcode" NL);
	DEBUG("LOW -OFFSET = $%X" NL, map_offset_low);
	DEBUG("HIGH-OFFSET = $%X" NL, map_offset_high);
	DEBUG("MASK        = $%02X" NL, map_mask);
	apply_memory_config();
}


// *** Implements the EOM opcode of 4510, called by the 65CE02 emulator
void cpu65_do_nop_callback ( void )
{
	if (cpu65.cpu_inhibit_interrupts) {
		cpu65.cpu_inhibit_interrupts = 0;
		DEBUG("CPU: EOM, interrupts were disabled because of MAP till the EOM" NL);
	} else
		DEBUG("CPU: NOP in not treated as EOM (no MAP before)" NL);
}


static inline Uint8 read_some_sid_register ( int addr )
{
	// currently we don't support reading SID registers at all (1351 mouse emulation may need POT-X and POT-Y in the future though, TODO)
	switch (addr & 0x1F) {
		case 0x19:
			if (!is_mouse_grab())
				return 0xFF;
			mouse_x = (mouse_x + hid_read_mouse_rel_x(-31, 31)) & 63;
			return mouse_x << 1;
		case 0x1A:
			if (!is_mouse_grab())
				return 0xFF;
			mouse_y = (mouse_y - hid_read_mouse_rel_y(-31, 31)) & 63;
			return mouse_y << 1;
		default:
			return 0xFF;
	}
}


static inline void write_some_sid_register ( int addr, Uint8 data )
{
	int instance = (addr >> 6) & 1; // Selects left/right SID based on address
	DEBUG("SID%d: writing register $%04X ($%04X) with data $%02X @ PC=$%04X" NL, ((addr >> 6) & 1) + 1, addr & 0x1F, addr + 0xD000, data, cpu65.pc);
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
			if (XEMU_LIKELY(addr < 0x80))
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
			//DEBUGPRINT("READ  D%03X" NL, addr);
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
			if (XEMU_LIKELY(addr < 0x80)) {
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
			if (addr == 0x607) {
				port_d607 = data;
			}
			//DEBUGPRINT("WRITE D%03X with data %02X" NL, addr, data);
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
	if (XEMU_UNLIKELY(addr < 2)) {	// "CPU port" at memory addr 0/1
		if ((memory[addr] & 7) != (data & 7)) {
			memory[addr] = data;
			DEBUG("MEM: applying new memory configuration because of CPU port writing" NL);
			apply_memory_config();
		} else
			memory[addr] = data;
	} else if (
		(XEMU_LIKELY(addr < 0x20000))
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


void write_phys_mem_for_dma ( int addr, Uint8 data )
{
	if (XEMU_LIKELY(addr <= 0xFFFFF))
		write_phys_mem(addr, data);
	else
		DEBUG("DMA-C65-BACKEND: writing memory above 1Mbyte (addr=$%X,data=%02X) PC=%04X" NL, addr, data, cpu65.pc);
}


Uint8 read_phys_mem ( int addr )
{
	return memory[addr & 0xFFFFF];
}


Uint8 read_phys_mem_for_dma ( int addr )
{
	if (XEMU_LIKELY(addr <= 0xFFFFF))
		return memory[addr & 0xFFFFF];
	else {
		DEBUG("DMA-C65-BACKEND: reading memory above 1Mbyte (addr=$%X) PC=%04X" NL, addr, cpu65.pc);
		return 0xFF;
	}
}



// This function is called by the 65CE02 emulator in case of reading a byte (regardless of data or code)
Uint8 cpu65_read_callback ( Uint16 addr )
{
	register int phys_addr = addr_trans_rd[addr >> 12] + addr;	// translating address with the READ table created by apply_memory_config()
	if (XEMU_LIKELY(phys_addr < 0x10FF00))
		return memory[phys_addr & 0xFFFFF];	// light optimization, do not call read_phys_mem for this single stuff :)
	else
		return io_read(phys_addr);
}



// This function is called by the 65CE02 emulator in case of writing a byte
void cpu65_write_callback ( Uint16 addr, Uint8 data )
{
	register int phys_addr = addr_trans_wr[addr >> 12] + addr;	// translating address with the WRITE table created by apply_memory_config()
	if (XEMU_LIKELY(phys_addr < 0x10FF00))
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
void cpu65_write_rmw_callback ( Uint16 addr, Uint8 old_data, Uint8 new_data )
{
	int phys_addr = addr_trans_wr[addr >> 12] + addr;	// translating address with the WRITE table created by apply_memory_config()
	if (XEMU_LIKELY(phys_addr < 0x10FF00))
		write_phys_mem(phys_addr, new_data);	// "normal" memory, just write once, no need to emulate the behaviour
	else {
		DEBUG("CPU: RMW opcode is used on I/O area for $%04X" NL, addr);
		io_write(phys_addr, old_data);		// first write back the old data ...
		io_write(phys_addr, new_data);		// ... then the new
	}
}


int dump_memory ( const char *fn )
{
        if (fn && *fn) {
                DEBUGPRINT("MEM: Dumping memory into file: %s" NL, fn);
                return xemu_save_file(fn, memory, 0x20000, "Cannot dump memory into file");
        } else {
                return 0;
        }
}


static void shutdown_callback ( void )
{
	int a;
	for (a = 0; a < 0x40; a++)
		DEBUG("VIC-3 register $%02X is %02X" NL, a, vic3_registers[a]);
	cia_dump_state (&cia1);
	cia_dump_state (&cia2);
#if !defined(XEMU_ARCH_HTML)
	(void)dump_memory(configdb.dumpmem);
#endif
	DEBUGPRINT("Scanline render info = \"%s\"" NL, scanline_render_debug_info);
	DEBUGPRINT("VIC3: D011=$%02X D018=$%02X D030=$%02X D031=$%02X" NL,
		vic3_registers[0x11], vic3_registers[0x18], vic3_registers[0x30], vic3_registers[0x31]
	);
	DEBUG("Execution has been stopped at PC=$%04X [$%05X]" NL, cpu65.pc, addr_trans_rd[cpu65.pc >> 12] + cpu65.pc);
}


int c65_reset_asked ( void )
{
	if (ARE_YOU_SURE("Are you sure to HARD RESET your Commodore-65?", i_am_sure_override | ARE_YOU_SURE_DEFAULT_YES)) {
		c65_reset();
		return 1;
	} else
		return 0;
}

void c65_reset ( void )
{
	memory[0] = memory[1] = 0xFF;
	map_mask = 0;
	vic3_registers[0x30] = 0;
	apply_memory_config();
	cpu65_reset();
	dma_reset();
	nmi_level = 0;
	DEBUG("RESET!" NL);
}



// Called by emutools_hid!!! to handle special private keys assigned to this emulator
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	if (pressed) {
		if (key == SDL_SCANCODE_F10) {	// reset
			c65_reset_asked();
		} else if (key == SDL_SCANCODE_KP_ENTER) {
			c64_toggle_joy_emu();
		} else if (key == SDL_SCANCODE_LSHIFT) {
			shift_status |= 1;
		} else if (key == SDL_SCANCODE_RSHIFT) {
			shift_status |= 2;
		}
		if (shift_status == 3 && set_mouse_grab(SDL_FALSE, 0)) {
			DEBUGPRINT("UI: mouse grab cancelled" NL);
		}
	} else {
		if (key == SDL_SCANCODE_LSHIFT) {
			shift_status &= 2;
		} else if (key == SDL_SCANCODE_RSHIFT) {
			shift_status &= 1;
		} else if (pos == -2 && key == 0) {	// special case pos = -2, key = 0, handled = mouse button (which?) and release event!
			if (handled == SDL_BUTTON_LEFT && set_mouse_grab(SDL_TRUE, 0)) {
				OSD(-1, -1, "Mouse grab activated. Press\nboth SHIFTs together to cancel.");
				DEBUGPRINT("UI: mouse grab activated" NL);
			}
			if (handled == SDL_BUTTON_RIGHT) {
				ui_enter();
			}
		}
	}
	return 0;
}


#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
int register_screenshot_request = 0;
static inline void do_pending_screenshot ( void )
{
	if (!register_screenshot_request)
		return;
	register_screenshot_request = 0;
	if (!xemu_screenshot_png(
		NULL, NULL,
		1,
		2,
		NULL,	// allow function to figure it out ;)
		SCREEN_WIDTH,
		SCREEN_HEIGHT,
		SCREEN_WIDTH
	)) {
		const char *p = strrchr(xemu_screenshot_full_path, DIRSEP_CHR);
		if (p)
			OSD(-1, -1, "%s", p + 1);
	}
}
#endif


static void update_emulator ( void )
{
#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
	// DO call this _RIGHT BEFORE_ xemu_update_screen() otherwise the texture
	// does not exist anymore OR not the full frame is rendered yet for screenshot!
	do_pending_screenshot();
#endif
	xemu_update_screen();
	hid_handle_all_sdl_events();
	xemugui_iteration();
	nmi_set(IS_RESTORE_PRESSED(), 2); // Custom handling of the restore key ...
	xemu_timekeeping_delay(40000);
	// Ugly CIA trick to maintain realtime TOD in CIAs :)
	const struct tm *t = xemu_get_localtime_last();
	const Uint8 sec10ths = xemu_get_microseconds_last() / 100000;
	cia_ugly_tod_updater(&cia1, t, sec10ths, 0);
	cia_ugly_tod_updater(&cia2, t, sec10ths, 0);
}


static int cycles;


static void emulation_loop ( void )
{
	vic3_open_frame_access();
	for (;;) {
		cycles += XEMU_UNLIKELY(dma_status) ? dma_update_multi_steps(cpu_cycles_per_scanline) : cpu65_step(
#ifdef CPU_STEP_MULTI_OPS
			cpu_cycles_per_scanline
#endif
		);	// FIXME: this is maybe not correct, that DMA's speed depends on the fast/slow clock as well?
		if (cycles >= cpu_cycles_per_scanline) {
			int exit_loop = 0;
			cia_tick(&cia1, 64);
			cia_tick(&cia2, 64);
			cycles -= cpu_cycles_per_scanline;
			if (XEMU_UNLIKELY(vic3_render_scanline())) {
				if (XEMU_UNLIKELY(inject_ready_check_status))
					inject_ready_check_do();
				if (frameskip) {
					frameskip = 0;
					hostfs_flush_all();
				} else {
					frameskip = 1;
					update_emulator();
					//vic3_open_frame_access();
					exit_loop = 1;
				}
				sids[0].sFrameCount++;
				sids[1].sFrameCount++;
			}
			vic3_check_raster_interrupt();
			if (exit_loop)
				return;
		}
	}
}



int main ( int argc, char **argv )
{
	//int cycles;
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Unusable Commodore 65 emulator from LGB", argc, argv);
	configdb_define_emulator_options();
	if (xemucfg_parse_all())
		return 1;
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	window_title_info_addon = emulator_speed_title;
        if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT * 2,// logical size (used with keeping aspect ratio by the SDL render stuffs)
		SCREEN_WIDTH, SCREEN_HEIGHT * 2,// window size
		SCREEN_FORMAT,			// pixel format
		0,				// we have *NO* pre-defined colours as with more simple machines (too many we need). we want to do this ourselves!
		NULL,				// -- "" --
		NULL,				// -- "" --
		configdb.sdlrenderquality,	// render scaling quality
		USE_LOCKED_TEXTURE,		// 1 = locked texture access
		shutdown_callback		// registered shutdown function
	))
		return 1;
	// Initialize C65 ...
	c65_init(
		SID_CYCLES_PER_SEC,		// SID cycles per sec
		AUDIO_SAMPLE_FREQ		// sound mix freq
	);
	osd_init_with_defaults();
	xemugui_init(configdb.gui);
	if (configdb.prg)
		inject_register_prg(configdb.prg, configdb.prgmode);
#ifdef FAKE_TYPING_SUPPORT
	if (configdb.go64) {
		if (configdb.autoload)
			c64_register_fake_typing(fake_typing_for_load64);
		else
			c64_register_fake_typing(fake_typing_for_go64);
	} else if (configdb.autoload)
		c64_register_fake_typing(fake_typing_for_load65);
#endif
	cycles = 0;
	if (audio)
		SDL_PauseAudioDevice(audio, 0);
	xemu_set_full_screen(configdb.fullscreen);
	if (!configdb.syscon)
		sysconsole_close(NULL);
	xemu_timekeeping_start();
	XEMU_MAIN_LOOP(emulation_loop, 25, 1);
	return 0;
}

/* --- SNAPSHOT RELATED --- */

#ifdef XEMU_SNAPSHOT_SUPPORT

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
	cpu65.cpu_inhibit_interrupts = (int)P_AS_BE32(buffer + 12);
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
	U32_AS_BE(buffer + 12, cpu65.cpu_inhibit_interrupts);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}


int c65emu_snapshot_loading_finalize ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	apply_memory_config();
	DEBUGPRINT("SNAP: loaded (finalize-callback!)." NL);
	return 0;
}
#endif
