/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2017,2020-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_config.h"
#include "xemu/emutools_gui.h"
#include "xemu/emutools_hid.h"
#include "xemu/z80.h"

#include "enterprise128.h"

#include "dave.h"
#include "nick.h"
#include "sdext.h"
#include "exdos_wd.h"
#include "roms.h"
#include "input_devices.h"
#include "cpu.h"
#include "primoemu.h"
#include "emu_rom_interface.h"
#include "epnet.h"
#include "zxemu.h"
#include "printer.h"
#include "emu_monitor.h"
#include "rtc.h"
#include "fileio.h"
#include "snapshot.h"

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>


int paused = 0;
int register_screenshot_request = 0;
static int cpu_cycles_for_dave_sync = 0;
static double balancer;
static double SCALER;
static int sram_ready = 0;
time_t unix_time;
static char emulator_speed_title[32] = "";




static void shutdown_callback(void)
{
	monitor_stop();
	sdext_shutdown();
	audio_close();
	printer_close();
#ifdef CONFIG_EPNET_SUPPORT
	epnet_uninit();
#endif
#ifdef CONFIG_EXDOS_SUPPORT
	wd_detach_disk_image();
#endif
	if (sram_ready)
		sram_save_all_segments();
	DEBUGPRINT("Shutdown callback, return." NL);
}


void clear_emu_events ( void )
{
	//hid_reset_events(1);
	kbd_matrix_reset();	// also reset the keyboard matrix as it seems some keys can be detected "stucked" ...
	mouse_reset_button();	// ... and also the mouse buttons :)
}


int set_cpu_clock ( int hz )
{
	CPU_CLOCK = hz;
	SCALER = (double)NICK_SLOTS_PER_SEC / (double)CPU_CLOCK;
	DEBUG("CPU: clock = %d scaler = %f" NL, CPU_CLOCK, SCALER);
	dave_set_clock();
	sprintf(emulator_speed_title, "%.2fMHz", hz / 1000000.0);
	return hz;
}


int set_cpu_clock_with_osd ( int hz )
{
	hz = set_cpu_clock(hz);
	OSD(-1, -1, "CPU speed: %.2f MHz", hz / 1000000.0);
	return hz;
}


// called by nick.c
static int emu_one_frame_rasters = -1;
static int emu_one_frame_frameskip = 0;

void emu_one_frame(int rasters, int frameskip)
{
	emu_one_frame_rasters = rasters;
	emu_one_frame_frameskip = frameskip;
}


static void __emu_one_frame(int rasters, int frameskip)
{
	hid_handle_all_sdl_events();
	if (!frameskip) {
#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
		if (XEMU_UNLIKELY(register_screenshot_request)) {
			register_screenshot_request = 0;
			screenshot();
		}
#endif
		//screen_present_frame(ep_pixels);	// this should be after the event handler, as eg screenshot function needs locked texture state if this feature is used at all
		xemu_update_screen();
	}
	xemugui_iteration();
	monitor_process_queued();
	xemu_timekeeping_delay((1000000.0 * rasters * 57.0) / (double)NICK_SLOTS_PER_SEC);
}


static void xep128_emulation ( void )
{
	//emu_timekeeping_check();
	rtc_update_trigger = 1;
	for (;;) {
		int t;
		if (XEMU_UNLIKELY(paused && !z80ex.prefix)) {
			/* Paused is non-zero for special cases, like pausing emulator :) or single-step execution mode
                           We only do this if z80ex.prefix is non-zero, ie not in the "middle" of a prefixed Z80 opcode or so ... */
			__emu_one_frame(312, 0); // keep UI stuffs (and some timing) intact ... with a faked about 312 scanline (normal frame) timing needed ...
			return;
		}
		if (XEMU_UNLIKELY(nmi_pending)) {
			t = z80ex_nmi();
			DEBUG("NMI: %d" NL, t);
			if (t)
				nmi_pending = 0;
		} else
			t = 0;
		//if (XEMU_UNLIKELY((dave_int_read & 0xAA) && t == 0)) {
		if ((dave_int_read & 0xAA) && t == 0) {
			t = z80ex_int();
			if (t)
				DEBUG("CPU: int and accepted = %d" NL, t);
		} else
			t = 0;
		if (XEMU_LIKELY(!t))
			t = z80ex_step();
		cpu_cycles_for_dave_sync += t;
		//DEBUG("DAVE: SYNC: CPU cycles = %d, Dave sync val = %d, limit = %d" NL, t, cpu_cycles_for_dave_sync, cpu_cycles_per_dave_tick);
		while (cpu_cycles_for_dave_sync >= cpu_cycles_per_dave_tick) {
			dave_tick();
			cpu_cycles_for_dave_sync -= cpu_cycles_per_dave_tick;
		}
		balancer += t * SCALER;
		//DEBUG("%s [balance=%f t=%d]" NL, buffer, balancer, t);
		while (balancer >= 0.5) {
			nick_render_slot();
			balancer -= 1.0;
			if (XEMU_UNLIKELY(emu_one_frame_rasters != -1)) {
				__emu_one_frame(
					emu_one_frame_rasters,
					emu_one_frame_frameskip
				);
				emu_one_frame_rasters = -1;
				return;
			}
		}
		//DEBUG("[balance=%f t=%d]" NL, balancer, t);
	}
}


static const char *rom_parse_opt_cb ( struct xemutools_config_st *unused, const char *optname, const char *optvalue )
{
	return rom_parse_opt(optname, optvalue);
}



