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
#include "io_mapper.h"
#include "sdcard.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// This source is meant to virtualize Hyppo's DOS functions to be able to access
// the host OS (what runs Xemu) filesystem via the normal HDOS calls (ie what
// would see the sd-card otherwise).

//#define DEBUGHDOS(...) DEBUG(__VA_ARGS__)
#define DEBUGHDOS(...) DEBUGPRINT(__VA_ARGS__)

static struct {
	Uint8 func;
	const char *func_name;
	int func_is_virtualized;
	Uint8 in_x, in_y, in_z;
	// The following ones are ONLY used in virtualized functions originated from hdos_enter()
	Uint8 virt_out_a, virt_out_x, virt_out_y, virt_out_z, virt_out_carry;
	char  setname_fn[64];
	const char *rootdir;	// pointer to malloc'ed string. ALWAYS ends with directory separator of your host OS!
	char *cwd;		// ALWAYS ends with directory separator of your host OS! HDOS cwd with _FULL_ path of your host OS!
	int cwd_is_root;	// boolean: cwd of HDOS is in the emulated root directory (meaning: strings cwd and rootdir are the same)
	int do_virt;
	int error_code;		// last error code
	int transfer_area_addr;
} hdos;

static int hdos_init_is_done = 0;

#define HDOS_DESCRIPTORS	4

static struct {
	union {
		int fd;
		XDIR *dirp;
	};
	enum { HDOS_DESC_CLOSED, HDOS_DESC_FILE, HDOS_DESC_DIR } status;
	char *basedirpath;	// pointer to malloc'ed string (when in use). ALWAYS ends with directory separator of your host OS!
	int dir_entry_no;
} desc_table[HDOS_DESCRIPTORS];

#define HDOSERR_INVALID_ADDRESS	0x10
#define HDOSERR_FILE_NOT_FOUND	0x88
#define HDOSERR_INVALID_DESC	0x89
#define HDOSERR_IS_DIRECTORY	0x86
#define HDOSERR_NOT_DIRECTORY	0x87
#define HDOSERR_IMAGE_WRONG_LEN	0x8A
#define HDOSERR_TOO_MANY_OPEN	0x84
#define HDOSERR_NO_SUCH_DISK	0x80
// Some questionable choice in Xemu as error code:
#define HDOSERR_END_DIR		0x85	// invalid cluster, it is returned by readdir if trying to read beyond end of directory
#define HDOSERR_CANNOT_OPEN_DIR	HDOSERR_FILE_NOT_FOUND



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


static int copy_mem_to_user ( unsigned int target_cpu_addr, const Uint8 *source, int size )
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


#ifdef TRAP_XEMU
#include "xemu/emutools_config.h"
// though not so much HDOS specfific, it's still Xemu related (Xemu's own trap handler)
// so we put it into hdos.c ...

static void reconstruct_commandline ( char *p, unsigned int max_size )
{
	int argc;
	char **argv;
	xemucfg_get_cli_info(NULL, &argc, &argv);
	*p = '\0';
	for (int a = 0; a < argc; a++) {
		if (strlen(p) + strlen(argv[a]) >= max_size - 2)
			return;
		strcat(p, argv[a]);
		if (a < argc - 1)
			strcat(p, " ");
	}
}

