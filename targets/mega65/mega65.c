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
#include "dma65.h"
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
#include "xemu/emutools_socketapi.h"
#include "rom.h"


static int nmi_level;			// please read the comment at nmi_set() below

int newhack = 0;

static int emulation_is_running = 0;
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
int cpu_cycles_per_step = 100; 	// some init value, will be overriden, but it must be greater initially than "only a few" anyway

static Uint8 nvram_original[sizeof nvram];
static int uuid_must_be_saved = 0;

int registered_screenshot_request = 0;

Uint8 last_dd00_bits = 3;		// Bank 0
const char *last_reset_type;



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
			in_hypervisor, D6XX_registers[0x7D] & 16, (c128_d030_reg & 1), vic_registers[0x31] & 64, vic_registers[0x54] & 64
	);
	// ^1 at c128... because it was inverted :-O --> FIXME: this is ugly workaround, the switch statement should be re-organized
	speed_wanted = (in_hypervisor || (D6XX_registers[0x7D] & 16)) ? 7 : ((((c128_d030_reg & 1) ^ 1) << 2) | ((vic_registers[0x31] & 64) >> 5) | ((vic_registers[0x54] & 64) >> 6));
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
				cpu65_set_timing(0);
				break;
			case 0:	// 000 - 2MHz
				cpu_cycles_per_scanline = (unsigned int)(videostd_1mhz_cycles_per_scanline * (float)(C128_MHZ_CLOCK));
				cpu_clock_speed_str_index = 1;
				cpu65_set_timing(0);
				break;
			case 2:	// 010 - 3.5MHz
			case 6:	// 110 - 3.5MHz
				cpu_cycles_per_scanline = (unsigned int)(videostd_1mhz_cycles_per_scanline * (float)(C65_MHZ_CLOCK));
				cpu_clock_speed_str_index = 2;
				cpu65_set_timing(1);
				break;
			case 1:	// 001 - 40MHz (or Xemu specified custom speed)
			case 3:	// 011 -		-- "" --
			case 7:	// 111 -		-- "" --
				cpu_cycles_per_scanline = (unsigned int)(videostd_1mhz_cycles_per_scanline * (float)(configdb.fast_mhz));
				cpu_clock_speed_str_index = 3;
				cpu65_set_timing(2);
				break;
		}
		// XXX use only DEBUG() here!
		DEBUGPRINT("SPEED: CPU speed is set to %s, cycles per scanline: %d in %s (1MHz cycles per scanline: %f)" NL, cpu_clock_speed_strs[cpu_clock_speed_str_index], cpu_cycles_per_scanline, videostd_name, videostd_1mhz_cycles_per_scanline);
		if (cpu_cycles_per_step > 1 && !hypervisor_is_debugged && !configdb.cpusinglestep)
			cpu_cycles_per_step = cpu_cycles_per_scanline;	// if in trace mode (or hyper-debug ...), do not set this! So set only if non-trace and non-hyper-debug
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


static int preinit_memory_item ( const char *name, const char *desc, Uint8 *target_ptr, const Uint8 *source_ptr, const int source_size, const int min_size, const int max_size, const char *fn )
{
	if (source_size < min_size || source_size > max_size || min_size > max_size)
		FATAL("MEMCONTENT: internal error, memcontent item \"%s\" (%s) given size (%d) is outside of interval %d...%d", name, desc, source_size, min_size, max_size);
	memset(target_ptr, 0, max_size);
	sha1_hash_str hash_str;
	if (XEMU_LIKELY(!fn || !*fn)) {
		sha1_checksum_as_string(hash_str, source_ptr, source_size);
		DEBUGPRINT("MEMCONTENT: \"%s\" (%s) was not requested, using the built-in ($%X bytes) [%s]." NL, name, desc, source_size, hash_str);
		goto internal;
	}
	const int size = xemu_load_file(fn, target_ptr, min_size, max_size, desc);
	if (XEMU_UNLIKELY(size > 0)) {
		sha1_checksum_as_string(hash_str, target_ptr, size);
		DEBUGPRINT("MEMCONTENT: \"%s\" (%s) loaded custom object ($%X bytes) from external file [%s]: %s" NL, name, desc, size, hash_str, xemu_load_filepath);
		return size;
	}
	sha1_checksum_as_string(hash_str, source_ptr, source_size);
	DEBUGPRINT("MEMCONTENT: \"%s\" (%s) **FAILED** to load custom file (using default - $%X bytes [%s]) by filename request: %s" NL, name, desc, source_size, hash_str, fn);
internal:
	memcpy(target_ptr, source_ptr, source_size);
	return 0;
}