int main (int argc, char *argv[])
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Enterprise-128 \"old XEP128 within the Xemu project now\" emulator from LGB");
	xemucfg_define_switch_option("audio", "Enable (buggy) audio output");
	xemucfg_define_switch_option("syscon", "Keep console window open (Windows-specific)");
	//{ DEBUGFILE_OPT,CONFITEM_STR,	"none",		0, "Enable debug messages written to a specified file" },
	xemucfg_define_str_option("ddn", NULL, "Default device name (none = not to set)");
	xemucfg_define_float_option("clock", (double)DEFAULT_CPU_CLOCK, "Z80 clock in MHz");
	xemucfg_define_str_option("filedir", "@files", "Default directory for FILE: device");
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	xemucfg_define_num_option("mousemode",	1, "Set mouse mode, 1-3 = J-column 2,4,8 bytes and 4-6 the same for K-column");
	xemucfg_define_switch_option("primo", "Start in Primo emulator mode");
	xemucfg_define_str_option("printfile", PRINT_OUT_FN, "Printing into this file");
	xemucfg_define_str_option("ram", "128", "RAM size in Kbytes (decimal) or segment specification(s) prefixed with @ in hex (VRAM is always assumed), like: @C0-CF,E0,E3-E7");
	xemucfg_define_proc_option("rom", rom_parse_opt_cb, "ROM image, format is \"rom@xx=filename\" (xx=start segment in hex), use rom@00 for EXOS or combined ROM set");
	xemucfg_define_str_option("sdimg", SDCARD_IMG_FN, "SD-card disk image (VHD) file name/path");
	xemucfg_define_str_option("sdl", NULL, "Sets SDL specific option(s) including rendering related stuffs");
	xemucfg_define_switch_option("skiplogo", "Disables Enterprise logo on start-up via XEP ROM");
	xemucfg_define_str_option("snapshot", NULL, "Load and use ep128emu snapshot");
	xemucfg_define_str_option("wdimg", NULL, "EXDOS WD disk image file name/path");
	xemucfg_define_switch_option("noxeprom", "Disables XEP internal ROM");
	//{ "epkey",	CONFITEM_STR,	NULL,		1, "Define a given EP/emu key, format epkey@xy=SDLname, where x/y are row/col in hex or spec code (ie screenshot, etc)." },
#ifdef SDL_HINT_RENDER_SCALE_QUALITY
	xemucfg_define_num_option("sdlrenderquality", RENDER_SCALE_QUALITY, "Setting SDL hint for scaling method/quality on rendering (0, 1, 2)");
#endif
	xemucfg_define_switch_option("besure", "Skip asking \"are you sure?\" on RESET or EXIT");
	xemucfg_define_switch_option("monitor", "Start monitor on console");
	xemucfg_define_str_option("gui", NULL, "Select GUI type for usage. Specify some insane str to get a list");
	if (xemucfg_parse_all(argc, argv))
		return 1;
	i_am_sure_override = xemucfg_get_bool("besure");
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
#ifdef SDL_HINT_RENDER_SCALE_QUALITY
		xemucfg_get_ranged_num("sdlrenderquality", 0, 2),	// render scaling quality
#else
		RENDER_SCALE_QUALITY,		// render scaling quality
#endif
		USE_LOCKED_TEXTURE,		// 1 = locked texture access
		shutdown_callback		// registered shutdown function
	))
		return 1;
	xemugui_init(xemucfg_get_str("gui"));	// allow to fail (do not exit if it fails). Some targets may not have X running
	hid_init(
		ep128_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// joystick events
	);
	osd_init_with_defaults();
	fileio_init(
#ifdef XEMU_ARCH_HTML
		"/",
#else
		sdl_pref_dir,
#endif
	"files");
	audio_init(xemucfg_get_bool("audio"));
	z80ex_init();
	set_ep_cpu(CPU_Z80);
	if (nick_init())
		return 1;
	const char *snapshot = xemucfg_get_str("snapshot");
	if (snapshot) {
		if (ep128snap_load(snapshot))
			snapshot = NULL;
	} // else
	if (!snapshot) {
		if (roms_load())
			return 1;
		primo_rom_seg = primo_search_rom();
		ep_set_ram_config(xemucfg_get_str("ram"));
	}
	mouse_setup(xemucfg_get_num("mousemode"));
	ep_reset();
	clear_emu_events();
#ifdef CONFIG_SDEXT_SUPPORT
	if (!snapshot)
		sdext_init(xemucfg_get_str("sdimg"));
#endif
#ifdef CONFIG_EXDOS_SUPPORT
	wd_exdos_reset();
	wd_attach_disk_image(xemucfg_get_str("wdimg"));
#endif
#ifdef CONFIG_EPNET_SUPPORT
	epnet_init(NULL);
#endif
	balancer = 0;
	set_cpu_clock((int)(xemucfg_get_ranged_float("clock", 1.0, 12.0) * 1000000.0));
	audio_start();
	xemu_set_full_screen(xemucfg_get_bool("fullscreen"));
	sram_ready = 1;
	if (xemucfg_get_bool("primo") && !snapshot) {
		if (primo_rom_seg != -1) {
			primo_emulator_execute();
			OSD(-1, -1, "Primo Emulator Mode");
		} else
			ERROR_WINDOW("Primo mode was requested, but PRIMO ROM was not loaded.\nRefusing Primo mode");
	}
	if (snapshot)
		ep128snap_set_cpu_and_io();
	if (!xemucfg_get_bool("syscon") && !xemucfg_get_bool("monitor"))
		sysconsole_close(NULL);
	if (xemucfg_get_bool("monitor"))
		monitor_start();
	clear_emu_events();
	xemu_timekeeping_start();
	DEBUGPRINT(NL "EMU: entering into main emulation loop" NL);
	// emscripten_set_main_loop(xep128_emulation, 50, 1);
	XEMU_MAIN_LOOP(xep128_emulation, 50, 1);
	return 0;
}