void trap_for_xemu ( const int func_no )
{
	switch (func_no) {			// A register = function code
		case 0x00:			// Function $00: identify emulation
			// On real hw, A=$FF, carry is clear on return, in emulation carry is set, and the above id values are returned in regs)
			// It must be these identify values even for a future non-Xemu (?) MEGA65 emulator though! The real emulator info get
			// function ($01) can be used to really tell the emulator name, version, etc.
			D6XX_registers[0x40] = 'X';	// -> A [capitcal ascii!]
			D6XX_registers[0x41] = 'e';	// -> X
			D6XX_registers[0x42] = 'm';	// -> Y
			D6XX_registers[0x43] = 'U';	// -> Z [capitcal ascii!]
			break;
		case 0x01: {			// Function $01: get emulator textual information, subfunction = Z, memory pointer X(low-byte)/Y(hi-byte) [buffer must fit in the low 32K of CPU addr space]
			const char *res = "";
			char work[0x100];
			switch (D6XX_registers[0x43]) {
				case 0:			// Z = 0: emulator base name (Xemu in our case, can be different for other emulators)
					res = "Xemu";
					break;
				case 1:			// version-kind-of-information
					res = XEMU_BUILDINFO_CDATE;
					break;
				case 2:			// git information
					res = XEMU_BUILDINFO_GIT;
					break;
				case 3:			// host-OS information
					xemu_get_uname_string(work, sizeof work);
					res = work;
					break;
				case 4:			// detailed info about hyppo (the real one, not Xemu's extensions)
					res = hyppo_version_string;
					break;
				case 5:			// executable name of Xemu
					xemucfg_get_cli_info(&res, NULL, NULL);
					break;
				case 6:			// CLI parameters of Xemu
					reconstruct_commandline(work, sizeof work);
					res = work;
					break;
				case 7: {		// get mount info on drive-0
					int i;
					res = sdcard_get_mount_info(0, &i);
					D6XX_registers[0x43] = !!i;	// passes back "internal" boolean value in Z!
					}
					break;
				case 8:	{		// get mount info on drive-1
					int i;
					res = sdcard_get_mount_info(1, &i);
					D6XX_registers[0x43] = !!i;	// passes back "internal" boolean value in Z!
					}
					break;
				default:
					break;
			}
			int len = strlen(res) + 1;
			if (len >= sizeof work) {
				D6XX_registers[0x40] = HDOSERR_INVALID_ADDRESS;	// well, it should be another kind of error, but anyway ...
				goto error_some;
			}
			for (int a = 0; a < len; a++) {
				//work[a] = *res;
				work[a] = (*res >= 'a' && *res <= 'z') ? *res - 32 : *res;
				res++;
			}
			if (copy_mem_to_user(D6XX_registers[0x41] + (D6XX_registers[0x42] << 8), (const Uint8*)work, len) <= 0) {
				D6XX_registers[0x40] = HDOSERR_INVALID_ADDRESS;
				goto error_some;
			}
			}
			break;
		default:
			goto error_nofunc;
	}
	D6XX_registers[0x47] |= CPU65_PF_C;	// signal OK status with setting carry
	return;
error_nofunc:
	D6XX_registers[0x40] = 0xFF;		// set A register to 0xFF as error code
error_some:
	D6XX_registers[0x47] &= ~CPU65_PF_C;	// clear carry (error!)
}
#endif


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


static int find_empty_desc_tab_slot ( void )
{
	for (int a = 0; a < HDOS_DESCRIPTORS; a++)
		if (desc_table[a].status == HDOS_DESC_CLOSED)
			return a;
	return -1;
}


static int close_desc ( unsigned int entry )
{
	if (entry >= HDOS_DESCRIPTORS)
		return -1;
	if (desc_table[entry].basedirpath) {
		free(desc_table[entry].basedirpath);
		desc_table[entry].basedirpath = NULL;
	}
	if (desc_table[entry].status == HDOS_DESC_FILE) {
		const int ret = close(desc_table[entry].fd);
		desc_table[entry].status = HDOS_DESC_CLOSED;
		DEBUGHDOS("HDOS: closing file descriptor #$%02X: %d" NL, entry, ret);
		if (ret)
			return -1;	// error value??
		return 0;
	} else if (desc_table[entry].status == HDOS_DESC_DIR) {
		const int ret = xemu_os_closedir(desc_table[entry].dirp);
		desc_table[entry].status = HDOS_DESC_CLOSED;
		DEBUGHDOS("HDOS: closing directory descriptor #$%02X: %d" NL, entry, ret);
		if (ret)
			return -1;	// error value??
		return 0;
	} else if (desc_table[entry].status ==  HDOS_DESC_CLOSED) {
		return -1;	// already closed?
	}
	FATAL("HDOS: %s() trying to close desc with unknown status!", __func__);
	return -1;
}