static void preinit_memory_for_start ( void )
{
	// This is an absolute minimum flash utility to replace the official one ;)
	// As Xemu does not have flash (it does not deal with real bitstreams, being an emulator), the official
	// flash utility during the boot process would throw ugly error and wait for a key to continue, which
	// is annoying. This short code just creates the minimal thing the flash utility expected to do, to be
	// able to continue without any side effect.
	static const Uint8 megaflashutility[] = {
		0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78,
		0xA9, 0x00, 0x8D, 0x00, 0x00, 0xA9, 0x47, 0x8D, 0x2F, 0xD0, 0xA9, 0x53, 0x8D, 0x2F, 0xD0, 0xA9,
		0x4C, 0x8D, 0x7F, 0xCF, 0x4C, 0x7F, 0xCF
	};
	//                  Option/name     Description            Target memory ptr   Built-in source ptr    Built-in size               Minsize  Maxsize  External-filename(or-NULL)
	//                  --------------  ---------------------- ------------------- ---------------------- --------------------------- -------- -------- --------------------------
	preinit_memory_item("extfreezer",   "Freezer",             main_ram + 0x12000, meminitdata_freezer,   MEMINITDATA_FREEZER_SIZE,   0x00100, 0x0E000, configdb.extfreezer);
	preinit_memory_item("extinitrom",   "Initial boot-ROM",    main_ram + 0x20000, meminitdata_initrom,   MEMINITDATA_INITROM_SIZE,   0x20000, 0x20000, configdb.extinitrom);
	preinit_memory_item("extonboard",   "On-boarding utility", main_ram + 0x40000, meminitdata_onboard,   MEMINITDATA_ONBOARD_SIZE,   0x00020, 0x10000, configdb.extonboard);
	preinit_memory_item("extflashutil", "MEGA-flash utility",  main_ram + 0x50000, megaflashutility,      sizeof megaflashutility,    0x00020, 0x07D00, configdb.extflashutil);
	preinit_memory_item("extbanner",    "MEGA65 banner",       main_ram + 0x57D00, meminitdata_banner,    MEMINITDATA_BANNER_SIZE,    0x01000, 0x08300, configdb.extbanner);
	preinit_memory_item("extchrwom",    "Character-WOM",       char_wom,           meminitdata_chrwom,    MEMINITDATA_CHRWOM_SIZE,    0x01000, 0x01000, configdb.extchrwom);
	preinit_memory_item("extcramutils", "Utils in CRAM",       colour_ram,         meminitdata_cramutils, MEMINITDATA_CRAMUTILS_SIZE, 0x08000, 0x08000, configdb.extcramutils);
	hickup_is_overriden =
	preinit_memory_item("hickup",       "Hyppo-Hickup",        hypervisor_ram,     meminitdata_hickup,    MEMINITDATA_HICKUP_SIZE,    0x04000, 0x04000, configdb.hickup);
	//                  ----------------------------------------------------------------------------------------------------------------------------------------------------------
	if (!hickup_is_overriden)
		hypervisor_debug_invalidate("no external hickup is loaded, built-in one does not have debug info");
}


