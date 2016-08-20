/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
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

#include <stdio.h>

#include <SDL.h>

#include "mega65.h"
#include "cpu65c02.h"
#include "cia6526.h"
#include "c65fdc.h"
#include "dmagic.h"
#include "c65hid.h"
#include "vic3.h"
#include "sid.h"
#include "sdcard.h"
#include "uart_monitor.h"
#include "emutools.h"



static SDL_AudioDeviceID audio = 0;

Uint8 memory[0x104001];			// 65CE02 MAP'able address space (with the special case of hypervisor memory of Mega65, +0x4001, 1 byte if for check kickstart size)
static Uint8 cpu_port[2];		// CPU I/O port at 0/1 (implemented by the VIC3 for real, on C65 but for the usual - C64/6510 - name, it's the "CPU port")
static struct Cia6526 cia1, cia2;	// CIA emulation structures for the two CIAs
static struct SidEmulation sid1, sid2;	// the two SIDs

// We re-map I/O requests to a high address space does not exist for real. cpu_read() and cpu_write() should handle this as an IO space request (with the lower 16 bits as addr from $D000)
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
// Mega65, we just maps hypervisor memory above 1Mbyte, which is really not the way of Mega65, but should work here now, till a sane mega65-specific implemenation is done
#define HYPERVISOR_MEM_REMAP_VIRTUAL (0x100000 - 0x8000)

#define TRAP_RESET	0x40
#define	TRAP_TRIGGERED	0x00

static int addr_trans_rd[16];		// address translating offsets for READ operation (it can be added to the CPU address simply, selected by the high 4 bits of the CPU address)
static int addr_trans_wr[16];		// address translating offsets for WRITE operation (it can be added to the CPU address simply, selected by the high 4 bits of the CPU address)
static int map_mask;			// MAP mask, should be filled at the MAP opcode, *before* calling apply_memory_config() then
// WARNING: map_offset_low and map_offset_high must be used FROM bit-8 only, the lower 8 bits must be zero always!
static int map_offset_low;		// MAP low offset, should be filled at the MAP opcode, *before* calling apply_memory_config() then
static int map_offset_high;		// MAP high offset, should be filled at the MAP opcode, *before* calling apply_memory_config() then

static int frame_counter;

int in_hypervisor;			// mega65 hypervisor mode
int mega65_capable;			// emulator founds kickstart, sub-set of mega65 features CAN BE usable. It is an ERROR to use any of mega65 specific stuff, if it's zero!
static Uint8 gs_regs[0x1000];		// mega65 specific I/O registers, currently an ugly way, as only some bytes are used, ie not VIC3/4, etc etc ...
static int rom_protect;			// C65 system ROM write protection
static int fpga_switches = 1 << 12;		// State of FPGA board switches (bits 0 - 15), set switch 12 (hypervisor serial output)
static char  hypervisor_monout[0x10000];
static char *hypervisor_monout_p = hypervisor_monout;


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
	int cp = (cpu_port[1] | (~cpu_port[0]));
	// Simple ones, only CPU MAP may apply not other factors
	// Also, these are the "lower" blocks, needs the offset for the "lower" area in case of CPU MAP'ed state
	addr_trans_wr[0] = addr_trans_rd[0] = addr_trans_wr[1] = addr_trans_rd[1] = (map_mask & 1) ? map_offset_low : 0;	// $0XXX + $1XXX, MAP block 0 [mask 1]
	addr_trans_wr[2] = addr_trans_rd[2] = addr_trans_wr[3] = addr_trans_rd[3] = (map_mask & 2) ? map_offset_low : 0;	// $2XXX + $3XXX, MAP block 1 [mask 2]
	addr_trans_wr[4] = addr_trans_rd[4] = addr_trans_wr[5] = addr_trans_rd[5] = (map_mask & 4) ? map_offset_low : 0;	// $4XXX + $5XXX, MAP block 2 [mask 4]
	addr_trans_wr[6] = addr_trans_rd[6] = addr_trans_wr[7] = addr_trans_rd[7] = (map_mask & 8) ? map_offset_low : 0;	// $6XXX + $7XXX, MAP block 3 [mask 8]
	// From this point, we must use the "high" area offset if it's CPU MAP'ed
	// $8XXX and $9XXX + $AXXX and $BXXX: in hypervisor mode, it's another Mega65 specific, otherwise, let's see two first 8K MAP blocks 4 and 5
	if (in_hypervisor) {
		// NOTE: for real, entering to hypervisor mode would need "normal" MAP (but with extended offset addresses)
		// since I have just "C65 MAP" implemtnation, do this hack to handle differently ...
		addr_trans_wr[8] = addr_trans_rd[8] = addr_trans_wr[9] = addr_trans_rd[9] =
			addr_trans_wr[0xA] = addr_trans_rd[0xA] = addr_trans_wr[0xB] = addr_trans_rd[0xB] =
			HYPERVISOR_MEM_REMAP_VIRTUAL;
	} else {
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
	}
	// $CXXX, MAP block 6 [mask 64]
	// Warning: all VIC3 reg $30 related ROM maps are for 8K size, *expect* of '@C000' (interface ROM) which is only 4K! Also this is in another ROM bank than the others
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


