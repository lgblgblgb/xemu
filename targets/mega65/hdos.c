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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


// This source is meant to virtualize Hyppo's DOS functions to
// be able to access the host OS (what runs Xemu) filesystem
// via the normal HDOS calls (ie what would see the sd-card
// otherwise).

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
#include <unistd.h>
#include <errno.h>
#endif

static struct {
	Uint8 func;
	const char *func_name;
	Uint8 in_x, in_y, in_z;
	char  setnam_fn[64];
	const char *rootdir;
	int do_virt;
	int fd_tab[0x100];
	XDIR *dp_tab[0x100];
} hdos;

static struct {
	union {
		int fd;
		XDIR *dirp;
	};
	enum { HDOS_DESC_CLOSED, HDOS_DESC_FILE, HDOS_DESC_DIR } status;
} desc_table[0x100];



//#define DEBUGHDOS(...) DEBUG(__VA_ARGS__)
#define DEBUGHDOS(...) DEBUGPRINT(__VA_ARGS__)



static const char *hdos_get_func_name ( const int func_no )
{
	if (XEMU_UNLIKELY((func_no & 1) || (unsigned int)func_no >= 0x80U)) {
		FATAL("%s(%d) invalid DOS trap function number", __func__, func_no);
		return "?";
	}
	// DOS function names are from hyppo's asm source from mega65-core repository
	// It should be updated if there is a change there. Also maybe at other parts of
	// this source too, to follow the ABI of the Hyppo-DOS here too.
	static const char INVALID_SUBFUNCTION[] = "INVALID_DOS_FUNC";
	static const char *func_names[] = {
		"getversion",					// 00
		"getdefaultdrive",				// 02
		"getcurrentdrive",				// 04
		"selectdrive",					// 06
		"getdisksize [UNIMPLEMENTED]",			// 08
		"getcwd [UNIMPLEMENTED]",			// 0A
		"chdir",					// 0C
		"mkdir [UNIMPLEMENTED]",			// 0E
		"rmdir [UNIMPLEMENTED]",			// 10
		"opendir",					// 12
		"readdir",					// 14
		"closedir",					// 16
		"openfile",					// 18
		"readfile",					// 1A
		"writefile [UNIMPLEMENTED]",			// 1C
		"mkfile [WIP]",					// 1E
		"closefile",					// 20
		"closeall",					// 22
		"seekfile [UNIMPLEMENTED]",			// 24
		"rmfile [UNIMPLEMENTED]",			// 26
		"fstat [UNIMPLEMENTED]",			// 28
		"rename [UNIMPLEMENTED]",			// 2A
		"filedate [UNIMPLEMENTED]",			// 2C
		"setname",					// 2E
		"findfirst",					// 30
		"findnext",					// 32
		"findfile",					// 34
		"loadfile",					// 36
		"geterrorcode",					// 38
		"setup_transfer_area",				// 3A
		"cdrootdir",					// 3C
		"loadfile_attic",				// 3E
		"d81attach0",					// 40
		"d81detach",					// 42
		"d81write_en",					// 44
		"d81attach1",					// 46
		"get_proc_desc",				// 48
		INVALID_SUBFUNCTION,				// 4A
		INVALID_SUBFUNCTION,				// 4C
		INVALID_SUBFUNCTION,				// 4E
		"gettasklist [UNIMPLEMENTED]",			// 50
		"sendmessage [UNIMPLEMENTED]",			// 52
		"receivemessage [UNIMPLEMENTED]",		// 54
		"writeintotask [UNIMPLEMENTED]",		// 56
		"readoutoftask [UNIMPLEMENTED]",		// 58
		INVALID_SUBFUNCTION,				// 5A
		INVALID_SUBFUNCTION,				// 5C
		INVALID_SUBFUNCTION,				// 5E
		"terminateothertask [UNIMPLEMENTED]",		// 60
		"create_task_native [UNIMPLEMENTED]",		// 62
		"load_into_task [UNIMPLEMENTED]",		// 64
		"create_task_c64 [UNIMPLEMENTED]",		// 66
		"create_task_c65 [UNIMPLEMENTED]",		// 68
		"exit_and_switch_to_task [UNIMPLEMENTED]",	// 6A
		"switch_to_task [UNIMPLEMENTED]",		// 6C
		"exit_task [UNIMPLEMENTED]",			// 6E
		"trap_task_toggle_rom_writeprotect",		// 70
		"trap_task_toggle_force_4502",			// 72
		"trap_task_get_mapping",			// 74
		"trap_task_set_mapping",			// 76
		INVALID_SUBFUNCTION,				// 78
		INVALID_SUBFUNCTION,				// 7A
		"trap_serial_monitor_write",			// 7C
		"reset_entry"					// 7E
	};
	return func_names[func_no >> 1];
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


static int copy_mem_to_user ( unsigned int target_cpu_addr, Uint8 *source, int size )
{
	int len = 0;
	while (size) {
		if (target_cpu_addr >= 0x8000)
			return -1;
		memory_debug_write_cpu_addr(target_cpu_addr++, *source++);
		size--;
		len++;
	}
	return len;
}


// Called when DOS trap is triggered.
// Can be used to take control (without hyppo doing it) but setting the needed register values,
// and calling hypervisor_leave() to avoid Hyppo to run.
void hdos_enter ( const Uint8 func_no )
{
	hdos.func = func_no;
	hdos.func_name = hdos_get_func_name(hdos.func);
	// NOTE: hdos.in_ things are the *INPUT* registers of the trap, cannot be used
	// to override the result passing back by the trap!
	// Here we store input register values can be even examined at the hdos_leave() stage
	hdos.in_x = cpu65.x;
	hdos.in_y = cpu65.y;
	hdos.in_z = cpu65.z;
	DEBUGHDOS("HDOS: entering function #$%02X (%s)" NL, hdos.func, hdos.func_name);
}


// Called when DOS trap is leaving.
// Can be used to examine the result hyppo did with a call, or even do some modifications.
void hdos_leave ( const Uint8 func_no )
{
	hdos.func = func_no;
	hdos.func_name = hdos_get_func_name(hdos.func);
	DEBUGHDOS("HDOS: leaving function #$%02X (%s) with carry %s" NL, hdos.func, hdos.func_name, cpu65.pf_c ? "SET" : "CLEAR");
	if (hdos.func == 0x2E && cpu65.pf_c) {	// HDOS setnam function. Also check carry set (which means "ok" by HDOS trap)
		// we always track this call, because we should know the actual name selected with some other calls then
		// let's do a local copy of the successfully selected name via hyppo (we know this by knowing that C flag is set by hyppo)
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
		DEBUGHDOS("HDOS: %s: selected filename is [%s]" NL, hdos.func_name, hdos.setnam_fn);
		return;
	}
	if (hdos.func == 0x40) {
		DEBUGHDOS("HDOS: %s(\"%s\") = %s" NL, hdos.func_name, hdos.setnam_fn, cpu65.pf_c ? "OK" : "FAILED");
		return;
	}
}


// Must be called on TRAP RESET, also causes to close all file descriptors (BTW, may be called on exit xemu,
// to nicely close open files/directories ...)
void hdos_reset ( void )
{
	DEBUGHDOS("HDOS: reset" NL);
	hdos.setnam_fn[0] = '\0';
	hdos.func = -1;
	for (int a = 0; a < 0x100; a++)
		if (desc_table[a].status == HDOS_DESC_FILE) {
			const int ret = close(desc_table[a].fd);
			DEBUGHDOS("HDOS: reset: closing file descriptor #$%02X: %d" NL, a, ret);
			desc_table[a].status = HDOS_DESC_CLOSED;
		} else if (desc_table[a].status == HDOS_DESC_DIR) {
			const int ret = xemu_os_closedir(desc_table[a].dirp);
			DEBUGHDOS("HDOS: reset: closing directory descriptor #$%02X: %d" NL, a , ret);
			desc_table[a].status = HDOS_DESC_CLOSED;
		}
}


// implementation is here, but prototype is in hypervisor.h and not in hdos.h as you would expect!
int hypervisor_hdos_virtualization_status ( const int to_enable, const char **root_ptr )
{
	if (to_enable >= 0) {
		if (!!hdos.do_virt != !!to_enable) {
			hdos.do_virt = to_enable;
			DEBUGPRINT("HDOS: switcihing virtualization %s" NL, to_enable ? "ON" : "OFF");
		}
	}
	if (root_ptr)
		*root_ptr = hdos.rootdir;
	return hdos.do_virt;
}


// Must be called, but **ONLY** once in Xemu's running lifetime, before using XEMU HDOS related things here!
void hdos_init ( const int do_virt, const char *virtroot )
{
	DEBUGHDOS("HDOS: initialization with do_virt=%d and virtroot=\"%s\"" NL, do_virt, virtroot ? virtroot : "<NULL>");
	for (int a = 0; a < 0x100; a++)
		desc_table[a].status = HDOS_DESC_CLOSED;
	hdos_reset();
	// HDOS virtualization related stuff
	// First build the default path for HDOS root, also try to create, maybe hasn't existed yet.
	// We may not using this one (but virtroot), however the default should exist.
	char hdosdir[PATH_MAX + 1];
	strcpy(hdosdir, sdl_pref_dir);
	strcat(hdosdir, "hdosroot" DIRSEP_STR);
	MKDIR(hdosdir);
	if (virtroot && *virtroot) {	// now we may want to override the default, if it's given at all
		XDIR *dirhandle = xemu_os_opendir(virtroot);	// try to open directory just for testing
		if (dirhandle) {
			// seems to work! use the virtroot path but prepare it for having directory separator as the last character
			xemu_os_closedir(dirhandle);
			strcpy(hdosdir, virtroot);
			if (hdosdir[strlen(hdosdir) - 1] != DIRSEP_CHR)
				strcat(hdosdir, DIRSEP_STR);
		}
			ERROR_WINDOW("HDOS: bad HDOS virtual directory given (cannot open as directory): %s\nUsing the default instead: %s", virtroot, hdosdir);
	}
	hdos.rootdir = xemu_strdup(hdosdir);	// populate the result of HDOS virtualization root directory
	hdos.do_virt = do_virt;			// though, virtualization itself can be turned on/off by this, still
	DEBUGPRINT("HDOS: virtualization is %s, root = \"%s\"" NL, hdos.do_virt ? "ENABDLED" : "DISABLED", hdos.rootdir);
}
