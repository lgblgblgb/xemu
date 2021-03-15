/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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


#include "xemu/emutools.h"
#include "xemu/emutools_files.h"
#include "mega65.h"
#include "xemu/cpu65.h"
#include "xemu/f011_core.h"
#include "xemu/f018_core.h"
#include "xemu/emutools_hid.h"
#include "vic4.h"
#include "sdcard.h"
#include "uart_monitor.h"
#include "hypervisor.h"
#include "xemu/c64_kbd_mapping.h"
#include "xemu/emutools_config.h"
#include "xemu/emutools_umon.h"
#include "m65_snapshot.h"
#include "memory_mapper.h"
#include "io_mapper.h"
#include "ethernet65.h"
#include "input_devices.h"
#include "memcontent.h"
#include "xemu/emutools_gui.h"
#include "audio65.h"
#include "inject.h"
#include "configdb.h"


static int nmi_level;			// please read the comment at nmi_set() below

int newhack = 0;

static int speed_current = -1;
static int paused = 0, paused_old = 0;
static int breakpoint_pc = -1;
#ifdef TRACE_NEXT_SUPPORT
static int orig_sp = 0;
static int trace_next_trigger = 0;
#endif
static int trace_step_trigger = 0;
#ifdef HAS_UARTMON_SUPPORT
static void (*m65mon_callback)(void) = NULL;
#endif
static const char emulator_paused_title[] = "TRACE/PAUSE";
static char emulator_speed_title[64] = "";
static char fast_mhz_in_string[16] = "";
static const char *cpu_clock_speed_strs[4] = { "1MHz", "2MHz", "3.5MHz", fast_mhz_in_string };
static unsigned int cpu_clock_speed_str_index = 0;
static unsigned int cpu_cycles_per_scanline;
static int cpu_cycles_per_step = 100; 	// some init value, will be overriden, but it must be greater initially than "only a few" anyway

static int force_external_rom = 0;

static Uint8 nvram_original[sizeof nvram];
static int uuid_must_be_saved = 0;

int register_screenshot_request = 0;

Uint8 last_dd00_bits = 3;		// Bank 0



void cpu65_illegal_opcode_callback ( void )
{
	// FIXME: implement this, it won't be ever seen now, as not even switch to 6502 NMOS persona is done yet ...
	FATAL("Internal error: 6502 NMOS persona is not supported yet.");
}


