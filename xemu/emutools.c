/* Xemu - emulation (running on Linux/Unix/Windows/OSX, utilizing SDL2) of some
   8 bit machines, including the Commodore LCD and Commodore 65 and MEGA65 as well.
   Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#define DEFINE_XEMU_OS_READDIR
#include "xemu/emutools.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#ifdef XEMU_ARCH_UNIX
#	include <signal.h>
#	include <sys/utsname.h>
#endif

#ifdef XEMU_MISSING_BIGGEST_ALIGNMENT_WORKAROUND
#	warning "System did not define __BIGGEST_ALIGNMENT__ Xemu assumes some default value."
#endif
#ifdef XEMU_OVERSIZED_BIGGEST_ALIGNMENT_WORKAROUND
#	warning "System deifned __BIGGEST_ALIGNMENT__ just too big Xemu will use a smaller default."
#endif
#ifdef XEMU_CPU_ARM
#	warning "Compiling for ARM (including Apple Silicon as well) CPU. Some features of Xemu won't be available because of some limitations of usual OSes on this ISA."
#endif

#ifdef XEMU_ARCH_WIN
#	include <windows.h>
#	include <stdio.h>
#	include <io.h>
#	include <fcntl.h>
#	include <math.h>
#endif

const char EMPTY_STR[] = "";
const int ZERO_INT = 0;
const int ONE_INT = 1;

#ifndef XEMU_NO_SDL_DIALOG_OVERRIDE
int (*SDL_ShowSimpleMessageBox_custom)(Uint32, const char*, const char*, SDL_Window* ) = SDL_ShowSimpleMessageBox;
int (*SDL_ShowMessageBox_custom)(const SDL_MessageBoxData*, int* ) = SDL_ShowMessageBox;
#endif

#ifdef XEMU_ARCH_MAC
int macos_gui_started = 0;
#endif

#ifdef XEMU_ARCH_WIN
static int atexit_callback_for_console_registered = 0;
#endif

int i_am_sure_override = 0;
const char *str_are_you_sure_to_exit = "Are you sure you want to exit Xemu?";

char **xemu_initial_argv = NULL;
int    xemu_initial_argc = -1;
int emu_exit_code = 0;
char *xemu_extra_env_var_setup_str = NULL;
Uint64 buildinfo_cdate_uts = 0;
const char *xemu_initial_cwd = NULL;
SDL_Window   *sdl_win = NULL;
static SDL_Renderer *sdl_ren = NULL;
static SDL_Texture  *sdl_tex = NULL;
SDL_PixelFormat *sdl_pix_fmt;
int sdl_on_x11 = 0, sdl_on_wayland = 0;
static Uint32 sdl_pixel_format_id;
static const char default_window_title[] = "XEMU";
int register_new_texture_creation = 0;
char *xemu_app_org = NULL, *xemu_app_name = NULL;
#ifdef XEMU_ARCH_HTML
static const char *emscripten_sdl_base_dir = EMSCRIPTEN_SDL_BASE_DIR;
#endif
char *sdl_window_title = (char*)default_window_title;
char *window_title_custom_addon = NULL;
char *window_title_info_addon = NULL;
Uint32 *sdl_pixel_buffer = NULL;
Uint32 *xemu_frame_pixel_access_p = NULL;
int texture_x_size_in_bytes;
int emu_is_fullscreen = 0;
int emu_is_headless = 0;
int emu_is_sleepless = 0;
int dialogs_allowed = 1;
#ifdef XEMU_ARCH_WIN
int emu_fs_is_utf8 = 0;	// assuming non-UTF-8 capable filesystem for Windows hosts, I'll test it later in xemu_pre_init() if it's really the case
#else
int emu_fs_is_utf8 = 1;	// assuming UTF-8 capable filesystem for any non-Windows hosts
#endif
const char *emu_wine_version = NULL;
static int win_xsize, win_ysize;
char *sdl_pref_dir = NULL, *sdl_base_dir = NULL, *sdl_inst_dir = NULL;
Uint32 sdl_winid;
static Uint32 black_colour;
static void (*shutdown_user_function)(void) = NULL;
int seconds_timer_trigger;
SDL_version sdlver_compiled, sdlver_linked;
static char *window_title_buffer, *window_title_buffer_end;
static struct timeval unix_time_tv;
static Uint64 et_old;
static int td_balancer, td_em_ALL, td_pc_ALL;
static Uint64 td_stat_counter = 0, td_stat_sum = 0;
static int td_stat_min = INT_MAX, td_stat_max = INT_MIN;
int sysconsole_is_open = 0;
FILE *debug_fp = NULL;
int chatty_xemu = 1;
int sdl_default_win_x_size;
int sdl_default_win_y_size;
int sdl_default_win_x_pos = SDL_WINDOWPOS_UNDEFINED;
int sdl_default_win_y_pos = SDL_WINDOWPOS_UNDEFINED;
static SDL_Rect sdl_whole_screen;
static SDL_Rect sdl_viewport, *sdl_viewport_ptr = NULL;
static unsigned int sdl_texture_x_size, sdl_texture_y_size;

static SDL_bool grabbed_mouse = SDL_FALSE, grabbed_mouse_saved = SDL_FALSE;
int allow_mouse_grab = 1;
static int sdl_viewport_changed;
static int follow_win_size = 0;

#if !SDL_VERSION_ATLEAST(2, 0, 4)
#error "At least SDL version 2.0.4 is needed!"
#endif

#ifdef	XEMU_VGA_FONT_8X8
#define	CHARACTER_SET_DEFINER_8X8	const Uint8 vga_font_8x8[256 *  8]
#endif
#ifdef	XEMU_VGA_FONT_8X14
#define	CHARACTER_SET_DEFINER_8X14	const Uint8 vga_font_8x14[256 * 14]
#endif
#ifdef	XEMU_VGA_FONT_8X16
#define	CHARACTER_SET_DEFINER_8X16	const Uint8 vga_font_8x16[256 * 16]
#endif
#define ALLOW_INCLUDE_VGAFONTS
#include "xemu/vgafonts.c"
#undef ALLOW_INCLUDE_VGAFONTS
#undef	CHARACTER_SET_DEFINER_8X8
#undef	CHARACTER_SET_DEFINER_8X14
#undef	CHARACTER_SET_DEFINER_8X16

#ifdef	XEMU_OSD_SUPPORT
#include "xemu/gui/osd.c"
#endif


int set_mouse_grab ( SDL_bool state, int force_allow )
{
	if (state && (!allow_mouse_grab || force_allow))
		return 0;
	else if (state != grabbed_mouse) {
		grabbed_mouse = state;
		SDL_SetRelativeMouseMode(state);
		SDL_SetWindowGrab(sdl_win, state);
		return 1;
	} else
		return 0;
}


SDL_bool is_mouse_grab ( void )
{
	return grabbed_mouse;
}


void save_mouse_grab ( void )
{
	grabbed_mouse_saved = grabbed_mouse;
	set_mouse_grab(SDL_FALSE, 1);
}


void restore_mouse_grab ( void )
{
	set_mouse_grab(grabbed_mouse_saved, 1);
}


static inline int get_elapsed_time ( Uint64 t_old, Uint64 *t_new, struct timeval *store_time )
{
#ifdef XEMU_OLD_TIMING
#define __TIMING_METHOD_DESC "gettimeofday"
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (store_time) {
		store_time->tv_sec = tv.tv_sec;
		store_time->tv_usec = tv.tv_usec;
	}
	*t_new = tv.tv_sec * 1000000UL + tv.tv_usec;
	return *t_new - t_old;
#else
#define __TIMING_METHOD_DESC "SDL_GetPerformanceCounter"
	if (store_time)
		gettimeofday(store_time, NULL);
	*t_new = SDL_GetPerformanceCounter();
	return 1000000UL * (*t_new - t_old) / SDL_GetPerformanceFrequency();
#endif
}



struct tm *xemu_get_localtime ( void )
{
#ifdef XEMU_ARCH_WIN64
	// Fix a potentional issue with Windows 64 bit ...
	const time_t temp = unix_time_tv.tv_sec;
	return localtime(&temp);
#else
	return localtime(&unix_time_tv.tv_sec);
#endif
}


time_t xemu_get_unixtime ( void )
{
	return unix_time_tv.tv_sec;
}


unsigned int xemu_get_microseconds ( void )
{
	return unix_time_tv.tv_usec;
}


Uint8 xemu_hour_to_bcd12h ( Uint8 hours, int hour_offset )
{
	// hour offset is only an ugly hack to shift hour for testing! The value can be negative too, and over/under-flow of the 0-23 hours range
	hours = abs((int)hours + hour_offset) % 24;
	if (hours == 0)
		return 0x12;                                // 00:mm -> 12:mmAM for HH = 0      (0x12 is BCD representation of 12)
	else if (hours < 12)
		return XEMU_BYTE_TO_BCD(hours);             // HH:mm -> HH:mmAM for 0 < HH < 12
	else if (hours == 12)
		return 0x12 + 0x80;                         // 12:mm -> 12:mmPM for HH = 12     (0x80 is PM flag, 0x12 is BCD representation of 12)
	else
		return XEMU_BYTE_TO_BCD(hours - 12) + 0x80; // HH:mm -> HH:mmPM for HH > 12     (0x80 is PM flag)
}


// It seems, SDL functions returning an allocated memory range, must be free'd with their SDL_free by the caller.
// However, I cannot be sure that _I know_ what was the original source of an allocation later, and how to
// free it. So though this seems to overkill, I need to introduce a native allocation area, what I can free with
// simple free(). This seems to be a fatal problem at least on Windows, even causing a crash!
void *xemu_sdl_to_native_allocation ( void *sdl_allocation, const size_t length )
{
	if (!sdl_allocation)
		return NULL;
	void *buffer = xemu_malloc(length);
	memcpy(buffer, sdl_allocation, length);
	SDL_free(sdl_allocation);	// free the original SDL allocation now!
	return buffer;
}


char *xemu_sdl_to_native_string_allocation ( char *sdl_string )
{
	return sdl_string ? xemu_sdl_to_native_allocation(sdl_string, strlen(sdl_string) + 1) : NULL;
}


void *xemu_malloc ( size_t size )
{
	void *p = malloc(size);
	if (XEMU_UNLIKELY(!p))
		FATAL("Cannot allocate %d bytes of memory.", (int)size);
	return p;
}


void *xemu_realloc ( void *p, size_t size )
{
	p = realloc(p, size);
	if (XEMU_UNLIKELY(!p))
		FATAL("Cannot re-allocate %d bytes of memory.", (int)size);
	return p;
}


void *_xemu_malloc_ALIGNED_emulated ( size_t size )
{
	void *p = xemu_malloc(size + __BIGGEST_ALIGNMENT__);
	unsigned int reminder = (unsigned int)((uintptr_t)p % (uintptr_t)__BIGGEST_ALIGNMENT__);
	DEBUG("ALIGNED-ALLOC: using malloc(): base_pointer=%p size=%d need_alignment_of=%d reminder=%d" NL, p, (int)size, __BIGGEST_ALIGNMENT__, reminder);
	if (reminder) {
		void *p_old = p;
		p += __BIGGEST_ALIGNMENT__ - reminder;
		DEBUGPRINT("ALIGNED-ALLOC: malloc() alignment-workaround: correcting alignment %p -> %p (reminder: %u, need_alignment_of: %u)" NL, p_old, p, reminder, __BIGGEST_ALIGNMENT__);
	} else
		DEBUG("ALIGNED-ALLOC: malloc() alignment-workaround: alignment was OK already" NL);
	return p;
}
#ifdef HAVE_MM_MALLOC
#ifdef XEMU_ARCH_WIN
extern void *_mm_malloc ( size_t size, size_t alignment );	// it seems mingw/win has issue not to define this properly ... FIXME? Ugly windows, always the problems ...
#endif
void *xemu_malloc_ALIGNED ( size_t size )
{
	// it seems _mm_malloc() is quite standard at least on gcc, mingw, clang ... so let's try to use it
	// unfortunately even the C11 standard (!) for this does not seem to work on Windows neither on Mac :(
	void *p = _mm_malloc(size, __BIGGEST_ALIGNMENT__);
	DEBUG("ALIGNED-ALLOC: using _mm_malloc(): base_pointer=%p size=%d alignment=%d" NL, p, (int)size, __BIGGEST_ALIGNMENT__);
	if (p == NULL) {
		DEBUGPRINT("ALIGNED-ALLOC: _mm_malloc() failed, errno=%d[%s] ... defaulting to malloc()" NL, errno, strerror(errno));
		return _xemu_malloc_ALIGNED_emulated(size);
	}
	return p;
}
#elif !defined(XEMU_ARCH_HTML)
#warning "No _mm_malloc() for this architecture ..."
#endif


char *xemu_strdup ( const char *s )
{
	char *p = strdup(s);
	if (XEMU_UNLIKELY(!p))
		FATAL("Cannot allocate memory for strdup()");
	return p;
}

void xemu_restrdup ( char **ptr, const char *str )
{
	size_t len = strlen(str) + 1;
	*ptr = xemu_realloc(*ptr, len);
	memcpy(*ptr, str, len);
}

// Just drop queued SDL events ...
void xemu_drop_events ( void )
{
	SDL_PumpEvents();
	SDL_FlushEvent(SDL_KEYDOWN);
	SDL_FlushEvent(SDL_KEYUP);
	SDL_FlushEvent(SDL_MOUSEMOTION);
	SDL_FlushEvent(SDL_MOUSEWHEEL);
	SDL_FlushEvent(SDL_MOUSEBUTTONDOWN);
	SDL_FlushEvent(SDL_MOUSEBUTTONUP);
}


/* Meaning of "setting":
	-1 (or any negative integer): toggle, switch between fullscreen / windowed mode automatically depending on the previous state
	 0: set windowed mode (if it's not that already, then nothing will happen)
	 1 (or any positive integer): set full screen mode (if it's not that already, then nothing will happen)
*/
void xemu_set_full_screen ( int setting )
{
	if (setting > 1)
		setting = 1;
	if (setting < 0)
		setting = !emu_is_fullscreen;
	if (setting == emu_is_fullscreen)
		return; // do nothing, already that!
	if (setting) {
		// entering into full screen mode ....
		SDL_GetWindowSize(sdl_win, &win_xsize, &win_ysize); // save window size, it seems there are some problems with leaving fullscreen then
		if (SDL_SetWindowFullscreen(sdl_win, SDL_WINDOW_FULLSCREEN_DESKTOP)) {
			fprintf(stderr, "Cannot enter full screen mode: %s" NL, SDL_GetError());
		} else {
			emu_is_fullscreen = 1;
			DEBUGPRINT("UI: entering fullscreen mode." NL);
		}
	} else {
		// leaving full screen mode ...
		if (SDL_SetWindowFullscreen(sdl_win, 0)) {
			fprintf(stderr, "Cannot leave full screen mode: %s" NL, SDL_GetError());
		} else {
			emu_is_fullscreen = 0;
			DEBUGPRINT("UI: leaving fullscreen mode." NL);
			SDL_SetWindowSize(sdl_win, win_xsize, win_ysize); // restore window size saved on leaving fullscreen, there can be some bugs ...
		}
	}
	SDL_RaiseWindow(sdl_win); // I have some problems with EP128 emulator that window went to the background. Let's handle that with raising it anyway :)
}


