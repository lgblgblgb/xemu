/* Re-CP/M: CP/M-like own implementation + Z80 emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_hid.h"
#include "console.h"
#include "hardware.h"
#include <string.h>


static struct {
	int	x,y;
	int	visible;
	int	phase;
	int	phase_counter;
	int	blinking;
	Uint8	text_color;
	Uint8	cursor_color;	// only low 4 bits!
} cursor;
static int console_width, console_height;
static Uint8 *video_ram;
static Uint8 *color_ram;
static Uint32 palette[16];
#define	CHARACTER_SET_DEFINER_8X16 static const Uint8 chargen[]
#include "xemu/vgafonts.c"
#undef	CHARACTER_SET_DEFINER_8X16
static const Uint8 console_colors[3*16] = {	// FIXME
	0x00, 0x00, 0x00,	// black
	0xFF, 0xFF, 0xFF,	// white
	0xF0, 0x00, 0x00,	// red
	0x00, 0xF0, 0xF0,	// cyan
	0x60, 0x00, 0x60,	// purple
	0x00, 0xA0, 0x00,	// green
	0x00, 0x00, 0xF0,	// blue
	0xD0, 0xD0, 0x00,	// yellow
	0xC0, 0xA0, 0x00,	// orange
	0xFF, 0xA0, 0x00,	// light orange
	0xF0, 0x80, 0x80,	// pink
	0x00, 0xFF, 0xFF,	// light cyan
	0xFF, 0x00, 0xFF,	// light purple
	0x00, 0xFF, 0x00,	// light green
	0x00, 0xA0, 0xFF,	// light blue
	0xFF, 0xFF, 0x00	// light yellow
};
static int   serial_delay;
static Uint8 kbd_queue[16];
static int   kbd_queue_len;
static int   kbd_waiting = 0;	// just an indicator [different cursor if we sense console input waiting from program]
static int   input_waiting;



static void conraw_clear ( void )
{
	memset(video_ram, 0x20, console_width * console_height);
	memset(color_ram, cursor.text_color, console_width * console_height);
	cursor.x = 0;
	cursor.y = 0;
}

static void conraw_scroll ( void )
{
	memmove(video_ram, video_ram + console_width, console_width * (console_height - 1));
	memmove(color_ram, color_ram + console_width, console_width * (console_height - 1));
	memset(video_ram + console_width * (console_height - 1), 0x20, console_width);
	memset(color_ram + console_width * (console_height - 1), cursor.text_color, console_width);
}

static void conraw_down ( void )
{
	if (cursor.y == console_height - 1)
		conraw_scroll();
	else
		cursor.y++;
}

static void conraw_putch ( Uint8 data )
{
	video_ram[cursor.y * console_width + cursor.x] = data;
	color_ram[cursor.y * console_width + cursor.x] = cursor.text_color;
	if (cursor.x == console_width - 1) {
		cursor.x = 0;
		conraw_down();
	} else
		cursor.x++;
	emu_cost_usecs += serial_delay;
}


void console_output ( Uint8 data )
{
	// CRLF?
//	cursor.x = 0;
//	if (cursor.y == console_height - 1)

	if (data == 13) {
		cursor.x = 0;
	} else if (data == 10) {
		conraw_down();
	} else if (data == 8) {
		if (cursor.x)
			cursor.x--;
	} else if (data < 32) {
		conraw_putch('^');
		conraw_putch('A' + data);
	} else {
		conraw_putch(data);
	}
	// All output should make cursor phase 'shown' to avoid blinking during longer changes (ie, moving cursor around)
	cursor.phase = 1;
	cursor.phase_counter = 0;
}

void conputs ( const char *s )
{
	while (*s)
		console_output(*s++);
}


// 0=no char ready, ottherwise there is (actual BIOS implementation should have 0xFF for having character)
int console_status ( void )
{
	kbd_waiting = 1;
	return kbd_queue_len ? 0xFF : 0;
}

// Unlike CP/M BIOS console input, this can't wait here, since we have to give the control back
// Thus in case of no character is read, 0 is given back. It's the task of the caller to emulate
// the right BIOS functionality with this.
int console_input ( void )
{
	if (console_status()) {
		int ret = kbd_queue[0];
		if (--kbd_queue_len)
			memmove(kbd_queue, kbd_queue + 1, kbd_queue_len);
		emu_cost_usecs += serial_delay;
		return ret;
	} else {
		input_waiting = 1;
		return 0;
	}
}


void console_cursor_blink ( int delay )
{
	if (cursor.blinking) {
		if (cursor.phase_counter >= delay) {
			cursor.phase = !cursor.phase;
			cursor.phase_counter = 0;
		} else
			cursor.phase_counter++;
	} else
		cursor.phase = 1;
}

// This will render our screen, also calls SDL event loop ...
void console_iteration ( void )
{
	int vp = 0;
	int cursor_line = (cursor.phase && cursor.visible) ? cursor.y : -1;
	int tail;
	Uint32 *pixel = xemu_start_pixel_buffer_access(&tail);
	for (int y = 0; y < console_height; y++) {
		for (int row = 0; row < FONT_HEIGHT; row++) {
			for (int x = 0; x < console_width; x++) {
				Uint32 fg  = palette[color_ram[vp + x] & 0xF];
				Uint32 bg  = palette[color_ram[vp + x] >>  4];
				Uint8 chln = chargen[video_ram[vp + x] * FONT_HEIGHT + row];
				if (XEMU_UNLIKELY(cursor_line == y && cursor.x == x)) {
					//Uint32 temp = fg;
					//fg = bg;
					//bg = temp;
					if (row > 12 || kbd_waiting) {
						chln = 0xFF;
						fg = palette[cursor.cursor_color];
					}
				}
				for (int bpos = 0; bpos < 8; bpos++, chln <<= 1)
					*pixel++ = (chln & 0x80) ? fg : bg;
				*pixel++ = bg;	// the 9th pixel is always blank for now
			}
			pixel += tail;
		}
		vp += console_width;
	}
	xemu_update_screen();
	hid_handle_all_sdl_events();
	kbd_waiting = 0;
}


void clear_emu_events ( void )
{
	hid_reset_events(1);
}

// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	return 0;
}



static void queue_key ( Uint8 k )
{
	if (k == 8)
		k = 127;
	if (kbd_queue_len < sizeof kbd_queue)
		kbd_queue[kbd_queue_len++] = k;
	else
		DEBUGPRINT("KBD: keyboard queue is full :(" NL);
}




// We uses this actually. it needs global macro set: CONFIG_KBD_ALSO_RAW_SDL_CALLBACK
void emu_callback_key_raw_sdl ( SDL_KeyboardEvent *ev )
{
	if (ev->state == SDL_PRESSED) {
		int k = ev->keysym.sym;
		DEBUGPRINT("KEY: %d [%c]\n", k, (k >= 0x20 && k < 127) ? k : '?');
		// FIXME: we want to use text edit even instead, so we don't mess with the modifier keys / layouts ...
		// ... that would need though some Xemu core HID modifications to allow that.
		if (k < 32 || k == 127) {	// push key event
			queue_key(k);
		}
	}
}

#if 0
void emu_callback_key_texteditng_sdl ( SDL_TextEditingEvent *ev )
{
	DEBUGPRINT("TEXTEDITING: \"%s\"" NL, ev->text);
}
#endif

void emu_callback_key_textinput_sdl  ( SDL_TextInputEvent   *ev )
{
	DEBUGPRINT("TEXTINPUT: \"%s\"" NL, ev->text);
	Uint8 *p = (Uint8*)ev->text;
	while (*p) {
		if (*p >= 0x20 && *p < 127)
			queue_key(*p);
		p++;
	}
}


int console_init ( int width, int height, int zoom_percent, int *map_to_ram, int baud_emu )
{
	int screen_width = width * 9;
	int screen_height = height * FONT_HEIGHT;
	int window_width = screen_width * zoom_percent / 100;
	int window_height = screen_height * zoom_percent / 100;
	static const struct KeyMappingDefault matrix_key_map_def[] = {
		STD_XEMU_SPECIAL_KEYS,
		{ 0, -1 }
	};
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		screen_width, screen_height,	// texture sizes
		screen_width, screen_height,	// logical size
		window_width, window_height,	// window size
		SCREEN_FORMAT,			// pixel format
		16,				// we have 16 colours
		console_colors,			// initialize palette from this constant array
		palette,			// initialize palette into this stuff
		RENDER_SCALE_QUALITY,		// render scaling quality
		USE_LOCKED_TEXTURE,		// 1 = locked texture access
		recpm_shutdown_callback		// shutdown function
	))
		return 1;
	hid_init(
		matrix_key_map_def,
		0,				// virtual shift position for matrix emulation, but we don't use matrix stuff in re-CP/M
		SDL_DISABLE			// joystick events are not used in CP/M emu, currently
	);
	if (map_to_ram) {
		color_ram = memory + 0x10000 - width * height;
		video_ram = color_ram - width * height;
		*map_to_ram = video_ram - memory;
	} else {
		video_ram = xemu_malloc(width * height);
		color_ram = xemu_malloc(width * height);
	}
	if (baud_emu) {
		// Assuming 11 bit communication (8 bit + start bit + stop bit + parity bit)
		serial_delay = (11 * 1000000) / baud_emu;
	} else {
		serial_delay = 0;
	}
	console_width = width;
	console_height = height;
	DEBUGPRINT("CONSOLE: %dx%d characters, %dx%d pixels, serial delay is %d usecs" NL, width, height, screen_width, screen_height, serial_delay);
	kbd_queue_len = 0;
	cursor.text_color = 1;
	conraw_clear();
	cursor.visible = 1;
	cursor.blinking = 1;
	cursor.phase = 1;
	cursor.phase_counter = 0;
	cursor.cursor_color = 2;
	SDL_StartTextInput();
	return 0;
}