static void mega65_init ( void )
{
	last_reset_type = "XEMU-STARTUP";
	hypervisor_debug_init(configdb.hickuprep, configdb.hyperdebug, configdb.hyperserialascii);
	if (hypervisor_is_debugged || configdb.cpusinglestep)
		cpu_cycles_per_step = 0;
	hid_init(
		c64_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// joy HID events enabled
	);
#ifdef HID_KBD_MAP_CFG_SUPPORT
	hid_keymap_from_config_file(configdb.keymap);
#endif
	joystick_emu = 2;	// use joystick port #2 by default
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
	// *** Initializes memory subsystem of MEGA65 emulation itself
	memory_init();
	// Load contents of NVRAM.
	// Also store as "nvram_original" so we can sense on shutdown of the emu, if we need to up-date the on-disk version
	// If we fail to load it (does not exist?) it will be written out anyway on exit.
	if (xemu_load_file(NVRAM_FILE_NAME, nvram, sizeof nvram, sizeof nvram, "Cannot load NVRAM state. Maybe first run of Xemu?\nOn next Xemu run, it should have been corrected though automatically!\nSo no need to worry.") == sizeof nvram) {
		memcpy(nvram_original, nvram, sizeof nvram);
	} else {
		// could not load from disk. Initialize to soma values.
		// Also, set nvram and nvram_original being different, so exit handler will sense the situation and save it.
		memset(nvram, 0, sizeof nvram);
		memset(nvram_original, 0xAA, sizeof nvram);
	}
	// Let's generate (if it does not exist) an UUID for myself. It can be read back via the 'UUID' registers.
	if (xemu_load_file(UUID_FILE_NAME, mega65_uuid, sizeof mega65_uuid, sizeof mega65_uuid, NULL) != sizeof mega65_uuid) {
		for (int a = 0; a < sizeof mega65_uuid; a++) {
			mega65_uuid[a] = rand();
		}
		uuid_must_be_saved = 1;
	}
	// Fill memory with the needed pre-initialized regions to be able to start.
	preinit_memory_for_start();
	// If we have no -8 option given, but we found a suitable disk image in the pref-dir,
	// with the desired name, let's use that! In this way, it may cure some complains,
	// that the default disk is "inside" the SD-card image which is hard to deal with.
	if (!configdb.disk8) {
		static const char default_d81_fn[] = "default.d81";
		char *fn = xemu_malloc(strlen(sdl_pref_dir) + strlen(default_d81_fn) + 1);
		sprintf(fn, "%s%s", sdl_pref_dir, default_d81_fn);
		off_t size = xemu_safe_file_size_by_name(fn);
		if (size != OFF_T_ERROR) {
			if (size == (off_t)D81_SIZE) {
				DEBUGPRINT("DISK: using external default disk image, since without -8 we found: %s" NL, fn);
				configdb.disk8 = fn;
			} else {
				ERROR_WINDOW("Found: %s\nfor default external disk image,\nbut it has wrong size", fn);
				free(fn);
			}
		} else
			free(fn);
	}
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
		cia2_in_a,		// callback: INA
		cia2_in_b,		// callback: INB
		NULL,			// callback: INSR
		cia2_setint_cb		// callback: SETINT ~ that would be NMI in our case
	);
	cia2.DDRA = 3; // Ugly workaround ... I think, SD-card setup "CRAM UTIL" (or better: Hyppo) should set this by its own. Maybe Xemu bug, maybe not?
	// *** Initialize DMA (we rely on memory and I/O decoder provided functions here for the purpose)
	dma_init(newhack ? DMA_FEATURE_HACK | DMA_FEATURE_DYNMODESET | configdb.dmarev : configdb.dmarev);
	// Initialize FDC
	fdc_init(disk_buffers + FD_BUFFER_POS);
	//
	sdcard_hack_mount_drive_9_now(configdb.disk9);	// FIXME: Ugly hack to support CLI forced drive-9 disk
#ifdef HAS_UARTMON_SUPPORT
	uartmon_init(configdb.uartmon);
#endif
	sprintf(fast_mhz_in_string, "%.2fMHz", configdb.fast_mhz);
	DEBUGPRINT("SPEED: fast clock is set to %.2fMHz." NL, configdb.fast_mhz);
	cpu65_init_mega_specific();
	cpu65_reset(); // reset CPU (though it fetches its reset vector, we don't use that on M65, but the KS hypervisor trap)
	rom_protect = 0;
	hypervisor_start_machine();
	speed_current = 0;
	machine_set_speed(1);
	if (configdb.useutilmenu)
		hwa_kbd_fake_key(0x20);
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
#ifdef HAVE_XEMU_UMON
	xumon_stop();
#endif
#ifdef XEMU_HAS_SOCKET_API
	xemusock_uninit();
#endif
	if (emulation_is_running)
		DEBUGPRINT("CPU: Execution ended at PC=$%04X (linear=%X)" NL, cpu65.pc, memory_cpurd2linear_xlat(cpu65.pc));
}


void reset_mega65 ( void )
{
	static const char reset_debug_msg[] = "SYSTEM: RESET - ";
	last_reset_type = "COLD";
	DEBUGPRINT("%sBEGIN" NL, reset_debug_msg);
	memset(D7XX + 0x20, 0, 0x40);	// stop audio DMA possibly going on
	rom_clear_reports();
	preinit_memory_for_start();
	eth65_reset();
	D6XX_registers[0x7D] &= ~16;	// FIXME: other default speed controls on reset?
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
	D6XX_registers[0x7E] = configdb.hicked;
	hypervisor_start_machine();
	DEBUGPRINT("%sEND" NL, reset_debug_msg);
}


void reset_mega65_cpu_only ( void )
{
	last_reset_type = "WARM";
	D6XX_registers[0x7D] &= ~16;	// FIXME: other default speed controls on reset?
	c128_d030_reg = 0xFF;
	machine_set_speed(0);
	memory_set_cpu_io_port_ddr_and_data(0xFF, 0xFF);
	map_mask = 0;
	in_hypervisor = 0;
	vic_registers[0x30] = 0;	// FIXME: hack! we need this, and memory_set_vic3_rom_mapping above too :(
	memory_set_vic3_rom_mapping(0);
	memory_set_do_map();
	cpu65_reset();
}