// 0 = full screen, 1 = default window size, 2 = zoom 200%, 3 = zoom 300%, 4 = zoom 400%
void xemu_set_screen_mode ( int setting )
{
	if (setting <= 0) {
		xemu_set_full_screen(1);
	} else {
		xemu_set_full_screen(0);
		SDL_SetWindowSize(sdl_win, sdl_default_win_x_size * setting, sdl_default_win_y_size * setting);
	}
	SDL_RaiseWindow(sdl_win);
}


static inline void do_sleep ( int td )
{
	if (XEMU_UNLIKELY(emu_is_sleepless))
		return;
#ifdef XEMU_ARCH_HTML
#define __SLEEP_METHOD_DESC "emscripten_set_main_loop_timing"
	// Note: even if td is zero (or negative ...) give at least a little time for the browser
	// do not detect the our JS script as a run-away one, suggesting to kill ...
	// Note: this is not an actual sleep, we can't do that in JS. Instead of just "throttle"
	// the "frequency" our main loop is called. This also means, that do_sleep should be
	// called as last in case of emscripten target, since this does not sleep at all for real,
	// unlike the other sleep methods for non-js targets.
	emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, td > 999 ? td / 1000 : 1);
#elif defined(XEMU_SLEEP_IS_SDL_DELAY)
#define __SLEEP_METHOD_DESC "SDL_Delay"
	if (td > 0)
		SDL_Delay(td / 1000);
#elif defined(XEMU_SLEEP_IS_USLEEP)
#define __SLEEP_METHOD_DESC "usleep"
	if (td > 0)
		usleep(td);
#elif defined(XEMU_SLEEP_IS_NANOSLEEP)
#define __SLEEP_METHOD_DESC "nanosleep"
	struct timespec req, rem;
	if (td <= 0)
		return;
	td *= 1000;
	req.tv_sec  = td / 1000000000UL;
	req.tv_nsec = td % 1000000000UL;
	for (;;) {
		if (nanosleep(&req, &rem)) {
			if (errno == EINTR) {
				req.tv_sec = rem.tv_sec;
				req.tv_nsec = rem.tv_nsec;
			} else
				FATAL("nanosleep() returned with unhandable error");
		} else
			return;
	}
#else
#error "No SLEEP method is defined with XEMU_SLEEP_IS_* macros!"
#endif
}


/* Should be called regularly (eg on each screen global update), this function
   tries to keep the emulation speed near to real-time of the emulated machine.
   It's assumed that this function is called at least at every 0.1 sec or even
   more frequently ... Ideally it should be used at 25Hz rate (for PAL full TV
   frame) or 50Hz (for PAL half frame).
   Input: td_em: time in microseconds would be need on the REAL (emulated)
   machine to do the task, since the last call of this function! */
void xemu_timekeeping_delay ( int td_em )
{
	int td, td_pc;
	time_t old_unix_time = unix_time_tv.tv_sec;
	Uint64 et_new;
	td_pc = get_elapsed_time(et_old, &et_new, NULL);	// get realtime since last call in microseconds
	if (td_pc < 0) return; // time goes backwards? maybe time was modified on the host computer. Skip this delay cycle
	td = td_em - td_pc; // the time difference (+X = emu is faster (than emulated machine) - real time emulation, -X = emu is slower - real time emulation is not possible)
	td_balancer += td;
	if (td_balancer > 0)
		do_sleep(td_balancer);
	/* Purpose:
	 * get the real time spent sleeping (sleep is not an exact science on a multitask OS)
	 * also this will get the starter time for the next frame
	 */
	// calculate real time slept
	td = get_elapsed_time(et_new, &et_old, &unix_time_tv);
	seconds_timer_trigger = (unix_time_tv.tv_sec != old_unix_time);
	if (seconds_timer_trigger) {
#ifdef		WINDOW_TITLE_PRE_UPDATE_CALLBACK
		WINDOW_TITLE_PRE_UPDATE_CALLBACK();
#endif
		snprintf(window_title_buffer_end, 64, "  [%d%% %d%%] %s %s",
			((td_em_ALL < td_pc_ALL) && td_pc_ALL) ? td_em_ALL * 100 / td_pc_ALL : 100,
			td_em_ALL ? (td_pc_ALL * 100 / td_em_ALL) : -1,
			window_title_custom_addon ? window_title_custom_addon : "running",
			window_title_info_addon ? window_title_info_addon : ""
		);
		SDL_SetWindowTitle(sdl_win, window_title_buffer);
		td_pc_ALL = td_pc;
		td_em_ALL = td_em;
	} else {
		td_pc_ALL += td_pc;
		td_em_ALL += td_em;
	}
	// Some statistics
	if (td_em_ALL > 0 && td_pc_ALL > 0) {
		int stat = td_pc_ALL * 100 / td_em_ALL;
		td_stat_counter++;
		td_stat_sum += stat;
		if (stat > td_stat_max)
			td_stat_max = stat;
		if (stat < td_stat_min && stat)
			td_stat_min = stat;
	}
	// Check: invalid, sleep was about for _minus_ time? eh, give me that time machine, dude! :)
	if (td < 0)
		return;
	// Balancing real and wanted sleep time on long run
	// Insane big values are forgotten, maybe emulator was stopped, or something like that
	td_balancer -= td;
	if (td_balancer >  1000000)
		td_balancer = 0;
	else if (td_balancer < -1000000) {
		// reaching this means the anomaly above, OR simply the fact, that emulator is too slow to emulate in real time!
		td_balancer = 0;
	}
}


static void atexit_callback_for_console ( void )
{
	sysconsole_close("Please review the console content (if you need it) before exiting!");
}


