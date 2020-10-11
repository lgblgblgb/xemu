/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_EP128_SCREEN_H_INCLUDED
#define XEMU_EP128_SCREEN_H_INCLUDED

#include <SDL_video.h>
#ifdef XEP128_NEED_SDL_WMINFO
#	include <SDL_syswm.h>
#endif

#define SCREEN_WIDTH	736
#define SCREEN_HEIGHT	288
#ifdef __EMSCRIPTEN__
//#define SCREEN_FORMAT	SDL_PIXELFORMAT_ABGR8888
#define SCREEN_FORMAT   SDL_PIXELFORMAT_ARGB8888
#else
#define SCREEN_FORMAT	SDL_PIXELFORMAT_ARGB8888
#endif

#define OSD_FADE_START	300
#define OSD_FADE_STOP	0x80
#define OSD_FADE_DEC	3

extern int is_fullscreen, warn_for_mouse_grab;
extern SDL_Window *sdl_win;
extern SDL_PixelFormat *sdl_pixel_format;
#ifdef XEP128_NEED_SDL_WMINFO
extern SDL_SysWMinfo sdl_wminfo;
#endif
extern Uint32 sdl_winid;

extern int  _sdl_emu_secured_message_box_ ( Uint32 sdlflag, const char *msg );
extern int  _sdl_emu_secured_modal_box_ ( const char *items_in, const char *msg );

extern void screen_grab ( SDL_bool state );
extern void screen_window_resized ( int new_xsize, int new_ysize );
extern void screen_set_fullscreen ( int state );
extern void screen_present_frame (Uint32 *ep_pixels);
extern int  screen_shot ( Uint32 *ep_pixels, const char *directory, const char *filename );
extern int  screen_init ( void );

extern void sdl_burn_events ( void );

extern void osd_disable ( void );
extern void osd_clear ( void );
extern void osd_update ( void );
extern void osd_write_char ( int x, int y, char ch );
extern void osd_write_string ( int x, int y, const char *s );
extern void osd_write_string_centered ( int y, const char *s );
extern void osd_notification ( const char *s );
extern void osd_replay ( int fade );

#endif
