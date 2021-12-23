/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/cpu65.h"
#define  XEMU_MEGA65_HDOS_H_ALLOWED
#include "hdos.h"
#include "hypervisor.h"
#include "memory_mapper.h"

#include <sys/stat.h>
#include <sys/types.h>


#if 0
#include "xemu/emutools_files.h"
#include "mega65.h"
#include "hypervisor.h"
#include "vic4.h"
#include "dma65.h"
#include "io_mapper.h"
#include "xemu/emutools_config.h"
#include "configdb.h"
#include "rom.h"

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif

static struct {
	Uint8 func;
	const char *func_desc;
	Uint8 in_x, in_y, in_z;
	char  setnam_fn[64];
	const char *rootdir;
} hdos;


//#define DEBUGHDOS(...) DEBUG(__VA_ARGS__)
#define DEBUGHDOS(...) DEBUGPRINT(__VA_ARGS__)



static const char *get_func_name ( const int func_no )
{
	static const char INVALID_SUBFUNCTION[] = "invalid_subfunction!";
	if (XEMU_UNLIKELY(func_no & 1))
		goto fatal;
	// the shift: DOS functions are EVEN numbers only.
	// however for more efficient switch-case we shift right by one for decoding purposes only, here!
	switch (func_no >> 1) {
		case 0x00 >> 1: return "getversion";
		case 0x02 >> 1: return "getdefaultdrive";
		case 0x04 >> 1: return "getcurrentdrive";
		case 0x06 >> 1: return "selectdrive";
		case 0x08 >> 1: return "getdisksize [UNIMPLEMENTED]";
		case 0x0A >> 1: return "getcwd [UNIMPLEMENTED]";
		case 0x0C >> 1: return "chdir";
		case 0x0E >> 1: return "mkdir [UNIMPLEMENTED]";
		case 0x10 >> 1: return "rmdir [UNIMPLEMENTED]";
		case 0x12 >> 1: return "opendir";
		case 0x14 >> 1: return "readdir";
		case 0x16 >> 1: return "closedir";
		case 0x18 >> 1: return "openfile";
		case 0x1A >> 1: return "readfile";
		case 0x1C >> 1: return "writefile [UNIMPLEMENTED]";
		case 0x1E >> 1: return "mkfile [WIP]";
		case 0x20 >> 1: return "closefile";
		case 0x22 >> 1: return "closeall";
		case 0x24 >> 1: return "seekfile [UNIMPLEMENTED]";
		case 0x26 >> 1: return "rmfile [UNIMPLEMENTED]";
		case 0x28 >> 1: return "fstat [UNIMPLEMENTED]";
		case 0x2A >> 1: return "rename [UNIMPLEMENTED]";
		case 0x2C >> 1: return "filedate [UNIMPLEMENTED]";
		case 0x2E >> 1: return "setname";
		case 0x30 >> 1: return "findfirst";
		case 0x32 >> 1: return "findnext";
		case 0x34 >> 1: return "findfile";
		case 0x36 >> 1: return "loadfile";
		case 0x38 >> 1: return "geterrorcode";
		case 0x3A >> 1: return "setup_transfer_area";
		case 0x3C >> 1: return "cdrootdir";
		case 0x3E >> 1: return "loadfile_attic";
		case 0x40 >> 1: return "d81attach0";
		case 0x42 >> 1: return "d81detach";
		case 0x44 >> 1: return "d81write_en";
		case 0x46 >> 1: return "d81attach1";
		case 0x48 >> 1: return "get_proc_desc";
		case 0x4A >> 1: return INVALID_SUBFUNCTION;
		case 0x4C >> 1: return INVALID_SUBFUNCTION;
		case 0x4E >> 1: return INVALID_SUBFUNCTION;
		case 0x50 >> 1: return "gettasklist [UNIMPLEMENTED]";
		case 0x52 >> 1: return "sendmessage [UNIMPLEMENTED]";
		case 0x54 >> 1: return "receivemessage [UNIMPLEMENTED]";
		case 0x56 >> 1: return "writeintotask [UNIMPLEMENTED]";
		case 0x58 >> 1: return "readoutoftask [UNIMPLEMENTED]";
		case 0x5A >> 1: return INVALID_SUBFUNCTION;
		case 0x5C >> 1: return INVALID_SUBFUNCTION;
		case 0x5E >> 1: return INVALID_SUBFUNCTION;
		case 0x60 >> 1: return "terminateothertask [UNIMPLEMENTED]";
		case 0x62 >> 1: return "create_task_native [UNIMPLEMENTED]";
		case 0x64 >> 1: return "load_into_task [UNIMPLEMENTED]";
		case 0x66 >> 1: return "create_task_c64 [UNIMPLEMENTED]";
		case 0x68 >> 1: return "create_task_c65 [UNIMPLEMENTED]";
		case 0x6A >> 1: return "exit_and_switch_to_task [UNIMPLEMENTED]";
		case 0x6C >> 1: return "switch_to_task [UNIMPLEMENTED]";
		case 0x6E >> 1: return "exit_task [UNIMPLEMENTED]";
		case 0x70 >> 1: return "trap_task_toggle_rom_writeprotect";
		case 0x72 >> 1: return "trap_task_toggle_force_4502";
		case 0x74 >> 1: return "trap_task_get_mapping";
		case 0x76 >> 1: return "trap_task_set_mapping";
		case 0x78 >> 1: return INVALID_SUBFUNCTION;
		case 0x7A >> 1: return INVALID_SUBFUNCTION;
		case 0x7C >> 1: return "trap_serial_monitor_write";
		case 0x7E >> 1: return "reset_entry";
	}
fatal:
	FATAL("%s(%d) invalid DOS trap function number", __func__, func_no);
	return "?";
}


