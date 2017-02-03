/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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

#ifndef __XEP128_KEYBOARD_MAPPING_H_INCLUDED
#define __XEP128_KEYBOARD_MAPPING_H_INCLUDED

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