// This is not an easy task, as:
// * we must deal with FS case insensivity madness (also on Host-OS side, lame Windows thing ...)
// * opening by "short file name"
// So at the end we need a directory scan unfortunately to be really sure we found the thing we want ...
static int try_open ( const char *basedirfn, const char *needfn, int open_mode, struct stat *st, char *fullpathout, void *result )
{
	if (strchr(needfn, '/') || strchr(needfn, '\\') || !*needfn)	// needfn could not contain directory component(s) and also cannot be empty
		return HDOSERR_FILE_NOT_FOUND;
	XDIR *dirp = xemu_os_opendir(basedirfn);
	if (!dirp)
		return HDOSERR_FILE_NOT_FOUND;
	char fn_found[FILENAME_MAX];
	while (!xemu_os_readdir(dirp, fn_found)) {
		// TODO: add check for "." and ".." not done in root of emulated HDOS FS
		if (!strcasecmp(fn_found, needfn) && strlen(fn_found) <= 63)
			goto found;
	}
	// TODO: open by shortname!!!
	// reason: first it should be tried with all the long file names.
	// FIXME: however maybe this is NOT what real hyppo would do, but note, in case of
	// real hyppo and sd-card it has access to _real_ short names, etc ...
#if 0
	xemu_os_rewinddir(dirp);
	// try again with short name policy now ...
	while ((entry = xemu_os_readdir(dirp, &entry_storage))) {

	}
#endif
	xemu_os_closedir(dirp);
	return HDOSERR_FILE_NOT_FOUND;
found:
	strcpy(fullpathout, basedirfn);
	if (fullpathout[strlen(fullpathout) - 1] != DIRSEP_CHR)
		strcat(fullpathout, DIRSEP_STR);
	strcat(fullpathout, fn_found);
	if (xemu_os_stat(fullpathout, st))
		return HDOSERR_FILE_NOT_FOUND;
	const int type = st->st_mode & S_IFMT;
	if (open_mode == -1) {		// -1 as "open_mode" func argument means: open as a DIRECTORY
		if (type != S_IFDIR)
			return HDOSERR_NOT_DIRECTORY;
		dirp = xemu_os_opendir(fullpathout);
		if (!dirp)
			return HDOSERR_FILE_NOT_FOUND;
		strcat(fullpathout, DIRSEP_STR);
		*(XDIR**)result = dirp;
	} else {			// if "open_mode" as not -1, then it means some kind of flags for open(), opening as a file
		if (type != S_IFREG)
			return HDOSERR_IS_DIRECTORY;
		int fd = xemu_os_open(fullpathout, open_mode);
		if (fd < 0)
			return HDOSERR_FILE_NOT_FOUND;
		*(int*)result = fd;
	}
	return 0;
}


static void hdos_virt_mount ( const int unit )
{
	hdos.func_is_virtualized = 1;
	int fd;
	char fullpath[PATH_MAX + 1];
	struct stat st;
	const int ret = try_open(hdos.cwd, hdos.setname_fn, O_RDWR, &st, fullpath, &fd);
	if (ret) {
		DEBUGHDOS("HDOS: VIRT: >> mount << would fail on try_open() = %d!" NL, ret);
		hdos.virt_out_a = ret;	// pass back the error code got from try_open()
		return;
	}
	xemu_os_close(fd);	// we don't need it anymore
	if (sdcard_force_external_mount(unit, fullpath, NULL)) {
		DEBUGHDOS("HDOS: VIRT: mount of image \"%s\" on unit %d FAILED :(" NL, fullpath, unit);
		hdos.virt_out_a = HDOSERR_IMAGE_WRONG_LEN;	// this is probably the only reason the mount call could fail
		return;
	}
	// it seems everything went out fine :D
	DEBUGHDOS("HDOS: VIRT: mount of image \"%s\" on unit %d went well." NL, fullpath, unit);
	hdos.virt_out_carry = 1;	// signal OK status
}


static void hdos_virt_opendir ( void )
{
	hdos.func_is_virtualized = 1;
	const int e = find_empty_desc_tab_slot();
	if (e < 0) {
		hdos.virt_out_a = HDOSERR_TOO_MANY_OPEN;
		return;
	}
	XDIR *dirp = xemu_os_opendir(hdos.cwd);
	if (!dirp) {
		hdos.virt_out_a = HDOSERR_CANNOT_OPEN_DIR;	// some error code; directory cannot be open (this SHOULD not happen though!)
		return;
	}
	desc_table[e].basedirpath = xemu_strdup(hdos.cwd);
	desc_table[e].status = HDOS_DESC_DIR;
	desc_table[e].dirp = dirp;
	desc_table[e].dir_entry_no = 0;
	hdos.virt_out_a = e;		// return the file descriptor
	hdos.virt_out_carry = 1;	// signal OK status
}


static void hdos_virt_close_dir_or_file ( void )
{
	hdos.func_is_virtualized = 1;
	if (close_desc(hdos.in_x))
		hdos.virt_out_a = HDOSERR_INVALID_DESC;
	else
		hdos.virt_out_carry = 1;	// signal OK status
}


