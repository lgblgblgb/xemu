/* Xemu - emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


extern int  osd_status;
extern void (*osd_update_callback)(void);
extern int  osd_notifications_enabled;
#ifdef XEMU_OSD_EXPORT_FONT
extern const Uint16 font_16x16[];
#endif

extern int  osd_init ( int xsize, int ysize, const Uint8 *palette, int palette_entries, int fade_dec, int fade_end );
extern int  osd_init_with_defaults ( void );
extern void osd_clear ( void );
extern void osd_clear_with_colour ( const int index );
extern void osd_texture_update ( const SDL_Rect *rect );
extern void osd_on ( int value );
extern void osd_off ( void );
extern void osd_global_enable ( int status );
extern void osd_set_colours ( int fg_index, int bg_index );
extern void osd_write_char ( int x, int y, char ch );
extern void osd_write_string ( int x, int y, const char *s );
extern void osd_hijack ( void(*updater)(void), int *xsize_ptr, int *ysize_ptr, Uint32 **pixel_ptr );

#define OSD_STATIC		0x1000
#define OSD_FADE_START		300
#define OSD_FADE_DEC_VAL	5
#define OSD_FADE_END_VAL	0x20

#define OSD_TEXTURE_X_SIZE	640
#define OSD_TEXTURE_Y_SIZE	200


#define OSD(x, y, ...) do { \
	if (osd_notifications_enabled) { \
		char _buf_for_msg_[4096]; \
		CHECK_SNPRINTF(snprintf(_buf_for_msg_, sizeof _buf_for_msg_, __VA_ARGS__), sizeof _buf_for_msg_); \
		fprintf(stderr, "OSD: %s" NL, _buf_for_msg_); \
		osd_clear(); \
		osd_write_string(x, y, _buf_for_msg_); \
		osd_texture_update(NULL); \
		osd_on(OSD_FADE_START); \
	} \
} while(0)