static int copy_mem_from_user ( Uint8 *target, int max_size, const int terminator_byte, unsigned int source_cpu_addr )
{
	int len = 0;
	if (terminator_byte >= 0)
		max_size--;
	for (;;) {
		// DOS calls should not have user specified data >= $8000!
		if (source_cpu_addr >= 0x8000)
			return -1;
		const Uint8 byte = memory_debug_read_cpu_addr(source_cpu_addr++);
		*target++ = byte;
		len++;
		if (len >= max_size) {
			if (terminator_byte >= 0)
				*target = terminator_byte;
			break;
		}
		if ((int)byte == terminator_byte)
			break;
	}
	return len;
}


static int copy_string_from_user ( char *target, const int max_size, unsigned int source_cpu_addr )
{
	return copy_mem_from_user((Uint8*)target, max_size, 0, source_cpu_addr);
}


void hdos_enter ( const Uint8 func_no )
{
	hdos.func = func_no;
	hdos.func_desc = get_func_name(hdos.func);
	hdos.in_x = cpu65.x;
	hdos.in_y = cpu65.y;
	hdos.in_z = cpu65.z;
	DEBUGHDOS("HDOS: entering function #$%02X (%s)" NL, hdos.func, hdos.func_desc);
}


void hdos_leave ( const Uint8 func_no )
{
	hdos.func = func_no;
	hdos.func_desc = get_func_name(hdos.func);
	DEBUGHDOS("HDOS: leaving function #$%02X (%s)" NL, hdos.func, hdos.func_desc);
	if (hdos.func == 0x2E && cpu65.pf_c) {	// HDOS setnam function. Also check carry set (which means "ok" by HDOS trap)
		if (copy_string_from_user(hdos.setnam_fn, sizeof hdos.setnam_fn, hdos.in_x + (hdos.in_y << 8)) >= 0)
			for (char *p = hdos.setnam_fn; *p; p++) {
				if (*p < 0x20 || *p >= 0x7F) {
					DEBUGHDOS("HDOS: setnam(): invalid character in filename $%02X" NL, *p);
					hdos.setnam_fn[0] = '\0';
					break;
				}
				if (*p >= 'A' && *p <= 'Z')
					*p = *p - 'A' + 'a';
			}
		DEBUGHDOS("HDOS: setnam(): selected filename is [%s]" NL, hdos.setnam_fn);
		return;
	}
	if (hdos.func == 0x40) {
		DEBUGHDOS("HDOS: d81attach(): setnam=\"%s\", result=%s" NL, hdos.setnam_fn, cpu65.pf_c ? "OK" : "FAILED");
		return;
	}
}


void hdos_reset ( void )
{
	DEBUGHDOS("HDOS: reset event." NL);
	hdos.setnam_fn[0] = '\0';
	hdos.func = -1;
}


void hdos_init ( const int do_virt, const char *virtroot )
{
	hdos_reset();
	if (do_virt) {
		// Setup root for the "faked" HDOS traps
		// First build our default one. We may need this anyway, if overriden one invalid
		char dir[PATH_MAX + 1];
		strcpy(dir, sdl_pref_dir);
		strcat(dir, "hdosroot" DIRSEP_STR);
		MKDIR(dir);	// try to make that directory, maybe hasn't existed yet
		if (virtroot && *virtroot) {
			XDIR *dirhandle = xemu_os_opendir(virtroot);
			if (dirhandle) {
				xemu_os_closedir(dirhandle);
				strcpy(dir, virtroot);
				if (dir[strlen(dir) - 1] != DIRSEP_CHR)
					strcat(dir, DIRSEP_STR);
			} else
				ERROR_WINDOW("HDOS: bad HDOS virtual directory given (cannot open as directory): %s\nUsing the default instead: %s", virtroot, dir);
		}
		hdos.rootdir = xemu_strdup(dir);
		DEBUGPRINT("HDOS: virtual directory is %s" NL, hdos.rootdir);
	} else {
		hdos.rootdir = NULL;
		DEBUGPRINT("HDOS: trap virtualization is not enabled." NL);
	}
}

