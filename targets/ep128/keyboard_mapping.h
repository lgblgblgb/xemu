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

#ifndef XEMU_EP128_KEYBOARD_MAPPING_H_INCLUDED
#define XEMU_EP128_KEYBOARD_MAPPING_H_INCLUDED

#include <SDL_keyboard.h>

struct keyMappingTable_st {
	SDL_Scancode	code;
	Uint8		posep;
	const char	*description;
};

extern const struct keyMappingTable_st *keymap_resolve_event ( SDL_Keysym sym );
extern void keymap_preinit_config_internal ( void );
extern void keymap_dump_config ( FILE *f );
extern int keymap_set_key_by_name ( const char *name, int posep );

#endif
