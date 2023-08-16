/* Xemu - emulation (running on Linux/Unix/Windows/OSX, utilizing SDL2) of some
   8 bit machines, including the Commodore LCD and Commodore 65 and MEGA65 as well.
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include <SDL.h>
#include "xemu/emutools_basicdefs.h"

#ifndef XEMU_NO_SDL_DIALOG_OVERRIDE
extern int (*SDL_ShowSimpleMessageBox_custom)(Uint32, const char*, const char*, SDL_Window* );
extern int (*SDL_ShowMessageBox_custom)(const SDL_MessageBoxData*, int* );
#else
#define SDL_ShowSimpleMessageBox_custom SDL_ShowSimpleMessageBox
#define SDL_ShowMessageBox_custom	SDL_ShowMessageBox
#endif

#ifdef XEMU_ARCH_HTML
#include <emscripten.h>
#define EMSCRIPTEN_SDL_BASE_DIR "/files/"
#define MSG_POPUP_WINDOW(sdlflag, title, msg, win) \
	do { if (1 || sdlflag == SDL_MESSAGEBOX_ERROR) { EM_ASM_INT({ window.alert(Pointer_stringify($0)); }, msg); } } while(0)
#else
#define MSG_POPUP_WINDOW(sdlflag, title, msg, win) SDL_ShowSimpleMessageBox_custom(sdlflag, title, msg, win)
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

#define XEMU_CPU_STAT_INFO_BUFFER_SIZE 64
extern void xemu_get_timing_stat_string ( char *buf, unsigned int size );
extern const char *xemu_get_uname_string ( void );

// You should define this in your emulator, most probably with resetting the keyboard matrix
// Purpose: emulator windows my cause the emulator does not get the key event normally, thus some keys "seems to be stucked"
extern void clear_emu_events ( void );

extern void xemu_drop_events ( void );

extern int  set_mouse_grab ( SDL_bool state, int force_allow );
extern SDL_bool is_mouse_grab ( void );
extern void save_mouse_grab ( void );
extern void restore_mouse_grab ( void );

extern int allow_mouse_grab;

static XEMU_INLINE int CHECK_SNPRINTF( int ret, int limit )
{
	if (ret < 0 || ret >= limit - 1) {
		fprintf(stderr, "SNPRINTF-ERROR: too long string or other error (ret=%d) ..." NL, ret);
		return -1;
	}
	return 0;
}

extern int dialogs_allowed;

#define _REPORT_WINDOW_(sdlflag, str, ...) do { \
	char _buf_for_win_msg_[4096]; \
	CHECK_SNPRINTF(snprintf(_buf_for_win_msg_, sizeof _buf_for_win_msg_, __VA_ARGS__), sizeof _buf_for_win_msg_); \
	fprintf(stderr, str ": %s" NL, _buf_for_win_msg_); \
	if (debug_fp)	\
		fprintf(debug_fp, str ": %s" NL, _buf_for_win_msg_);	\
	if (dialogs_allowed) {	\
		if (sdl_win) {	\
			save_mouse_grab(); \
			MSG_POPUP_WINDOW(sdlflag, sdl_window_title, _buf_for_win_msg_, sdl_win); \
			clear_emu_events(); \
			xemu_drop_events(); \
			SDL_RaiseWindow(sdl_win); \
			restore_mouse_grab(); \
			xemu_timekeeping_start(); \
		} else \
			MSG_POPUP_WINDOW(sdlflag, sdl_window_title, _buf_for_win_msg_, sdl_win); \
	}	\
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

extern char **xemu_initial_argv;
extern int    xemu_initial_argc;
extern Uint64 buildinfo_cdate_uts;
extern const char *xemu_initial_cwd;
extern char *sdl_window_title;
extern char *window_title_custom_addon;
extern char *window_title_info_addon;
extern SDL_Window   *sdl_win;
extern Uint32 sdl_winid;
extern SDL_PixelFormat *sdl_pix_fmt;
extern int sdl_on_x11, sdl_on_wayland;
extern char *xemu_app_org, *xemu_app_name;
extern int seconds_timer_trigger;
extern char *sdl_pref_dir, *sdl_base_dir, *sdl_inst_dir;
extern int sysconsole_is_open;
extern int sdl_default_win_x_size, sdl_default_win_y_size;
extern int register_new_texture_creation;
extern SDL_version sdlver_compiled, sdlver_linked;
extern Uint32 *xemu_frame_pixel_access_p;
extern int emu_is_headless;
extern int emu_is_sleepless;
extern int emu_fs_is_utf8;

#define XEMU_VIEWPORT_ADJUST_LOGICAL_SIZE	1
//#define XEMU_VIEWPORT_WIN_SIZE_FOLLOW_LOGICAL	2

extern void xemu_set_viewport ( unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, unsigned int flags );
extern void xemu_get_viewport ( unsigned int *x1, unsigned int *y1, unsigned int *x2, unsigned int *y2 );

extern void xemu_window_snap_to_optimal_size ( int forced );

extern int xemu_init_debug ( const char *fn );
extern time_t xemu_get_unixtime ( void );
extern struct tm *xemu_get_localtime ( void );
extern Uint8 xemu_hour_to_bcd12h ( Uint8 hours, int hour_offset );
extern unsigned int xemu_get_microseconds ( void );
extern void *xemu_malloc ( size_t size );
extern void *xemu_realloc ( void *p, size_t size );

extern int xemu_is_first_time_user ( void );

#if !defined(XEMU_ARCH_HTML) && !defined(XEMU_CPU_ARM)
#define HAVE_MM_MALLOC
#endif

#ifdef HAVE_MM_MALLOC
extern void *xemu_malloc_ALIGNED ( size_t size );
#else
extern void *_xemu_malloc_ALIGNED_emulated ( size_t size );
#define xemu_malloc_ALIGNED _xemu_malloc_ALIGNED_emulated
#endif

extern const char EMPTY_STR[];
extern const int ZERO_INT;
extern const int ONE_INT;

extern char *xemu_strdup ( const char *s );
extern void xemu_restrdup ( char **ptr, const char *str );
extern void xemu_set_full_screen ( int setting );
extern void xemu_set_screen_mode ( int setting );
extern void xemu_timekeeping_delay ( int td_em );
extern void xemu_pre_init ( const char *app_organization, const char *app_name, const char *slogan, const int argc, char **argv );
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
extern int xemu_set_icon_from_xpm ( char *xpm[] );
extern void xemu_timekeeping_start ( void );
extern void xemu_sleepless_temporary_mode ( const int enable );
extern void xemu_render_dummy_frame ( Uint32 colour, int texture_x_size, int texture_y_size );
extern Uint32 *xemu_start_pixel_buffer_access ( int *texture_tail );
extern void xemu_update_screen ( void );


static XEMU_INLINE Uint16 xemu_u8p_to_u16le ( const Uint8 *const p ) {
	return p[0] | (p[1] << 8);
}
static XEMU_INLINE Uint32 xemu_u8p_to_u32le ( const Uint8 *const p ) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}
static XEMU_INLINE Uint64 xemu_u8p_to_u64le ( const Uint8 *const p ) {
	return (Uint64)p[0] | ((Uint64)p[1] << 8) | ((Uint64)p[2] << 16) | ((Uint64)p[3] << 24) | ((Uint64)p[4] << 32) | ((Uint64)p[5] << 40) | ((Uint64)p[6] << 48) | ((Uint64)p[7] << 56);
}
static XEMU_INLINE void xemu_u16le_to_u8p ( Uint8 *const p, const Uint16 data ) {
	p[0] = (data      ) & 0xFF;
	p[1] = (data >>  8) & 0xFF;
}
static XEMU_INLINE void xemu_u32le_to_u8p ( Uint8 *const p, const Uint32 data ) {
	p[0] = (data      ) & 0xFF;
	p[1] = (data >>  8) & 0xFF;
	p[2] = (data >> 16) & 0xFF;
	p[3] = (data >> 24) & 0xFF;
}
static XEMU_INLINE void xemu_u64le_to_u8p ( Uint8 *const p, const Uint64 data ) {
	p[0] = (data      ) & 0xFF;
	p[1] = (data >>  8) & 0xFF;
	p[2] = (data >> 16) & 0xFF;
	p[3] = (data >> 24) & 0xFF;
	p[4] = (data >> 32) & 0xFF;
	p[5] = (data >> 40) & 0xFF;
	p[6] = (data >> 48) & 0xFF;
	p[7] = (data >> 56) & 0xFF;
}

typedef char  sha1_hash_str[41];
typedef Uint8 sha1_hash_bytes[20];

extern void sha1_checksum_as_words ( Uint32 hash[5], const Uint8 *data, Uint32 size );
extern void sha1_checksum_as_bytes ( sha1_hash_bytes hash_bytes, const Uint8 *data, Uint32 size );
extern void sha1_checksum_as_string ( sha1_hash_str hash_str, const Uint8 *data, Uint32 size );

#if	defined(XEMU_OSD_SUPPORT)
// OSD support requested without defined XEMU_OSD_FONT8HEIGHT: fall back to use 8 pixel height font by default in this case
#	if	!defined(XEMU_OSD_FONT8HEIGHT)
#		define	XEMU_OSD_FONT8HEIGHT	8
#	endif
// Based on requested OSD font height set up some macros
#	if	XEMU_OSD_FONT8HEIGHT == 8
#		define	XEMU_VGA_FONT_8X8
#		define	XEMU_OSD_FONTBIN vga_font_8x8
#	elif	XEMU_OSD_FONT8HEIGHT == 14
#		define	XEMU_VGA_FONT_8X14
#		define	XEMU_OSD_FONTBIN vga_font_8x14
#	elif	XEMU_OSD_FONT8HEIGHT == 16
#		define	XEMU_VGA_FONT_8X16
#		define	XEMU_OSD_FONTBIN vga_font_8x16
#	else
#		error "XEMU_OSD_FONT8HEIGHT has been defined but has invalid numeric value!"
#	endif
#endif

#ifdef	XEMU_VGA_FONT_8X8
extern const Uint8 vga_font_8x8[256 *  8];
#endif
#ifdef	XEMU_VGA_FONT_8X14
extern const Uint8 vga_font_8x14[256 * 14];
#endif
#ifdef	XEMU_VGA_FONT_8X16
extern const Uint8 vga_font_8x16[256 * 16];
#endif

#ifdef XEMU_OSD_SUPPORT
#include "xemu/gui/osd.h"
#endif

#ifdef DEFINE_XEMU_OS_READDIR
#include <dirent.h>
extern int   xemu_readdir ( DIR *dirp, char *fn, const int fnmaxsize );
#endif

extern int   xemu_file_exists ( const char *fn );

#endif
