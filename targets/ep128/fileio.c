/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2019-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_gui.h"
#include "enterprise128.h"
#include "fileio.h"
#include "xemu/z80.h"
#include "cpu.h"
#include "emu_rom_interface.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#define EXOS_USER_SEGMAP_P (0X3FFFFC + memory)
#define HOST_OS_STR "Host OS "
#define FILE_TOO_LARGE "Too large file"

#define FILEIO_MAX_FILE_SIZE	67108864L

#define SET_CHANNEL(v) channel = ((v) - 1) & 0xFF

char fileio_cwd[PATH_MAX + 1];
static int channel = 0;

static int   fio_fd  [0x100];
static int   fio_off [0x100];
static char *fio_name[0x100];
static int   fio_size[0x100];
static Uint8 fio_prot[0x100];



void fileio_init ( const char *dir, const char *subdir )
{
	int a;
	for (a = 0; a < 0x100; a++)
		fio_fd[a] = -1;
	if (dir && *dir && *dir != '?') {
		strcpy(fileio_cwd, dir);
		if (subdir) {
			strcat(fileio_cwd, subdir);
			if (subdir[strlen(subdir) - 1] != DIRSEP_CHR)
				strcat(fileio_cwd, DIRSEP_STR);
		}
		DEBUGPRINT("FILEIO: base directory is: %s" NL, fileio_cwd);
		mkdir(fileio_cwd
#ifndef _WIN32
			, 0777
#endif
		);
	} else
		fileio_cwd[0] = '\0';
}


static void fileio_host_errstr ( void )
{
	char buffer[65];
	snprintf(buffer, sizeof buffer, HOST_OS_STR "%s", ERRSTR());
	xep_set_error(buffer);
}



static int host_file_check ( int fd, int *file_size, char **filename_store, const char *filename_in )
{
	struct stat st;
	if (fstat(fd, &st)) {
		fileio_host_errstr();
		close(fd);
		return -1;
	}
	if (st.st_size > FILEIO_MAX_FILE_SIZE) {
		xep_set_error(HOST_OS_STR FILE_TOO_LARGE);
		close(fd);
		return -1;
	}
	*file_size = st.st_size;
	*filename_store = strdup(filename_in);
	if (!*filename_store) {
		xep_set_error(HOST_OS_STR "Cannot allocate memory");
		close(fd);
		return -1;
	}
	return fd;
}


/* Opens a file on host-OS/FS side.
   Note: EXOS is case-insensitve on file names.
   Host OS FS "under" Xep128 may (Win32) or may not (UNIX) be case sensitve, thus we walk through the directory with tring to find matching file name.
   Argument "create" must be non-zero for create channel call, otherwise it should be zero.
   O_BINARY flag is a *MUST* for Windows! On non-windows systems we define O_BINARY as zero in a header file, thus using it won't affect non-win32 systems!
*/
static int open_host_file ( const char *dirname, const char *filename, int create, char **used_filename, int *file_size )
{
	DIR *dir;
	struct dirent *entry;
	int mode = create ? (O_TRUNC | O_CREAT | O_RDWR) : O_RDWR;
	dir = opendir(dirname);
	if (!dir) {
		xep_set_error(HOST_OS_STR "Cannot open base directory");
		return -1;
	}
	while ((entry = readdir(dir))) {
		if (!strcasecmp(entry->d_name, filename)) {
			int ret;
			char buffer[PATH_MAX + 1];
			closedir(dir);
			if (CHECK_SNPRINTF(snprintf(buffer, sizeof buffer, "%s%s%s", dirname, DIRSEP_STR, entry->d_name), sizeof buffer))
				return -1;
			DEBUGPRINT("FILEIO: opening file \"%s\"" NL, buffer);
			ret = open(buffer, mode | O_BINARY, 0666);
			if (ret < 0 && !create)	// handle the situation when host OS file is read-only ...
				ret = open(buffer, O_RDONLY | O_BINARY);	// re-try in read-only mode ...
			if (ret < 0) {
				fileio_host_errstr();
				DEBUGPRINT("FILEIO: open in directory-walk-method failed: %s" NL, ERRSTR());
			} else
				ret = host_file_check(ret, file_size, used_filename, buffer);
			return ret;
		}
	}
	closedir(dir);
	if (create) {
		int ret;
		char buffer[PATH_MAX + 1];
		if (CHECK_SNPRINTF(snprintf(buffer, sizeof buffer, "%s%s%s", dirname, DIRSEP_STR, filename), sizeof buffer))
			return -1;
		ret = open(buffer, mode | O_BINARY, 0666);
		if (ret < 0) {
			fileio_host_errstr();
			DEBUGPRINT("FILEIO: open in create-case-for-new-file failed: %s" NL, ERRSTR());
		} else
			ret = host_file_check(ret, file_size, used_filename, buffer);
		return ret;
	}
	xep_set_error(HOST_OS_STR "File not found");
	DEBUGPRINT("FILEIO: No file found matching the open request" NL);
	return -1;
}