const char *xemu_get_uname_string ( void )
{
#ifdef XEMU_ARCH_UNIX
	static const char *result = NULL;
	if (!result) {
		char buf[1024];
		struct utsname uts;
		uname(&uts);
		if (snprintf(buf, sizeof buf, "%s %s %s %s %s",
			uts.sysname, uts.nodename,
			uts.release, uts.version, uts.machine
		) >= sizeof buf) {
			strcpy(buf, "<buffer-is-too-small>");
		}
		result = xemu_strdup(buf);
	}
	return result;
#elif defined(XEMU_ARCH_WIN)
	static const char *result = NULL;
	if (!result) {
		char buf[1024];
		char host_name[128];
		DWORD size_used = sizeof host_name;
		if (!GetComputerNameA(host_name, &size_used))
			strcpy(host_name, "<?name?>");
		// query version, strange windows have no simple way and/or "obsoleted" to get version number :(
		// Also functions like this GetVersionEx and such are strange, for example it needs LPOSVERSIONINFOA pointer
		// according to mingw at least rather than the documented LPOSVERSIONINFOA ... Huh??
		OSVERSIONINFOA info;
		ZeroMemory(&info, sizeof info);
		info.dwOSVersionInfoSize = sizeof info;
		GetVersionEx(&info);
		// query architecture
		SYSTEM_INFO sysinfo;
		GetNativeSystemInfo(&sysinfo);
		//WORD w = sysinfo.DUMMYUNIONNAME.DUMMYSTRUCTNAME.wProcessorArchitecture; What is this shit? Windows is horrible ...
		const char *isa_name = "(Xemu-unknown-ISA)";
		switch (sysinfo.wProcessorArchitecture) {
			case 9:		// PROCESSOR_ARCHITECTURE_AMD64: x86_64 (intel or AMD)
				isa_name = "x86_64";	break;
			case 5: 	// PROCESSOR_ARCHITECTURE_ARM
				isa_name = "ARM";	break;
			case 12:	// PROCESSOR_ARCHITECTURE_ARM64
				isa_name = "ARM64";	break;
			case 6:		// PROCESSOR_ARCHITECTURE_IA64 (itanium, heh)
				isa_name = "Itanium";	break;
			case 0:		// PROCESSOR_ARCHITECTURE_INTEL (32 bit x86?)
				isa_name = "x86";	break;
			case 0xffff:	// PROCESSOR_ARCHITECTURE_UNKNOWN
				isa_name = "(Windows-unknown-ISA)";
				break;
		}
		// Huh, Windows is a real pain to collect _basic_ system informations ... on UNIX just an uname() and you're done ...
		if (snprintf(buf, sizeof buf, "Windows %s %u.%u %s%s%s",
			host_name,
			(unsigned int)info.dwMajorVersion, (unsigned int)info.dwMinorVersion,
			isa_name,
			emu_wine_version ? " WINE-" : "",
			emu_wine_version ? emu_wine_version : ""
		) >= sizeof buf) {
			strcpy(buf, "<buffer-is-too-small>");
		}
		result = xemu_strdup(buf);
	}
	return result;
#else
	static const char result[] = XEMU_ARCH_NAME " (Xemu-no-uname)";
	return result;
#endif
}


void xemu_get_timing_stat_string ( char *buf, unsigned int size )
{
	if (td_stat_counter) {
		Uint32 ticks = SDL_GetTicks() / 1000;
		snprintf(buf, size,
			"avg=%.2f%%, min=%d%%, max=%d%% (%u counts), uptime=%02d:%02d",
			td_stat_sum / (double)td_stat_counter,
			td_stat_min == INT_MAX ? 0 : td_stat_min,
			td_stat_max,
			(unsigned int)td_stat_counter,
			ticks / 60, ticks % 60
		);
	} else
		snprintf(buf, size, "Currently unavailable");
}


static void shutdown_emulator ( void )
{
	DEBUG("XEMU: Shutdown callback function has been called." NL);
	if (shutdown_user_function)
		shutdown_user_function();
	if (sdl_win) {
		SDL_DestroyWindow(sdl_win);
		sdl_win = NULL;
	}
	atexit_callback_for_console();
	if (td_stat_counter) {
		char td_stat_str[XEMU_CPU_STAT_INFO_BUFFER_SIZE];
		xemu_get_timing_stat_string(td_stat_str, sizeof td_stat_str);
		DEBUGPRINT(NL "TIMING: Xemu CPU usage: %s" NL "XEMU: good by(T)e." NL, td_stat_str);
	}
	if (debug_fp) {
		fclose(debug_fp);
		debug_fp = NULL;
	}
	// It seems, calling SQL_Quit() at least on Windows causes "segfault".
	// Not sure why, but to be safe, I just skip calling it :(
	//SDL_Quit();
#ifndef XEMU_ARCH_WIN
	SDL_Quit();
#endif
}



int xemu_init_debug ( const char *fn )
{
#ifdef DISABLE_DEBUG
	printf("Logging is disabled at compile-time." NL);
#else
	if (debug_fp) {
		ERROR_WINDOW("Debug file %s already used, you can't call emu_init_debug() twice!\nUse it before emu_init_sdl() if you need it!", fn);
		return 1;
	} else if (fn) {
		debug_fp = fopen(fn, "wb");
		if (!debug_fp) {
			ERROR_WINDOW("Cannot open requested debug file: %s", fn);
			return 1;
		}
		DEBUGPRINT("Logging into file: %s (fd=%d)." NL, fn, fileno(debug_fp));
		xemu_dump_version(debug_fp, NULL);
		return 0;
	}
#endif
	return 0;
}


#if !defined(XEMU_ARCH_HTML) && !defined(XEMU_ARCH_ANDROID)
static char *GetHackedPrefDir ( const char *base_path, const char *name )
{
	static const char prefdir_is_here_marker[] = "prefdir-is-here.txt";
	char path[PATH_MAX];
	sprintf(path, "%s%s%c", base_path, name, DIRSEP_CHR);
	char file[PATH_MAX + sizeof(prefdir_is_here_marker)];
	sprintf(file, "%s%s", path, prefdir_is_here_marker);
	int fd = open(file, O_RDONLY | O_BINARY);
	if (fd < 0)
		return NULL;
	close(fd);
	return xemu_strdup(path);
}
#endif


// "First time user" check. We use SDL file functions here only, to be implementation independent (ie, no need for emutools_files.c ...)
int xemu_is_first_time_user ( void )
{
	static int is_first_time_user = -1;
	if (is_first_time_user >= 0)
		return is_first_time_user;
	char fn[PATH_MAX];
	snprintf(fn, sizeof fn, "%s%s", sdl_pref_dir, "notfirsttimeuser.txt");
	SDL_RWops *file = SDL_RWFromFile(fn, "rb");
	if (file) {
		SDL_RWclose(file);
		is_first_time_user = 0;	// not first time user
		return is_first_time_user;
	}
	// First time user, it seems, since we don't have our file
	// Thus write the "signal file" so next time, it won't be first time ... ;) Confusing sentence ...
	file = SDL_RWFromFile(fn, "wb");
	if (!file) {
		ERROR_WINDOW("Xemu cannot write the preferences directory!\nFile: %s\nError: %s", fn, SDL_GetError());
		return 0;	// pretend not first time user, since there is some serious problem already ... leave our static variable as is, also!!
	}
	static const char message[] = "DO NOT DELETE. Xemu uses this file to tell, if Xemu has been already run on this system at all.";
	SDL_RWwrite(file, message, strlen(message), 1);	// Nah, we simply do not check if write worked ...
	SDL_RWclose(file);
	is_first_time_user = 1;
	return is_first_time_user;	// first time user detected!!
}


#if !defined(XEMU_ARCH_HTML) && !defined(XEMU_ARCH_ANDROID)
// It seems SDL_GetBasePath() can be defunct on some architectures.
// This function is intended to be used only by xemu_pre_init() and contains workaround.
static char *_getbasepath ( void )
{
	char *p = xemu_sdl_to_native_string_allocation(SDL_GetBasePath());
	if (p)
		return p;
#ifdef XEMU_ARCH_UNIX
	// We assume that SDL_GetBasePath only may have problem on certain UNIXes like for example on OpenBSD
	DEBUGPRINT("SDL: WARNING: could not query SDL base directory: %s. Reverting back to Xemu's implementation." NL, SDL_GetError());
	// reverting back to our own method ...
	char exepath[PATH_MAX + 1];
	snprintf(exepath, sizeof exepath, "%s%s", xemu_initial_argv[0][0] == DIRSEP_CHR ? "" : xemu_initial_cwd, xemu_initial_argv[0]);
	p = strrchr(exepath, DIRSEP_CHR);
	if (p)
		p[1] = '\0';
	else
		return NULL;
	return xemu_strdup(exepath);
#endif
	return NULL;
}
#endif


static inline Uint64 _get_uts_from_cdate ( void )
{
	if (strlen(XEMU_BUILDINFO_CDATE) != 14)
		FATAL("Wrong XEMU_BUILDINFO_CDATE length (%d)!", (int)strlen(XEMU_BUILDINFO_CDATE));
	struct tm t = {
		.tm_year  = (XEMU_BUILDINFO_CDATE[ 0] - '0') * 1000 + (XEMU_BUILDINFO_CDATE[ 1] - '0') * 100 + (XEMU_BUILDINFO_CDATE[2] - '0') * 10 + (XEMU_BUILDINFO_CDATE[3] - '0') - 1900,
		.tm_mon   = (XEMU_BUILDINFO_CDATE[ 4] - '0') *   10 + (XEMU_BUILDINFO_CDATE[ 5] - '0') - 1,
		.tm_mday  = (XEMU_BUILDINFO_CDATE[ 6] - '0') *   10 + (XEMU_BUILDINFO_CDATE[ 7] - '0'),
		.tm_hour  = (XEMU_BUILDINFO_CDATE[ 8] - '0') *   10 + (XEMU_BUILDINFO_CDATE[ 9] - '0'),
		.tm_min   = (XEMU_BUILDINFO_CDATE[10] - '0') *   10 + (XEMU_BUILDINFO_CDATE[11] - '0'),
		.tm_sec   = (XEMU_BUILDINFO_CDATE[12] - '0') *   10 + (XEMU_BUILDINFO_CDATE[13] - '0'),
		.tm_isdst = -1
	};
	return (Uint64)mktime(&t);
}


#ifdef XEMU_ARCH_WIN
static inline char *check_windows_utf8_fs ( void )
{
	emu_fs_is_utf8 = 0;
	// test file name (it's in Hungarian: "flood-proof mirror-drilling device", does not make sense, but contains all "special" Hungarian characters)
	static const char test_file_fn[] = "árvíztűrő_tükörfúrógép.txt";
	const int len = strlen(sdl_pref_dir) + strlen(test_file_fn) + 1;
	char fn_a[len];
	wchar_t fn_w[len];
	strcpy(fn_a, sdl_pref_dir);
	strcat(fn_a, test_file_fn);
	if (MultiByteToWideChar(CP_UTF8, 0, fn_a, -1, fn_w, len) <= 0)
		return "could not convert test file name to wchar_t";
	static const wchar_t mode_w[] = {'w', 'b', 0};
	FILE *f = _wfopen(fn_w, mode_w);
	if (!f)
		return "could not create test file with wide-func";
	fclose(f);
	f = fopen(fn_a, "rb");
	if (f) {
		fclose(f);
		_wunlink(fn_w);
		emu_fs_is_utf8 = 1;
		return NULL;
	}
	_wunlink(fn_w);
	return "could not use UTF-8 files without wchar_t aware functions";
}