static void hdos_virt_readdir ( void )
{
	hdos.func_is_virtualized = 1;
	if (hdos.in_x >= HDOS_DESCRIPTORS || desc_table[hdos.in_x].status != HDOS_DESC_DIR) {
		hdos.virt_out_a = HDOSERR_INVALID_DESC;
		return;
	}
	if (hdos.in_y >= 0x80) {
		hdos.virt_out_a = HDOSERR_INVALID_ADDRESS;
		return;
	}
	Uint8 mem[87];
	char fn_found[FILENAME_MAX];
	// FIXME: remove this? it seems, Hyppo never returns with the volume label anyway
#if 0
	if (in_emu_root && desc_table[hdos.in_x].dir_entry_no == 0) {
		// fake a volume label as the response, in case of emulation as root dir
		memset(mem, 0, sizeof mem);
		static const char volume_name[] = "XEMU-VRT";
		memcpy(mem, volume_name, strlen(volume_name));
		memcpy(mem + 65, volume_name, strlen(volume_name));
		mem[64] = strlen(volume_name);
		mem[86] = 8;	// type: volume label
		desc_table[hdos.in_x].dir_entry_no = 1;
		if (copy_mem_to_user(hdos.in_y << 8, mem, sizeof mem) != sizeof mem)
			hdos.virt_out_a = HDOSERR_INVALID_ADDRESS;
		else
			hdos.virt_out_carry = 1;	// signal OK status
		return;
	}
#endif
readdir_again:
	if (xemu_os_readdir(desc_table[hdos.in_x].dirp, fn_found)) {
		// FIXME: there should be error checking here, but we assume for now, that NULL means end of directory
		// But anyway, what should we do in case of an error during readdir? Not so much ...
		DEBUGHDOS("HDOS: VIRT: %s(): end-of-directory" NL, __func__);
		hdos.virt_out_a = HDOSERR_END_DIR;
		return;
	}
	desc_table[hdos.in_x].dir_entry_no++;
	memset(mem, 0, sizeof mem);	// pre-fill with zero for our whole buffer
	memset(mem + 65, 0x20, 8 + 3);	// pre-fill with spaces for the short name!
	if (fn_found[0] == '.') {
		if (fn_found[1] == '\0' || (fn_found[1] == '.' && fn_found[2] == '\0')) {
			if (hdos.cwd_is_root)
				goto readdir_again;	// if emulated as root dir, do not pass back "." and ".."
			else {
				// . or .. is accepted, however it would cause problems with name resolution, so let's do it at our own for this case
				mem[64] = fn_found[1] ? 2 : 1;		// fn length: 1 or 2 ("." or "..")
				memcpy(mem, fn_found, mem[64]);
				memcpy(mem + 65, fn_found, mem[64]);
			}
		} else
			goto readdir_again;	// entry names starting with '.' (other than '.' and '..' handled above!) can be problematic, let's ignore them!
	} else {
		// Copy, check (length and chars) and convert filename
		for (Uint8 *s = (Uint8*)fn_found, *t = mem, *sn = mem + 65; *s; s++, t++, mem[64]++) {
			if (mem[64] >= 63)
				goto readdir_again;	// skip file: too long name
			Uint8 c = *s;
			if (c < 0x20 || c >= 0x80)
				goto readdir_again;	// skip file: contains invalid character (as considered by us, at least) Also catches UTF-8 sequences, fortunately
			if (c >= 'a' && c <= 'z')
				c -= 32;
			*t = c;
			// For the faked 'short name' part of the story ... This is a very faulty and bad algorithm, but who cares about short names ;)
			// It would be exteremly hard to do correctly, anyway ...
			if (c == '.') {
				sn = mem + 65 + 8;
				memset(sn, 0x20, 3);
			} else if (sn < mem + 65 + 11)
				*sn++ = c;
		}
	}
	// Stat our file about details
	char fn[strlen(desc_table[hdos.in_x].basedirpath) + strlen(fn_found) + 1];
	sprintf(fn, "%s%s", desc_table[hdos.in_x].basedirpath, fn_found);
	struct stat st;
	if (xemu_os_stat(fn, &st))
		goto readdir_again;
	const int type = st.st_mode & S_IFMT;
	if (type != S_IFDIR && type != S_IFREG)	// some irregular file, we should not pass back
		goto readdir_again;
	if (type == S_IFREG && st.st_size >= 0x80000000U)	// do not pass back crazily large file
		goto readdir_again;
	// -- OK, directory entry - finally - seems to be OK to pass back as the result --
	DEBUGHDOS("HDOS: VIRT: %s(): accepted filename = \"%s\"" NL, __func__, fn_found);
	const unsigned int fake_start_cluster = desc_table[hdos.in_x].dir_entry_no + 0x10;	// don't use too low cluster numbers
	mem[78] =  fake_start_cluster        & 0xFF;
	mem[79] = (fake_start_cluster >>  8) & 0xFF;
	mem[80] = (fake_start_cluster >> 16) & 0xFF;
	mem[80] = (fake_start_cluster >> 24) & 0xFF;
	// File type: set "subdir" bit, if entry is a directory
	if (type == S_IFDIR) {
		mem[86] = 0x10;
		// not so critical but let's present size zero if it's a directory (also size got by stat() on directories varies
		// a lot in meaning on Windows or UNIX'es, so let's keep a consistent behaviour across Xemu platforms!)
		st.st_size = 0;
	}
	// File size in bytes
	mem[82] =  st.st_size        & 0xFF;
	mem[83] = (st.st_size >>  8) & 0xFF;
	mem[84] = (st.st_size >> 16) & 0xFF;
	mem[85] = (st.st_size >> 24) & 0xFF;
	// Copy the answer to the memory of the caller requested
	if (copy_mem_to_user(hdos.in_y << 8, mem, sizeof mem) != sizeof mem) {
		hdos.virt_out_a = HDOSERR_INVALID_ADDRESS;
		return;
	}
	hdos.virt_out_carry = 1;	// signal OK status
}