static void hypervisor_enter ( int trapno )
{
	// Sanity checks
	if (!mega65_capable)
		FATAL("FATAL: hypervisor_enter() called without Mega65 capable mode");
	if (trapno > 0x7F || trapno < 0)
		FATAL("FATAL: got invalid trap number %d", trapno);
	if (in_hypervisor)
		FATAL("FATAL: already in hypervisor mode while calling hypervisor_enter()");
	// First, save machine status into hypervisor registers, TODO: incomplete, can be buggy!
	gs_regs[0x640] = cpu_a;
	gs_regs[0x641] = cpu_x;
	gs_regs[0x642] = cpu_y;
	gs_regs[0x643] = cpu_z;
	gs_regs[0x644] = cpu_bphi >> 8;	// "B" register
	gs_regs[0x645] = cpu_sp;
	gs_regs[0x646] = cpu_sphi >> 8;	// stack page register
	gs_regs[0x647] = cpu_get_p();
	gs_regs[0x648] = cpu_pc & 0xFF;
	gs_regs[0x649] = cpu_pc >> 8;
	gs_regs[0x64A] = ((map_offset_low  >> 16) & 0x0F) | ((map_mask & 0x0F) << 4);
	gs_regs[0x64B] = ( map_offset_low  >>  8) & 0xFF  ;
	gs_regs[0x64C] = ((map_offset_high >> 16) & 0x0F) | ( map_mask & 0xF0);
	gs_regs[0x64D] = ( map_offset_high >>  8) & 0xFF  ;
	gs_regs[0x650] = cpu_port[0];
	gs_regs[0x651] = cpu_port[1];
	gs_regs[0x652] = vic_iomode;
	// Now entering into hypervisor mode
	in_hypervisor = 1;	// this will cause apply_memory_config to map hypervisor RAM, also for checks later to out-of-bound execution of hypervisor RAM, etc ...
	vic_iomode = VIC4_IOMODE;
	cpu_port[0] = 0x3F;	// apply_memory_config watch this also ...
	cpu_port[1] = 0x35;	// and this too (this sets, all-RAM + I/O config)
	cpu_pfd = 0;		// clear decimal mode ... according to Paul, punnishment will be done, if it's removed :-)
	cpu_pfi = 1;		// disable IRQ in hypervisor mode
	cpu_pfe = 1;		// 8 bit stack in hypervisor mode
	cpu_sphi = 0xBE00;	// set a nice shiny stack page
	cpu_bphi = 0xBF00;	// ... and base page (aka zeropage)
	cpu_sp = 0xFF;
	map_mask = (map_mask & 0xF) | 0x30;	// mapping: 0011XXXX (it seems low region map mask is not changed by hypervisor entry)
	// FIXME: as currently I can do only C65-MAP, we cheat a bit, and use a special case in apply_memory_config() instead of a "legal" map-hi offset
	apply_memory_config();	// now the memory mapping is changed
	cpu_pc = 0x8000 | (trapno << 2);	// load PC with the address assigned for the given trap number
	printf("MEGA65: entering into hypervisor mode, trap=$%02X PC=$%04X" NL, trapno, cpu_pc);
}