static inline BOOL is_running_as_win_admin ( void )
{
	BOOL ret = FALSE;
	HANDLE token = NULL;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		DWORD size = sizeof(TOKEN_ELEVATION);
		TOKEN_ELEVATION elevation;
		if (GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size))
			ret = elevation.TokenIsElevated;
	}
	if (token)
		CloseHandle(token);
	if (ret)
		DEBUGPRINT("WINDOWS: **WARNING** running as Administrator!!!" NL);
	return ret;
}

static inline int detect_wine ( void )
{
	free((void*)emu_wine_version);
	emu_wine_version = NULL;
	static const char *(CDECL *wine_get_version_ptr)(void);
	HMODULE ntdll = GetModuleHandle("ntdll.dll");
	if (!ntdll)
		return 0;
	wine_get_version_ptr = (void*)GetProcAddress(ntdll, "wine_get_version");
	if (wine_get_version_ptr) {
		const char *p = wine_get_version_ptr();
		if (p) {
			emu_wine_version = xemu_strdup(p);
			return 1;
		}
	}
	return 0;
}

static const char xemu_windows_registry_path[] = "Software\\X-Emulators";
static const char xemu_windows_registry_class[] = "HKEY_LOCAL_MACHINE";

static DWORD get_registry_key_dword ( const char *name )
{
	DWORD answer = 0;
	DWORD answer_size = sizeof(answer);
	DWORD type_got = 0;
	LSTATUS ret = RegGetValue(HKEY_LOCAL_MACHINE, xemu_windows_registry_path, name, RRF_RT_DWORD, &type_got, &answer, &answer_size);
	if (ret == ERROR_SUCCESS)
		return answer;
	return 0;
}
#endif


void xemu_pre_init ( const char *app_organization, const char *app_name, const char *slogan, const int argc, char **argv )
{
#ifdef XEMU_ARCH_WIN
	static const char reg_key_allowadminrun[] = "AllowAdministratorRun";
	static const char reg_key_noutf8warning[] = "NoUTF8Warning";
	const DWORD registry_hack_allow_administrator	= get_registry_key_dword(reg_key_allowadminrun);
	const DWORD registry_hack_no_utf8_warning	= get_registry_key_dword(reg_key_noutf8warning);
	(void)detect_wine();
#endif
	if (!buildinfo_cdate_uts)
		buildinfo_cdate_uts = _get_uts_from_cdate();
	if (xemu_initial_argc < 0)
		xemu_initial_argc = argc;
	if (xemu_initial_argc < 1)
		FATAL("%s(): Cannot extract argc [%d?]", __func__, xemu_initial_argc);
	if (!xemu_initial_argv)
		xemu_initial_argv = argv;
	if (!xemu_initial_argv)
		FATAL("%s(): Cannot extract argv [NULL?]", __func__);
	for (int i = 0; i < xemu_initial_argc; i++)
		if (!xemu_initial_argv[i])
			FATAL("%s(): Cannot extract argv[%d] [NULL?]", __func__, i);
	if (!xemu_initial_cwd) {
		char buffer[PATH_MAX + 2];
		if (getcwd(buffer, sizeof buffer)) {
			if (buffer[strlen(buffer) - 1] != DIRSEP_CHR)
				strcat(buffer, DIRSEP_STR);
			xemu_initial_cwd = xemu_strdup(buffer);
		}
	}
	if (!xemu_initial_cwd)
		FATAL("%s(): getcwd() resolution does not work", __func__);
#if defined(XEMU_DO_NOT_DISALLOW_ROOT) && !defined(XEMU_ARCH_SINGLEUSER)
#	warning "Running as root/admin check is deactivated."
#endif
#ifdef	XEMU_ARCH_WIN
	// Windows: Some check to disallow dangerous things (running Xemu as administrator)
	if (is_running_as_win_admin()) {
#ifndef 	XEMU_DO_NOT_DISALLOW_ROOT
		if (!emu_wine_version) {	// ignore the check if it's wine, it seems it always runs programs with elevated privs
			if (!registry_hack_allow_administrator)
				WARNING_WINDOW(
					"Running Xemu as Administrator is dangerous, stop doing that!\n"
					"Alternatively, if you're really sure what you're doing,\n"
					"you can set a DWORD typed registry key %s\\%s\\%s with value of 1 to silent this warning.",
					xemu_windows_registry_class,
					xemu_windows_registry_path,
					reg_key_allowadminrun
				);
		}
#endif
	}
#endif
#ifdef XEMU_ARCH_UNIX
#ifndef XEMU_DO_NOT_DISALLOW_ROOT
	// UNIX: Some check to disallow dangerous things (running Xemu as user/group root)
	if (getuid() == 0 || geteuid() == 0)
		FATAL("Xemu must not be run as user root");
	if (getgid() == 0 || getegid() == 0)
		FATAL("Xemu must not be run as group root");
#endif
	// ignore SIGHUP, eg closing the terminal Xemu was started from ...
	signal(SIGHUP, SIG_IGN);	// ignore SIGHUP, eg closing the terminal Xemu was started from ...
#endif
#ifdef XEMU_ARCH_HTML
	if (chatty_xemu)
		xemu_dump_version(stdout, slogan);
	MKDIR(emscripten_sdl_base_dir);
	sdl_base_dir = (void*)emscripten_sdl_base_dir;
	sdl_pref_dir = (void*)emscripten_sdl_base_dir;
	sdl_inst_dir = (void*)emscripten_sdl_base_dir;
	// In case of emscripten we do all the SDL init here!!!
	// Please note: with emscripten, you can't use SDL_INIT_TIMER and SDL_INIT_HAPTIC subsystems it seems, it will
	// give error on SDL_Init (I can understand with timer, as it would require multithreading)
	if (SDL_Init(SDL_INIT_EVERYTHING & ~(SDL_INIT_TIMER | SDL_INIT_HAPTIC)))
		FATAL("Cannot initialize SDL: %s", SDL_GetError());
	atexit(shutdown_emulator);
#else
	char *p;
	sysconsole_open();
	if (chatty_xemu)
		xemu_dump_version(stdout, slogan);
	// Initialize SDL with no subsystems
	// This is needed, because SDL_GetPrefPath and co. are not safe on every platforms without it.
	// But we DO want to use *before* the real SDL_Init, as the configuration file may describe
	// parameters on the init itself, but to read the config file we must know where it is.
	// You see, chicken & egg problem. We try to work this around with the solution to do the
	// minimal init for the path info functions, then we shuts SDL down, and the "real" SDL_Init
	// with subsystems etc will happen later!
	if (SDL_Init(0))
		FATAL("Cannot pre-initialize SDL without any subsystem: %s", SDL_GetError());
	atexit(shutdown_emulator);
#ifndef	XEMU_ARCH_ANDROID
	sdl_base_dir = _getbasepath();
	if (!sdl_base_dir)
		FATAL("Cannot query SDL base directory: %s", SDL_GetError());
	p = GetHackedPrefDir(sdl_base_dir, app_name);
#else
	p = NULL;
#endif	// XEMU_ARCH_ANDROID
	if (!p)
		p = xemu_sdl_to_native_string_allocation(SDL_GetPrefPath(app_organization, app_name));
	if (p) {
		sdl_pref_dir = p;
		sdl_inst_dir = xemu_malloc(strlen(p) + strlen(INSTALL_DIRECTORY_ENTRY_NAME) + strlen(DIRSEP_STR) + 1);
		sprintf(sdl_inst_dir, "%s%s" DIRSEP_STR, p, INSTALL_DIRECTORY_ENTRY_NAME);
	} else
		FATAL("Cannot query SDL preference directory: %s", SDL_GetError());
	// We shouldn't end up with sdl_base_dir==NULL here.
	// But if we do (see above like with Android), just pretend that it's the same as our pref directory.
	if (!sdl_base_dir)
		sdl_base_dir = sdl_pref_dir;
#endif
	xemu_app_org = xemu_strdup(app_organization);
	xemu_app_name = xemu_strdup(app_name);
#ifdef XEMU_ARCH_WIN
	if (emu_wine_version)
		DEBUGPRINT("WINDOWS: running on WINE %s" NL, emu_wine_version);
	p = check_windows_utf8_fs();
	if (p) {
		DEBUGPRINT("WINDOWS: UTF-8 filenames are NOT supported: %s" NL, p);
		if (!emu_wine_version && !registry_hack_no_utf8_warning)
			WARNING_WINDOW(
				"Your windows is too old to support UTF-8 file functions!\n"
				"You may encounter problems with files/paths containing any non US-ASCII characters!\n"
				"Alternatively, if you're really sure what you're doing,\n"
				"you can set a DWORD typed registry key %s\\%s\\%s with value of 1 to silent this warning.",
				xemu_windows_registry_class,
				xemu_windows_registry_path,
				reg_key_noutf8warning
			);
	} else
		DEBUGPRINT("WINDOWS: UTF-8 filenames ARE supported, cool!" NL);
	if (!p != (GetACP() == 65001U) && !registry_hack_no_utf8_warning)
		ERROR_WINDOW("Mismatch between Windows UTF8 checks!\nPlease report the problem!\nCP=%u, p=%s", GetACP(), p ? p : "NULL");
#endif
#ifdef XEMU_CONFIGDB_SUPPORT
	// If configDB support is compiled in, we can define some common options, should apply for ALL emulators.
	// This way, it's not needed to define those in all of the emulator targets ...
	// TODO: it's an unfinished project here ...
#endif
}