static void hdos_virt_cdroot ( void )
{
	hdos.func_is_virtualized = 1;
	if (!hdos.cwd_is_root) {
		strcpy(hdos.cwd, hdos.rootdir);	// cwd malloced area must be enough, since this is the shortest possible data to put in
		hdos.cwd_is_root = 1;
	}
	hdos.virt_out_carry = 1;	// signal OK status
}


static void hdos_virt_cd ( void )
{
	hdos.func_is_virtualized = 1;
	if (hdos.setname_fn[0] == '.') {
		if (hdos.setname_fn[1] == '\0') {
			// '.': in this case we don't need to do anything too much ...
			hdos.virt_out_carry = 1;	// signal OK status
			return;
		}
		if (hdos.setname_fn[1] == '.' && hdos.setname_fn[2] == '\0') {
			// '..': "cd-up" situation
			if (hdos.cwd_is_root) {
				// cannot "cd-up" from the emulated root
				hdos.virt_out_a = HDOSERR_FILE_NOT_FOUND;
				return;
			}
			// -- Otherwise, let's do the "cd-up" with manipulating 'cwd' only --
			for (char *p = hdos.cwd + strlen(hdos.cwd) - 2;; p--)
				if (*p == DIRSEP_CHR) {
					p[1] = '\0';
					break;
				}
			hdos.cwd_is_root = !strcmp(hdos.cwd, hdos.rootdir);	// set 'is CWD root' flag now, it's possible that user cd-up'ed into the the emulated root
			DEBUGHDOS("HDOS: VIRT: %s(\"..\"): now at \"%s\" is_root=%d" NL, __func__, hdos.cwd, hdos.cwd_is_root);
			hdos.virt_out_carry = 1;	// signal OK status
			return;
		}
		// if not '.' nor '..', then we simply refuse to deal with directories starts with '.'
		hdos.virt_out_a = HDOSERR_FILE_NOT_FOUND;
		return;
	}
	// -- the rest: 'cd' into some subdir other than '.' or '..' --
	struct stat st;
	XDIR *dirp;
	char pathout[PATH_MAX + 1];
	//static int try_open ( const char *basedirfn, const char *needfn, int open_mode, struct stat *st, char *pathout, void *result );
	int ret = try_open(hdos.cwd, hdos.setname_fn, -1, &st, pathout, &dirp);
	if (ret) {
		hdos.virt_out_a = ret;
		return;
	}
	xemu_os_closedir(dirp);			// not needed to much here, let's close it
	strcat(pathout, DIRSEP_STR);
	xemu_restrdup(&hdos.cwd, pathout);
	hdos.cwd_is_root = 0;		// if we managed to 'cd' into something (and it is cannot be '.' or '..' at this point!) we cannot be in the root anymore!
	hdos.virt_out_carry = 1;	// signal OK status
}


