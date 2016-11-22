/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and some Mega-65 features as well.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   The goal of emutools.c is to provide a relative simple solution
   for relative simple emulators using SDL2.

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

#ifndef __XEMU_COMMON_EMUTOOLS_H_INCLUDED
#define __XEMU_COMMON_EMUTOOLS_H_INCLUDED

#include <SDL.h>
#include "xemu/emutools_basicdefs.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EMSCRIPTEN_SDL_BASE_DIR "/files/"
#define XEMUEXIT(n)	do { emscripten_cancel_main_loop(); emscripten_force_exit(n); exit(n); } while (0)
#define MSG_POPUP_WINDOW(sdlflag, title, msg, win) \
	do { if (1 || sdlflag == SDL_MESSAGEBOX_ERROR) { EM_ASM_INT({ window.alert(Pointer_stringify($0)); }, msg); } } while(0)
#else
#define XEMUEXIT(n)	exit(n)
#define MSG_POPUP_WINDOW(sdlflag, title, msg, win) SDL_ShowSimpleMessageBox(sdlflag, title, msg, win)
#endif

#define APP_ORG "xemu-lgb"
#define APP_DESC_APPEND " / LGB"

// You should define this in your emulator, most probably with resetting the keyboard matrix
// Purpose: emulator windows my cause the emulator does not get the key event normally, thus some keys "seems to be stucked"
extern void clear_emu_events ( void );

extern void emu_drop_events ( void );

#define _REPORT_WINDOW_(sdlflag, str, ...) do { \
	char _buf_for_win_msg_[4096]; \
	snprintf(_buf_for_win_msg_, sizeof _buf_for_win_msg_, __VA_ARGS__); \
	fprintf(stderr, str ": %s" NL, _buf_for_win_msg_); \
	if (debug_fp)	\
		fprintf(debug_fp, str ": %s" NL, _buf_for_win_msg_);	\
	MSG_POPUP_WINDOW(sdlflag, sdl_window_title, _buf_for_win_msg_, sdl_win); \
	clear_emu_events(); \
	emu_drop_events(); \
	SDL_RaiseWindow(sdl_win); \
	emu_timekeeping_start(); \
} while (0)

#define INFO_WINDOW(...)	_REPORT_WINDOW_(SDL_MESSAGEBOX_INFORMATION, "INFO", __VA_ARGS__)
#define WARNING_WINDOW(...)	_REPORT_WINDOW_(SDL_MESSAGEBOX_WARNING, "WARNING", __VA_ARGS__)
#define ERROR_WINDOW(...)	_REPORT_WINDOW_(SDL_MESSAGEBOX_ERROR, "ERROR", __VA_ARGS__)

#define FATAL(...) do { \
	ERROR_WINDOW(__VA_ARGS__); \
	XEMUEXIT(1); \
} while (0)

extern int _sdl_emu_secured_modal_box_ ( const char *items_in, const char *msg );
#define QUESTION_WINDOW(items, msg) _sdl_emu_secured_modal_box_(items, msg)

extern char *sdl_window_title;
extern char *window_title_custom_addon;
extern SDL_Window   *sdl_win;
extern Uint32 sdl_winid;
extern SDL_PixelFormat *sdl_pix_fmt;
extern int seconds_timer_trigger;
extern const char emulators_disclaimer[];
extern char *sdl_pref_dir, *sdl_base_dir;

extern int emu_init_debug ( const char *fn );
extern time_t emu_get_unixtime ( void );
extern struct tm *emu_get_localtime ( void );
extern void *emu_malloc ( size_t size );
extern void *emu_realloc ( void *p, size_t size );
#ifdef __EMSCRIPTEN__
#define emu_malloc_ALIGNED emu_malloc
#else
extern void *emu_malloc_ALIGNED ( size_t size );
#endif

extern char *emu_strdup ( const char *s );
extern int emu_load_file ( const char *fn, void *buffer, int maxsize );
extern void emu_set_full_screen ( int setting );
extern void emu_timekeeping_delay ( int td_em );
extern int emu_init_sdl (
        const char *window_title,               // title of our window
        const char *app_organization,           // organization produced the application, used with SDL_GetPrefPath()
        const char *app_name,                   // name of the application, used with SDL_GetPrefPath()
        int is_resizable,                       // allow window resize? [0 = no]
        int texture_x_size, int texture_y_size, // raw size of texture (in pixels)
        int logical_x_size, int logical_y_size, // "logical" size in pixels, ie to correct aspect ratio, etc, can be the as texture of course, if it's OK ...
        int win_x_size, int win_y_size,         // default window size, in pixels [note: if logical/texture size combo does not match in ratio with this, black stripes you will see ...]
        Uint32 pixel_format,                    // SDL pixel format we want to use (an SDL constant, like SDL_PIXELFORMAT_ARGB8888) Note: it can gave serve impact on performance, ARGB8888 recommended
        int n_colours,                          // number of colours emulator wants to use
        const Uint8 *colours,                   // RGB components of each colours, we need 3 * n_colours bytes to be passed!
        Uint32 *store_palette,                  // this will be filled with generated palette, n_colours Uint32 values will be placed
        int render_scale_quality,               // render scale quality, must be 0, 1 or 2 _ONLY_
        int locked_texture_update,              // use locked texture method [non zero], or malloc'ed stuff [zero]. NOTE: locked access doesn't allow to _READ_ pixels and you must fill ALL pixels!
        void (*shutdown_callback)(void)         // callback function called on exit (can be nULL to not have any emulator specific stuff)
);
extern void emu_timekeeping_start ( void );
extern void emu_render_dummy_frame ( Uint32 colour, int texture_x_size, int texture_y_size );
extern Uint32 *emu_start_pixel_buffer_access ( int *texture_tail );
extern void emu_update_screen ( void );

extern int  osd_init ( int xsize, int ysize, const Uint8 *palette, int palette_entries, int fade_dec, int fade_end );
extern int  osd_init_with_defaults ( void );
extern void osd_clear ( void );
extern void osd_update ( void );
extern void osd_on ( int value );
extern void osd_off ( void );
extern void osd_global_enable ( int status );
extern void osd_set_colours ( int fg_index, int bg_index );
extern void osd_write_char ( int x, int y, char ch );
extern void osd_write_string ( int x, int y, const char *s );

#define OSD_STATIC 0x1000

#endif
