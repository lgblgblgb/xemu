/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_COMMON_BASIC_TEXT_H_INCLUDED
#define XEMU_COMMON_BASIC_TEXT_H_INCLUDED

#ifdef BASIC_TEXT_SUPPORT

#define BASIC_TO_TEXT_FLAG_TEX	1

extern int xemu_basic_to_text_malloc ( Uint8 **buffer, int output_super_limit, const Uint8 *prg, int real_addr, const Uint8 *prg_limit, int basic_dialect, int flags );
extern int xemu_basic_to_text ( Uint8 *output, int output_size, const Uint8 *prg, int real_addr, const Uint8 *prg_limit, int basic_dialect, int flags );

#endif

#endif
