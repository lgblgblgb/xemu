/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_COMMON_EMUTOOLS_H_INCLUDED
#define XEMU_COMMON_EMUTOOLS_H_INCLUDED

#ifdef XEMU_ARCH_OSX
// It seems SDL2 on OSX produces LOTS of warning because of usage 'memset_pattern4'.
// It seems SDL2 has a bug not including header string.h which is the place where that function is defined on OSX.
// Let's try to fix that, by manually including string.h here ...
#include <string.h>
#endif
#include <SDL.h>
#include "xemu/emutools_basicdefs.h"

#ifdef XEMU_ARCH_HTML
#include <emscripten.h>
#define EMSCRIPTEN_SDL_BASE_DIR "/files/"
#define MSG_POPUP_WINDOW(sdlflag, title, msg, win) \
	do { if (1 || sdlflag == SDL_MESSAGEBOX_ERROR) { EM_ASM_INT({ window.alert(Pointer_stringify($0)); }, msg); } } while(0)
#else
#define MSG_POPUP_WINDOW(sdlflag, title, msg, win) SDL_ShowSimpleMessageBox(sdlflag, title, msg, win)
#define INSTALL_DIRECTORY_ENTRY_NAME "default-files"
#endif

#ifdef XEMU_ARCH_MAC
extern int macos_gui_started;
#endif

#define APP_ORG "xemu-lgb"
#ifndef APP_DESC_APPEND
#define APP_DESC_APPEND " - Xemu"
#endif

#ifdef XEMU_ARCH_HTML
#define XEMU_MAIN_LOOP(func,p1,p2) emscripten_set_main_loop(func,p1,p2)
#else
#define XEMU_MAIN_LOOP(func,p1,p2) for (;;) func()
#endif

extern void sysconsole_open   ( void );
extern void sysconsole_close  ( const char *waitmsg );
extern int  sysconsole_toggle ( int set );

// You should define this in your emulator, most probably with resetting the keyboard matrix
// Purpose: emulator windows my cause the emulator does not get the key event normally, thus some keys "seems to be stucked"
extern void clear_emu_events ( void );

extern void xemu_drop_events ( void );

extern int  set_mouse_grab ( SDL_bool state );
extern SDL_bool is_mouse_grab ( void );
extern void save_mouse_grab ( void );
extern void restore_mouse_grab ( void );

static XEMU_INLINE int CHECK_SNPRINTF( int ret, int limit )
{
	if (ret < 0 || ret >= limit - 1) {
		fprintf(stderr, "SNPRINTF-ERROR: too long string or other error (ret=%d) ..." NL, ret);
		return -1;
	}
	return 0;
}

#define _REPORT_WINDOW_(sdlflag, str, ...) do { \
	char _buf_for_win_msg_[4096]; \
	CHECK_SNPRINTF(snprintf(_buf_for_win_msg_, sizeof _buf_for_win_msg_, __VA_ARGS__), sizeof _buf_for_win_msg_); \
	fprintf(stderr, str ": %s" NL, _buf_for_win_msg_); \
	if (debug_fp)	\
		fprintf(debug_fp, str ": %s" NL, _buf_for_win_msg_);	\
	if (sdl_win) { \
		save_mouse_grab(); \
		MSG_POPUP_WINDOW(sdlflag, sdl_window_title, _buf_for_win_msg_, sdl_win); \
		clear_emu_events(); \
		xemu_drop_events(); \
		SDL_RaiseWindow(sdl_win); \
		restore_mouse_grab(); \
		xemu_timekeeping_start(); \
	} else \
		MSG_POPUP_WINDOW(sdlflag, sdl_window_title, _buf_for_win_msg_, sdl_win); \
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

extern int i_am_sure_override;
extern const char *str_are_you_sure_to_exit;

#define ARE_YOU_SURE_OVERRIDE		1
#define ARE_YOU_SURE_DEFAULT_YES	2
#define ARE_YOU_SURE_DEFAULT_NO		4

extern int ARE_YOU_SURE ( const char *s, int flags );

