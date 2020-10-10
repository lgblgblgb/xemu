/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015-2017,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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


#include "xep128.h"
#include "main.h"
#include "dave.h"
#include "nick.h"
#include "configuration.h"
#include "sdext.h"
#include "exdos_wd.h"
#include "roms.h"
#include "screen.h"
#include "input.h"
#include "cpu.h"
#include "primoemu.h"
#include "emu_rom_interface.h"
#include "epnet.h"
#include "zxemu.h"
#include "printer.h"
#include "joystick.h"
#include "console.h"
#include "emu_monitor.h"
#include "rtc.h"
#include "fileio.h"
#include "xemu/z80.h"
#include "gui.h"
#include "snapshot.h"

#include <string.h>
#include <stdlib.h>
#include <SDL.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>


static Uint32 *ep_pixels;
static const int _cpu_speeds[4] = { 4000000, 6000000, 7120000, 10000000 };
static int _cpu_speed_index = 0;
static int guarded_exit = 0;
static unsigned int ticks;
int paused = 0;
static int cpu_cycles_for_dave_sync = 0;
static int td_balancer;
static Uint64 et_start, et_end;
static int td_em_ALL = 0, td_pc_ALL = 0, td_count_ALL = 0;
static double balancer;
static double SCALER;
static int sram_ready = 0;
time_t unix_time;

int chatty_xemu = 1;	// needed by the ugly mix of old Xep128 solutions and newer Xemu headers :-O


/* Ugly indeed, but it seems some architecture/OS does not support "standard"
   aligned allocations or give strange error codes ... Note: this one only
   works, if you don't want to free() the result pointer!! */
void *alloc_xep_aligned_mem ( size_t size )
{
	// it seems _mm_malloc() is quite standard at least on gcc, mingw, clang ... so let's try to use it
#if defined(__EMSCRIPTEN__) || defined(__arm__)
	return SDL_malloc(size);
#else
	void *p = _mm_malloc(size, __BIGGEST_ALIGNMENT__);
	DEBUG("ALIGNED-ALLOC: base_pointer=%p size=%d alignment=%d" NL, p, (int)size, __BIGGEST_ALIGNMENT__);
	return p;
#endif
}



void shutdown_sdl(void)
{
	if (guarded_exit) {
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
	if (sdl_win) {
#ifdef __EMSCRIPTEN__
		// This is used, because window title would remain as Emu would run after exit, which is not the case ...
		SDL_SetWindowTitle(sdl_win, WINDOW_TITLE " v" VERSION " - EXITED");
#endif
		SDL_DestroyWindow(sdl_win);
	}
	console_close_window_on_exit();
	/* last stuff! */
	if (debug_fp) {
		DEBUGPRINT("Closing debug messages log file on exit." NL);
		fclose(debug_fp);
		debug_fp = NULL;
	}
	SDL_Quit();
}



static int get_elapsed_time ( Uint64 t_old, Uint64 *t_new, time_t *store_unix_time )
{
#ifdef XEMU_OLD_TIMING
#define __TIMING_METHOD_DESC "gettimeofday"
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (store_unix_time)
		*store_unix_time = tv.tv_sec;
	*t_new = tv.tv_sec * 1000000UL + tv.tv_usec;
	return *t_new - t_old;
#else
#define __TIMING_METHOD_DESC "SDL_GetPerformanceCounter"
	if (store_unix_time)
		*store_unix_time = time(NULL);
	*t_new = SDL_GetPerformanceCounter();
	return 1000000 * (*t_new - t_old) / SDL_GetPerformanceFrequency();
#endif
}



static inline void emu_sleep ( int td )
{
	if (td <= 0)
		return;
#ifdef __EMSCRIPTEN__
#define __SLEEP_METHOD_DESC "emscripten_set_main_loop_timing"
	// If too short period of sleep (not enough for 1ms), give some time for browser to run
	// to avoid the "stop the script" warning or so ...
	// For Js, it's not really a sleep what name would mean for function (emu_sleep) but
	// rather then a setTimeout value for the handler
	emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, td > 999 ? td / 1000 : 1);
#elif defined(XEMU_SLEEP_IS_SDL_DELAY)
#define __SLEEP_METHOD_DESC "SDL_Delay"
	SDL_Delay(td / 1000);
#elif defined(XEMU_SLEEP_IS_USLEEP)
#define __SLEEP_METHOD_DESC "usleep"
	usleep(td);
#else
#define __SLEEP_METHOD_DESC "nanosleep"
	struct timespec req, rem;
	td *= 1000;
	req.tv_sec  = td / 1000000000UL;
	req.tv_nsec = td % 1000000000UL;
	for (;;) {
		if (nanosleep(&req, &rem)) {
			if (errno == EINTR) {
				req.tv_sec = rem.tv_sec;
				req.tv_nsec = rem.tv_nsec;
			} else {
				ERROR_WINDOW("Nanosleep() returned with unhandlable error");
				return;
			}
		} else
			return;
	}
#endif
}



