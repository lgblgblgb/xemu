/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2016,2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

#ifndef __XEP128_GUI_H_INCLUDED
#define __XEP128_GUI_H_INCLUDED

// FIXME: very ugly hack, EP128 emulator sill uses its own things, we have to deal with ...

#include "xemu/emutools_gui.h"

#define XEPGUI_FSEL_DIRECTORY		XEMUGUI_FSEL_DIRECTORY
#define XEPGUI_FSEL_OPEN		XEMUGUI_FSEL_OPEN
#define XEPGUI_FSEL_SAVE		XEMUGUI_FSEL_SAVE
#define XEPGUI_FSEL_FLAG_STORE_DIR	XEMUGUI_FSEL_FLAG_STORE_DIR

#define xepgui_init		xemugui_init
#define xepgui_iteration	xemugui_iteration
#define xepgui_file_selector	xemugui_file_selector

#endif