static void hypervisor_leave ( void )
{
	// Sanity checks
	if (!mega65_capable)
		FATAL("FATAL: hypervisor_leave() called without Mega65 capable mode");
	if (!in_hypervisor)
		FATAL("FATAL: not in hypervisor mode while calling hypervisor_leave()");
	// First, restore machine status from hypervisor registers
	cpu_a    = gs_regs[0x640];
	cpu_x    = gs_regs[0x641];
	cpu_y    = gs_regs[0x642];
	cpu_z    = gs_regs[0x643];
	cpu_bphi = gs_regs[0x644] << 8;	// "B" register
	cpu_sp   = gs_regs[0x645];
	cpu_sphi = gs_regs[0x646] << 8;	// stack page register
	cpu_set_p(gs_regs[0x647]);
	cpu_pfe = gs_regs[0x647] & 32;	// cpu_set_p() does NOT set 'E' bit by design, so we do at our own
	cpu_pc   = gs_regs[0x648] | (gs_regs[0x649] << 8);
	map_offset_low  = ((gs_regs[0x64A] & 0xF) << 16) | (gs_regs[0x64B] << 8);
	map_offset_high = ((gs_regs[0x64C] & 0xF) << 16) | (gs_regs[0x64D] << 8);
	map_mask = (gs_regs[0x64A] >> 4) | (gs_regs[0x64C] & 0xF0);
	cpu_port[0] = gs_regs[0x650];
	cpu_port[1] = gs_regs[0x651];
	vic_iomode = gs_regs[0x652] & 3;
	if (vic_iomode == VIC_BAD_IOMODE)
		vic_iomode = VIC3_IOMODE;	// I/O mode "2" (binary: 10) is not used, I guess
	// Now leaving hypervisor mode ...
	in_hypervisor = 0;
	apply_memory_config();
	printf("MEGA65: leaving hypervisor mode, (user) PC=$%04X" NL, cpu_pc);
}



static void hypervisor_serial_monitor_push_char ( Uint8 chr )
{
	if (hypervisor_monout_p >= hypervisor_monout - 1 + sizeof hypervisor_monout)
		return;
	*(hypervisor_monout_p++) = (char)chr;
}