static void emu_timekeeping_check ( void )
{
	// check how much time we slept, initiated by last call of emu_timekeeping_delay()
	// we also store current UT in "unix_time" to be used by emulator (ie, RTC emulation)
	int td = get_elapsed_time(et_end, &et_start, &unix_time);
	if (td >= 0)			// td should be greater than zero or sleep was about for _minus_ time? eh, give me that time machine, dude! :)
		td_balancer -= td;	// time-difference balancer, decrease with time slept
	else
		DEBUG("TIMING: negative amount of time spent for sleeping?!" NL);
	rtc_update_trigger = 1;
}




/* This is the emulation timing stuff
 * Should be called at the END of the emulation loop.
 * Input parameter: microseconds needed for the "real" (emulated) computer to do our loop 
 * This function also does the sleep itself */
static void emu_timekeeping_delay ( int td_em )
{
	int td, td_pc = get_elapsed_time(et_start, &et_end, NULL);	// the time was needed for our emulation loop
	if (td_pc < 0) {
		DEBUG("TIMING: negative amount of time spent for an emulation loop?!" NL);
		td = 0;
	} else
		td = td_em - td_pc; // the time difference (+X = PC is faster - real time EP emulation, -X = EP is faster - real time EP emulation is not possible)
	DEBUG("DELAY: pc=%d em=%d sleep=%d" NL, td_pc, td_em, td);
	/* for reporting only: BEGIN */
	td_em_ALL += td_em;
	td_pc_ALL += td_pc;
	if (td_count_ALL == 50) {
		char buf[256];
		//DEBUG("STAT: count = %d, EM = %d, PC = %d, usage = %f%" NL, td_count_ALL, td_em_ALL, td_pc_ALL, 100.0 * (double)td_pc_ALL / (double)td_em_ALL);
		snprintf(buf, sizeof buf, "%s [%.2fMHz ~ %d%%]%s", WINDOW_TITLE " v" VERSION " ",
			CPU_CLOCK / 1000000.0,
			td_em_ALL ? (td_pc_ALL * 100 / td_em_ALL) : -1,
			paused ? " PAUSED" : ""
		);
		SDL_SetWindowTitle(sdl_win, buf);
		td_count_ALL = 0;
		td_pc_ALL = 0;
		td_em_ALL = 0;
	} else
		td_count_ALL++;
	/* for reporting only: END */
	td_balancer += td;
	/* insane time-diff balancer values ... */
	if (td_balancer >  1000000 || td_balancer < -1000000)
		td_balancer = 0;
	DEBUG("Balancer = %d" NL, td_balancer);
	// Should be the last, as with Emscripten, it's not a real sleep, but the settimeout JS stuff ...
	emu_sleep(td_balancer);
}




/* Should be started on each time, emulation is started/resumed (ie after any delay in emulation like pause, etc)
 * You DO NOT need this during the active emulation loop! */
