/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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
#include "xemu/f011_core.h"
#include "xemu/f018_core.h"
#include "xemu/emutools_hid.h"
#include "vic4.h"
#include "sdcard.h"
#include "uart_monitor.h"
#include "hypervisor.h"
#include "xemu/c64_kbd_mapping.h"
#include "xemu/emutools_config.h"
#include "m65_snapshot.h"
#include "memory_mapper.h"
#include "io_mapper.h"


static SDL_AudioDeviceID audio = 0;

static int nmi_level;			// please read the comment at nmi_set() below



#define TRAP_RESET	0x40

static int fast_mhz, cpu_cycles_per_scanline_for_fast_mode, speed_current;
static char fast_mhz_in_string[8];
static int frame_counter;
static int   paused = 0, paused_old = 0;
static int   breakpoint_pc = -1;
static int   trace_step_trigger = 0;
static void (*m65mon_callback)(void) = NULL;
static const char emulator_paused_title[] = "TRACE/PAUSE";
static char emulator_speed_title[] = "????MHz";
static int cpu_cycles_per_step = 100; 	// some init value, will be overriden, but it must be greater initially than "only a few" anyway





void machine_set_speed ( int verbose )
{
	int speed_wanted;
	// TODO: Mega65 speed is not handled yet. Reasons: too slow emulation for average PC, and the complete control of speed, ie lack of C128-fast (2MHz mode,
	// because of incomplete VIC register I/O handling).
	// Actually the rule would be something like that (this comment is here by intent, for later implementation FIXME TODO), some VHDL draft only:
	// cpu_speed := vicii_2mhz&viciii_fast&viciv_fast
	// if hypervisor_mode='0' and ((speed_gate='1') and (force_fast='0')) then -- LGB: vicii_2mhz seems to be a low-active signal?
	// case cpu_speed is ...... 100=1MHz, 101=1MHz, 110=3.5MHz, 111=48Mhz, 000=2MHz, 001=48MHz, 010=3.5MHz, 011=48MHz
	// else 48MHz end if;
	// it seems hypervisor always got full speed, and force_fast (ie, POKE 0,65) always forces the max
	// TODO: what is speed_gate? (it seems to be a PMOD input and/or keyboard controll with CAPS-LOCK)
	// TODO: how 2MHz is selected, it seems a double decoded VIC-X registers which is not so common in VIC modes yet, I think ...
	//Uint8 desired = (in_hypervisor || force_fast) ? 7 : (((c128_d030_reg & 1) << 2) | ((vic_registers[0x31] & 64) >> 5) | ((vic_registers[0x54] & 64) >> 6));
	//if (desired == current_speed_config)
	//	return;
	if (verbose)
		printf("SPEED: in_hypervisor=%d force_fast=%d c128_fast=%d, c65_fast=%d m65_fast=%d" NL,
			in_hypervisor, force_fast, (c128_d030_reg & 1) ^ 1, vic_registers[0x31] & 64, vic_registers[0x54] & 64
	);
	speed_wanted = (in_hypervisor || force_fast) ? 7 : (((c128_d030_reg & 1) << 2) | ((vic_registers[0x31] & 64) >> 5) | ((vic_registers[0x54] & 64) >> 6));
	if (speed_wanted != speed_current) {
		speed_current = speed_wanted;
		switch (speed_wanted) {
			case 4:	// 100 - 1MHz
			case 5:	// 101 - 1MHz
				cpu_cycles_per_scanline = CPU_C64_CYCLES_PER_SCANLINE;
				strcpy(emulator_speed_title, "1MHz");
				break;
			case 0:	// 000 - 2MHz
				cpu_cycles_per_scanline = CPU_C128_CYCLES_PER_SCANLINE;
				strcpy(emulator_speed_title, "2MHz");
				break;
			case 2:	// 010 - 3.5MHz
			case 6:	// 110 - 3.5MHz
				cpu_cycles_per_scanline = CPU_C65_CYCLES_PER_SCANLINE;
				strcpy(emulator_speed_title, "3.5MHz");
				break;
			case 1:	// 001 - 48MHz (or Xemu specified custom speed)
			case 3:	// 011 -		-- "" --
			case 7:	// 111 -		-- "" --
				cpu_cycles_per_scanline = cpu_cycles_per_scanline_for_fast_mode;
				strcpy(emulator_speed_title, fast_mhz_in_string);
				break;
		}
		DEBUG("SPEED: CPU speed is set to %s" NL, emulator_speed_title);
		if (cpu_cycles_per_step > 1)	// if in trace mode, do not set this!
			cpu_cycles_per_step = cpu_cycles_per_scanline;
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
	memory_init();
	D6XX_registers[0x7E] = emucfg_get_num("kicked");
	// *** Trying to load kickstart image
	p = emucfg_get_str("kickup");
	if (emu_load_file(p, hypervisor_ram, 0x4001) == 0x4000) {
		DEBUG("MEGA65: %s loaded into hypervisor memory." NL, p);
	} else {
		// note, hypervisor_ram is pre-initialized with the built-in kickstart already, included from memory_mapper.c
		WARNING_WINDOW("Kickstart %s cannot be found. Using the default (maybe outdated!) built-in version", p);
		hypervisor_debug_invalidate("no kickup could be loaded, built-in one does not have debug info");
	}
	// *** Image file for SDCARD support
	if (sdcard_init(emucfg_get_str("sdimg"), emucfg_get_str("8")) < 0)
		FATAL("Cannot find SD-card image (which is a must for Mega65 emulation): %s", emucfg_get_str("sdimg"));
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
	// *** Initialize DMA (we rely on memory and I/O decoder provided functions here for the purpose)
	dma_init(
		emucfg_get_num("dmarev"),
		memory_dma_source_mreader,	// dma_reader_cb_t set_source_mreader
		memory_dma_source_mwriter,	// dma_writer_cb_t set_source_mwriter
		memory_dma_target_mreader,	// dma_reader_cb_t set_target_mreader
		memory_dma_target_mwriter,	// dma_writer_cb_t set_target_mwriter
		io_dma_reader,			// dma_reader_cb_t set_source_ioreader
		io_dma_writer,			// dma_writer_cb_t set_source_iowriter
		io_dma_reader,			// dma_reader_cb_t set_target_ioreader
		io_dma_writer,			// dma_writer_cb_t set_target_iowriter
		memory_dma_list_reader		// dma_reader_cb_t set_list_reader
	);
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
	fast_mhz = emucfg_get_num("fastclock");
	if (fast_mhz < 3 || fast_mhz > 200) {
		ERROR_WINDOW("Fast clock given by -fastclock switch must be between 3...200MHz. Bad value, defaulting to %dMHz", MEGA65_DEFAULT_FAST_CLOCK);
		fast_mhz = 48;
	}
	sprintf(fast_mhz_in_string, "%dMHz", fast_mhz);
	cpu_cycles_per_scanline_for_fast_mode = 64 * fast_mhz;
	DEBUGPRINT("SPEED: fast clock is set to %dMHz, %d CPU cycles per scanline." NL, fast_mhz, cpu_cycles_per_scanline_for_fast_mode);
	cpu_reset(); // reset CPU (though it fetches its reset vector, we don't use that on M65, but the KS hypervisor trap)
	rom_protect = 0;
	hypervisor_enter(TRAP_RESET);
	speed_current = 0;
	machine_set_speed(1);
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







static void shutdown_callback ( void )
{
#ifdef MEMDUMP_FILE
	FILE *f;
#endif
	int a;
	for (a = 0; a < 0x40; a++)
		DEBUG("VIC-3 register $%02X is %02X" NL, a, vic_registers[a]);
	cia_dump_state (&cia1);
	cia_dump_state (&cia2);
#ifdef MEMDUMP_FILE
	// Dump hypervisor memory to a file, so you can check it after exit.
	f = fopen(MEMDUMP_FILE, "wb");
	if (f) {
		fwrite(chip_ram, 1, sizeof chip_ram, f);
		fwrite(colour_ram, 1, 2048, f);
		fwrite(fast_ram, 1, sizeof fast_ram, f);
		fclose(f);
		DEBUGPRINT("Memory state (chip+fast RAM, 256K) is dumped into " MEMDUMP_FILE NL);
	}
#endif
#ifdef UARTMON_SOCKET
	uartmon_close();
#endif
	DEBUG("Execution has been stopped at PC=$%04X" NL, cpu_pc);
}



static void reset_mega65 ( void )
{
	force_fast = 0;	// FIXME: other default speed controls on reset?
	c128_d030_reg = 0xFF;
	machine_set_speed(0);
	memory_set_cpu_io_port_ddr_and_data(0xFF, 0xFF);
	map_mask = 0;
	in_hypervisor = 0;
	vic_registers[0x30] = 0;	// FIXME: hack! we need this, and memory_set_vic3_rom_mapping above too :(
	memory_set_vic3_rom_mapping(0);
	memory_set_do_map();
	cpu_reset();
	dma_reset();
	nmi_level = 0;
	D6XX_registers[0x7E] = emucfg_get_num("kicked");
	hypervisor_enter(TRAP_RESET);
	DEBUG("RESET!" NL);
}



// Called by emutools_hid!!! to handle special private keys assigned to this emulator
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	// Check for special, emulator-related hot-keys (not C65 key)
	if (pressed) {
		if (key == SDL_SCANCODE_F10)
			reset_mega65();
		else if (key == SDL_SCANCODE_KP_ENTER) {
			c64_toggle_joy_emu();
			OSD(-1, -1, "Joystick emulation on port #%d", joystick_emu);
		} else if (key == SDL_SCANCODE_ESCAPE)
			set_mouse_grab(SDL_FALSE);
	} else
		if (pos == -2 && key == 0) {	// special case pos = -2, key = 0, handled = mouse button (which?) and release event!
			if (handled == SDL_BUTTON_LEFT) {
				OSD(-1, -1, "Mouse grab activated.\nPress ESC to cancel.");
				set_mouse_grab(SDL_TRUE);
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
	vic_render_screen();
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
	if (brk < 0)
		cpu_cycles_per_step = cpu_cycles_per_scanline;
	else
		cpu_cycles_per_step = 0;
}



int main ( int argc, char **argv )
{
	int cycles, frameskip;
	sysconsole_open();
	xemu_dump_version(stdout, "The Incomplete Commodore-65/Mega-65 emulator from LGB");
	emucfg_define_str_option("8", NULL, "Path of EXTERNAL D81 disk image (not on/the SD-image)");
	emucfg_define_num_option("dmarev", 0, "Revision of the DMAgic chip  (0=F018A, other=F018B)");
	emucfg_define_num_option("fastclock", MEGA65_DEFAULT_FAST_CLOCK, "Clock of M65 fast mode (in MHz)");
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
	osd_init_with_defaults();
	// Initialize Mega65
	mega65_init(
		SID_CYCLES_PER_SEC,		// SID cycles per sec
		AUDIO_SAMPLE_FREQ		// sound mix freq
	);
	// Start!!
	skip_unhandled_mem = emucfg_get_bool("skipunhandledmem");
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
		while (unlikely(paused)) {	// paused special mode, ie tracing support, or something ...
			if (unlikely(dma_status))
				break;		// if DMA is pending, do not allow monitor/etc features
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
				if (paused) {
					DEBUGPRINT("TRACE: entering into trace mode @ $%04X" NL, cpu_pc);
					cpu_cycles_per_step = 0;
				} else {
					DEBUGPRINT("TRACE: leaving trace mode @ $%04X" NL, cpu_pc);
					if (breakpoint_pc < 0)
						cpu_cycles_per_step = cpu_cycles_per_scanline;
					else
						cpu_cycles_per_step = 0;
				}
			}
		}
		if (unlikely(in_hypervisor)) {
			hypervisor_debug();
		}
		if (unlikely(breakpoint_pc == cpu_pc)) {
			DEBUGPRINT("TRACE: Breakpoint @ $%04X hit, Xemu moves to trace mode after the execution of this opcode." NL, cpu_pc);
			paused = 1;
		}
		cycles += unlikely(dma_status) ? dma_update_multi_steps(cpu_cycles_per_scanline) : cpu_step(
#ifdef CPU_STEP_MULTI_OPS
			cpu_cycles_per_step
#endif
		);	// FIXME: this is maybe not correct, that DMA's speed depends on the fast/slow clock as well?
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

#define SNAPSHOT_M65_BLOCK_VERSION	2
#define SNAPSHOT_M65_BLOCK_SIZE		(0x100 + sizeof(D6XX_registers))

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
	in_hypervisor = 1;	// simulate hypervisor mode, to allow to write some regs now instead of causing a TRAP now ...
	io_write(0x367D, D6XX_registers[0x7D]);			// write $(D)67D in VIC-IV I/O mode! (sets ROM protection, linear addressing mode enable ...)
	// TODO FIXME: see if there is a need for other registers from D6XX_registers to write back to take effect on loading snapshot!
	// end of spec, hypervisor-needed faked mode for loading snapshot ...
	map_mask = (int)P_AS_BE32(buffer + 0);
	map_offset_low = (int)P_AS_BE32(buffer + 4);
	map_offset_high = (int)P_AS_BE32(buffer + 8);
	cpu_inhibit_interrupts = (int)P_AS_BE32(buffer + 12);
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
	U32_AS_BE(buffer + 12, cpu_inhibit_interrupts);
	U32_AS_BE(buffer + 16, in_hypervisor);
	U32_AS_BE(buffer + 20, map_megabyte_low);
	U32_AS_BE(buffer + 24, map_megabyte_high);
	U32_AS_BE(buffer + 28, force_fast);	// see notes on this at load_state and finalize stuff!
	// +32 is free for 4 bytes now ... can be used later
	buffer[36] = memory_get_cpu_io_port(0);
	buffer[37] = memory_get_cpu_io_port(1);
	memcpy(buffer + 0x100, D6XX_registers, sizeof D6XX_registers);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}


int m65emu_snapshot_loading_finalize ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	printf("SNAP: loaded (finalize-callback: begin)" NL);
	memory_set_vic3_rom_mapping(vic_registers[0x30]);
	memory_set_do_map();
	force_fast = force_fast_loaded;	// force_fast is handled through different places, so we must have a "finalize" construct and saved separately to have the actual effect ...
	machine_set_speed(1);
	printf("SNAP: loaded (finalize-callback: end)" NL);
	OSD(-1, -1, "Snapshot has been loaded.");
	return 0;
}
#endif
