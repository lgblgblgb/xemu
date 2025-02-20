/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_HYPERVISOR_H_INCLUDED
#define XEMU_MEGA65_HYPERVISOR_H_INCLUDED

#define TRAP_DOS			0x00
#define TRAP_XEMU			0x04
#define TRAP_FREEZER_USER_CALL		0x3F
#define TRAP_RESET			0x40
#define TRAP_FREEZER_RESTORE_PRESS	0x42
#define TRAP_MATRIX			0x43

extern bool in_hypervisor;
extern int  hickup_is_overriden;
extern int  hypervisor_is_debugged;
extern char hyppo_version_string[64];

extern int  hypervisor_debug_init ( const char *fn, int hypervisor_debug, int use_hypervisor_serial_out_asciizer );
extern void hypervisor_debug ( void );

extern void hypervisor_enter ( int trapno );
extern void hypervisor_enter_via_write_trap ( int trapno );
extern int  hypervisor_queued_enter ( int trapno );
extern void hypervisor_start_machine ( void );
extern void hypervisor_leave ( void );
extern void hypervisor_serial_monitor_push_char ( Uint8 chr );
extern void hypervisor_serial_monitor_open_file ( const char *fn );
extern void hypervisor_serial_monitor_close_file ( const char *fn );
extern void hypervisor_debug_invalidate ( const char *reason );
extern void hypervisor_debug_late_enable ( void );
extern int  hypervisor_hdos_virtualization_status ( const int set, const char **root_ptr );	// prototype is here, but it's implemented in hdos.c not in hypervisor.c
extern void hypervisor_hdos_close_descriptors ( void );						// prototype is here, but it's implemented in hdos.c not in hypervisor.c
extern char*hypervisor_hdos_get_sysfile_path ( const char *fn );				// prototype is here, but it's implemented in hdos.c not in hypervisor.c

#endif