static void get_file_name ( char *p )
{
	int de = Z80_DE;
	int len = read_cpu_byte(de);
	while (len--)
		*(p++) = tolower(read_cpu_byte(++de));
	*p = '\0';
}


void fileio_func_open_channel_remember ( void )
{
	SET_CHANNEL(Z80_A);
}


// channel number is set up with fileio_func_open_channel_remember() *before* this call! from XEP ROM separated trap!
void fileio_func_open_or_create_channel ( int create )
{
	int r;
	char fnbuf[PATH_MAX + 1];
	// check content of Z80 A register. It should be zero from channel RAM allocate func. If not, it's an error!
	if (Z80_A) {
		DEBUGPRINT("FILEIO: channel RAM allocation EXOS call in XEP ROM failed (A = %02Xh)? Return!" NL, Z80_A);
		return;	// simply pass control back with the same error code in A we got
	}
	// channel number (C variable channel) already set via fileio_func_open_channel_remember() in a separated XEP TRAP!
	if (fio_fd[channel] >= 0) {
		DEBUGPRINT("FILEIO: open/create channel, already used channel for %d, fd is %d" NL, channel, fio_fd[channel]);
		Z80_A = 0xF9;	// channel number is already used! (maybe it's useless to be tested, as EXOS wouldn't allow that anyway?)
		return;
	}
	get_file_name(fnbuf);
	DEBUGPRINT("FILEIO: file name got = \"%s\"" NL, fnbuf);
	if (!*fnbuf) {
		DEBUGPRINT("FILEIO: file name was empty \"%s\" ..." NL, fnbuf);
		if (create)
			r = -1;	// GUI for create is not yet supported ...
		else
			r = xemugui_file_selector(
				XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
				WINDOW_TITLE " - Select file for opening via FILE: device",
				fileio_cwd,
				fnbuf,
				sizeof fnbuf
			);
		if (r) {
			xep_set_error(HOST_OS_STR "No file selected");
			Z80_A = XEP_ERROR_CODE;
			DEBUGPRINT("FILEIO: no file selected!" NL);
			return;
		}
		memmove(fnbuf, fnbuf + strlen(fileio_cwd), strlen(fnbuf + strlen(fileio_cwd)) + 1);
	}
	r = open_host_file(fileio_cwd, fnbuf, create, &fio_name[channel], &fio_size[channel]);
	//xep_set_error(ERRSTR());
	DEBUGPRINT("FILEIO: %s channel #%d result = %d filename = \"%s\" as %s with size of %d" NL, create ? "create" : "open", channel, r, fnbuf, fio_name[channel], fio_size[channel]);
	if (r < 0) {
		// open_host_file() already issued the xep_set_error() call to set a message up ...
		Z80_A = XEP_ERROR_CODE;
	} else {
		fio_fd[channel] = r;
		fio_off[channel] = 0;	// file offset
		fio_prot[channel] = 0;	// protection byte not so much used by EXOS, but anyway ...
		Z80_A = 0;
	}
}


