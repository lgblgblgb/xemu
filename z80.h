/* X-Emulators
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

#ifndef __XEMU_Z80_H_INCLUDED
#define __XEMU_Z80_H_INCLUDED

#include <SDL_types.h>

#define Z80EX_OPSTEP_FAST_AND_ROUGH
#define Z80EX_ED_TRAPPING_SUPPORT
#define Z80EX_CALLBACK_PROTOTYPE extern

#define Z80EX_TYPES_DEFINED
#define Z80EX_BYTE              Uint8
#define Z80EX_SIGNED_BYTE       Sint8
#define Z80EX_WORD              Uint16
#define Z80EX_DWORD             Uint32

#include "z80ex/z80ex.h"

extern Z80EX_CONTEXT z80ex;

#endif