int reset_mega65_asked ( void )
{
	if (ARE_YOU_SURE("Are you sure to RESET your emulated machine?", i_am_sure_override | ARE_YOU_SURE_DEFAULT_YES)) {
		reset_mega65();
		return 1;
	} else
		return 0;
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


static void update_emulator ( void )
{
	vic4_close_frame_access();
	// XXX: some things has been moved here from the main loop, however update_emulator is called from other places as well, FIXME check if it causes problems or not!
	if (XEMU_UNLIKELY(inject_ready_check_status))
		inject_ready_check_do();
	audio65_sid_inc_framecount();
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
	//vic4_close_frame_access();
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
	//rtc_regs[2] = xemu_hour_to_bcd12h(t->tm_hour, configdb.rtc_hour_offset);	// hours
	rtc_regs[2] = XEMU_BYTE_TO_BCD((t->tm_hour + configdb.rtc_hour_offset + 24) % 24) | 0x80;	// hours (24H format, bit 7 always set)
	rtc_regs[3] = XEMU_BYTE_TO_BCD(t->tm_mday);	// day of mounth
	rtc_regs[4] = XEMU_BYTE_TO_BCD(t->tm_mon) + 1;	// month
	rtc_regs[5] = XEMU_BYTE_TO_BCD(t->tm_year - 100);	// year
//	}
}


static void emulation_loop ( void )
{
	static int cycles = 0;	// used for "balance" CPU cycles per scanline, must be static!
	xemu_window_snap_to_optimal_size(0);
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
			// we still need to feed our emulator with update events ... It also slows this pause-busy-loop down to every full frames (~25Hz) <--- XXX totally inaccurate now!
			// note, that it messes timing up a bit here, as there is update_emulator() calls later in the "normal" code as well
			// this can be a bug, but real-time emulation is not so much an issue if you eg doing trace of your code ...
			// XXX it's maybe a problem to call this!!! update_emulator() is called here which closes frame but no no reopen then ... FIXME: handle this somehow!
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
		if (XEMU_UNLIKELY(in_hypervisor))
			hypervisor_debug();
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
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Evolving MEGA65 emulator from LGB");
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
		TEXTURE_WIDTH, TEXTURE_HEIGHT,	// texture sizes
		TEXTURE_WIDTH, TEXTURE_HEIGHT,	// logical size (used with keeping aspect ratio by the SDL render stuffs)
		TEXTURE_WIDTH, TEXTURE_HEIGHT,	// window size
		TEXTURE_FORMAT,			// pixel format
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
		configdb.stereoseparation,
		configdb.audiobuffersize
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
	if (configdb.umon == 1)
		configdb.umon = XUMON_DEFAULT_PORT;
	xumon_init(configdb.umon);
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
	rom_stubrom_requested = configdb.usestubrom;
	rom_initrom_requested = configdb.useinitrom;
	rom_load_custom(configdb.rom);
	audio65_start();
	xemu_set_full_screen(configdb.fullscreen_requested);
	if (!configdb.syscon)
		sysconsole_close(NULL);
	xemu_timekeeping_start();
	emulation_is_running = 1;
	// FIXME: for emscripten (anyway it does not work too much currently) there should be 50 or 60 (PAL/NTSC) instead of (fixed, and wrong!) 25!!!!!!
	XEMU_MAIN_LOOP(emulation_loop, 25, 1);
	return 0;
}

/* --- SNAPSHOT RELATED --- */

#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_M65_BLOCK_VERSION	2
#define SNAPSHOT_M65_BLOCK_SIZE		(0x100 + sizeof(D6XX_registers) + sizeof(D7XX))


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
	//force_fast_loaded = (int)P_AS_BE32(buffer + 28);	// activated in m65emu_snapshot_loading_finalize() as force_fast can be set at multiple places through loading snapshot!
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
	//U32_AS_BE(buffer + 28, force_fast);	// see notes on this at load_state and finalize stuff!
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
	//force_fast = force_fast_loaded;	// force_fast is handled through different places, so we must have a "finalize" construct and saved separately to have the actual effect ...
	machine_set_speed(1);
	DEBUGPRINT("SNAP: loaded (finalize-callback: end)" NL);
	OSD(-1, -1, "Snapshot has been loaded.");
	return 0;
}
#endif