static void cia_setint_cb ( int level )
{
	printf("%s: IRQ level changed to %d" NL, cia1.name, level);
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


static Uint8 cia_read_keyboard ( Uint8 ddr_mask_unused )
{
	return
		((KBSEL &   1) ? 0xFF : kbd_matrix[0]) &
		((KBSEL &   2) ? 0xFF : kbd_matrix[1]) &
		((KBSEL &   4) ? 0xFF : kbd_matrix[2]) &
		((KBSEL &   8) ? 0xFF : kbd_matrix[3]) &
		((KBSEL &  16) ? 0xFF : kbd_matrix[4]) &
		((KBSEL &  32) ? 0xFF : kbd_matrix[5]) &
		((KBSEL &  64) ? 0xFF : kbd_matrix[6]) &
		((KBSEL & 128) ? 0xFF : kbd_matrix[7])
	;
}



static void cia2_outa ( Uint8 mask, Uint8 data )
{
	vic2_16k_bank = (3 - (data & 3)) * 0x4000;
	printf("VIC2: 16K BANK is set to $%04X" NL, vic2_16k_bank);
}



// Just for easier test to have a given port value for CIA input ports
static Uint8 cia_port_in_dummy ( Uint8 mask )
{
	return 0xFF;
}



static void audio_callback(void *userdata, Uint8 *stream, int len)
{
	printf("AUDIO: audio callback, wants %d samples" NL, len);
	// We use the trick, to render boths SIDs with step of 2, with a byte offset
	// to get a stereo stream, wanted by SDL.
	sid_render(&sid2, ((short *)(stream)) + 0, len / 2, 2);		// SID @ left
	sid_render(&sid1, ((short *)(stream)) + 1, len / 2, 2);		// SID @ right
}



static void c65_init ( const char *disk_image_name, int sid_cycles_per_sec, int sound_mix_freq )
{
	SDL_AudioSpec audio_want, audio_got;
	hid_init();
	// *** Init memory space
	memset(memory, 0xFF, sizeof memory);
	in_hypervisor = 0;
	memset(gs_regs, 0, sizeof gs_regs);
	rom_protect = 1;
	// *** Trying to load kickstart image
	if (emu_load_file(KICKSTART_NAME, memory + HYPERVISOR_MEM_REMAP_VIRTUAL + 0x8000, 0x4001) == 0x4000) {
		// Found kickstart ROM, emulate Mega65 startup somewhat ...
		mega65_capable = 1;
		printf("MEGA65: " KICKSTART_NAME " loaded into hypervisor memory, Mega65 capable mode is set" NL);
	} else {
		ERROR_WINDOW("Cannot load " KICKSTART_NAME " (not found, or not 16K sized), emulate only C65");
		printf("MEGA65: " KICKSTART_NAME " cannot be loaded, Mega65 capable mode is disabled" NL);
		mega65_capable = 0;
	}
	// *** Mega65 specific SDCARD support
	sdcard_init(SDCARD_NAME);
	// *** Load ROM image (TODO: make it optional when mega65_capable is true, thus KICKUP is loaded)
	if (emu_load_file(ROM_NAME, memory + 0x20000, 0x20001) != 0x20000)
		FATAL("Cannot load C65 system ROM: " ROM_NAME);
	// *** Initialize VIC3
	vic3_init();
	// *** Memory configuration (later override will happen for mega65 mode though, this is only the default)
	cpu_port[0] = cpu_port[1] = 0xFF;	// the "CPU I/O port" on 6510/C64, implemented by VIC3 for real in C65!
	map_mask = 0;				// as all 8K blocks are unmapped, we don't need to worry about the low/high offset to set here
	apply_memory_config();			// VIC3 $30 reg is already filled, so it's OK to call this now
	// *** CIAs
	cia_init(&cia1, "CIA-1",
		NULL,	// callback: OUTA(mask, data)
		NULL,	// callback: OUTB(mask, data)
		NULL,	// callback: OUTSR(mask, data)
		NULL,	// callback: INA(mask)
		cia_read_keyboard,	// callback: INB(mask)
		NULL,	// callback: INSR(mask)
		cia_setint_cb	// callback: SETINT(level)
	);
	cia_init(&cia2, "CIA-2",
		cia2_outa,	// callback: OUTA(mask, data)
		NULL,	// callback: OUTB(mask, data)
		NULL,	// callback: OUTSR(mask, data)
		cia_port_in_dummy,	// callback: INA(mask)
		NULL,	// callback: INB(mask)
		NULL,	// callback: INSR(mask)
		NULL	// callback: SETINT(level)	that would be NMI in our case
	);
	// *** Initialize DMA
	dma_init();
	// Initialize FDC
	fdc_init(disk_image_name);
	// SIDs, plus SDL audio
	sid_init(&sid1, sid_cycles_per_sec, sound_mix_freq);
	sid_init(&sid2, sid_cycles_per_sec, sound_mix_freq);
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
			printf("AUDIO: audio device is #%d: %s" NL, i, SDL_GetAudioDeviceName(i, 0));
		// Sanity check that we really got the same audio specification we wanted
		if (audio_want.freq != audio_got.freq || audio_want.format != audio_got.format || audio_want.channels != audio_got.channels) {
			SDL_CloseAudioDevice(audio);	// forget audio, if it's not our expected format :(
			audio = 0;
			ERROR_WINDOW("Audio parameter mismatches.");
		}
		printf("AUDIO: initialized (#%d), %d Hz, %d channels, %d buffer sample size." NL, audio, audio_got.freq, audio_got.channels, audio_got.samples);
	} else
		ERROR_WINDOW("Cannot open audio device!");
	//
#ifdef UARTMON_SOCKET
	uartmon_init(UARTMON_SOCKET);
#endif
	// *** RESET CPU, also fetches the RESET vector into PC
	cpu_reset();
	// *** In case of Mega65 mode, let's override the system configuration a bit ... It will also re-set PC to the trap address
	if (mega65_capable) {
		rom_protect = 0;
		hypervisor_enter(TRAP_RESET);
	}
	puts("INIT: end of initialization!");
}