int xemu_init_sdl ( void )
{
#ifndef XEMU_ARCH_HTML
	const Uint32 XEMU_SDL_INIT_EVERYTHING =
#if defined(XEMU_ARCH_WIN) && defined(SDL_INIT_SENSOR)
		// FIXME: SDL or Windows has the bug that SDL_INIT_SENSOR when used, there is some "sensor manager" problem, so we left it out
		// SDL_INIT_SENSOR was introduced somewhere in 2.0.9, however since it's a macro, it's safer not to test actual SDL version number
		SDL_INIT_EVERYTHING & ~(SDL_INIT_SENSOR | SDL_INIT_HAPTIC);
#warning	"NOTE: SDL_INIT_SENSOR and SDL_INIT_HAPTIC is not used on Windows because seems to cause problems :("
#else
		SDL_INIT_EVERYTHING;
#endif
	if (!SDL_WasInit(XEMU_SDL_INIT_EVERYTHING)) {
		DEBUGPRINT("SDL: no SDL subsystem initialization has been done yet, do it!" NL);
		SDL_Quit();	// Please read the long comment at the pre-init func above to understand this SDL_Quit() here and then the SDL_Init() right below ...
		DEBUG("SDL: before SDL init" NL);
		if (SDL_Init(XEMU_SDL_INIT_EVERYTHING)) {
			ERROR_WINDOW("Cannot initialize SDL: %s", SDL_GetError());
			return 1;
		}
		DEBUG("SDL: after SDL init" NL);
		if (!SDL_WasInit(XEMU_SDL_INIT_EVERYTHING))
			FATAL("SDL_WasInit()=0 after init??");
	} else
		DEBUGPRINT("SDL: no SDL subsystem initialization has been done already." NL);
#endif
	SDL_VERSION(&sdlver_compiled);
	SDL_GetVersion(&sdlver_linked);
	const char *sdl_video_driver = SDL_GetCurrentVideoDriver();
	if (!sdl_video_driver)
		FATAL("SDL_GetCurrentVideoDriver() == NULL");
	sdl_on_x11 = !strcasecmp(sdl_video_driver, "x11");
	sdl_on_wayland = !strcasecmp(sdl_video_driver, "wayland");
	if (SDL_GetDisplayBounds(0, &sdl_whole_screen))
		FATAL("SDL_GetDisplayBounds(0, &...) failed: %s", SDL_GetError());
	if (chatty_xemu)
		printf( "SDL version: (%s) compiled with %d.%d.%d, used with %d.%d.%d on platform %s" NL
			"SDL system info: %d bits %s, %d cores, l1_line=%d, RAM=%dMbytes, max_alignment=%d%s, CPU features: "
			"3DNow=%d AVX=%d AVX2=%d AltiVec=%d MMX=%d RDTSC=%d SSE=%d SSE2=%d SSE3=%d SSE41=%d SSE42=%d" NL
			"SDL drivers: video = %s (%dx%d), audio = %s" NL,
			SDL_GetRevision(),
			sdlver_compiled.major, sdlver_compiled.minor, sdlver_compiled.patch,
			sdlver_linked.major, sdlver_linked.minor, sdlver_linked.patch,
			SDL_GetPlatform(),
			ARCH_BITS, ENDIAN_NAME, SDL_GetCPUCount(), SDL_GetCPUCacheLineSize(), SDL_GetSystemRAM(), __BIGGEST_ALIGNMENT__,
#ifdef XEMU_MISSING_BIGGEST_ALIGNMENT_WORKAROUND
			" (set-by-Xemu)",
#else
			"",
#endif
			SDL_Has3DNow(),SDL_HasAVX(),SDL_HasAVX2(),SDL_HasAltiVec(),SDL_HasMMX(),SDL_HasRDTSC(),SDL_HasSSE(),SDL_HasSSE2(),SDL_HasSSE3(),SDL_HasSSE41(),SDL_HasSSE42(),
			sdl_video_driver, sdl_whole_screen.w, sdl_whole_screen.h, SDL_GetCurrentAudioDriver()
		);
#if defined(XEMU_ARCH_WIN)
#	define SDL_VER_MISMATCH_WARN_STR "Xemu was not compiled with the linked DLL for SDL.\nPlease upgrade your DLL too, not just Xemu binary."
#elif defined(XEMU_ARCH_OSX)
#	define SDL_VER_MISMATCH_WARN_STR "Xemu was not compuled with the linked dylib for SDL.\nPlease upgrade your dylib too, not just Xemu binary."
#endif
#ifdef SDL_VER_MISMATCH_WARN_STR
	if (sdlver_compiled.major != sdlver_linked.major || sdlver_compiled.minor != sdlver_linked.minor || sdlver_compiled.patch != sdlver_linked.patch)
		WARNING_WINDOW(SDL_VER_MISMATCH_WARN_STR);
#endif
	return 0;
}


void xemu_window_snap_to_optimal_size ( int forced )
{
	// XXX TODO check if fullscreen state is active?
	// though it must be checked if it's needed at all (ie: SDL is OK with resizing window in fullscreen mode without any effect BEFORE switcing back from fullscreen)
	static Uint32 last_resize = 0;
	Uint32 now = 0;
	if (!forced && sdl_viewport_changed && follow_win_size) {
		now = SDL_GetTicks();
		if (now - last_resize >= 1000) {
			sdl_viewport_changed = 0;
			forced = 1;
		}
	}
	if (!forced)
		return;
	int w, h;
	SDL_GetWindowSize(sdl_win, &w, &h);
	float rat = (float)w / (float)sdl_viewport.w;
	const float rat2 = (float)h / (float)sdl_viewport.h;
	if (rat2 > rat)
		rat = rat2;
	rat = roundf(rat);	// XXX TODO: depends on math.h mingw warning!
	// XXX TODO: check if window is not larger than the screen itself
	if (rat < 1)
		rat = 1;
	const int w2 = rat * sdl_viewport.w;
	const int h2 = rat * sdl_viewport.h;
	if (w != w2 || h != h2) {
		last_resize = now;
		SDL_SetWindowSize(sdl_win, w2, h2);
		DEBUGPRINT("SDL: auto-resizing window to %d x %d (zoom level approximated: %d)" NL, w2, h2, (int)rat);
	} else
		DEBUGPRINT("SDL: no auto-resizing was needed (same size)" NL);
}


void xemu_set_viewport ( unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, unsigned int flags )
{
	if (XEMU_UNLIKELY(x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0)) {
		sdl_viewport_ptr = NULL;
		sdl_viewport.x = 0;
		sdl_viewport.y = 0;
		sdl_viewport.w = sdl_texture_x_size;
		sdl_viewport.h = sdl_texture_y_size;
	} else {
		if (XEMU_UNLIKELY(x1 > x2 || y1 > y2 || x1 >= sdl_texture_x_size || y1 >= sdl_texture_y_size || x2 >= sdl_texture_x_size || y2 >= sdl_texture_y_size)) {
			FATAL("Invalid xemu_set_viewport(%d,%d,%d,%d) for texture (%d x %d)", x1, y1, x2, y2, sdl_texture_x_size, sdl_texture_y_size);
		} else {
			sdl_viewport_ptr = &sdl_viewport;
			sdl_viewport.x = x1;
			sdl_viewport.y = y1;
			sdl_viewport.w = x2 - x1 + 1;
			sdl_viewport.h = y2 - y1 + 1;
		}
	}
	sdl_viewport_changed = 1;
	follow_win_size = 0;
	if ((flags & XEMU_VIEWPORT_ADJUST_LOGICAL_SIZE)) {
		SDL_RenderSetLogicalSize(sdl_ren, sdl_viewport.w, sdl_viewport.h);
		// XXX this should be not handled this way
		sdl_default_win_x_size = sdl_viewport.w;
		sdl_default_win_y_size = sdl_viewport.h;
		//if ((flags & XEMU_VIEWPORT_WIN_SIZE_FOLLOW_LOGICAL))
		//XXX remove this XEMU_VIEWPORT_WIN_SIZE_FOLLOW_LOGICAL then!
		follow_win_size = 1;
	}
}


void xemu_get_viewport ( unsigned int *x1, unsigned int *y1, unsigned int *x2, unsigned int *y2 )
{
	if (x1)
		*x1 = sdl_viewport.x;
	if (y1)
		*y1 = sdl_viewport.y;
	if (x2)
		*x2 = sdl_viewport.x + sdl_viewport.w - 1;
	if (y2)
		*y2 = sdl_viewport.y + sdl_viewport.h - 1;
}


static int xemu_create_main_texture ( void )
{
	DEBUGPRINT("SDL: creating main texture %d x %d" NL, sdl_texture_x_size, sdl_texture_y_size);
	SDL_Texture *new_tex = SDL_CreateTexture(sdl_ren, sdl_pixel_format_id, SDL_TEXTUREACCESS_STREAMING, sdl_texture_x_size, sdl_texture_y_size);
	if (!new_tex) {
		DEBUGPRINT("SDL: cannot create main texture: %s" NL, SDL_GetError());
		return 1;
	}
	if (sdl_tex) {
		DEBUGPRINT("SDL: destroying old main texture" NL);
		SDL_DestroyTexture(sdl_tex);
	}
	sdl_tex = new_tex;
	return 0;
}


void xemu_set_default_win_pos_from_string ( const char *s )
{
	if (s && *s) {
		int x, y;
		if (sscanf(s, "%d,%d", &x, &y) == 2) {
			sdl_default_win_x_pos = x;
			sdl_default_win_y_pos = y;
		}
	}
}


void xemu_default_win_pos_file_op ( const char r_or_w )
{
	static const char relpath[] = "window-position.info";
	static char fmode[] = "?b";
	char fn[strlen(sdl_pref_dir) + strlen(relpath) + 1];
	strcpy(fn, sdl_pref_dir);
	strcat(fn, relpath);
	fmode[0] = r_or_w;
	FILE *f = fopen(fn, fmode);
	if (f) {
		int x, y;
		if (r_or_w == 'w') {
			SDL_GetWindowPosition(sdl_win, &x, &y);
			fprintf(f, "%d,%d", x, y);
		} else {
			if (fscanf(f, "%d,%d", &x, &y) == 2) {
				sdl_default_win_x_pos = x;
				sdl_default_win_y_pos = y;
			}
		}
		fclose(f);
	}
}


static void setenv_from_string ( const char *templ )
{
	if (!templ)
		return;
	for (char *env = xemu_strdup(templ);; env = NULL) {
		char *token = strtok(env, ":");
		if (!token)
			break;
		const char *c = token;
		while (*c && *c > 32 && *c < 127)
			c++;
		if (*c || token[0] == '=' || !strchr(token, '=') || (strncmp(token, "SDL_", 4) && strncmp(token, "GTK_", 4) && strncmp(token, "GDK_", 4) && strncmp(token, "XEMU_", 4))) {
			ERROR_WINDOW("Bad environment variable specification: %s", token);
		} else {
			DEBUGPRINT("XEMU: setting up environment variable: %s" NL, token);
			putenv(token);
		}
	}
}


