/* Test-case for a very simple and inaccurate Videoton TV computer
   (a Z80 based 8 bit computer) emulator using SDL2 library.

   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This emulator is HIGHLY inaccurate and unusable.

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

const struct KeyMapping tvc_key_map[] = {
	// Row 0
	{ SDL_SCANCODE_5,	0x00 },	// 5
	{ SDL_SCANCODE_3,	0x01 },	// 3
	{ SDL_SCANCODE_2,	0x02 }, // 2
	{ SDL_SCANCODE_GRAVE,	0x03 }, // 0	we relocate 0, to match the "imagined" Hungarian keyboard layout ...
	{ SDL_SCANCODE_6,	0x04 }, // 6
	{ 100,			0x05 },	// í	this is hard one, the scancode "100" is ISO keyboard, not ANSI and has different meaning on platforms ...
	{ SDL_SCANCODE_1,	0x06 }, // 1
	{ SDL_SCANCODE_4,	0x07 },	// 4
	// Row 1
	{ -1,			0x10 },	// ^	TODO!
	{ SDL_SCANCODE_8,	0x11 },	// 8
	{ SDL_SCANCODE_9,	0x12 },	// 9
	{ SDL_SCANCODE_MINUS,	0x13 },	// ü	Position on HUN kbd
	{ -1,			0x14 },	// *	TODO!
	{ SDL_SCANCODE_EQUALS,	0x15 },	// ó	Position on HUN kbd
	{ SDL_SCANCODE_0,	0x16 },	// ö	Position on HUN kbd
	{ SDL_SCANCODE_7,	0x17 },	// 7
	// Row 2
	{ SDL_SCANCODE_T,	0x20 },	// t
	{ SDL_SCANCODE_E,	0x21 },	// e
	{ SDL_SCANCODE_W,	0x22 },	// w
	{ -1,			0x23 },	// ;	TODO!
	{ SDL_SCANCODE_Z,	0x24 },	// z
	{ -1,			0x25 },	// @	TODO!
	{ SDL_SCANCODE_Q,	0x26 },	// q
	{ SDL_SCANCODE_R,	0x27 },	// r
	// Row 3
	{ -1,			0x30 },	// ]	TODO!	sadly, stupid HUN layout uses that at normal place :(
	{ SDL_SCANCODE_I,	0x31 },	// i
	{ SDL_SCANCODE_O,	0x32 },	// o
	{ SDL_SCANCODE_LEFTBRACKET,	0x33 },	// ő	on HUN kbd
	{ -1,			0x34 },	// [	TODO!	sadly, stupid HUN layout uses that at normal place :(
	{ SDL_SCANCODE_RIGHTBRACKET,	0x35 },	// ú	on HUN kbd
	{ SDL_SCANCODE_P,	0x36 },	// p
	{ SDL_SCANCODE_U,	0x37 },	// u
	// Row 4
	{ SDL_SCANCODE_G,	0x40 },	// g
	{ SDL_SCANCODE_D,	0x41 },	// d
	{ SDL_SCANCODE_S,	0x42 },	// s
	{ -1,			0x43 },	// blackslash	TODO!
	{ SDL_SCANCODE_H,	0x44 },	// h
	{ -1,			0x45 },	// <		TODO!
	{ SDL_SCANCODE_A,	0x46 },	// a
	{ SDL_SCANCODE_F,	0x47 },	// f
	// Row 5
	{ SDL_SCANCODE_BACKSPACE, 0x50 },	// DEL
	{ SDL_SCANCODE_K,	0x51 },	// k
	{ SDL_SCANCODE_L,	0x52 },	// l
	{ SDL_SCANCODE_APOSTROPHE,	0x53 },	// á	on HUN kbd
	{ SDL_SCANCODE_RETURN,	0x54 },	// RETURN
	{ SDL_SCANCODE_BACKSLASH,	0x55 },	// ű	on HUN kbd
	{ SDL_SCANCODE_SEMICOLON,	0x56 },	// é	on HUN kbd
	{ SDL_SCANCODE_J,	0x57 },	// j
	// Row 6
	{ SDL_SCANCODE_B,	0x60 },	// b
	{ SDL_SCANCODE_C,	0x61 },	// c
	{ SDL_SCANCODE_X,	0x62 },	// x
	{ SDL_SCANCODE_LSHIFT,	0x63 },	// SHIFT
	{ SDL_SCANCODE_RSHIFT,	0x63 },	// SHIFT (right shift is also shift ...)
	{ SDL_SCANCODE_N,	0x64 },	// n
	{ SDL_SCANCODE_TAB,	0x65 },	// LOCK
	{ SDL_SCANCODE_Y,	0x66 },	// y
	{ SDL_SCANCODE_V,	0x67 },	// v
	// Row 7
	{ SDL_SCANCODE_LALT,	0x70 },	// ALT
	{ SDL_SCANCODE_COMMA,	0x71 },	// ,?
	{ SDL_SCANCODE_PERIOD,	0x72 },	// .:
	{ SDL_SCANCODE_ESCAPE,	0x73 },	// ESC
	{ SDL_SCANCODE_LCTRL,	0x74 },	// CTRL
	{ SDL_SCANCODE_SPACE,	0x75 },	// SPACE
	{ SDL_SCANCODE_SLASH,	0x76 },	// -_
	{ SDL_SCANCODE_M,	0x77 },	// m
	// Row 8, has "only" cursor control (joy), _and_ INS
	{ SDL_SCANCODE_INSERT,	0x80 },	// INS
	{ SDL_SCANCODE_UP,	0x81 }, // cursor up
	{ SDL_SCANCODE_DOWN,	0x82 },	// cursor down
	{ SDL_SCANCODE_RIGHT,	0x85 }, // cursor right
	{ SDL_SCANCODE_LEFT,	0x86 }, // cursor left
	// Standard Xemu hot-keys, given by a macro
	STD_XEMU_SPECIAL_KEYS,
	// **** this must be the last line: end of mapping table ****
	{ 0, -1 }
};