void fileio_func_close_channel ( void )
{
	SET_CHANNEL(Z80_A);
	if (fio_fd[channel] < 0) {
		DEBUGPRINT("FILEIO: close, invalid channel for %d, fd is %d" NL, channel, fio_fd[channel]);
		Z80_A = 0xFB;	// invalid channel
	} else {
		DEBUGPRINT("FILEIO: close, closing channel %d (fd = %d)" NL, channel, fio_fd[channel]);
		close(fio_fd[channel]);
		fio_fd[channel] = -1;
		free(fio_name[channel]);
		fio_name[channel] = NULL;
		Z80_A = 0;
	}
}



static int increment_offset ( int inc /*, int is_write */ )
{
	fio_off[channel] += inc;
	if (/*is_write &&*/ fio_off[channel] > fio_size[channel])
		fio_size[channel] = fio_off[channel];
	if (fio_off[channel] > FILEIO_MAX_FILE_SIZE) {
		Z80_A = XEP_ERROR_CODE;
		xep_set_error(HOST_OS_STR FILE_TOO_LARGE);
		return -1;
	}
	return 0;
}


static int host_reseek ( void )
{
	off_t ret = lseek(fio_fd[channel], fio_off[channel], SEEK_SET);
	if (ret != fio_off[channel]) {
		Z80_A = XEP_ERROR_CODE;
		if (ret)
			fileio_host_errstr();
		else
			xep_set_error(HOST_OS_STR "Invalid seek retval");
		return -1;
	}
	if (fio_off[channel] > FILEIO_MAX_FILE_SIZE) {
		Z80_A = XEP_ERROR_CODE;
		xep_set_error(HOST_OS_STR FILE_TOO_LARGE);
		return -1;
	}
	DEBUG("FILEIO: internal host seek %d for channel %d" NL, fio_off[channel], channel);
	return 0;
}


void fileio_func_read_block ( void )
{
	int rb;
	Uint8 buffer[0xFFFF], *p;
	SET_CHANNEL(Z80_A);
	DEBUGPRINT("FILEIO: read block on channel %d (fd = %d) BC=%04Xh DE=%04Xh" NL, channel, fio_fd[channel], Z80_BC, Z80_DE);
	if (fio_fd[channel] < 0) {
		DEBUGPRINT("FILEIO: read block problem = invalid channel" NL);
		Z80_A = 0xFB;	// invalid channel
		return;
	}
	if (host_reseek())	// this may set error str and Z80_A!
		return;
	Z80_A = 0;
	rb = 0;
	while (Z80_BC) {
		int r;
		r = read(fio_fd[channel], buffer + rb, Z80_BC);
		DEBUGPRINT("FILEIO: read block on channel %d (fd = %d), %d byte(s) requested at offset %d (file size = %d), result is %d (got so far: %d)" NL,
			channel, fio_fd[channel], Z80_BC,
			fio_off[channel], fio_size[channel],
			r, rb
		);
		if (r > 0) {
			rb += r;
			Z80_BC -= r;
			if (increment_offset(r))	// this may set error str and Z80_A!
				break;
		} else if (!r) {
			Z80_A = 0xE4;	// attempt to read after end of file
			break;
		} else {
			fileio_host_errstr();
			Z80_A = XEP_ERROR_CODE;
			break;
		}
	}
	p = buffer;
	while (rb--)
		write_cpu_byte_by_segmap(Z80_DE++, EXOS_USER_SEGMAP_P, *(p++));
}


void fileio_func_read_character ( void )
{
	SET_CHANNEL(Z80_A);
	if (fio_fd[channel] < 0) {
		DEBUGPRINT("FILEIO: read character, invalid channel for %d, fd is %d" NL, channel, fio_fd[channel]);
		Z80_A = 0xFB;	// invalid channel
	} else {
		int r;
		if (host_reseek())	// this may set error str and Z80_A!
			return;
		r = read(fio_fd[channel], &Z80_B, 1);
		if (r == 1) {
			Z80_A = 0;
			increment_offset(1); // this may set error str and Z80_A!
		} else if (!r)
			Z80_A = 0xE4;	// attempt to read after end of file
		else {
			fileio_host_errstr();
			Z80_A = XEP_ERROR_CODE;
		}
	}
}