/* Return value: 0 = ok, otherwise: ERROR, caller must exit, and can't use any other functionality, otherwise crash would happen.*/
int xemu_post_init (
	const char *window_title,		// title of our window
	int is_resizable,			// allow window resize? [0 = no]
	int texture_x_size, int texture_y_size,	// raw size of texture (in pixels)
	int logical_x_size, int logical_y_size,	// "logical" size in pixels, ie to correct aspect ratio, etc, can be the as texture of course, if it's OK ...
	int win_x_size, int win_y_size,		// default window size, in pixels [note: if logical/texture size combo does not match in ratio with this, black stripes you will see ...]
	Uint32 pixel_format,			// SDL pixel format we want to use (an SDL constant, like SDL_PIXELFORMAT_ARGB8888) Note: it can gave serve impact on performance, ARGB8888 recommended
	int n_colours,				// number of colours emulator wants to use
	const Uint8 *colours,			// RGB components of each colours, we need 3 * n_colours bytes to be passed!
	Uint32 *store_palette,			// this will be filled with generated palette, n_colours Uint32 values will be placed
	int render_scale_quality,		// render scale quality, must be 0, 1 or 2 _ONLY_
	int locked_texture_update,		// use locked texture method [non zero], or malloc'ed stuff [zero]. NOTE: locked access doesn't allow to _READ_ pixels and you must fill ALL pixels!
	void (*shutdown_callback)(void)		// callback function called on exit (can be nULL to not have any emulator specific stuff)
) {
	if (emu_is_headless) {
		dialogs_allowed = 0;
		i_am_sure_override = 1;
	}
	srand((unsigned int)time(NULL));
	setenv_from_string(xemu_extra_env_var_setup_str);
	if (!debug_fp)
		xemu_init_debug(getenv("XEMU_DEBUG_FILE"));
	if (!debug_fp && chatty_xemu)
		printf("Logging into file: not enabled." NL);
	if (!sdl_pref_dir)
		FATAL("xemu_pre_init() hasn't been called yet!");
	if (xemu_byte_order_test()) {
		ERROR_WINDOW("Byte order test failed!!");
		return 1;
	}
#ifndef	XEMU_ARCH_HTML
	if (emu_is_headless)
		setenv_from_string("SDL_VIDEODRIVER=dummy:SDL_AUDIODRIVER=dummy");
#endif
#ifdef SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR
	// Disallow disabling compositing (of KDE, for example)
	// Maybe needed before SDL_Init(), so it's here before calling xemu_init_sdl()
	SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
	if (xemu_init_sdl())	// it is possible that is has been already called, but it's not a problem
		return 1;
	shutdown_user_function = shutdown_callback;
	DEBUGPRINT("TIMING: sleep = %s, query = %s" NL, __SLEEP_METHOD_DESC, __TIMING_METHOD_DESC);
	DEBUGPRINT("SDL preferences directory: %s" NL, sdl_pref_dir);
	DEBUG("SDL install directory: %s" NL, sdl_inst_dir);
	DEBUG("SDL base directory: %s" NL, sdl_base_dir);
#ifndef XEMU_ARCH_HTML
	if (MKDIR(sdl_inst_dir) && errno != EEXIST)
		ERROR_WINDOW("Warning: cannot create directory %s: %s", sdl_inst_dir, strerror(errno));
#endif
#ifndef XEMU_ARCH_WIN
	do {
		char *p = getenv("HOME");
		if (p && strlen(sdl_pref_dir) > strlen(p) + 1 && !strncmp(p, sdl_pref_dir, strlen(p)) && sdl_pref_dir[strlen(p)] == DIRSEP_CHR) {
			char s[PATH_MAX];
			sprintf(s, "%s" DIRSEP_STR ".%s", p, xemu_app_org);
			p = sdl_pref_dir + strlen(p) + 1;
			if (symlink(p, s)) {
				if (errno != EEXIST)
					WARNING_WINDOW("Warning: cannot create symlink %s to %s: %s", p, s, strerror(errno));
			} else
				INFO_WINDOW("Old-style link for pref.directory has been created as %s\npointing to: %s", s, p);
		}
	} while (0);
#endif
	/* SDL hints */
	// Moved here (instead of near the end of this func) since some of hints needed to be given
	// rearly (like SDL_HINT_RENDER_SCALE_QUALITY before creating texture?)
#if defined(SDL_HINT_THREAD_STACK_SIZE) && defined(XEMU_THREAD_STACK_SIZE)
	// string as positive number: use stack size, zero: use thread backend default (glibc usually gives 8Mb, other maybe small!)
	// Leave that to user, if XEMU_THREAD_STACK_SIZE is defined, it will be set.
	SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, STRINGIFY(XEMU_THREAD_STACK_SIZE));
#endif
#ifdef SDL_HINT_RENDER_SCALE_QUALITY
	const char render_scale_quality_s[2] = { '0' + (render_scale_quality & 3), '\0' };
	SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, render_scale_quality_s, SDL_HINT_OVERRIDE);		// render scale quality 0, 1, 2
#endif
#ifdef SDL_HINT_VIDEO_X11_NET_WM_PING
	SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_PING, "0");				// disable WM ping, SDL dialog boxes makes WMs things emu is dead (?)
#endif
#ifdef SDL_HINT_RENDER_VSYNC
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");					// disable vsync aligned screen rendering
#endif
#ifdef SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4
	SDL_SetHint(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, "1");				// 1 = disable ALT-F4 close on Windows
#endif
#ifdef SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");			// 1 = do minimize the SDL_Window if it loses key focus when in fullscreen mode
#endif
#ifdef SDL_HINT_VIDEO_ALLOW_SCREENSAVER
	SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");				// 1 = enable screen saver
#endif
#if defined(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK) && defined(XEMU_ARCH_MAC)
#	warning "Activating workaround for lame newer Apple notebooks (no right click on touchpads)"
	SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");			// 1 = enable CTRL + click = right click (needed for modern Mac touchpads, it seems)
#endif
	/* end of SDL hints section */
	if (sdl_default_win_x_pos == SDL_WINDOWPOS_UNDEFINED && sdl_default_win_y_pos == SDL_WINDOWPOS_UNDEFINED)
		xemu_default_win_pos_file_op('r');	// try to load window position if sdl_default_win_?_pos not yet initialised
	if (
		sdl_default_win_x_pos != SDL_WINDOWPOS_UNDEFINED && sdl_default_win_y_pos != SDL_WINDOWPOS_UNDEFINED &&
		(sdl_default_win_x_pos < 0 || sdl_default_win_y_pos < 0 || sdl_default_win_x_pos > sdl_whole_screen.w - 32 || sdl_default_win_y_pos > sdl_whole_screen.h - 32)
	) {
		DEBUGPRINT("SDL: default win pos coords (%d,%d) are invalid for screen (%dx%d)." NL, sdl_default_win_x_pos, sdl_default_win_y_pos, sdl_whole_screen.w, sdl_whole_screen.h);
		sdl_default_win_x_pos = SDL_WINDOWPOS_UNDEFINED;
		sdl_default_win_y_pos = SDL_WINDOWPOS_UNDEFINED;
	}
	DEBUGPRINT("SDL: opening window at (%d,%d) with size of (%d,%d)" NL, sdl_default_win_x_pos, sdl_default_win_y_pos, win_x_size, win_y_size);
	sdl_window_title = xemu_strdup(window_title);
	sdl_win = SDL_CreateWindow(
		window_title,
		sdl_default_win_x_pos, sdl_default_win_y_pos,
		win_x_size, win_y_size,
		SDL_WINDOW_SHOWN | (is_resizable ? SDL_WINDOW_RESIZABLE : 0)
	);
	sdl_default_win_x_size = win_x_size;
	sdl_default_win_y_size = win_y_size;
	DEBUGPRINT("SDL window native pixel format: %s" NL, SDL_GetPixelFormatName(SDL_GetWindowPixelFormat(sdl_win)));
	if (!sdl_win) {
		ERROR_WINDOW("Cannot create SDL window: %s", SDL_GetError());
		return 1;
	}
	window_title_buffer = xemu_malloc(strlen(window_title) + 128);
	strcpy(window_title_buffer, window_title);
	window_title_buffer_end = window_title_buffer + strlen(window_title);
	//SDL_SetWindowMinimumSize(sdl_win, SCREEN_WIDTH, SCREEN_HEIGHT * 2);
	SDL_RendererInfo ren_info;
	for (int a = 0, max = SDL_GetNumRenderDrivers(); a < max; a++) {
		if (!SDL_GetRenderDriverInfo(a, &ren_info))
			DEBUGPRINT("SDL renderer driver #%d: \"%s\"" NL, a, ren_info.name);
		else
			DEBUGPRINT("SDL renderer driver #%d: FAILURE TO QUERY (%s)" NL, a, SDL_GetError());
	}
	sdl_ren = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_ACCELERATED);
	if (!sdl_ren) {
		ERROR_WINDOW("Cannot create accelerated SDL renderer: %s", SDL_GetError());
		sdl_ren = SDL_CreateRenderer(sdl_win, -1, 0);
		if (!sdl_ren) {
			ERROR_WINDOW("... and not even non-accelerated driver could be created, giving up: %s", SDL_GetError());
			return 1;
		} else {
			INFO_WINDOW("Created non-accelerated driver. NOTE: it will severly affect the performance!");
		}
	}
	SDL_SetRenderDrawColor(sdl_ren, 0, 0, 0, SDL_ALPHA_OPAQUE);
	if (!SDL_GetRendererInfo(sdl_ren, &ren_info)) {
		DEBUGPRINT("SDL renderer used: \"%s\" max_tex=%dx%d tex_formats=%d ", ren_info.name, ren_info.max_texture_width, ren_info.max_texture_height, ren_info.num_texture_formats);
		for (int a = 0; a < ren_info.num_texture_formats; a++) {
			const char *p = SDL_GetPixelFormatName(ren_info.texture_formats[a]);
			if (p) {
				static const char name_head[] = { 'S','D','L','_','P','I','X','E','L','F','O','R','M','A','T','_' };
				if (!strncmp(p, name_head, sizeof name_head))
					p += sizeof name_head;
			}
			DEBUGPRINT("%c%s", a ? ' ' : '(', p ? p : "?");
		}
		DEBUGPRINT(")" NL);
	}
	SDL_RenderSetLogicalSize(sdl_ren, logical_x_size, logical_y_size);	// this helps SDL to know the "logical ratio" of screen, even in full screen mode when scaling is needed!
	sdl_texture_x_size = texture_x_size;
	sdl_texture_y_size = texture_y_size;
	sdl_pixel_format_id = pixel_format;
	xemu_set_viewport(0, 0, 0, 0, 0);
	if (xemu_create_main_texture()) {
		ERROR_WINDOW("Cannot create SDL texture: %s", SDL_GetError());
		return 1;
	}
	texture_x_size_in_bytes = texture_x_size * 4;
	sdl_winid = SDL_GetWindowID(sdl_win);
	/* Intitialize palette from given RGB components */
	sdl_pix_fmt = SDL_AllocFormat(pixel_format);
	black_colour = SDL_MapRGBA(sdl_pix_fmt, 0, 0, 0, 0xFF);	// used to initialize pixel buffer
	while (n_colours--)
		store_palette[n_colours] = SDL_MapRGBA(sdl_pix_fmt, colours[n_colours * 3], colours[n_colours * 3 + 1], colours[n_colours * 3 + 2], 0xFF);
	/* texture access / buffer */
	if (!locked_texture_update)
		sdl_pixel_buffer = xemu_malloc_ALIGNED(texture_x_size_in_bytes * texture_y_size);
	// play a single frame game, to set a consistent colour (all black ...) for the emulator. Also, it reveals possible errors with rendering
	xemu_render_dummy_frame(black_colour, texture_x_size, texture_y_size);
	if (chatty_xemu)
		printf(NL);
