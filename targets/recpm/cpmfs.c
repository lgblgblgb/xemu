/* Re-CP/M: CP/M-like own implementation + Z80 emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "cpmfs.h"
#include "hardware.h"
#include "bdos.h"
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "console.h"


#define MAX_OPEN_FILES	32

static struct {
	char	pattern[8 + 3 + 1];	// FCB formatted pattern [ie, the file name we search for, probbaly with '?' wildcard chars]
	char	found[8 + 3 + 1];	// FCB formatted filename of the current found item
	char	host_name[13];		// host OS filename found [not CP/M!], must be 8 + 3 + 1 + 1 = 
	char	host_path[PATH_MAX];	// host OS full-path filename found
	int	result_is_valid;
	int	stop_search;
	int	options;
	int	drive;
	struct stat st;
} ff;
static struct {
	DIR 	*dir;			// directory stream, NULL if not "mounted"
	char	dir_path[PATH_MAX];	// host OS directory of the drive (with ALWAYS a trailing dirsep char!), or null string (ie dir_path[0] = 0) if not "mounted"
	int	ro;			// drive is software write-protected, ie "read-only"
} drives[26];
static struct {
	int	fd;
	Uint8	checksum[0x10];
	int	drive;
	Uint16	last_fcb_addr;
	int	sequence;
} files[MAX_OPEN_FILES];

int current_drive;


#ifdef XEMU_ARCH_WIN
#define realpath(r,a) _fullpath(a,r,PATH_MAX)
#endif



void cpmfs_init ( void )
{
	ff.result_is_valid = 0;
	ff.stop_search = 1;
	current_drive = -1;
	for (int a = 0; a < 26; a++) {
		drives[a].dir_path[0] = 0;
		drives[a].dir = NULL;
	}
	for (int a = 0; a < MAX_OPEN_FILES; a++)
		files[a].fd = -1;
	DEBUGPRINT("CPMFS: initialized, %d max open files, PATH_MAX=%d" NL, MAX_OPEN_FILES, PATH_MAX);
}


void cpmfs_close_all_files ( void )
{
	for (int a = 0; a < MAX_OPEN_FILES; a++)
		if (files[a].fd >= 0) {
			close(files[a].fd);
			files[a].fd = -1;
		}
}

void cpmfs_uninit ( void )
{
	cpmfs_close_all_files();
	for (int a = 0; a < 26; a++)
		if (drives[a].dir)
			closedir(drives[a].dir);
}



int cpmfs_mount_drive ( int drive, const char *dir_path, int dirbase_part_only )
{
	if (drive >= 26 || drive < 0)
		return 1;
	if (!dir_path || !dir_path[0]) {
		drives[drive].dir_path[0]  = 0;
		if (drives[drive].dir) {
			closedir(drives[drive].dir);
			drives[drive].dir = NULL;
			conprintf("CPMFS: drive %c has been umounted\r\n", drive + 'A');
		}
		return 0;
	}
	char path_resolved[PATH_MAX];
	if (!realpath(dir_path, path_resolved)) {
		conprintf("CPMFS: drive %c cannot be mounted, realpath() failure\r\n", drive + 'A');
		return 1;
	}
	if (dirbase_part_only) {
		char *p = strrchr(path_resolved, DIRSEP_CHR);
		if (!p)
			return 1;
		*p = 0;
	}
	int len = strlen(path_resolved);
	if (len > PATH_MAX - 14) {
		conprintf("CPMFS: drive %c cannot be mounted, too long path\r\n", drive + 'A');
		return 1;
	}
	DIR *dir = opendir(path_resolved);
	if (!dir) {
		conprintf("CPMFS: drive %c cannot be mounted, host directory cannot be open\r\n", drive + 'A');
		return 1;
	}
	if (path_resolved[len - 1] != DIRSEP_CHR) {
		path_resolved[len++] = DIRSEP_CHR;
		path_resolved[len] = 0;
	}
	memcpy(drives[drive].dir_path, path_resolved, len + 1);	// copy the terminator \0 too (+1)
	if (drives[drive].dir) {
		closedir(drives[drive].dir);
		conprintf("CPMFS: drive %c remounting.\r\n", drive + 'A');
	}
	conprintf("CPMFS: drive %c <- %s\r\n", drive + 'A', path_resolved);
	drives[drive].dir = dir;
	drives[drive].ro = 0;
	if (current_drive == -1) {
		current_drive = drive;
		conprintf("CPMFS: drive %c has been selected as the current (first mount)\r\n", drive + 'A');
	}
	return 0;
}

char *cpmfs_search_file_get_result_path ( void )
{
	return ff.result_is_valid ? ff.host_path : NULL;
}


static int fn_part ( char *dest, const char *src, int len, int maxlen, int jokery )
{
	if (len > maxlen)
		return 1;
	for (int a = 0, c = 32; a < maxlen; a++) {
		if (a < len) {
			c = src[a];
			if (c == '*') {		// technically this shouldn't allowed to be on FCB for search, but we also use for internal parsing not on CP/M level but in our C code
				if (jokery) {
					c = '?';
					len = 0;	// to trick parser to stop for the rest and give only '?'
					*dest++ = c;
					continue;
				} else
					return 1;
			} else if (c == '?') {
				if (!jokery)
					return 1;
				*dest++ = c;
			} else if (c >= 'a' && c <= 'z') {
				*dest++ = c - 'a' + 'A';
			} else if (c < 32 || c >= 127) {
				return 1;
			} else {
				*dest++ = c;
			}
			c = 32;
		} else {
			*dest++ = c;
		}
	}
	return 0;
}


static int fn_take_apart ( const char *name, char *base_name, char *ext_name, int jokery )
{
	char *p = strchr(name, '.');
	return p ? (
		p == name || !p[1] ||
		fn_part(base_name, name,  p - name,      8, jokery) ||
		fn_part(ext_name,  p + 1, strlen(p + 1), 3, jokery)
	) : (
		fn_part(base_name, name,  strlen(name),  8, jokery) ||
		fn_part(ext_name,  NULL,  0,             3, jokery)
	);
}


static int pattern_match ( void )
{
	for (int a = 0; a < 8 + 3; a++)
		if (ff.pattern[a] != '?' && ff.found[a] != ff.pattern[a])
			return 1;
	return 0;
}



int cpmfs_search_file ( void )
{
	ff.result_is_valid = 0;
	if (ff.stop_search) {
		DEBUGPRINT("FCB: FIND: stop_search condition!" NL);
		return -1;
	}
	DIR *dir = drives[ff.drive].dir;
	if (!dir) {
		DEBUGPRINT("FCB: FIND: drive %c directory is not open?!" NL, ff.drive);
		ff.stop_search = 1;
		return -1;
	}
	for (;;) {
		struct dirent *entry = readdir(dir);
		if (!entry) {
			DEBUGPRINT("FCB: FIND: entry NULL returned, end of directory maybe?" NL);
			ff.stop_search = 1;
			return -1;
		}
		if (fn_take_apart(entry->d_name, ff.found, ff.found + 8, 0)) {
			DEBUGPRINT("FCB: FIND: ruling out filename \"%s\"" NL, entry->d_name);
			continue;
		}
		ff.found[8 + 3] = 0;	// FIXME: just for debug, to be able to print out!
		DEBUGPRINT("FCB: FIND: considering formatted filename \"%s\"" NL, ff.found);
		if (pattern_match()) {
			DEBUGPRINT("FCB: FIND: no pattern match for this file" NL);
			continue;
		}
		// stat file, store full path etc
		strcpy(ff.host_name, entry->d_name);
		strcpy(ff.host_path, drives[ff.drive].dir_path);
		strcat(ff.host_path, entry->d_name);
		DEBUGPRINT("FCB: FIND: trying to stat file: %s" NL, ff.host_path);
		if (stat(ff.host_path, &ff.st)) {
			DEBUGPRINT("FCB: FIND: cannot stat() file" NL);
			continue;
		}
		if ((ff.st.st_mode & S_IFMT) != S_IFREG) {
			DEBUGPRINT("FCB: FIND: skipping file, not a regular one!" NL);
			continue;
		}
		DEBUGPRINT("FCB: FIND: cool, file is accepted!" NL);
		// Also, if there was no joker characters, there cannot be more results, so close our directory
		if (!(ff.options & CPMFS_SF_JOKERY))
			ff.stop_search = 1;
		ff.result_is_valid = 1;
		// creating directory entry in DMA if it was requested, or something like that :-O
		if ((ff.options & CPMFS_SF_STORE_IN_DMA0)) {
			Uint8 *res = memory + cpm_dma + 32 * (ff.options & 3);
			memset(memory + cpm_dma, 0, 0x80);	// just to be sure, clear the whole current DMA area
			memcpy(res + 1, ff.found, 11);		// copy the file name into the desired 32-byte slice of the DMA
			res[12] = 1;	// TODO: trying what they will do ...
			res[13] = 1;
			res[14] = 1;
			res[15] = 1;
			return ff.options & 3;
		} else
			return 0;
	}
}


// !! if CPMFS_SF_INPUT_IS_FCB is set, "drive" is ignored, and "input" is treated as a memory pointer to a CP/M search FCB
// !! if it is NOT specified, drive selects the drive (as-is, no "current drive" notion) and "input" is C-string style variable (with dot-notion etc)
int cpmfs_search_file_setup ( int drive, const Uint8 *input, int options )
{
	ff.result_is_valid = 0;
	ff.stop_search = 1;
	ff.options = options;
	if ((options & CPMFS_SF_INPUT_IS_FCB)) {
		for (int a = 0; a < 8 + 3; a++) {
			Uint8 c = input[a + 1] & 0x7F;	// FIXME: we ignore the highest bits stuffs ...
			if (!(options & CPMFS_SF_JOKERY) && c == '?')
				return 1;	// wildcard (joker[y]) is not allowed if not requested ...
			if (c >= 'a' && c <= 'z')
				c = c - 'a' + 'A';	// in FCB, it should be capital case already, maybe this is not needed, but who knows
			ff.pattern[a] = c;
		}
		drive = input[0] & 0x7F;
		drive = drive ? drive - 1 : current_drive;
	} else {
		// Input is NOT an FCB, but a C-string (null terminated etc), with dot notion, so we must convert it first
		FATAL("%s(): non-FCB input is not implemented yet", __func__);	// FIXME / TODO
		if (fn_take_apart((const char*)input, ff.pattern, ff.pattern + 8, (options & CPMFS_SF_JOKERY)))
			return 1;
	}
	if (drive < 0 || drive > 26 || !drives[drive].dir)
		return 1;
#if 0
	DIR *dir = opendir(drives[drive].dir_path);
	if (dir) {
		if (drives[drive].dir)
			closedir(drives[drive].dir);
		drives[drive].dir = dir;
	} else
#endif
		rewinddir(drives[drive].dir);
	ff.drive = drive;
	ff.stop_search = 0;
	return 0;
}