#define HDOS_VIRT_HYPPO_UNIMPLEMENTED()	hdos.func_is_virtualized = 1
#define HDOS_VIRT_XEMU_UNIMPLEMENTED()	do { \
						DEBUGPRINT("HDOS: VIRT: Unimplemented by Xemu!! %s ~ #$%02X)" NL, hdos.func_name, hdos.func); \
						hdos.func_is_virtualized = 1; \
					} while (0)


// Called when DOS trap is triggered.
// Can be used to take control (without hyppo doing it) but setting the needed register values,
// and calling hypervisor_leave() to avoid Hyppo to run.
void hdos_enter ( const Uint8 func_no )
{
	if (XEMU_UNLIKELY(!hdos_init_is_done))
		FATAL("%s() is called before HDOS subsystem init!", __func__);
	hdos.func = func_no;
	hdos.func_name = hdos_get_func_name(hdos.func);
	hdos.func_is_virtualized = 0;
	// NOTE: hdos.in_ things are the *INPUT* registers of the trap, cannot be used
	// to override the result passing back by the trap!
	// Here we store input register values can be even examined at the hdos_leave() stage
	hdos.in_x = cpu65.x;
	hdos.in_y = cpu65.y;
	hdos.in_z = cpu65.z;
	DEBUGHDOS("HDOS: entering function #$%02X (%s)" NL, hdos.func, hdos.func_name);
	if (hdos.do_virt) {
		// Can be overriden by virtualized functions.
		// these virt_out stuffs won't be used otherwise, if a virtualized function does not set hdos.func_is_virtualized
		hdos.virt_out_a = 0xFF;	// set to $FF by default, override in virtualized function implementations if needed
		hdos.virt_out_x = cpu65.x;
		hdos.virt_out_y = cpu65.y;
		hdos.virt_out_z = cpu65.z;
		hdos.virt_out_carry = 0;	// assumming **ERROR** (carry flag is clear) by default! Override that to '1' in virt functions if it's OK!
		// Let's see the virualized functions.
		// Those should set hdos.func_is_virtualized, also the hdos.virt_out_carry is operation was OK, and probably they want to
		// set some register values as output in hdos.virt_out_a, hdos.virt_out_x, hdos.virt_out_y and hdos.virt_out_z
		// The reason of this complicated method: the switch-case below contains the call of virtualized DOS function implementations and
		// they can decide to NOT set hdos.func_is_virtualized conditionally, so it's even possible to have virtualized DOS file functions
		// just for a certain directory / file names, and so on! (though it's currently not so much planned to use ...).
		// The reason for ">> 1" everywhere: to have a continous space of switch values and allow better chance for C compiler to generate a jump-table.
		switch (hdos.func >> 1) {
			case 0x00 >> 1:	// get version, do NOT virtualize this!
			case 0x02 >> 1:	// get default drive
			case 0x04 >> 1:	// get current drive
				hdos.func_is_virtualized = 1;
				hdos.virt_out_a = 0;
				hdos.virt_out_carry = 1;
				break;
			case 0x06 >> 1:	// select drive (we allow zero only in Xemu)
				hdos.func_is_virtualized = 1;
				if (hdos.in_x)
					hdos.virt_out_a = HDOSERR_NO_SUCH_DISK;
				else
					hdos.virt_out_carry = 1;	// OK
				break;
			case 0x08 >> 1:	// getdisksize [NOT IMPLEMENTED]
				HDOS_VIRT_HYPPO_UNIMPLEMENTED();
				break;
			case 0x0A >> 1:	// getcwd [NOT IMPLEMENTED]
				HDOS_VIRT_HYPPO_UNIMPLEMENTED();
				break;
			case 0x0C >> 1:
				hdos_virt_cd();
				break;
			case 0x0E >> 1:	// mkdir [NOT IMPLEMENTED]
				HDOS_VIRT_HYPPO_UNIMPLEMENTED();
				break;
			case 0x10 >> 1:	// rmdir [NOT IMPLEMENETED]
				HDOS_VIRT_HYPPO_UNIMPLEMENTED();
				break;
			case 0x12 >> 1:
				hdos_virt_opendir();
				break;
			case 0x14 >> 1:
				hdos_virt_readdir();
				break;
			case 0x16 >> 1:
			case 0x20 >> 1:
				hdos_virt_close_dir_or_file();
				break;
			case 0x2E >> 1:	// setname, do NOT virtualize this! (though we track/store result in hdos_leave) It's also great that we have hyppo's check on filename syntax, etc.
				break;
			case 0x38 >> 1:	// get last error code
				hdos.func_is_virtualized = 1;
				hdos.virt_out_a = hdos.error_code;
				hdos.virt_out_carry = 1;
				break;
			case 0x3A >> 1:	// setup transfer area, do NOT virtualize this! (though we track/store result in hdos_leave)
				break;
			case 0x3C >> 1:
				hdos_virt_cdroot();
				break;
			case 0x40 >> 1:
				hdos_virt_mount(0);
				break;
			case 0x46 >> 1:
				hdos_virt_mount(1);
				break;
		}
		if (hdos.func_is_virtualized) {
			D6XX_registers[0x40] = hdos.virt_out_a;
			D6XX_registers[0x41] = hdos.virt_out_x;
			D6XX_registers[0x42] = hdos.virt_out_y;
			D6XX_registers[0x43] = hdos.virt_out_z;
			if (hdos.virt_out_carry) {
				D6XX_registers[0x47] |= CPU65_PF_C;	// was OK (carry is _SET_ if OK)
				if (hdos.func != 0x38)			// FIXME: allow get last error function to querty error code more than once. Is this really needed?
					hdos.error_code = 0;		// FIXME: not sure ... it would also cause to reset last error when query on get last error code
			} else {
				D6XX_registers[0x47] &= ~CPU65_PF_C;	// error (carry is _CLEAR_ if ERROR)
				hdos.error_code = hdos.virt_out_a;	// also remember the error code for the get last error function
			}
			DEBUGHDOS("HDOS: VIRT: returning %s (A=$%02X) from virtualized function #$%02X bypassing Hyppo" NL, hdos.virt_out_carry ? "OK" : "ERROR", hdos.virt_out_a, hdos.func);
			// forced leave of hypervisor mode now, bypassing hyppo to process this DOS trap function
			hypervisor_leave();
		} else
			DEBUGHDOS("HDOS: VIRT: unvirtualized DOS function #$%02X pass-through to Hyppo" NL, hdos.func);
	}
}