void machine_set_speed ( int verbose )
{
	int speed_wanted;
	// Actually the rule would be something like that (this comment is here by intent, for later implementation FIXME TODO), some VHDL draft only:
	// cpu_speed := vicii_2mhz&viciii_fast&viciv_fast
	// if hypervisor_mode='0' and ((speed_gate='1') and (force_fast='0')) then -- LGB: vicii_2mhz seems to be a low-active signal?
	// case cpu_speed is ...... 100=1MHz, 101=1MHz, 110=3.5MHz, 111=50Mhz, 000=2MHz, 001=50MHz, 010=3.5MHz, 011=50MHz
	// else 50MHz end if;
	// it seems hypervisor always got full speed, and force_fast (ie, POKE 0,65) always forces the max
	// TODO: what is speed_gate? (it seems to be a PMOD input and/or keyboard controll with CAPS-LOCK)
	// TODO: how 2MHz is selected, it seems a double decoded VIC-X registers which is not so common in VIC modes yet, I think ...
	//Uint8 desired = (in_hypervisor || force_fast) ? 7 : (((c128_d030_reg & 1) << 2) | ((vic_registers[0x31] & 64) >> 5) | ((vic_registers[0x54] & 64) >> 6));
	//if (desired == current_speed_config)
	//	return;
	if (verbose)
		DEBUGPRINT("SPEED: in_hypervisor=%d force_fast=%d c128_fast=%d, c65_fast=%d m65_fast=%d" NL,
			in_hypervisor, force_fast, (c128_d030_reg & 1), vic_registers[0x31] & 64, vic_registers[0x54] & 64
	);
	// ^1 at c128... because it was inverted :-O --> FIXME: this is ugly workaround, the switch statement should be re-organized
	speed_wanted = (in_hypervisor || force_fast) ? 7 : ((((c128_d030_reg & 1) ^ 1) << 2) | ((vic_registers[0x31] & 64) >> 5) | ((vic_registers[0x54] & 64) >> 6));
	// videostd_changed: we also want to force recalulation if PAL/NTSC change happened, even if the speed setting remains the same!
	if (speed_wanted != speed_current || videostd_changed) {
		speed_current = speed_wanted;
		videostd_changed = 0;
		switch (speed_wanted) {
			// NOTE: videostd_1mhz_cycles_per_scanline is set by vic4.c and also includes the video standard
			case 4:	// 100 - 1MHz
			case 5:	// 101 - 1MHz
				cpu_cycles_per_scanline = (unsigned int)(videostd_1mhz_cycles_per_scanline * (float)(C64_MHZ_CLOCK));
				cpu_clock_speed_str_index = 0;
				cpu65_set_ce_timing(0);
				break;
			case 0:	// 000 - 2MHz
				cpu_cycles_per_scanline = (unsigned int)(videostd_1mhz_cycles_per_scanline * (float)(C128_MHZ_CLOCK));
				cpu_clock_speed_str_index = 1;
				cpu65_set_ce_timing(0);
				break;
			case 2:	// 010 - 3.5MHz
			case 6:	// 110 - 3.5MHz
				cpu_cycles_per_scanline = (unsigned int)(videostd_1mhz_cycles_per_scanline * (float)(C65_MHZ_CLOCK));
				cpu_clock_speed_str_index = 2;
				cpu65_set_ce_timing(1);
				break;
			case 1:	// 001 - 40MHz (or Xemu specified custom speed)
			case 3:	// 011 -		-- "" --
			case 7:	// 111 -		-- "" --
				cpu_cycles_per_scanline = (unsigned int)(videostd_1mhz_cycles_per_scanline * (float)(configdb.fast_mhz));
				cpu_clock_speed_str_index = 3;
				cpu65_set_ce_timing(1);
				break;
		}
		// XXX use only DEBUG() here!
		DEBUGPRINT("SPEED: CPU speed is set to %s, cycles per scanline: %d (1MHz cycles per scanline: %f)" NL, cpu_clock_speed_strs[cpu_clock_speed_str_index], cpu_cycles_per_scanline, videostd_1mhz_cycles_per_scanline);
		if (cpu_cycles_per_step > 1)	// if in trace mode, do not set this!
			cpu_cycles_per_step = cpu_cycles_per_scanline;
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


static void cia2_out_a ( Uint8 data )
{
	// XXX  My code
#if 0
	vic2_16k_bank = ((~(data | (~cia2.DDRA))) & 3) << 14;
	vic_vidp_legacy = 1;
	vic_chrp_legacy = 1;
	vic_sprp_legacy = 1;
	// TODO FIXME: add sprites pointers!
	DEBUG("VIC2: 16K BANK is set to $%04X (CIA mask=$%02X)" NL, vic2_16k_bank, cia2.DDRA);
#endif
	// Code from HMW, XXX FIXME
	// Note, I have removed the REG_CRAM2K since it's not possible to hit this callback anyways if CIA is "covered" by colour RAM
	if (REG_HOTREG) {
		// Bank select
		data &= (cia2.DDRA & 3); // Mask bank bits through CIA DDR register bits
		REG_SCRNPTR_B1 = (~data << 6) | (REG_SCRNPTR_B1 & 0x3F);
		REG_CHARPTR_B1 = (~data << 6) | (REG_CHARPTR_B1 & 0x3F);
		REG_SPRPTR_B1  = (~data << 6) | (REG_SPRPTR_B1 & 0x3F);
		last_dd00_bits = data;
		//DEBUGPRINT("VIC2: (hotreg)Wrote to $DD00: $%02x screen=$%08x char=$%08x spr=$%08x" NL, data, SCREEN_ADDR, CHARSET_ADDR, SPRITE_POINTER_ADDR);
	}
}


// Just for easier test to have a given port value for CIA input ports
static Uint8 cia_port_in_dummy ( void )
{
	return 0xFF;
}


#ifdef XEMU_SNAPSHOT_SUPPORT
static void m65_snapshot_saver_on_exit_callback ( void )
{
	if (!configdb.snapsave)
		return;
	if (xemusnap_save(configdb.snapsave))
		ERROR_WINDOW("Could not save snapshot \"%s\": %s", configdb.snapsave, xemusnap_error_buffer);
	else
		INFO_WINDOW("Snapshot has been saved to \"%s\".", configdb.snapsave);
}
#endif


static int load_memory_preinit_cache ( int to_prezero, const char *config_name, const char *fn, const char *description, Uint8 *memory_pointer, int size )
{
	if (to_prezero) {
		DEBUGPRINT("BIN: config \"%s\" as \"%s\": pre-zero policy, clearing memory content." NL, config_name, description);
		memset(memory_pointer, 0, size);
	} else
		DEBUGPRINT("BIN: config \"%s\" as \"%s\": no pre-zero policy, using some (possible) built-in default content." NL, config_name, description);
	if (fn) {
		if (!strcmp(fn, "-")) {
			DEBUGPRINT("BIN: config \"%s\" as \"%s\": has option override policy to force and use pre-zeroed content." NL, config_name, description);
			memset(memory_pointer, 0, size);
			return 0;
		} else {
			DEBUGPRINT("BIN: config \"%s\" as \"%s\": has option override, trying to load content: \"%s\"." NL, config_name, description, fn);
			return xemu_load_file(fn, memory_pointer, size, size, description);
		}
	} else {
		DEBUGPRINT("BIN: config \"%s\" as \"%s\": has no option override, using the previously stated policy." NL, config_name, description);
		return 0;
	}
}


static Uint8 rom_init_image[0x20000];
static Uint8 c000_init_image[0x1000];


//#define BANNER_MEM_ADDRESS	0x3D00
#define BANNER_MEM_ADDRESS	(0x58000 - 3*256)

static void refill_memory_from_preinit_cache ( void )
{
	memcpy(char_wom, meminitdata_chrwom, MEMINITDATA_CHRWOM_SIZE);
	memcpy(colour_ram, meminitdata_cramutils, MEMINITDATA_CRAMUTILS_SIZE);
	memcpy(main_ram +  BANNER_MEM_ADDRESS, meminitdata_banner, MEMINITDATA_BANNER_SIZE);
	memcpy(main_ram + 0x20000, rom_init_image, sizeof rom_init_image);
	memcpy(hypervisor_ram, meminitdata_kickstart, MEMINITDATA_KICKSTART_SIZE);
	memcpy(main_ram +  0xC000, c000_init_image, sizeof c000_init_image);
}


int refill_c65_rom_from_preinit_cache ( void )
{
	int ret;
	if (force_external_rom) {
		DEBUGPRINT("ROM: forcing re-apply of ROM image" NL);
		memcpy(main_ram + 0x20000, rom_init_image, sizeof rom_init_image);
		// memcpy(char_wom, rom_init_image + 0xD000, sizeof char_wom);	// also fill char-WOM [FIXME: do we really want this?!]
		// The 128K ROM image is actually holds the reset bector at the lower 64K, ie C65 would start in "C64 mode" for real, just it switches into C65 mode then ...
		ret = rom_init_image[0xFFFC] | (rom_init_image[0xFFFD] << 8);	// pass back new reset vector
	} else {
		ret = -1; // no refill force external rom policy ...
	}
	if (configdb.force_upload_fonts) {
		DEBUGPRINT("ROM: forcing upload font definitions from ROM area to WOM" NL);
		memcpy(char_wom + 0x0000, main_ram + 0x2D000, 0x1000);
		memcpy(char_wom + 0x1000, main_ram + 0x29000, 0x1000);
	}
	return ret;
}


static void mega65_init ( void )
{
	hypervisor_debug_init(configdb.kickuplist, configdb.hyperdebug, configdb.hyperserialascii);
	hid_init(
		c64_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// joy HID events enabled
	);
#ifdef HID_KBD_MAP_CFG_SUPPORT
	hid_keymap_from_config_file(configdb.keymap);
#endif
	joystick_emu = 1;
	nmi_level = 0;
	// *** FPGA switches ...
	do {
		int switches[16], r = xemucfg_integer_list_from_string(configdb.fpga, switches, 16, ",");
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
	// *** Pre-init some memory areas from built-in resources, or by loaded binaries, see the
	//     previous part of the source here and refill_memory_from_preinit_cache() function definitation
	// Notes about the used "built-in" contents as "binary blobs":
	// C65 ROM image is not included in Xemu built-in, since it's a copyrighted, non-CPL licensed material
	// Other "blobs" are built-in, they are extracted from mega65-core project (unmodified) and it's also a GPL
	// project, with all sources available on-line, thus no licensing/copyright problem here.
	// For mega65-core source, visit https://github.com/MEGA65/mega65-core
	// For C000 utilties: mega65-core currently under reorganization, no C000 utilties are provided.
	force_external_rom = ((load_memory_preinit_cache(1, "loadrom", configdb.loadrom, "C65 ROM image", rom_init_image, sizeof rom_init_image) == (int)sizeof(rom_init_image)) && configdb.forcerom);
	if (force_external_rom)
		DEBUGPRINT("MEM: forcing external ROM usage (hypervisor leave memory re-fill policy)" NL);
	else if (configdb.forcerom)
		ERROR_WINDOW("-forcerom is ignored, because no -loadrom <filename> option was used, or it was not a succesfull load operation at least");
	load_memory_preinit_cache(0, "loadcram", configdb.loadcram, "CRAM utilities", meminitdata_cramutils, MEMINITDATA_CRAMUTILS_SIZE);
	load_memory_preinit_cache(0, "loadbanner", configdb.loadbanner, "M65 logo", meminitdata_banner, MEMINITDATA_BANNER_SIZE);
	load_memory_preinit_cache(1, "loadc000", configdb.loadc000, "C000 utilities", c000_init_image, sizeof c000_init_image);
	if (load_memory_preinit_cache(0, "kickup", configdb.kickup, "M65 kickstart", meminitdata_kickstart, MEMINITDATA_KICKSTART_SIZE)  != MEMINITDATA_KICKSTART_SIZE)
		hypervisor_debug_invalidate("no kickup is loaded, built-in one does not have debug info");
	// *** Initializes memory subsystem of MEGA65 emulation itself
	memory_init();
	// Load contents of NVRAM.
	// Also store as "nvram_original" so we can sense on shutdown of the emu, if we need to up-date the on-disk version
	// If we fail to load it (does not exist?) it will be written out anyway on exit.
	if (xemu_load_file(NVRAM_FILE_NAME, nvram, sizeof nvram, sizeof nvram, "Cannot load NVRAM state. Maybe first run of Xemu?\nOn next Xemu run, it should have been corrected though automatically!\nSo no need to worry.") == sizeof nvram) {
		memcpy(nvram_original, nvram, sizeof nvram);
	} else {
		// could not load from disk. Initialize to soma values.
		// Alsa, set nvram and nvram_original being different, so exit handler will sense the situation and save it.
		memset(nvram, 0, sizeof nvram);
		memset(nvram_original, 0xAA, sizeof nvram);
	}
	// We generate (if does not exist) an UUID for ourself. It can be read back via the 'UUID' registers.
	if (xemu_load_file(UUID_FILE_NAME, mega65_uuid, sizeof mega65_uuid, sizeof mega65_uuid, NULL) != sizeof mega65_uuid) {
		for (int a = 0; a < sizeof mega65_uuid; a++) {
			mega65_uuid[a] = rand();
		}
		uuid_must_be_saved = 1;
	}
	// fill the actual M65 memory areas with values managed by load_memory_preinit_cache() calls
	// This is a separated step, to be able to call refill_memory_from_preinit_cache() later as well, in case of a "deep reset" functionality is needed for Xemu (not just CPU/hw reset),
	// without restarting Xemu for that purpose.
	refill_memory_from_preinit_cache();
	// *** Image file for SDCARD support
	if (sdcard_init(configdb.sdimg, configdb.disk8, configdb.virtsd) < 0)
		FATAL("Cannot find SD-card image (which is a must for MEGA65 emulation): %s", configdb.sdimg);
	// *** Initialize VIC4
	vic_init();
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
	cia2.DDRA = 3; // Ugly workaround ... I think, SD-card setup "CRAM UTIL" (or better: the kickstart) should set this by its own. Maybe Xemu bug, maybe not?
	// *** Initialize DMA (we rely on memory and I/O decoder provided functions here for the purpose)
	dma_init(newhack ? DMA_FEATURE_HACK | DMA_FEATURE_DYNMODESET | configdb.dmarev : configdb.dmarev);
	// Initialize FDC
	fdc_init(disk_buffers + FD_BUFFER_POS);
	//
#ifdef HAS_UARTMON_SUPPORT
	uartmon_init(configdb.uartmon);
#endif
	sprintf(fast_mhz_in_string, "%.2fMHz", configdb.fast_mhz);
	DEBUGPRINT("SPEED: fast clock is set to %.2fMHz." NL, configdb.fast_mhz);
	cpu65_reset(); // reset CPU (though it fetches its reset vector, we don't use that on M65, but the KS hypervisor trap)
	rom_protect = 0;
	hypervisor_start_machine();
	speed_current = 0;
	machine_set_speed(1);
	DEBUG("INIT: end of initialization!" NL);
#ifdef XEMU_SNAPSHOT_SUPPORT
	xemusnap_init(m65_snapshot_definition);
	if (configdb.snapload) {
		if (xemusnap_load(configdb.snapload))
			FATAL("Couldn't load snapshot \"%s\": %s", configdb.snapload, xemusnap_error_buffer);
	}
	atexit(m65_snapshot_saver_on_exit_callback);
#endif
}


int dump_memory ( const char *fn )
{
	if (fn && *fn) {
		DEBUGPRINT("MEM: Dumping memory into file: %s" NL, fn);
		return xemu_save_file(fn, main_ram, (128 + 256) * 1024, "Cannot dump memory into file");
	} else {
		return 0;
	}
}


static void shutdown_callback ( void )
{
	// Write out NVRAM if changed!
	if (memcmp(nvram, nvram_original, sizeof(nvram))) {
		DEBUGPRINT("NVRAM: changed, writing out on exit." NL);
		xemu_save_file(NVRAM_FILE_NAME, nvram, sizeof nvram, "Cannot save changed NVRAM state! NVRAM changes will be lost!");
	}
	if (uuid_must_be_saved) {
		uuid_must_be_saved = 0;
		DEBUGPRINT("UUID: must be saved." NL);
		xemu_save_file(UUID_FILE_NAME, mega65_uuid, sizeof mega65_uuid, NULL);
	}
	eth65_shutdown();
	for (int a = 0; a < 0x40; a++)
		DEBUG("VIC-3 register $%02X is %02X" NL, a, vic_registers[a]);
	cia_dump_state (&cia1);
	cia_dump_state (&cia2);
#if !defined(XEMU_ARCH_HTML)
	(void)dump_memory(configdb.dumpmem);
#endif
#ifdef HAS_UARTMON_SUPPORT
	uartmon_close();
#endif
	DEBUG("Execution has been stopped at PC=$%04X" NL, cpu65.pc);
}



void reset_mega65 ( void )
{
	eth65_reset();
	force_fast = 0;	// FIXME: other default speed controls on reset?
	c128_d030_reg = 0xFF;
	machine_set_speed(0);
	memory_set_cpu_io_port_ddr_and_data(0xFF, 0xFF);
	map_mask = 0;
	in_hypervisor = 0;
	vic_registers[0x30] = 0;	// FIXME: hack! we need this, and memory_set_vic3_rom_mapping above too :(
	memory_set_vic3_rom_mapping(0);
	memory_set_do_map();
	vic_reset();	// FIXME: we may need a RESET on VIC-IV what ROM would not initialize but could be used by some MEGA65-aware program? [and hyppo does not care to reset?]
	cpu65_reset();
	dma_reset();
	nmi_level = 0;
	D6XX_registers[0x7E] = configdb.kicked;
	hypervisor_start_machine();
	DEBUGPRINT("SYSTEM RESET." NL);
}


int reset_mega65_asked ( void )
{
	if (ARE_YOU_SURE("Are you sure to HARD RESET your emulated machine?", i_am_sure_override | ARE_YOU_SURE_DEFAULT_YES)) {
		reset_mega65();
		return 1;
	} else
		return 0;
}


static void update_emulator ( void )
{
	if (XEMU_UNLIKELY(inject_ready_check_status))
		inject_ready_check_do();
	sid1.sFrameCount++;
	sid2.sFrameCount++;
	strcpy(emulator_speed_title, cpu_clock_speed_strs[cpu_clock_speed_str_index]);
	strcat(emulator_speed_title, " ");
	strcat(emulator_speed_title, videostd_name);
	hid_handle_all_sdl_events();
	xemugui_iteration();
	nmi_set(IS_RESTORE_PRESSED(), 2);	// Custom handling of the restore key ...
	// this part is used to trigger 'RESTORE trap' with long press on RESTORE.
	// see input_devices.c for more information
	kbd_trigger_restore_trap();
#ifdef HAS_UARTMON_SUPPORT
	uartmon_update();
#endif
	// Screen updating, final phase
	vic4_close_frame_access();
	// Let's sleep ...
	xemu_timekeeping_delay(videostd_frametime);
	// Ugly CIA trick to maintain realtime TOD in CIAs :)
//	if (seconds_timer_trigger) {
	const struct tm *t = xemu_get_localtime();
	const Uint8 sec10ths = xemu_get_microseconds() / 100000;
	// UPDATE CIA TODs:
	cia_ugly_tod_updater(&cia1, t, sec10ths, configdb.rtc_hour_offset);
	cia_ugly_tod_updater(&cia2, t, sec10ths, configdb.rtc_hour_offset);
	// UPDATE the RTC too:
	rtc_regs[0] = XEMU_BYTE_TO_BCD(t->tm_sec);	// seconds
	rtc_regs[1] = XEMU_BYTE_TO_BCD(t->tm_min);	// minutes
	rtc_regs[2] = xemu_hour_to_bcd12h(t->tm_hour, configdb.rtc_hour_offset);	// hours
	rtc_regs[3] = XEMU_BYTE_TO_BCD(t->tm_mday);	// day of mounth
	rtc_regs[4] = XEMU_BYTE_TO_BCD(t->tm_mon) + 1;	// month
	rtc_regs[5] = XEMU_BYTE_TO_BCD(t->tm_year - 100);	// year
//	}
}


#ifdef HAS_UARTMON_SUPPORT
void m65mon_show_regs ( void )
{
	Uint8 pf = cpu65_get_pf();
	umon_printf(
		"PC   A  X  Y  Z  B  SP   MAPL MAPH LAST-OP     P  P-FLAGS   RGP uS IO\r\n"
		"%04X %02X %02X %02X %02X %02X %04X "		// register banned message and things from PC to SP
		"%04X %04X %02X       %02X %02X "		// from MAPL to P
		"%c%c%c%c%c%c%c%c ",				// P-FLAGS
		cpu65.pc, cpu65.a, cpu65.x, cpu65.y, cpu65.z, cpu65.bphi >> 8, cpu65.sphi | cpu65.s,
		map_offset_low >> 8, map_offset_high >> 8, cpu65.op,
		pf, 0,	// flags
		(pf & CPU65_PF_N) ? 'N' : '-',
		(pf & CPU65_PF_V) ? 'V' : '-',
		(pf & CPU65_PF_E) ? 'E' : '-',
		'-',
		(pf & CPU65_PF_D) ? 'D' : '-',
		(pf & CPU65_PF_I) ? 'I' : '-',
		(pf & CPU65_PF_Z) ? 'Z' : '-',
		(pf & CPU65_PF_C) ? 'C' : '-'
	);
}

void m65mon_dumpmem16 ( Uint16 addr )
{
	int n = 16;
	umon_printf(":000%04X:", addr);
	while (n--)
		umon_printf("%02X", cpu65_read_callback(addr++));
}

void m65mon_dumpmem28 ( int addr )
{
	int n = 16;
	addr &= 0xFFFFFFF;
	umon_printf(":%07X:", addr);
	while (n--)
		umon_printf("%02X", memory_debug_read_phys_addr(addr++));
}

void m65mon_setmem28( int addr, int cnt, Uint8* vals )
{
  for (int k = 0; k < cnt; k++)
  {
    memory_debug_write_phys_addr(addr++, vals[k]);
  }
}

void m65mon_set_trace ( int m )
{
	paused = m;
}

#ifdef TRACE_NEXT_SUPPORT
void m65mon_do_next ( void )
{
	if (paused) {
		umon_send_ok = 0;			// delay command execution!
		m65mon_callback = m65mon_show_regs;	// register callback
		trace_next_trigger = 2;			// if JSR, then trigger until RTS to next_addr
		orig_sp = cpu65.sphi | cpu65.s;
		paused = 0;
	} else {
		umon_printf(UMON_SYNTAX_ERROR "trace can be used only in trace mode");
	}
}
#endif

void m65mon_do_trace ( void )
{
	if (paused) {
		umon_send_ok = 0; // delay command execution!
		m65mon_callback = m65mon_show_regs; // register callback
		trace_step_trigger = 1;	// trigger one step
	} else {
		umon_printf(UMON_SYNTAX_ERROR "trace can be used only in trace mode");
	}
}

void m65mon_do_trace_c ( void )
{
	umon_printf(UMON_SYNTAX_ERROR "command 'tc' is not implemented yet");
}
#ifdef TRACE_NEXT_SUPPORT
void m65mon_next_command ( void )
{
	if (paused)
		m65mon_do_next();
}
#endif
void m65mon_empty_command ( void )
{
	if (paused)
		m65mon_do_trace();
}

void m65mon_breakpoint ( int brk )
{
	breakpoint_pc = brk;
	if (brk < 0)
		cpu_cycles_per_step = cpu_cycles_per_scanline;
	else
		cpu_cycles_per_step = 0;
}
#endif


static void emulation_loop ( void )
{
	static int cycles = 0;	// used for "balance" CPU cycles per scanline, must be static!
	vic4_open_frame_access();
	// video standard (PAL/NTSC) affects the "CPU cycles per scanline" variable, which is used in this main emulation loop below.
	// thus, if vic-4 emulation set videostd_changed, we should react with enforce a re-calibration.
	// videostd_changed is set by vic4_open_frame_access() in vic4.c, thus we do here right after calling it.
	// machine_set_speed() will react to videostd_changed flag, so it's just enough to call it from here
	machine_set_speed(0);
	for (;;) {
#ifdef TRACE_NEXT_SUPPORT
		if (trace_next_trigger == 2) {
			if (cpu65.op == 0x20) {		// was the current opcode a JSR $nnnn ? (0x20)
				trace_next_trigger = 1;	// if so, let's loop until the stack pointer returns back, then pause
			} else {
				trace_next_trigger = 0;	// if the current opcode wasn't a JSR, then lets pause immediately after
				paused = 1;
			}
		} else if (trace_next_trigger == 1) {	// are we presently stepping over a JSR?
			if ((cpu65.sphi | cpu65.s) == orig_sp ) {	// did the current sp return to its original position?
				trace_next_trigger = 0;	// if so, lets pause the emulation, as we have successfully stepped over the JSR
				paused = 1;
			}
		}
#endif
		while (XEMU_UNLIKELY(paused)) {	// paused special mode, ie tracing support, or something ...
			if (XEMU_UNLIKELY(dma_status))
				break;		// if DMA is pending, do not allow monitor/etc features
#ifdef HAS_UARTMON_SUPPORT
			if (m65mon_callback) {	// delayed uart monitor command should be finished ...
				m65mon_callback();
				m65mon_callback = NULL;
				uartmon_finish_command();
			}
#endif
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
				if (paused) {
					DEBUGPRINT("TRACE: entering into trace mode @ $%04X" NL, cpu65.pc);
					cpu_cycles_per_step = 0;
				} else {
					DEBUGPRINT("TRACE: leaving trace mode @ $%04X" NL, cpu65.pc);
					if (breakpoint_pc < 0)
						cpu_cycles_per_step = cpu_cycles_per_scanline;
					else
						cpu_cycles_per_step = 0;
				}
			}
		}
		if (XEMU_UNLIKELY(in_hypervisor)) {
			hypervisor_debug();
		}
		if (XEMU_UNLIKELY(breakpoint_pc == cpu65.pc)) {
			DEBUGPRINT("TRACE: Breakpoint @ $%04X hit, Xemu moves to trace mode after the execution of this opcode." NL, cpu65.pc);
			paused = 1;
		}
		cycles += XEMU_UNLIKELY(dma_status) ? dma_update_multi_steps(cpu_cycles_per_scanline) : cpu65_step(
#ifdef CPU_STEP_MULTI_OPS
			cpu_cycles_per_step
#endif
		);	// FIXME: this is maybe not correct, that DMA's speed depends on the fast/slow clock as well?
		if (cycles >= cpu_cycles_per_scanline) {
			cycles -= cpu_cycles_per_scanline;
			cia_tick(&cia1, 32);	// FIXME: why 32?????? why fixed????? what should be the CIA "tick" frequency for real? Is it dependent on NTSC/PAL?
			cia_tick(&cia2, 32);
			if (XEMU_UNLIKELY(vic4_render_scanline()))
				break;	// break the (main, "for") loop, if frame is over!
		}
	}
	update_emulator();
}


int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Incomplete MEGA65 emulator from LGB");
	configdb_define_emulator_options(sizeof configdb);
	if (xemucfg_parse_all(argc, argv))
		return 1;
	// xemucfg_dump_db("After returning from xemucfg_parse_all in main()");
	DEBUGPRINT("XEMU: emulated MEGA65 model ID: %d" NL, configdb.mega65_model);
#ifdef HAVE_XEMU_INSTALLER
	xemu_set_installer(configdb.installer);
#endif
	newhack = !newhack;	// hehe, the meaning is kind of inverted, but never mind ...
	if (newhack)
		DEBUGPRINT("WARNING: *** NEW M65 HACK MODE ACTIVATED ***" NL);
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	window_title_info_addon = emulator_speed_title;
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size (used with keeping aspect ratio by the SDL render stuffs)
		SCREEN_WIDTH, SCREEN_HEIGHT,	// window size
		SCREEN_FORMAT,			// pixel format
		0,				// we have *NO* pre-defined colours as with more simple machines (too many we need). we want to do this ourselves!
		NULL,				// -- "" --
		NULL,				// -- "" --
		configdb.sdlrenderquality,	// render scaling quality
		USE_LOCKED_TEXTURE,		// 1 = locked texture access
		shutdown_callback		// registered shutdown function
	))
		return 1;
	osd_init_with_defaults();
	xemugui_init(configdb.selectedgui);
	// Initialize MEGA65
	mega65_init();
	audio65_init(
		SID_CYCLES_PER_SEC,		// SID cycles per sec
		AUDIO_SAMPLE_FREQ,		// sound mix freq
		configdb.mastervolume,
		configdb.stereoseparation
	);
	DEBUGPRINT("MEM: UNHANDLED memory policy: %d" NL, configdb.skip_unhandled_mem);
	if (configdb.skip_unhandled_mem)
		skip_unhandled_mem = 3; // silent ignore all = 3
	else
		skip_unhandled_mem = 0;	// ask = 0
	eth65_init(
#ifdef HAVE_ETHERTAP
		configdb.ethertap
#else
		NULL
#endif
	);