void emu_timekeeping_start ( void )
{
	(void)get_elapsed_time(0, &et_start, &unix_time);
	et_end = et_start;
	td_balancer = 0;
	rtc_update_trigger = 1;
}



int set_cpu_clock ( int hz )
{
	if (hz <  1000000) hz =  1000000;
	if (hz > 12000000) hz = 12000000;
	CPU_CLOCK = hz;
	SCALER = (double)NICK_SLOTS_PER_SEC / (double)CPU_CLOCK;
	DEBUG("CPU: clock = %d scaler = %f" NL, CPU_CLOCK, SCALER);
	dave_set_clock();
	return hz;
}



int set_cpu_clock_with_osd ( int hz )
{
	hz = set_cpu_clock(hz);
	OSD("CPU speed: %.2f MHz", hz / 1000000.0);
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
	SDL_Event e;
	while (SDL_PollEvent(&e) != 0)
		switch (e.type) {
			case SDL_WINDOWEVENT:
				if (!is_fullscreen && e.window.event == SDL_WINDOWEVENT_RESIZED) {
					DEBUG("UI: Window is resized to %d x %d" NL,
						e.window.data1,
						e.window.data2
					);
					screen_window_resized(e.window.data1, e.window.data2);
				}
				break;
			case SDL_QUIT:
				if (QUESTION_WINDOW("?No|!Yes", "Are you sure to exit?") == 1)
					XEMUEXIT(0);
				return;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				if (e.key.repeat == 0 && (e.key.windowID == sdl_winid || e.key.windowID == 0)) {
					int code = emu_kbd(e.key.keysym, e.key.state == SDL_PRESSED);
					if (code == 0xF9)		// // OSD REPLAY, default key GRAVE
						osd_replay(e.key.state == SDL_PRESSED ? 0 : OSD_FADE_START);
					else if (code && e.key.state == SDL_PRESSED)
						switch(code) {
#ifndef __EMSCRIPTEN__
							case 0xFF:	// FULLSCREEN toogle, default key F11
								screen_set_fullscreen(!is_fullscreen);
								break;
							case 0xFE:	// EXIT, default key F9
								if (QUESTION_WINDOW("?No|!Yes", "Are you sure to exit?") == 1)
									XEMUEXIT(0);
								break;
							case 0xFD:	// SCREENSHOT, default key F10
								screen_shot(ep_pixels, current_directory, "screenshot-*.png");
								break;
#endif
							case 0xFC:	// RESET, default key PAUSE
								if (e.key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT)) {
									zxemu_on = 0;
									(void)ep_init_ram();
								}
								ep_reset();
								break;
							case 0xFB:	// DOWNGRADE CPU SPEED, default key PAGE DOWN
								if (_cpu_speed_index)
									set_cpu_clock_with_osd(_cpu_speeds[-- _cpu_speed_index]);
								break;
							case 0xFA:	// UPGRADE CPU SPEED, default key PAGE UP
								if (_cpu_speed_index < 3)
									set_cpu_clock_with_osd(_cpu_speeds[++ _cpu_speed_index]);
								break;
							case 0xF8:	// CONSOLE, key pad minus
								if (!console_is_open)
									console_open_window();
								break;
						}
				} else if (e.key.repeat == 0)
					DEBUG("UI: NOT HANDLED KEY EVENT: repeat = %d windowid = %d [our win = %d]" NL, e.key.repeat, e.key.windowID, sdl_winid);
				break;
			case SDL_MOUSEMOTION:
				if (e.motion.windowID == sdl_winid)
					emu_mouse_motion(e.motion.xrel, e.motion.yrel);
				break;
			case SDL_MOUSEWHEEL:
				if (e.wheel.windowID == sdl_winid)
					emu_mouse_wheel(
						e.wheel.x, e.wheel.y,
						e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
					);
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				if (e.button.windowID == sdl_winid)
					emu_mouse_button(e.button.button, e.button.state == SDL_PRESSED);
				break;
			default:
				joy_sdl_event(&e);
				break;
		}
	if (!frameskip)
		screen_present_frame(ep_pixels);	// this should be after the event handler, as eg screenshot function needs locked texture state if this feature is used at all
	xepgui_iteration();
	monitor_process_queued();
	emu_timekeeping_delay((1000000.0 * rasters * 57.0) / (double)NICK_SLOTS_PER_SEC);
}