// Called when DOS trap is leaving.
// Can be used to examine the result hyppo did with a call, or even do some modifications.
void hdos_leave ( const Uint8 func_no )
{
	hdos.func = func_no;
	hdos.func_name = hdos_get_func_name(hdos.func);
	DEBUGHDOS("HDOS: leaving function #$%02X (%s) with carry %s" NL, hdos.func, hdos.func_name, cpu65.pf_c ? "SET" : "CLEAR");
	// if "func_is_virtualized" flag is set, we don't want to mess things up further, as it was handled before in hdos.c somewhere
	if (hdos.func_is_virtualized) {
		DEBUGHDOS("HDOS: VIRT: was marked as virtualized, so end of %s in %s()" NL, hdos.func_name, __func__);
		hdos.func_is_virtualized = 0;	// just to be sure, though it's set to zero on next hdos_enter()
		return;
	}
	if (hdos.func == 0x2E && cpu65.pf_c) {	// HDOS setnam function. Also check carry set (which means "ok" by HDOS trap)
		// We always track this call, because we should know the actual name selected with some other calls then,
		// since the result can be used _later_ by both of virtualized and non-virtualized functions, possibly.
		// Let's do a local copy of the successfully selected name via hyppo (we know this by knowing that C flag is set by hyppo)
		// FIXME: in case of error, is setname buffer modified?
		// FIXME: is only Y used for _page_ and X ignored, also is tranfer area addr is really modified as a "side effect"?
		char setnam_current[sizeof hdos.setname_fn];
		// FIXME: copy routine should not fail ever if hyppo already accepted!
		if (copy_string_from_user(hdos.setname_fn, sizeof hdos.setname_fn, hdos.in_x + (hdos.in_y << 8)) >= 0)
			// hdos.transfer_area_address = hdos.in_y << 8;	// WTF? setname has X/Y as input!
			for (char *p = hdos.setname_fn; *p; p++) {
				if (*p < 0x20 || *p >= 0x7F) {
					// FIXME: this should not happen if hyppo already accepted!
					DEBUGHDOS("HDOS: setnam(): invalid character in filename $%02X" NL, *p);
					hdos.setname_fn[0] = '\0';
					break;
				}
				if (*p >= 'A' && *p <= 'Z')
					*p = *p - 'A' + 'a';
			}
		DEBUGHDOS("HDOS: %s: selected filename is [%s] from $%04X" NL, hdos.func_name, hdos.setname_fn, hdos.in_x + (hdos.in_y << 8));
		return;
	}
	if (hdos.func == 0x3A && cpu65.pf_c) {	// HDOS setup transfer area. We don't virtualize this, but maintain a copy of the value set as we need the pointer set
		hdos.transfer_area_addr = hdos.in_y << 8;
		DEBUGHDOS("HDOS: transfer area address is set to $%04X" NL, hdos.transfer_area_addr);
		return;
	}
	if (hdos.func == 0x40) {
		DEBUGHDOS("HDOS: %s(\"%s\") = %s" NL, hdos.func_name, hdos.setname_fn, cpu65.pf_c ? "OK" : "FAILED");
		return;
	}
}