// *** Implements the MAP opcode of 4510, called by the 65CE02 emulator
void cpu_do_aug ( void )
{
	cpu_inhibit_interrupts = 1;	// disable interrupts to the next "EOM" (ie: NOP) opcode
	printf("CPU: MAP opcode, input A=$%02X X=$%02X Y=$%02X Z=$%02X" NL, cpu_a, cpu_x, cpu_y, cpu_z);
	map_offset_low  = (cpu_a << 8) | ((cpu_x & 15) << 16);	// offset of lower half (blocks 0-3)
	map_offset_high = (cpu_y << 8) | ((cpu_z & 15) << 16);	// offset of higher half (blocks 4-7)
	map_mask        = (cpu_z & 0xF0) | (cpu_x >> 4);	// "is mapped" mask for blocks (1 bit for each)
	puts("MEM: applying new memory configuration because of MAP CPU opcode");
	printf("LOW -OFFSET = $%X" NL, map_offset_low);
	printf("HIGH-OFFSET = $%X" NL, map_offset_high);
	printf("MASK        = $%02X" NL, map_mask);
	apply_memory_config();
}



// *** Implements the EOM opcode of 4510, called by the 65CE02 emulator
void cpu_do_nop ( void )
{
	if (cpu_inhibit_interrupts) {
		cpu_inhibit_interrupts = 0;
		puts("CPU: EOM, interrupts were disabled because of MAP till the EOM");
	} else
		puts("CPU: NOP not treated as EOM (no MAP before)");
}



