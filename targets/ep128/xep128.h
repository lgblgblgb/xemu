/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

#ifndef __XEP128_XEP128_H_INCLUDED
#define __XEP128_XEP128_H_INCLUDED

#include "xemu/emutools_basicdefs.h"

#define VARALIGN	MAXALIGNED
#define	XEPEXIT(n)	XEMUEXIT(n)

#define	DIRSEP		DIRSEP_STR


/* Ugly hack: now override DATADIR to the old Xep128 defaults for legacy Xep128 users.
   This must be changed some time though ... */
#ifdef DATADIR
#undef DATADIR
#define DATADIR		"/usr/local/lib/xep128"
#endif

/* the old: */


#include <stdio.h>
#include <SDL_types.h>
#include <SDL_messagebox.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define DESCRIPTION		"Enterprise-128 Emulator"
#define WINDOW_TITLE		"Xep128"
#define VERSION			"0.3"
#define COPYRIGHT		"(C)2015,2016 LGB Gabor Lenart"
#define PROJECT_PAGE		"http://xep128.lgb.hu/"



#define CONFIG_USE_LODEPNG
#define CONFIG_EXDOS_SUPPORT
#define CONFIG_W5300_SUPPORT
#define DEFAULT_CPU_CLOCK	4000000


#define COMBINED_ROM_FN		"combined.rom"
#define SDCARD_IMG_FN		"sdcard.img"
#define PRINT_OUT_FN		"@print.out"
#define DEFAULT_CONFIG_FILE	"@config"
#define DEFAULT_CONFIG_SAMPLE_FILE "@config-sample"

//#define ERRSTR()		sys_errlist[errno]
#define ERRSTR()		strerror(errno)


extern FILE *debug_fp;
#if 0
#ifdef DISABLE_DEBUG
#define DEBUG(...)
#define DEBUGPRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG(...) do {	\
        if (debug_fp)	\
                fprintf(debug_fp, __VA_ARGS__);	\
} while(0)
#define DEBUGPRINT(...) do {	\
        printf(__VA_ARGS__);	\
        DEBUG(__VA_ARGS__);	\
} while(0)
#endif
#endif

extern void osd_notification ( const char *s );
#define OSD(...) do { \
	char _buf_for_win_msg_[4096]; \
	snprintf(_buf_for_win_msg_, sizeof _buf_for_win_msg_, __VA_ARGS__); \
	DEBUGPRINT("OSD: %s" NL, _buf_for_win_msg_); \
	osd_notification(_buf_for_win_msg_); \
} while(0)

extern int _sdl_emu_secured_message_box_ ( Uint32 sdlflag, const char *msg );
#define _REPORT_WINDOW_(sdlflag, str, ...) do { \
	char _buf_for_win_msg_[4096]; \
	snprintf(_buf_for_win_msg_, sizeof _buf_for_win_msg_, __VA_ARGS__); \
	DEBUGPRINT(str ": %s" NL, _buf_for_win_msg_); \
	_sdl_emu_secured_message_box_(sdlflag, _buf_for_win_msg_); \
} while(0)
#define INFO_WINDOW(...)	_REPORT_WINDOW_(SDL_MESSAGEBOX_INFORMATION, "INFO", __VA_ARGS__)
#define WARNING_WINDOW(...)	_REPORT_WINDOW_(SDL_MESSAGEBOX_WARNING, "WARNING", __VA_ARGS__)
#define ERROR_WINDOW(...)	_REPORT_WINDOW_(SDL_MESSAGEBOX_ERROR, "ERROR", __VA_ARGS__)
#define FATAL(...)		do { ERROR_WINDOW(__VA_ARGS__); XEPEXIT(1); } while(0)

#define CHECK_MALLOC(p)		do {	\
	if (!p) FATAL("Memory allocation error. Not enough memory?");	\
} while(0)

extern int _sdl_emu_secured_modal_box_ ( const char *items_in, const char *msg );
#define QUESTION_WINDOW(items, msg) _sdl_emu_secured_modal_box_(items, msg)

//#define ERRSTR() sys_errlist[errno]
#define ERRSTR() strerror(errno)

#endif