// Must be called on TRAP RESET, also causes to close all file descriptors (BTW, may be called on exit xemu,
// to nicely close open files/directories ...)
void hdos_reset ( void )
{
	DEBUGHDOS("HDOS: reset" NL);
	hdos.setname_fn[0] = '\0';
	hdos.func = -1;
	hdos.error_code = 0;
	hdos.transfer_area_addr = 0;
	hypervisor_hdos_close_descriptors();
}


// implementation is here, but prototype is in hypervisor.h and not in hdos.h as you would expect!
void hypervisor_hdos_close_descriptors ( void )
{
	if (hdos_init_is_done)
		for (int a = 0; a < HDOS_DESCRIPTORS; a++)
			(void)close_desc(a);
}


// implementation is here, but prototype is in hypervisor.h and not in hdos.h as you would expect!
int hypervisor_hdos_virtualization_status ( const int set, const char **root_ptr )
{
	if (set >= 0) {
		if (!!hdos.do_virt != !!set) {
			hdos.do_virt = set;
			DEBUGPRINT("HDOS: virtualization is now %s" NL, set ? "ENABLED" : "DISABLED");
		}
	}
	if (root_ptr)
		*root_ptr = hdos.rootdir;
	return hdos.do_virt;
}


// Must be called, but **ONLY** once in Xemu's running lifetime, before using XEMU HDOS related things here!
void hdos_init ( const int do_virt, const char *virtroot )
{
	if (hdos_init_is_done)
		FATAL("%s() called more than once!", __func__);
	hdos_init_is_done = 1;
	DEBUGHDOS("HDOS: initialization with do_virt=%d and virtroot=\"%s\"" NL, do_virt, virtroot ? virtroot : "<NULL>");
	for (int a = 0; a < HDOS_DESCRIPTORS; a++) {
		desc_table[a].status = HDOS_DESC_CLOSED;
		desc_table[a].basedirpath = NULL;
	}
	hdos_reset();
	// HDOS virtualization related stuff
	// First build the default path for HDOS root, also try to create, maybe hasn't existed yet.
	// We may not using this one (but virtroot), however the default should exist.
	char hdosdir[PATH_MAX + 1];
	sprintf(hdosdir, "%s%s%c", sdl_pref_dir, HDOSROOT_SUBDIR_NAME, DIRSEP_CHR);
	MKDIR(hdosdir);
	XDIR *dirp = xemu_os_opendir(hdosdir);
	if (!dirp)
		FATAL("Cannot open default HDOS root: %s", hdosdir);
	xemu_os_closedir(dirp);
	if (virtroot && *virtroot) {	// now we may want to override the default, if it's given at all
		XDIR *dirp = xemu_os_opendir(virtroot);	// try to open directory just for testing
		if (dirp) {
			// seems to work! use the virtroot path but prepare it for having directory separator as the last character
			xemu_os_closedir(dirp);
			strcpy(hdosdir, virtroot);
			if (hdosdir[strlen(hdosdir) - 1] != DIRSEP_CHR)
				strcat(hdosdir, DIRSEP_STR);
		} else
			ERROR_WINDOW("HDOS: bad HDOS virtual directory given (cannot open as directory): %s\nUsing the default instead: %s", virtroot, hdosdir);
	}
	hdos.rootdir = xemu_strdup(hdosdir);	// populate the result of HDOS virtualization root directory
	hdos.cwd = xemu_strdup(hdosdir);	// also for cwd (current working directory)
	hdos.cwd_is_root = 1;
	hdos.do_virt = do_virt;			// though, virtualization itself can be turned on/off by this, still
	DEBUGPRINT("HDOS: virtualization is %s, root = \"%s\"" NL, hdos.do_virt ? "ENABLED" : "DISABLED", hdos.rootdir);
}