static void xep128_emulation ( void )
{
	emu_timekeeping_check();
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





int main (int argc, char *argv[])
{
	const char *snapshot;
	atexit(shutdown_sdl);
	if (SDL_Init(
#ifdef __EMSCRIPTEN__
		// It seems there is an issue with emscripten SDL2: SDL_Init does not work if TIMER and/or HAPTIC is tried to be intialized or just "EVERYTHING" is used!!
		SDL_INIT_EVERYTHING & ~(SDL_INIT_TIMER | SDL_INIT_HAPTIC)
#else
		SDL_INIT_EVERYTHING
#endif
	) != 0) {
		ERROR_WINDOW("Fatal SDL initialization problem: %s", SDL_GetError());
		return 1;
	}
	if (config_init(argc, argv)) {
#ifdef __EMSCRIPTEN__
		ERROR_WINDOW("Error with config parsing. Please check the (javascript) console of your browser to learn about the error.");
#endif
		return 1;
	}
	guarded_exit = 1;	// turn on guarded exit, with custom de-init stuffs
	DEBUGPRINT("EMU: sleeping = \"%s\", timing = \"%s\"" NL,
		__SLEEP_METHOD_DESC, __TIMING_METHOD_DESC
	);
	fileio_init(
#ifdef __EMSCRIPTEN__
		"/",
#else
		app_pref_path,
#endif
	"files");
	if (screen_init())
		return 1;
	//if (xepgui_init(NULL))
	//	return 1;
	xepgui_init(NULL);	// allow to fail (do not exit if it fails). Some targets may not have X running
	audio_init(config_getopt_int("audio"));
	z80ex_init();
	set_ep_cpu(CPU_Z80);
	ep_pixels = nick_init();
	if (ep_pixels == NULL)
		return 1;
	snapshot = config_getopt_str("snapshot");
	if (strcmp(snapshot, "none")) {
		if (ep128snap_load(snapshot))
			snapshot = NULL;
	} else
		snapshot = NULL;
	if (!snapshot) {
		if (roms_load())
			return 1;
		primo_rom_seg = primo_search_rom();
		ep_set_ram_config(config_getopt_str("ram"));
	}
	mouse_setup(config_getopt_int("mousemode"));
	ep_reset();
	kbd_matrix_reset();
	joy_sdl_event(NULL); // this simply inits joy layer ...
#ifdef CONFIG_SDEXT_SUPPORT
	if (!snapshot)
		sdext_init();
#endif
#ifdef CONFIG_EXDOS_SUPPORT
	wd_exdos_reset();
	wd_attach_disk_image(config_getopt_str("wdimg"));
#endif
#ifdef CONFIG_EPNET_SUPPORT
	epnet_init(NULL);
#endif
	ticks = SDL_GetTicks();
	balancer = 0;
	set_cpu_clock(DEFAULT_CPU_CLOCK);
	emu_timekeeping_start();
	audio_start();
	if (config_getopt_int("fullscreen"))
		screen_set_fullscreen(1);
	DEBUGPRINT(NL "EMU: entering into main emulation loop" NL);
	sram_ready = 1;
	if (strcmp(config_getopt_str("primo"), "none") && !snapshot) {
		// TODO: da stuff ...
		primo_emulator_execute();
		OSD("Primo Emulator Mode");
	}
	if (snapshot)
		ep128snap_set_cpu_and_io();
	console_monitor_ready();	// OK to run monitor on console now!
#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop(xep128_emulation, 50, 1);
#else
	for (;;)
		xep128_emulation();
#endif
	printf("EXITING FROM main()?!" NL);
	return 0;
}
