/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2024 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_INJECT_H_INCLUDED
#define XEMU_MEGA65_INJECT_H_INCLUDED

extern void inject_ready_check_do        ( void );
extern int  inject_register_ready_status ( const char *debug_msg, void (*callback)(void*), void *userdata );

extern int  inject_register_prg          ( const char *prg_file, const int prg_mode, const bool startup );
extern void inject_register_allow_disk_access ( void );
extern void inject_register_command      ( const char *s );
extern int  inject_register_import_basic_text ( const char *fn );
extern int  inject_hwa_pasting ( char *s, const int single_case );

#endif