#define RETURN_ON_IO_READ_NOT_IMPLEMENTED(func, fb) \
	do { printf("IO: NOT IMPLEMENTED read (emulator lacks feature), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_READ_NO_NEW_VIC_MODE(func, fb) \
	do { printf("IO: ignored read (not new VIC mode), %s $%04X fallback to answer $%02X" NL, func, addr, fb); \
	return fb; } while (0)
#define RETURN_ON_IO_WRITE_NOT_IMPLEMENTED(func) \
	do { printf("IO: NOT IMPLEMENTED write (emulator lacks feature), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)
#define RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE(func) \
	do { printf("IO: ignored write (not new VIC mode), %s $%04X with data $%02X" NL, func, addr, data); \
	return; } while(0)
#define WARN_IO_MODE_WR(func) \
	printf("IO: write operation defaults (not new VIC mode) to VIC-2 registers, though it would be: \"%s\" (a=$%04X, d=$%02X)" NL, func, addr, data)
#define WARN_IO_MODE_RD(func) \
	printf("IO: read operation defaults (not new VIC mode) to VIC-2 registers, though it would be: \"%s\" (a=$%04X)" NL, func, addr)


// Call this ONLY with addresses between $D000-$DFFF
// Ranges marked with (*) needs "vic_new_mode"
Uint8 io_read ( int addr )
{
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
			switch (addr) {
				case 0xD680:
					return sdcard_read_status();
				case 0xD6F0:
					return fpga_switches & 0xFF;
				case 0xD6F1:
					return (fpga_switches >> 8) & 0xFF;
				default:
					printf("MEGA65: reading Mega65 specific I/O @ $%04X result is $%02X" NL, addr, gs_regs[addr & 0xFFF]);
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
		printf("IO: reading colour RAM at offset $%04X" NL, addr - 0xD800);
		return memory[0x1F800 + addr - 0xD800];
	}
	if (addr < 0xDD00) {	// $DC00 - $DCFF	CIA-1
		Uint8 result = cia_read(&cia1, addr & 0xF);
		//RETURN_ON_IO_READ_NOT_IMPLEMENTED("CIA-1", 0xFF);
		printf("%s: reading register $%X result is $%02X" NL, cia1.name, addr & 15, result);
		return result;
	}
	if (addr < 0xDE00) {	// $DD00 - $DDFF	CIA-2
		Uint8 result = cia_read(&cia2, addr & 0xF);
		//RETURN_ON_IO_READ_NOT_IMPLEMENTED("CIA-2", 0xFF);
		printf("%s: reading register $%X result is $%02X" NL, cia2.name, addr & 15, result);
		return result;
	}
	// Only IO-1 and IO-2 areas left, if SD-card buffer is mapped for Mega65, this is our only case left!
	do {
		int result = sdcard_read_buffer(addr - 0xDE00);	// try to read SD buffer
		if (result >= 0)	// if non-negative number got, answer is really the SD card (mapped buffer)
			return result;
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
			printf("MEGA65: writing Mega65 specific I/O range @ $%04X with $%02X" NL, addr, data);
			switch (addr) {
				case 0xD67C:	// hypervisor serial monitor port
					hypervisor_serial_monitor_push_char(data);
					break;
				case 0xD67D:
					rom_protect = data & 4;
					break;
				case 0xD67F:	// hypervisor enter/leave trap
					if (in_hypervisor)
						hypervisor_leave();	// if in hypevisor mode, any write port causes to leave hypervisor
					else	// the opposite of above, enter into hypervisor mode
						hypervisor_enter(TRAP_TRIGGERED);	// FIXME: is it always this one?
					break;
				case 0xD680:
					sdcard_command(data);
					break;
				case 0xD681:
				case 0xD682:
				case 0xD683:
				case 0xD684:
					sdcard_select_sector(addr - 0xD681, data);
					break;
			}
                        return;
		} else if (vic_iomode)
			RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("UART");
		else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("UART");
	}
	if (addr < 0xD800) {	// $D700 - $D7FF	DMA (*)
		printf("DMA: writing register $%04X (data = $%02X)" NL, addr, data);
		if (vic_iomode) {
			dma_write_reg(addr & 0xF, data);
			return;
		} else
			RETURN_ON_IO_WRITE_NO_NEW_VIC_MODE("DMA controller");
	}
	if (addr < ((vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00)) {	// $D800-$DC00/$E000	COLOUR NIBBLES, mapped to $1F800 in BANK1
		memory[0x1F800 + addr - 0xD800] = data;
       //return memory[0x1F800 + addr - 0xD800];
		printf("IO: writing colour RAM at offset $%04X" NL, addr - 0xD800);
		return;
	}
	if (addr < 0xDD00) {	// $DC00 - $DCFF	CIA-1
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("CIA-1");
		printf("%s: writing register $%X with data $%02X" NL, cia1.name, addr & 15, data);
		cia_write(&cia1, addr & 0xF, data);
		return;
	}
	if (addr < 0xDE00) {	// $DD00 - $DDFF	CIA-2
		//RETURN_ON_IO_WRITE_NOT_IMPLEMENTED("CIA-2");
		printf("%s: writing register $%X with data $%02X" NL, cia2.name, addr & 15, data);
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



void write_phys_mem ( int addr, Uint8 data )
{
	// !!!! The following line was for C65 to make it secure, only access 1Mbyte of memory ...
	//addr &= 0xFFFFF;
	if (addr < 2) {
		if ((cpu_port[addr] & 7) != (data & 7)) {
			cpu_port[addr] = data;
			puts("MEM: applying new memory configuration because of CPU port writing");
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
	)
		memory[addr] = data;
}



Uint8 read_phys_mem ( int addr )
{
	// !!!! The following line was for C65 to make it secure, only access 1Mbyte of memory ...
	//addr &= 0xFFFFF;
	if (addr < 2)
		return cpu_port[addr];
	//if (in_hypervisor)	// DEBUG
	//	printf("MEGA65: read byte from %X is $%02X" NL, addr, memory[addr]);
	return memory[addr];
}



// This function is called by the 65CE02 emulator in case of reading a byte (regardless of data or code)
Uint8 cpu_read ( Uint16 addr )
{
	int phys_addr = addr_trans_rd[addr >> 12] + addr;	// translating address with the READ table created by apply_memory_config()
	//if (in_hypervisor)	// DEBUG
	//	printf("MEGA65: cpu_read, addr=%X phys_addr=%X" NL, addr, phys_addr);
	if (phys_addr >= IO_REMAP_VIRTUAL) {
		if ((addr & 0xF000) != 0xD000)
			FATAL("Internal error: IO is not on the IO space!");
		return io_read(addr);	// addr should be in $DXXX range to hit this, hopefully ...
	}
	return read_phys_mem(phys_addr);
}



// This function is called by the 65CE02 emulator in case of writing a byte
void cpu_write ( Uint16 addr, Uint8 data )
{
	int phys_addr = addr_trans_wr[addr >> 12] + addr;	// translating address with the WRITE table created by apply_memory_config()
	if (phys_addr >= IO_REMAP_VIRTUAL) {
		if ((addr & 0xF000) != 0xD000)
			FATAL("Internal error: IO is not on the IO space!");
		io_write(addr, data);	// addr should be in $DXXX range to hit this, hopefully ...
		return;
	}
	write_phys_mem(phys_addr, data);
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
	if (phys_addr >= IO_REMAP_VIRTUAL) {
		if ((addr & 0xF000) != 0xD000)
			FATAL("Internal error: IO is not on the IO space!");
		if (addr < 0xD800 || addr >= (vic3_registers[0x30] & 1) ? 0xE000 : 0xDC00) {	// though, for only memory areas other than colour RAM (avoids unneeded warnings as well)
			printf("CPU: RMW opcode is used on I/O area for $%04X" NL, addr);
			io_write(addr, old_data);	// first write back the old data ...
		}
		io_write(addr, new_data);	// ... then the new
		return;
	}
	write_phys_mem(phys_addr, new_data);	// "normal" memory, just write once, no need to emulate the behaviour
}



static void shutdown_callback ( void )
{
#ifdef MEMDUMP_FILE
	FILE *f;
#endif
	int a;
	for (a = 0; a < 0x40; a++)
		printf("VIC-3 register $%02X is %02X" NL, a, vic3_registers[a]);
	cia_dump_state (&cia1);
	cia_dump_state (&cia2);
#ifdef MEMDUMP_FILE
	// Dump memory, so some can inspect the result (low 128K, RAM only)
	f = fopen(MEMDUMP_FILE, "wb");
	if (f) {
		fwrite(memory, 1, 0x20000, f);
		fclose(f);
		puts("Memory is dumped into " MEMDUMP_FILE);
	}
#endif
#ifdef UARTMON_SOCKET
	uartmon_close();
#endif
	if (hypervisor_monout != hypervisor_monout_p) {
		*hypervisor_monout_p = 0;
		printf("HYPERVISOR_MONITOR_OUT:" NL "%s" NL "HYPERVISOR_MONITOR_OUT: *END-OF-OUTPUT*" NL, hypervisor_monout);
	} else
		printf("HYPERVISOR_MONITOR_OUT: *NO-OUTPUT-BUFFER*" NL);
	printf("Execution has been stopped at PC=$%04X [$%05X]" NL, cpu_pc, addr_trans_rd[cpu_pc >> 12] + cpu_pc);
}



static void emulate_keyboard ( SDL_Scancode key, int pressed )
{
	// Check for special, emulator-related hot-keys (not C65 key)
	if (pressed) {
		if (key == SDL_SCANCODE_F11) {
			emu_set_full_screen(-1);
			return;
		} else if (key == SDL_SCANCODE_F9) {
			exit(0);
		} else if (key == SDL_SCANCODE_F10) {
			cpu_port[0] = cpu_port[1] = 0xFF;
			map_mask = 0;
			vic3_registers[0x30] = 0;
			apply_memory_config();
			cpu_reset();
			if (mega65_capable)
				hypervisor_enter(TRAP_RESET);
			puts("RESET!");
			return;
		}
	}
	// If not an emulator hot-key, try to handle as a C65 key
	// This function also updates the keyboard matrix in that case
	hid_key_event(key, pressed);
}



static void update_emulator ( void )
{
	SDL_Event e;
	while (SDL_PollEvent(&e) != 0) {
		switch (e.type) {
			case SDL_QUIT:
				exit(0);
			case SDL_KEYUP:
			case SDL_KEYDOWN:
				if (e.key.repeat == 0 && (e.key.windowID == sdl_winid || e.key.windowID == 0))
					emulate_keyboard(e.key.keysym.scancode, e.key.state == SDL_PRESSED);
				break;
			case SDL_JOYDEVICEADDED:
			case SDL_JOYDEVICEREMOVED:
				hid_joystick_device_event(e.jdevice.which, e.type == SDL_JOYDEVICEADDED);
				break;
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
				hid_joystick_button_event(e.type == SDL_JOYBUTTONDOWN);
				break;
			case SDL_JOYHATMOTION:
				hid_joystick_hat_event(e.jhat.value);
				break;
			case SDL_JOYAXISMOTION:
				if (e.jaxis.axis < 2)
					hid_joystick_motion_event(e.jaxis.axis, e.jaxis.value);
				break;
			case SDL_MOUSEMOTION:
				hid_mouse_motion_event(e.motion.xrel, e.motion.yrel);
				break;
		}
	}
#ifdef UARTMON_SOCKET
	uartmon_update();
#endif
	// Screen rendering: begin
	vic3_render_screen();
	// Screen rendering: end
	emu_timekeeping_delay(40000);
}



static const char registerMessage[]  = "PC   A  X  Y  Z  B  SP   MAPL MAPH LAST-OP     P  P-FLAGS   RGP uS IO";


void m65mon_show_regs ( void )
{
	umon_printf("%s\r\n%04X %02X %02X %02X %02X %02X %04X "	// register banned message and things from PC to SP
		"%04X %04X %02X          %02X",			// from MAPL to P (FIXME: last-op should be disasm? also MAPL/H is only C65 stuff here)
		registerMessage,
		cpu_pc, cpu_a, cpu_x, cpu_y, cpu_z, cpu_bphi >> 8, cpu_sphi | cpu_sp,
		map_offset_low >> 8, map_offset_high >> 8, cpu_op, cpu_get_p()
	);
}




int main ( int argc, char **argv )
{
	int cycles, frameskip;
	printf("**** The Incomplete Commodore-65/Mega-65 emulator from LGB" NL
	"INFO: Texture resolution is %dx%d" NL "%s" NL,
		SCREEN_WIDTH, SCREEN_HEIGHT,
		emulators_disclaimer
	);
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
		argc > 1 ? argv[1] : NULL,	// FIXME: disk image name (in Mega65 mode, this will be SD-card image rather than a D81 in the future ...)
		SID_CYCLES_PER_SEC,		// SID cycles per sec
		AUDIO_SAMPLE_FREQ		// sound mix freq
	);
	// Start!!
	cycles = 0;
	frameskip = 0;
	frame_counter = 0;
	vic3_blink_phase = 0;
	emu_timekeeping_start();
	if (audio)
		SDL_PauseAudioDevice(audio, 0);
	for (;;) {
		int opcyc;
		// Trying to use at least some approx stuff :)
		// In FAST mode, the divider is 7. (see: vic3.c)
		// Otherwise it's 2, thus giving about *3.5 slower CPU ... or something :)
		if (in_hypervisor) {
			//printf("MEGA65: hypervisor mode execution at $%04X" NL, cpu_pc);
			if(cpu_pc < 0x8000 || cpu_pc > 0xBFFF)
				ERROR_WINDOW("Executing program in hypervisor mode outside of the hypervisor mem $%04X", cpu_pc);
		}
		opcyc = cpu_step();
		// FIXME: maybe CIAs are not fed with the actual CPU clock and that cause the "too fast" C64 for me?
		// ... though I tried to correct used CPU cycles with that divider hack at least to approx. the ~1MHz clock in non-FAST mode (see vic3.c)
		// ... Now it seems to be approx. OK :-) Hopefully, but I have no idea what happens in C65 mode, as - it seems - the usual
		// periodic IRQ is generated by VIC3 raster interrupt in C65 mode and not by CIA1.
		cia_tick(&cia1, opcyc);
		cia_tick(&cia2, opcyc);
		cycles += (opcyc * 7) / clock_divider7_hack;
		if (cycles >= 227) {
			scanline++;
			//printf("VIC3: new scanline (%d)!" NL, scanline);
			cycles -= 227;
			if (scanline == 312) {
				//puts("VIC3: new frame!");
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
			//printf("RASTER=%d COMPARE=%d\n",scanline,compare_raster);
			//vic_interrupt();
			vic3_check_raster_interrupt();
		}
		// Just a wild guess: update DMA state for every CPU cycles
		// However it does not care about that some DMA commands need more time
		// (ie COPY vs FILL).
		// It seems two DMA updates are needed by CPU clock cycle not to apply the WORKAROUND
		// situation (see in c65dma.c). Hopefully in this way, this is now closer to the native
		// speed of DMA compared to the CPU. Or not :-D
		while (dma_status && (opcyc--)) {
			dma_update();
			dma_update();
		}
	}
	puts("Goodbye!");
	return 0;
}
