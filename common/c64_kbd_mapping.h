/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and some Mega-65 features as well.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef __XEMU_COMMON_C64_KBD_MAPPING
#define __XEMU_COMMON_C64_KBD_MAPPING

// Keyboard position of "shift" which is "virtually pressed" ie for cursor up/left
#define VIRTUAL_SHIFT_POS	0x64

// Emulate joystick via a "virtual keyboard matrix row"
#define HID_JOY_EMU_ROW		0xF
#define HID_JOY_EMU_LEFT_BPOS	2
#define HID_JOY_EMU_RIGHT_BPOS	3
#define HID_JOY_EMU_UP_BPOS	0
#define HID_JOY_EMU_DOWN_BPOS	1
#define HID_JOY_EMU_FIRE_BPOS	4
#define HID_JOY_EMU_UNUSED_MASK	0xE0

extern const struct KeyMapping c64_key_map[];

extern Uint8 c64_get_joy_state ( void );

#endif