#	include "build/xemu-48x48.xpm"
	xemu_set_icon_from_xpm(favicon_xpm);
	return 0;
}


int xemu_set_icon_from_xpm ( char *xpm[] )
{
	int width, height, colours, chperpix;
	if (sscanf(xpm[0], "%d %d %d %d", &width, &height, &colours, &chperpix) != 4) {
		ERROR_WINDOW("Icon internal error: bad format");
		return -1;
	}
	if (chperpix != 1) {
		ERROR_WINDOW("Icon internal error: not one-char per pixel format");
		return -1;
	}
	Uint8 *data = xemu_malloc(height * width);
	SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(data, width, height, 8, width, 0, 0, 0, 0);
	if (!surf) {
		ERROR_WINDOW("Icon internal error: cannot allocate surface: %s", SDL_GetError());
		free(data);
		return -1;
	}
	int i = 1;
	while (colours) {
		SDL_Color *palentry = &(surf->format->palette->colors[(Uint8)xpm[i][0]]);
		Uint8 *p = (Uint8*)strchr(xpm[i] + 1, '#');
		if (p) {
			int vals[6];	// ugly, but again, Windows ... it does not support %02hhx for scanf(). Really what windows is for? it does not know any standards at all :-O
			for (int a = 0; a < 6; a++) {
				int hdig = p[a + 1];
				if (hdig >= '0' && hdig <= '9')
					vals[a] = hdig - '0';
				else if (hdig >= 'A' && hdig <= 'F')
					vals[a] = hdig - 'A' + 10;
				else
					vals[a] = hdig - 'a' + 10;
			}
			palentry->r = (vals[0] << 4) + vals[1];
			palentry->g = (vals[2] << 4) + vals[3];
			palentry->b = (vals[4] << 4) + vals[5];
			palentry->a = 0xFF;
		} else
			palentry->a = 0x00;
		colours--;
		i++;
	}
	Uint8 *d = data;
	while (height) {
		memcpy(d, xpm[i++], width);
		d += width;
		height--;
	}
	SDL_SetWindowIcon(sdl_win, surf);
	SDL_FreeSurface(surf);
	free(data);
	return 0;
}


// this is just for the time keeping stuff, to avoid very insane values (ie, years since the last update for the first call ...)
void xemu_timekeeping_start ( void )
{
	(void)get_elapsed_time(0, &et_old, &unix_time_tv);
	td_balancer = 0;
	td_em_ALL = 0;
	td_pc_ALL = 0;
	seconds_timer_trigger = 1;
}


void xemu_sleepless_temporary_mode ( const int enable )
{
	static int enabled = 0;
	if ((enable && enabled) || (!enable && !enabled))
		return;
	enabled = enable;
	static int sleepless_old = 0;
	if (enable) {
		DEBUGPRINT("TIMING: enabling temporary sleepless mode: %s" NL, emu_is_sleepless ? "already sleepless" : "OK");
		sleepless_old = emu_is_sleepless;
		emu_is_sleepless = 1;
	} else {
		DEBUGPRINT("TIMING: disabling temporary sleepless mode: %s" NL, sleepless_old ? "constant sleepless" : "OK");
		emu_is_sleepless = sleepless_old;
		if (!emu_is_sleepless)
			xemu_timekeeping_start();
	}
}


void xemu_render_dummy_frame ( Uint32 colour, int texture_x_size, int texture_y_size )
{
	int tail;
	Uint32 *pp = xemu_start_pixel_buffer_access(&tail);
	for (int y = 0; y < texture_y_size; y++) {
		for (int x = 0; x < texture_x_size; x++)
			*(pp++) = colour;
		pp += tail;
	}
	seconds_timer_trigger = 1;
	xemu_update_screen();
}


/* You *MUST* call this _ONCE_ before any access of pixels of the rendering target
   after render is done. Then pixels can be written but especially in locked_texture
   mode, you CAN'T read the previous frame pixels back! Also then you need to update
   *ALL* pixels of the texture before calling xemu_update_screen() func at the end!
   tail should have added at the end of each lines of the texture, in theory it should
   be zero (when it's not needed ...) but you CANNOT be sure, if it's really true!
   tail is meant in 4 bytes (ie Uint32 pointer)! */
Uint32 *xemu_start_pixel_buffer_access ( int *texture_tail )
{
	if (register_new_texture_creation) {
		register_new_texture_creation = 0;
		xemu_create_main_texture();
	}
	if (sdl_pixel_buffer) {
		*texture_tail = 0;		// using non-locked texture access, "tail" is always zero
		xemu_frame_pixel_access_p = sdl_pixel_buffer;
		return sdl_pixel_buffer;	// using non-locked texture access, return with the malloc'ed buffer
	} else {
		int pitch;
		void *pixels;
		if (SDL_LockTexture(sdl_tex, NULL, &pixels, &pitch))
			FATAL("Cannot lock texture: %s", SDL_GetError());
		if ((pitch & 3))
			FATAL("Not dword aligned texture pitch value got!");
		pitch -= texture_x_size_in_bytes;
		if (pitch < 0)
			FATAL("Negative pitch value got for the texture size!");
		*texture_tail = (pitch >> 2);
		xemu_frame_pixel_access_p = pixels;
		return pixels;
	}
}


/* Call this, to "show" the result given by filled pixel buffer whose pointer is
   got by calling emu_start_pixel_buffer_access(). Please read the notes at
   emu_start_pixel_buffer_access() carefully, especially, if you use the locked
   texture method! */
void xemu_update_screen ( void )
{
	if (sdl_pixel_buffer) {
		SDL_UpdateTexture(sdl_tex, NULL, sdl_pixel_buffer, texture_x_size_in_bytes);
	} else {
		SDL_UnlockTexture(sdl_tex);
		xemu_frame_pixel_access_p = NULL;	// not valid anymore!
	}
	//if (seconds_timer_trigger)
		SDL_RenderClear(sdl_ren); // Note: it's not needed at any price, however eg with full screen or ratio mismatches, unused screen space will be corrupted without this!
	SDL_RenderCopy(sdl_ren, sdl_tex, sdl_viewport_ptr, NULL);
#ifdef XEMU_OSD_SUPPORT
	_osd_render();
#endif
	SDL_RenderPresent(sdl_ren);
}


int ARE_YOU_SURE ( const char *s, int flags )
{
	if ((flags & ARE_YOU_SURE_OVERRIDE))
		return 1;
	static const char *selector_default_yes = "!Yes|?No";
	static const char *selector_default_no  = "Yes|*No";
	static const char *selector_generic     = "Yes|No";
	const char *selector;
	if ((flags & ARE_YOU_SURE_DEFAULT_YES))
		selector = selector_default_yes;
	else if ((flags & ARE_YOU_SURE_DEFAULT_NO))
		selector = selector_default_no;
	else
		selector = selector_generic;
	return (QUESTION_WINDOW(selector, (s != NULL && *s != '\0') ? s : "Are you sure?") == 0);
}


int _sdl_emu_secured_modal_box_ ( const char *items_in, const char *msg )
{
	char items_buf[512], *items = items_buf;
	int buttonid = 0;
	SDL_MessageBoxButtonData buttons[16];
	SDL_MessageBoxData messageboxdata = {
		SDL_MESSAGEBOX_WARNING	// .flags
#if SDL_VERSION_ATLEAST(2, 0, 12)
		| SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT
#endif
		,
		sdl_win,		// .window
		default_window_title,	// .title
		msg,			// .message
		0,			// number of buttons, will be updated!
		buttons,
		NULL			// &colorScheme
	};
	if (!SDL_WasInit(0))
		FATAL("Calling _sdl_emu_secured_modal_box_() without non-zero SDL_Init() before!");
	strcpy(items_buf, items_in);
	for (;;) {
		char *p = strchr(items, '|');
		switch (*items) {
			case '!':
				buttons[messageboxdata.numbuttons].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
				buttonid = messageboxdata.numbuttons;
				items++;
				break;
			case '?':
				buttons[messageboxdata.numbuttons].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
				items++;
				break;
			case '*':
				buttons[messageboxdata.numbuttons].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
				buttonid = messageboxdata.numbuttons;
				items++;
				break;
			default:
				buttons[messageboxdata.numbuttons].flags = 0;
				break;
		}
#ifdef XEMU_ARCH_HTML
		if ((buttons[messageboxdata.numbuttons].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT)) {
			DEBUGPRINT("Emscripten: faking chooser box answer %d for \"%s\"" NL, messageboxdata.numbuttons, msg);
			return messageboxdata.numbuttons;
		}
#endif
		buttons[messageboxdata.numbuttons].text = items;
		buttons[messageboxdata.numbuttons].buttonid = messageboxdata.numbuttons;
		messageboxdata.numbuttons++;
		if (!p)
			break;
		*p = 0;
		items = p + 1;
	}
	if (dialogs_allowed) {
		save_mouse_grab();
		SDL_ShowMessageBox_custom(&messageboxdata, &buttonid);
		xemu_drop_events();
		clear_emu_events();
		SDL_RaiseWindow(sdl_win);
		restore_mouse_grab();
		xemu_timekeeping_start();
	} else
		DEBUGPRINT("UI: returning #%d (%s) for choice in dialog box, as dialogs are NOT allowed!" NL, buttonid, buttons[buttonid].text);
	return buttonid;
}


/* Note, Windows has some braindead idea about console, ie even the standard stdout/stderr/stdin does not work with
   a GUI application. We have to dance a bit, to fool Windows to do what is SHOULD according the standard to be used
   by every other operating systems. Ehhh, Microsoft, please, get some real designers and programmers :-)
   Though one thing is clear: I am *NOT* a Windows developer, I can't even understand what this does exactly to be
   honest, just try&error, and some advices from other people. For example, in Win64 there are some warnings about
   this function. I can't do anything, since Windows API is a nightmare, using non-C-standard types for system
   calls, I have no idea ... */

#ifdef XEMU_ARCH_WIN
static int redirect_stdfp ( const DWORD handle_const, FILE *std, const char *mode, const char *desc )
{
	const HANDLE lStdHandle = GetStdHandle(handle_const);
	if (lStdHandle == NULL || lStdHandle == INVALID_HANDLE_VALUE) {
		DEBUGPRINT("WINDOWS: cannot redirect %s: GetStdHandle() failed" NL, desc);
		return 1;
	}
	const int hConHandle = _open_osfhandle((INT_PTR)lStdHandle, _O_TEXT);
	if (hConHandle < 0) {
		DEBUGPRINT("WINDOWS: cannot redirect %s: _open_osfhandle() failed" NL, desc);
		return 1;
	}
	FILE *fp = _fdopen(hConHandle, mode);
	if (!fp) {
		DEBUGPRINT("WINDOWS: cannot redirect %s: _fdopen() failed" NL, desc);
		return 1;
	}
	*std = *fp;
	setvbuf(std, NULL, _IONBF, 0);
	return 0;
}
#endif