#ifdef HAVE_XEMU_UMON
	if (configdb.umon != 0) {
		int port = configdb.umon;
		int threaded;
		if (port < 0) {
			port = -port;
			threaded = 0;
		} else
			threaded = 1;
		if (port > 1023 && port < 65536)
			xumon_init(port, threaded);
		else
			ERROR_WINDOW("UMON: Invalid TCP port: %d", port);
	}
#endif
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
	if (audio) {
		DEBUGPRINT("AUDIO: start" NL);
		SDL_PauseAudioDevice(audio, 0);
	}
	xemu_set_full_screen(configdb.fullscreen_requested);
	if (!configdb.syscon)
		sysconsole_close(NULL);
	xemu_timekeeping_start();
	// FIXME: for emscripten (anyway it does not work too much currently) there should be 50 or 60 (PAL/NTSC) instead of (fixed, and wrong!) 25!!!!!!
	XEMU_MAIN_LOOP(emulation_loop, 25, 1);
	return 0;
}

/* --- SNAPSHOT RELATED --- */

#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_M65_BLOCK_VERSION	2
#define SNAPSHOT_M65_BLOCK_SIZE		(0x100 + sizeof(D6XX_registers) + sizeof(D7XX))

static int force_fast_loaded;


int m65emu_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_M65_BLOCK_SIZE];
	int a;
	if (block->block_version != SNAPSHOT_M65_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad M65 block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	memcpy(D6XX_registers, buffer + 0x100, sizeof D6XX_registers);
	memcpy(D7XX, buffer + 0x200, sizeof D7XX);
	in_hypervisor = 1;	// simulate hypervisor mode, to allow to write some regs now instead of causing a TRAP now ...
	io_write(0x367D, D6XX_registers[0x7D]);			// write $(D)67D in VIC-IV I/O mode! (sets ROM protection, linear addressing mode enable ...)
	// TODO FIXME: see if there is a need for other registers from D6XX_registers to write back to take effect on loading snapshot!
	// end of spec, hypervisor-needed faked mode for loading snapshot ...
	map_mask = (int)P_AS_BE32(buffer + 0);
	map_offset_low = (int)P_AS_BE32(buffer + 4);
	map_offset_high = (int)P_AS_BE32(buffer + 8);
	cpu65.cpu_inhibit_interrupts = (int)P_AS_BE32(buffer + 12);
	in_hypervisor = (int)P_AS_BE32(buffer + 16);	// sets hypervisor state from snapshot (hypervisor/userspace)
	map_megabyte_low = (int)P_AS_BE32(buffer + 20);
	map_megabyte_high = (int)P_AS_BE32(buffer + 24);
	force_fast_loaded = (int)P_AS_BE32(buffer + 28);	// activated in m65emu_snapshot_loading_finalize() as force_fast can be set at multiple places through loading snapshot!
	// +32 is free for 4 bytes now ... can be used later
	memory_set_cpu_io_port_ddr_and_data(buffer[36], buffer[37]);
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
	U32_AS_BE(buffer + 12, cpu65.cpu_inhibit_interrupts);
	U32_AS_BE(buffer + 16, in_hypervisor);
	U32_AS_BE(buffer + 20, map_megabyte_low);
	U32_AS_BE(buffer + 24, map_megabyte_high);
	U32_AS_BE(buffer + 28, force_fast);	// see notes on this at load_state and finalize stuff!
	// +32 is free for 4 bytes now ... can be used later
	buffer[36] = memory_get_cpu_io_port(0);
	buffer[37] = memory_get_cpu_io_port(1);
	memcpy(buffer + 0x100, D6XX_registers, sizeof D6XX_registers);
	memcpy(buffer + 0x200, D7XX, sizeof D7XX);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}


int m65emu_snapshot_loading_finalize ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	DEBUGPRINT("SNAP: loaded (finalize-callback: begin)" NL);
	memory_set_vic3_rom_mapping(vic_registers[0x30]);
	memory_set_do_map();
	force_fast = force_fast_loaded;	// force_fast is handled through different places, so we must have a "finalize" construct and saved separately to have the actual effect ...
	machine_set_speed(1);
	DEBUGPRINT("SNAP: loaded (finalize-callback: end)" NL);
	OSD(-1, -1, "Snapshot has been loaded.");
	return 0;
}
#endif