void fileio_func_write_block ( void )
{
	int wb, de;
	Uint8 buffer[0xFFFF], *p;
	SET_CHANNEL(Z80_A);
	if (fio_fd[channel] < 0) {
		Z80_A = 0xFB;	// invalid channel
		return;
	}
	if (host_reseek())	// this may set error str and Z80_A!
		return;
	wb = Z80_BC;
	p = buffer;
	de = Z80_DE;
	Z80_A = 0;
	while (wb--)
		*(p++) = read_cpu_byte_by_segmap(de++, EXOS_USER_SEGMAP_P);
	p = buffer;
	while (Z80_BC) {
		int r = write(fio_fd[channel], p, Z80_BC);
		if (r > 0) {
			Z80_BC -= r;
			Z80_DE += r;
			if (increment_offset(r))	// this may set error str and Z80_A!
				break;
		} else if (!r) {
			xep_set_error(HOST_OS_STR "Cannot write block");
			Z80_A = XEP_ERROR_CODE;
			break;
		} else {
			fileio_host_errstr();
			Z80_A = XEP_ERROR_CODE;
			break;
		}
	}
}


void fileio_func_write_character ( void )
{
	SET_CHANNEL(Z80_A);
	if (fio_fd[channel] < 0)
		Z80_A = 0xFB;	// invalid channel
	else {
		int r;
		if (host_reseek())	// this may set error str and Z80_A!
			return;
		r = write(fio_fd[channel], &Z80_B, 1);
		if (r == 1) {
			Z80_A = 0;
			increment_offset(1); // this may set error str and Z80_A!
		} else if (!r) {
			xep_set_error(HOST_OS_STR "Cannot write character");
			Z80_A = XEP_ERROR_CODE;
		} else {
			fileio_host_errstr();
			Z80_A = XEP_ERROR_CODE;
		}
	}
}


void fileio_func_channel_read_status ( void )
{
	Z80_A = 0xE7;
}



void fileio_func_set_channel_status ( void )
{
	SET_CHANNEL(Z80_A);
	if (fio_fd[channel] < 0) {
		Z80_A = 0xFB;	// invalid channel
	} else {
		Uint8 stat[16];
		int a, de;
		for (a = 0, de = Z80_DE; a < 16; a++)
			stat[a] = a < 9 ? read_cpu_byte_by_segmap(de++, EXOS_USER_SEGMAP_P) : 0;
		if (Z80_C & 1)
			fio_off[channel] = stat[0] | (stat[1] << 8) | (stat[2] << 16) | (stat[3] << 24);
		else {
			stat[0] =  fio_off[channel] & 0xFF;
			stat[1] = (fio_off[channel] >> 8) & 0xFF;
			stat[2] = (fio_off[channel] >> 16) & 0xFF;
			stat[3] = (fio_off[channel] >> 24) & 0xFF;
		}
		if (Z80_C & 4)
			fio_prot[channel] = stat[8];	// protection byte?!
		else
			stat[8] = fio_prot[channel];
		stat[4] =  fio_size[channel] & 0xFF;
		stat[5] = (fio_size[channel] >> 8) & 0xFF;
		stat[6] = (fio_size[channel] >> 16) & 0xFF;
		stat[7] = (fio_size[channel] >> 24) & 0xFF;
		for (a = 0, de = Z80_DE; a < 16; a++)
			write_cpu_byte_by_segmap(de++, EXOS_USER_SEGMAP_P, stat[a]);
		Z80_DE = de;	// will DE change?! FIXME ! simply remove this line, if it's not the case ...
		Z80_A = 0;	// OK
		Z80_C = 3;	// read flags
	}
}



void fileio_func_special_function ( void )
{
	Z80_A = 0xE7;
}



void fileio_func_init ( void )
{
	int a;
	for (a = 0; a < 0x100; a++)
		if (fio_fd[a] != -1) {
			close(fio_fd[a]);
			fio_fd[a] = -1;
			free(fio_name[a]);
			fio_name[a] = NULL;
		}
}


void fileio_func_buffer_moved ( void )
{
	// no return code needed, and we don't care as we have our data in the C code :)
}



void fileio_func_destroy_channel ( void )
{
	// Currently we don't allow files to be deleted via FILE:.
	// Let's just close the channel ...
	fileio_func_close_channel();
}



void fileio_func_not_used_call ( void )
{
	Z80_A = 0xE7;
}