extern char *sdl_window_title;
extern char *window_title_custom_addon;
extern char *window_title_info_addon;
extern SDL_Window   *sdl_win;
extern Uint32 sdl_winid;
extern SDL_PixelFormat *sdl_pix_fmt;
extern char *xemu_app_org, *xemu_app_name;
extern int seconds_timer_trigger;
extern char *sdl_pref_dir, *sdl_base_dir, *sdl_inst_dir;
extern int sysconsole_is_open;
extern int sdl_default_win_x_size, sdl_default_win_y_size;

extern int xemu_init_debug ( const char *fn );
extern time_t xemu_get_unixtime ( void );
extern struct tm *xemu_get_localtime ( void );
extern unsigned int xemu_get_microseconds ( void );
extern void *xemu_malloc ( size_t size );
extern void *xemu_realloc ( void *p, size_t size );

#if !defined(__EMSCRIPTEN__) && !defined(__arm__)
#define HAVE_MM_MALLOC
#endif

#ifdef HAVE_MM_MALLOC
extern void *xemu_malloc_ALIGNED ( size_t size );
#else
#define xemu_malloc_ALIGNED xemu_malloc
#endif

extern char *xemu_strdup ( const char *s );
extern void xemu_set_full_screen ( int setting );
extern void xemu_set_screen_mode ( int setting );
extern void xemu_set_scanlines ( int setting );
extern void xemu_timekeeping_delay ( int td_em );
extern void xemu_pre_init ( const char *app_organization, const char *app_name, const char *slogan );
extern int xemu_init_sdl ( void );
extern int xemu_post_init (
        const char *window_title,               // title of our window
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
extern int xemu_change_display_mode(
	int texture_x_size, int texture_y_size,	
	int logical_x_size, int logical_y_size,	
	int win_x_size, int win_y_size,
	Uint32 pixel_format,
	int locked_texture_update
);
extern void xemu_set_viewport(int left, int top, int right, int bottom, int adjust_window_size);
extern void xemu_clear_viewport();
extern int xemu_set_icon_from_xpm ( char *xpm[] );
extern void xemu_timekeeping_start ( void );
extern void xemu_render_dummy_frame ( Uint32 colour, int texture_x_size, int texture_y_size );
extern Uint32 *xemu_start_pixel_buffer_access ( int *texture_tail );
extern void xemu_update_screen ( void );

extern int  osd_status;
extern const Uint16 font_16x16[];

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

#define OSD_STATIC		0x1000
#define OSD_FADE_START		300
#define OSD_FADE_DEC_VAL	5
#define OSD_FADE_END_VAL	0x20

#define OSD_TEXTURE_X_SIZE	640
#define OSD_TEXTURE_Y_SIZE	200


#define OSD(x, y, ...) do { \
	char _buf_for_msg_[4096]; \
	CHECK_SNPRINTF(snprintf(_buf_for_msg_, sizeof _buf_for_msg_, __VA_ARGS__), sizeof _buf_for_msg_); \
	fprintf(stderr, "OSD: %s" NL, _buf_for_msg_); \
	osd_clear(); \
	osd_write_string(x, y, _buf_for_msg_); \
	osd_update(); \
	osd_on(OSD_FADE_START); \
} while(0)

#include <dirent.h>
#ifdef XEMU_ARCH_WIN
	typedef _WDIR XDIR;
	extern int   xemu_winos_utf8_to_wchar ( wchar_t *restrict o, const char *restrict i, size_t size );
	extern int   xemu_os_open   ( const char *fn, int flags );
	extern int   xemu_os_creat  ( const char *fn, int flags, int pmode );
	extern FILE *xemu_os_fopen  ( const char *restrict fn, const char *restrict mode );
	extern int   xemu_os_unlink ( const char *fn );
	extern int   xemu_os_mkdir  ( const char *fn, const int mode );
	extern XDIR *xemu_os_opendir ( const char *fn );
	extern struct dirent *xemu_os_readdir ( XDIR *dirp, struct dirent *entry );
	extern int   xemu_os_closedir ( XDIR *dir );
#else
	typedef	DIR	XDIR;
#	define	xemu_os_open			open
#	define	xemu_os_creat			creat
#	define	xemu_os_fopen			fopen
#	define	xemu_os_unlink			unlink
#	define	xemu_os_mkdir			mkdir
#	define	xemu_os_opendir			opendir
#	define	xemu_os_readdir(dirp,not_used)	readdir(dirp)
#	define	xemu_os_closedir 		closedir
#endif
#define	xemu_os_close	close

#endif
