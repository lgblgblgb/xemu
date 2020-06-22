/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   The goal of emutools.c is to provide a relative simple solution
   for relative simple emulators using SDL2.

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

#ifndef XEMU_COMMON_EMUTOOLS_FILES_H_INCLUDED
#define XEMU_COMMON_EMUTOOLS_FILES_H_INCLUDED

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef	XEMU_ARCH_WIN
#	define FILE_BROWSER	"explorer"
#elif	defined(XEMU_ARCH_MAC)
#	define FILE_BROWSER	"open"
#else
#	define FILE_BROWSER	"xdg-open"
#endif

#define OFF_T_ERROR ((off_t)-1)

extern void *xemu_load_buffer_p;
extern char  xemu_load_filepath[PATH_MAX];

extern int     xemu_load_file ( const char *filename, void *store_to, int min_size, int max_size, const char *cry );
extern int     xemu_save_file ( const char *filename, void *data, int size, const char *cry );
extern int     xemu_open_file ( const char *filename, int mode, int *mode2, char *filepath_back );
extern ssize_t xemu_safe_read ( int fd, void *buffer, size_t length );
extern ssize_t xemu_safe_write ( int fd, const void *buffer, size_t length );
extern int     xemu_safe_close ( int fd );
extern off_t   xemu_safe_file_size_by_fd ( int fd );
extern off_t   xemu_safe_file_size_by_name ( const char *name );
extern int     xemu_create_sparse_file ( const char *os_path, Uint64 size );

#if defined(HAVE_XEMU_INSTALLER) && !defined(HAVE_XEMU_EXEC_API)
#define HAVE_XEMU_EXEC_API
#endif

#ifdef HAVE_XEMU_EXEC_API
#define XEMUEXEC_STILL_RUNNING 259
#ifdef XEMU_ARCH_WIN
typedef void* xemuexec_process_t;
#define XEMUEXEC_NULL_PROCESS_ID NULL
#else
typedef int xemuexec_process_t;
#define XEMUEXEC_NULL_PROCESS_ID 0
#endif
extern xemuexec_process_t xemuexec_run ( char *const args[] );
extern int xemuexec_check_status ( xemuexec_process_t pid, int wait );
#endif

extern void xemuexec_open_native_file_browser ( char *dir );

#ifdef HAVE_XEMU_INSTALLER
extern void xemu_set_installer ( const char *filename );
#endif

#endif