void sysconsole_open ( void )
{
#ifdef XEMU_ARCH_WIN
	CONSOLE_SCREEN_BUFFER_INFO coninfo;
	if (sysconsole_is_open)
		return;
	sysconsole_is_open = 0;
	FreeConsole();
	if (!AllocConsole()) {
		ERROR_WINDOW("Cannot allocate windows console!");
		return;
	}
	// disallow closing console (this would kill the app, too!!!!)
#if 1
	HWND hwnd = GetConsoleWindow();
	if (hwnd != NULL) {
		HMENU hmenu = GetSystemMenu(hwnd, FALSE);
		if (hmenu != NULL)
			DeleteMenu(hmenu, SC_CLOSE, MF_BYCOMMAND);
		else
			DEBUGPRINT("WINDOWS: GetSystemMenu() failed to give the menu for our console window." NL);
	} else
		DEBUGPRINT("WINDOWS: GetConsoleWindow() failed to give a handle." NL);
#endif
	// end of close madness
	SetConsoleOutputCP(65001); // CP_UTF8, just to be sure to use the constant as not all mingw versions seems to define it
	SetConsoleTitle("Xemu Console");
	// set the screen buffer to be big enough to let us scroll text
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);
	coninfo.dwSize.Y = 1024;
	//coninfo.dwSize.X = 100;
	SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);
	// redirect unbuffered stdin/stdout/stderr to the console:
	redirect_stdfp(STD_OUTPUT_HANDLE, stdout, "w", "STDOUT");
	redirect_stdfp(STD_INPUT_HANDLE,  stdin,  "r", "STDIN" );
	redirect_stdfp(STD_ERROR_HANDLE,  stderr, "w", "STDERR");
	// make cout, wcout, cin, wcin, wcerr, cerr, wclog and clog point to console as well
	// sync_with_stdio();
	// Set Con Attributes
	SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY);
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
	SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
	DEBUGPRINT("WINDOWS: console is open" NL);
	if (!atexit_callback_for_console_registered) {
		atexit(atexit_callback_for_console);
		atexit_callback_for_console_registered = 1;
	}
#endif
	sysconsole_is_open = 1;
}


#ifdef XEMU_ARCH_WIN
static CHAR sysconsole_getch( void )
{
	HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
	if (!h)	// console cannot be accessed or WTF?
		return 0;
	DWORD cc, mode_saved;
	GetConsoleMode(h, &mode_saved);
	SetConsoleMode(h, mode_saved & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
	TCHAR c = 0;
	ReadConsole(h, &c, 1, &cc, NULL);
	SetConsoleMode(h, mode_saved);
	return c;
}
#endif


#ifdef XEMU_ARCH_WIN
static int file_handle_redirect ( const char *target, const char *symname, const char *mode, FILE *handle )
{
	if (!freopen(target, mode, handle)) {
		ERROR_WINDOW("Failed to redirect [%s] to \"%s\"\n%s", symname, target, strerror(errno));
		return 1;
	}
	return 0;
}
#endif


void sysconsole_close ( const char *waitmsg )
{
	if (!sysconsole_is_open)
		return;
#ifdef XEMU_ARCH_WIN
	if (waitmsg) {
		// FIXME: for some reason on Windows (no idea why), window cannot be open from an atexit callback
		// So instead of a GUI element here with a dialog box, we must rely on the console to press a key to continue ...
		printf("\n\n*** %s\nPress SPACE to continue.", waitmsg);
		while (sysconsole_getch() != 32)
			SDL_Delay(1);
	}
	if (!FreeConsole()) {
		if (!waitmsg)
			ERROR_WINDOW("Cannot release windows console!");
	} else {
		sysconsole_is_open = 0;
#if 1
		// redirect std file handled to "NUL" to avoid strange issues after closing the console, like corrupting
		// other files (for unknown reasons) by further I/O after FreeConsole() ...
		int ret = file_handle_redirect(NULL_DEVICE, "stderr", "w", stderr);
		ret |=    file_handle_redirect(NULL_DEVICE, "stdout", "w", stdout);
		ret |=    file_handle_redirect(NULL_DEVICE, "stdin",  "r", stdin );
		DEBUG("WINDOWS: console has been closed (file_handle_redirect: %s)" NL, ret ? "ERROR" : "OK");
#else
		DEBUGPRINT("WINDOWS: console has been closed" NL);
#endif
	}
#elif defined(XEMU_ARCH_MAC)
	if (macos_gui_started) {
		DEBUGPRINT("MACOS: GUI-startup detected, closing standard in/out/error file descriptors ..." NL);
		for (int fd = 0; fd <= 2; fd++) {
			int devnull = open("/dev/null", O_WRONLY);	// WR by will, so even for STDIN, it will cause an error on read
			if (devnull >= 0 && devnull != fd)
				dup2(devnull, fd);
			if (devnull > 2)
				close(devnull);
		}
#if 0
	// FIXME: do we really need this? AFAIK MacOS may cause to log things if terminal is not there but there is output, which is not so nice ...
	for (int fd = 0; fd < 100; fd++) {
		if (isatty(fd)) {
			if (fd <= 2) {
				int dupres = 0;
				int devnull = open("/dev/null", O_WRONLY);	// WR by will, so even for STDIN, it will cause an error on read
				if (devnull >= 0 && devnull != fd)
					dupres = dup2(devnull, fd);
				if (devnull > 2)
					close(devnull);
			} else
				close(fd);
		}
	}
#endif
	}
	sysconsole_is_open = 0;
#else
	sysconsole_is_open = 0;
#endif
}


int sysconsole_toggle ( int set )
{
	switch (set) {
		case 0:
			sysconsole_close(NULL);
			break;
		case 1:
			sysconsole_open();
			break;
		default:
			if (sysconsole_is_open)
				sysconsole_close(NULL);
			else
				sysconsole_open();
			break;
	}
	return sysconsole_is_open;
}


int xemu_file_exists ( const char *fn )
{
	struct stat st;
	return !stat(fn, &st);
}


int xemu_readdir ( DIR *dirp, char *fn, const int fnmaxsize )
{
	errno = 0;
	const struct dirent *p = readdir(dirp);
	if (!p)
		return -1;
	if (strlen(p->d_name) >= fnmaxsize) {
		return -1;
	}
	strcpy(fn, p->d_name);
	return 0;
}


/* -------------------------- SHA1 checksumming -------------------------- */


static inline Uint32 leftrotate ( Uint32 i, unsigned int count )
{
	count &= 31;
	return (i << count) + (i >> (32 - count));
}


static void sha1_chunk ( Uint32 h[5], const Uint8 *data )
{
	// Note: this function has been written by me (LGB) by following the pseudocode from Wikipedia
	Uint32 w[80];
	// Split our chunk into sixteen 32-bit big-endian words
	for (unsigned int i = 0; i < 16; i++) {
		w[i] = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
		data += 4;
	}
	// Extend the sixteen 32-bit words into eighty 32-bit words
	for (unsigned int i = 16; i <= 79; i++)
		w[i] = leftrotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	// Initialize variables from the current state of the hash (previous chunk OR default hash values for SHA-1)
	Uint32 a = h[0];
	Uint32 b = h[1];
	Uint32 c = h[2];
	Uint32 d = h[3];
	Uint32 e = h[4];
	// Main loop for the 80 words
	for (unsigned int i = 0; i <= 79; i++) {
		Uint32 f, k;
		if (i <= 19) {		// 0 ... 19
			f = (b & c) | ((~b) & d);
			k = 0x5A827999U;
		} else if (i <= 39) {	// 20 .. 39
			f = b ^ c ^ d;
			k = 0x6ED9EBA1U;
		} else if (i <= 59) {	// 40 .. 59
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDCU;
		} else { 		// 60 .. 79
			f = b ^ c ^ d;
			k = 0xCA62C1D6U;
		}
		Uint32 temp = leftrotate(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = leftrotate(b, 30);
		b = a;
		a = temp;
	}
	// Update hash value with this chunk's result
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
}


void sha1_checksum_as_words ( Uint32 hash[5], const Uint8 *data, Uint32 size )
{
	hash[0] = 0x67452301U;
	hash[1] = 0xEFCDAB89U;
	hash[2] = 0x98BADCFEU;
	hash[3] = 0x10325476U;
	hash[4] = 0xC3D2E1F0U;
	Uint64 size_in_bits = (Uint64)size << 3;
	// Process full chunks (if any)
	while (size >= 64) {	// 64 bytes = 512 bits
		sha1_chunk(hash, data);
		data += 64;
		size -= 64;
	}
	// Process remaining sub-chunk + SHA specific additions: it may result in two chunks, actually!
	Uint8 tail[128], *tail_p;
	memset(tail, 0, sizeof tail);
	if (size)
		memcpy(tail, data, size);
	tail[size++] = 0x80;	// append '1' bit at the end of message
	if (size > 64 - 8) {
		// Needs an additional chunk
		sha1_chunk(hash, tail);
		tail_p = tail + 64;
	} else
		tail_p = tail;
	// Last chunk, with size information at the end
	for (unsigned int i = 63; i >= 56; i--) {
		tail_p[i] = size_in_bits & 0xFF;
		size_in_bits >>= 8;
	}
	sha1_chunk(hash, tail_p);
}


void sha1_checksum_as_bytes ( sha1_hash_bytes hash_bytes, const Uint8 *data, Uint32 size )
{
	Uint32 hash[5];
	sha1_checksum_as_words(hash, data, size);
	for (unsigned int i = 0; i < 5; i++) {
		*hash_bytes++ = (hash[i] >> 24) & 0xFF;
		*hash_bytes++ = (hash[i] >> 16) & 0xFF;
		*hash_bytes++ = (hash[i] >>  8) & 0xFF;
		*hash_bytes++ = (hash[i]      ) & 0xFF;
	}
}


void sha1_checksum_as_string ( sha1_hash_str hash_str, const Uint8 *data, Uint32 size )
{
	Uint32 hash[5];
	sha1_checksum_as_words(hash, data, size);
	sprintf(hash_str, "%08x%08x%08x%08x%08x", hash[0], hash[1], hash[2], hash[3], hash[4]);
}


#if defined(XEMU_ARCH_WIN64) && defined(_USE_32BIT_TIME_T)
#error "_USE_32BIT_TIME_T is defined while compiling for 64-bit Windows!"
#endif
