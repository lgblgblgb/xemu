/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
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

#include "xemu/emutools.h"
#include "inject.h"
#include "xemu/emutools_files.h"
#include "memory_mapper.h"
#include "vic4.h"
#include "xemu/emutools_hid.h"
#include "xemu/f011_core.h"
#include "input_devices.h"
#include "hypervisor.h"
#include "sdcard.h"
#include "configdb.h"

#define C64_BASIC_LOAD_ADDR	0x0801
#define C65_BASIC_LOAD_ADDR	0x2001

#define KEY_PRESS_TIMEOUT	6

#define IMPORT_BAS_TEXT_TEMPFILE "@lastimporttext.bas"

static int check_status = 0;
static int key2release = -1;
static int key2release_timeout;
static void *inject_ready_userdata;
static void (*inject_ready_callback)(void*);
static struct {
	Uint8 *stream;
	char  *cmd, *cmd_p;	// used by the command inject stuff
	int   size;
	int   load_addr;
	int   c64_mode;
	int   run_it;
	bool  at_startup;
} prg = {
	.stream	= NULL,
	.cmd	= NULL,
	.cmd_p	= NULL
};
static Uint8 *under_ready_p;
static char *kbd_hwa_pasting = NULL;
static int   kbd_hwa_pasting_single_case;


static void _cbm_screen_write_char ( Uint8 *p, const char c )
{
	if (c == '@')
		*p = 0;
	else if (c >= 'a' && c <= 'z')
		*p = c - 'a' + 1;
	else if (c >= 'A' && c <= 'Z')
		*p = c - 'A' + 1;
	else
		*p = c;
}


static void _cbm_screen_write ( Uint8 *p, const char *s )
{
	while (*s)
		_cbm_screen_write_char(p++, *s++);
}


#define CBM_SCREEN_PRINTF(scrp, ...)	do {	\
	char __buffer__[80];			\
	sprintf(__buffer__, ##__VA_ARGS__);	\
	_cbm_screen_write(scrp, __buffer__);	\
	} while (0)


static void press_return ( void )
{
	if (key2release >= 0)
		KBD_RELEASE_KEY(key2release);
	DEBUGPRINT("INJECT: pressing key RETURN" NL);
	key2release = 1;
	key2release_timeout = KEY_PRESS_TIMEOUT;
	KBD_PRESS_KEY(1);
	hwa_kbd_set_fake_key(13);
}


static int is_ready_on_screen ( const int delete_all_ready )
{
	if (in_hypervisor && !delete_all_ready)
		return 0;
	static const Uint8 ready_msg[] = { 0x12, 0x05, 0x01, 0x04, 0x19, 0x2E };	// "READY." in screen codes
	const int width = vic4_query_screen_width();
	const int height = vic4_query_screen_height();
	const Uint8 *cstart = (VIC4_LIKE_IO_MODE() && REG_VICIII_ATTRIBS) ? vic4_query_colour_address() : NULL;
	Uint8 *start = vic4_query_screen_address();
	int num = 0;
	// Check every lines of the screen (not the "0th" line, because we need "READY." in the previous line!)
	// NOTE: I cannot rely on exact position as different ROMs can have different line position for the "READY." text!
	// NOTE: Also, there are differences how cursor is shown, see later in this code as comments:
	for (int i = 1; i < height - 2; i++) {
		// We need this pointer later, to "fake" a command on the screen
		under_ready_p = start + i * width;
		if (!memcmp(under_ready_p - width, ready_msg, sizeof ready_msg)) {
			if (delete_all_ready) {
				// this changes "READY."->"READY " (space insteaf of ".") to avoid confusion when waiting "READY." but having one even before starting to wait
				*(under_ready_p - width + 5) = 32;
				num++;
			} else if (*under_ready_p == 0xA0 || (		// 0xA0 -> cursor is shown, and the READY. in the previous line (C65/C64 ROMs, older MEGA65 ROMs)
				cstart &&
				*under_ready_p == 0x20 &&		// 0x20 AND hw attrib inverse, and the READY. in the previous line (used method on newer MEGA65 ROMs)
				(*(cstart + (unsigned int)(under_ready_p - start)) & 0xF0) == 0x20
			))
				return 1;
		}
	}
	if (delete_all_ready)
		DEBUGPRINT("INJECT: READY. has been invalidated %d times (%dx%d@$%X)" NL, num, width, height, (unsigned int)(start - main_ram));
	return num;
}


static void do_prg_test_inject ( const char *arg, const char *def_startup, const bool do_press_return )
{
// Must be one char but as a string ...
#define PRG_TEST_DELIM_CHAR ";"
// Must be one char and as a char ...
#define PRG_TEST_DELIM_CHAR_ADDR '@'
	char opt[strlen(arg) + 1];
	char cmdbuf[strlen(arg) + 1];
	strcpy(opt, arg);
	cmdbuf[0] = '\0';
	for (char *sav, *p = strtok_r(opt, PRG_TEST_DELIM_CHAR, &sav); p; p = strtok_r(NULL, PRG_TEST_DELIM_CHAR, &sav)) {
		char *pa = strchr(p, PRG_TEST_DELIM_CHAR_ADDR);
		if (pa) {
			*(pa++) = '\0';
			int base = 10;
			if (*pa == '$') {
				pa++;
				base = 16;
			}
			char *ep;
			const long addr = strtol(pa, &ep, base);
			if (addr < 0 || addr >= 0x10000000L || *ep)
				FATAL("Bad syntax in -prgtest : cannot convert \"%s\" into a valid base-%d value", pa, base);
			DEBUGPRINT("INJECT: prgtest: loading \"%s\" from $%X" NL, p, (unsigned int)addr);
			const int r = xemu_load_file(p, NULL, 1, 8 << 20, "Cannot inject file");
			if (r <= 1)
				XEMUEXIT(1);
			for (unsigned int i = 0; i < r; i++)
				debug_write_linear_byte((unsigned int)addr + i, ((Uint8*)xemu_load_buffer_p)[i]);
			free(xemu_load_buffer_p);
			xemu_load_buffer_p = NULL;
		} else {
			strcat(cmdbuf, p);
			strcat(cmdbuf, ":");
		}
	}
	if (cmdbuf[0])
		DEBUGPRINT("INJECT: prgtest: overridding startup sequence to \"%s\"" NL, cmdbuf);
	else
		DEBUGPRINT("INJECT: prgtest: using default startup sequence \"%s\"" NL, def_startup);
	CBM_SCREEN_PRINTF(under_ready_p, " %s", cmdbuf[0] ? cmdbuf : def_startup);
	if (do_press_return || cmdbuf[0])
		press_return();
}


static void exit_on_ready ( void *unused )
{
	DEBUGPRINT("INJECT: Exiting because of -prgexit and READY prompt hit." NL);
	XEMUEXIT(0);
}


static void prg_inject_callback ( void *unused )
{
	DEBUGPRINT("INJECT: hit 'READY.' trigger, about to inject %d bytes from $%04X." NL, prg.size, prg.load_addr);
	fdc_allow_disk_access(FDC_ALLOW_DISK_ACCESS);
	memcpy(main_ram + prg.load_addr, prg.stream, prg.size);
	clear_emu_events();	// clear keyboard & co state, ie for C64 mode, probably had MEGA key pressed still
	CBM_SCREEN_PRINTF(under_ready_p - vic4_query_screen_width() + 7, "<$%04X-$%04X,%d bytes>", prg.load_addr, prg.load_addr + prg.size - 1, prg.size);
	if (prg.run_it) {
		// We must modify BASIC pointers ... Important to know the C64/C65 differences!
		if (prg.c64_mode) {
			main_ram[0x2D] =  prg.size + prg.load_addr;
			main_ram[0x2E] = (prg.size + prg.load_addr) >> 8;
		} else {
			main_ram[0xAE] =  prg.size + prg.load_addr;
			main_ram[0xAF] = (prg.size + prg.load_addr) >> 8;
			main_ram[0x82] =  prg.size + prg.load_addr;
			main_ram[0x83] = (prg.size + prg.load_addr) >> 8;
		}
		// If program was detected as BASIC (by load-addr) we want to auto-RUN it
		static const char def_cmd[] = "RUN:";
		if (configdb.prg_test && prg.at_startup) {	// ... though if we have special '-prgtest' option, there are few other things ...
			do_prg_test_inject(configdb.prg_test, def_cmd, true);
		} else {
			CBM_SCREEN_PRINTF(under_ready_p, " %s", def_cmd);
			press_return();
		}
	} else {
		char def_cmd[50];
		// In this case we DO NOT press RETURN for user, as maybe the SYS addr is different, or user does not want this at all!
		sprintf(def_cmd, "SYS%u:REM **YOU CAN PRESS RETURN**", (Uint16)prg.load_addr);
		if (configdb.prg_test && prg.at_startup)
			do_prg_test_inject(configdb.prg_test, def_cmd, false);
		else
			CBM_SCREEN_PRINTF(under_ready_p, " %s", def_cmd);
	}
	free(prg.stream);
	prg.stream = NULL;
	if (configdb.prg_exit && prg.at_startup)
		inject_register_ready_status("Exit on ready", exit_on_ready, NULL);
}


static void import_callback2 ( void *unused )
{
	CBM_SCREEN_PRINTF(under_ready_p, " SCNCLR:RUN:");
	if (configdb.disk9)
		sdcard_external_mount(1, configdb.disk9, NULL);
	press_return();
}


static void import_callback ( void *unused )
{
	DEBUGPRINT("INJECT: hit 'READY.' trigger, about to import BASIC65 text program." NL);
	fdc_allow_disk_access(FDC_ALLOW_DISK_ACCESS);	// re-allow disk access
	if (!sdcard_external_mount(1, IMPORT_BAS_TEXT_TEMPFILE, "Mount failure for BASIC65 import")) {
		CBM_SCREEN_PRINTF(under_ready_p, " IMPORT\"FILESEQ\",U9:");
		press_return();
		inject_register_ready_status("BASIC65 text import2", import_callback2, NULL);
	}
}


static void command_callback ( void *unused )
{
	if (!prg.cmd_p)
		return;
	DEBUGPRINT("INJECT: hit 'READY.' trigger, injecting command at offset %d" NL, (int)(prg.cmd_p - prg.cmd));
	if (prg.cmd_p == prg.cmd)
		fdc_allow_disk_access(FDC_ALLOW_DISK_ACCESS);	// re-allow disk access
	Uint8 *p = under_ready_p;
	*p++ = 32;
	for (;;) {
		const char c = *(prg.cmd_p++);
		if (!c) {
			free(prg.cmd);
			prg.cmd_p = NULL;
			prg.cmd = NULL;
			break;
		}
		if (c == '|') {
			is_ready_on_screen(1);	// clear already present READY. to be sure not to confuse us on waiting for another one ...
			check_status = 1;	// re-engage the whole stuff ... (but with the next "part" of the command, where prg.cmd_p points to)
			break;
		}
		_cbm_screen_write_char(p++, c);
	}
	clear_emu_events();
	press_return();
}


static void allow_disk_access_callback ( void *unused )
{
	DEBUGPRINT("INJECT: re-enable disk access on READY. prompt" NL);
	fdc_allow_disk_access(FDC_ALLOW_DISK_ACCESS);
}


void inject_register_allow_disk_access ( void )
{
	fdc_allow_disk_access(FDC_DENY_DISK_ACCESS);	// deny now!
	// register event for the READY. prompt for re-enable
	inject_register_ready_status("Disk access re-enabled", allow_disk_access_callback, NULL);
}


// You can register multi-step command separated by '|' character. command_callback will parse that
void inject_register_command ( const char *s )
{
	inject_register_ready_status("Command", command_callback, NULL);
	fdc_allow_disk_access(FDC_DENY_DISK_ACCESS);	// deny disk access, to avoid problem when autoboot disk image is used otherwise
	if (prg.cmd)
		free(prg.cmd);
	prg.cmd = xemu_strdup(s);
	prg.cmd_p = prg.cmd;
}


int inject_register_prg ( const char *prg_fn, const int prg_mode, const bool startup )
{
	if (prg.stream) {
		free(prg.stream);
		prg.stream = NULL;
	}
	prg.size = xemu_load_file(prg_fn, NULL, 3, 0x1F800, "Cannot load PRG to be injected");
	prg.at_startup = startup;
	if (prg.size < 3)
		goto error;
	prg.stream = xemu_load_buffer_p;
	xemu_load_buffer_p = NULL;
	prg.load_addr = prg.stream[0] + (prg.stream[1] << 8);
	prg.size -= 2;
	memmove(prg.stream, prg.stream + 2, prg.size);
	// TODO: needs to be fixed to check ROM boundary (or eg from addr zero)
	if (prg.load_addr + prg.size >= 0x1F800) {
		ERROR_WINDOW("Program to be injected is too large (%d bytes, load address: $%04X)\nFile: %s",
			prg.size,
			prg.load_addr,
			prg_fn
		);
		goto error;
	}
	switch (prg_mode) {
		case 0:	// auto detection
			if (prg.load_addr == C64_BASIC_LOAD_ADDR) {
				prg.c64_mode = 1;
				prg.run_it = 1;
			} else if (prg.load_addr == C65_BASIC_LOAD_ADDR) {
				prg.c64_mode = 0;
				prg.run_it = 1;
			} else {
				char msg[512];
				snprintf(msg, sizeof msg, "PRG to load: %s\nCannot detect C64/C65 mode for non-BASIC (load address: $%04X) PRG, please specify:", prg_fn, prg.load_addr);
				prg.c64_mode = QUESTION_WINDOW("C65|C64|Ouch, CANCEL!", msg);
				if (prg.c64_mode != 0 && prg.c64_mode != 1)
					goto error;
				prg.run_it = 0;
			}
			break;
		case 64:
			prg.c64_mode = 1;
			prg.run_it = (prg.load_addr == C64_BASIC_LOAD_ADDR);
			break;
		case 65:
			prg.c64_mode = 0;
			prg.run_it = (prg.load_addr == C65_BASIC_LOAD_ADDR);
			break;
		default:
			ERROR_WINDOW("Invalid parameter for prg-mode!");
			goto error;
	}
	DEBUGPRINT("INJECT: prepare for C6%c mode, %s program, $%04X load address, %d bytes from file: %s" NL,
		prg.c64_mode ? '4' : '5',
		prg.run_it ? "BASIC (RUNable)" : "ML (SYSable)",
		prg.load_addr,
		prg.size,
		prg_fn
	);
	clear_emu_events();
	if (prg.c64_mode)
		KBD_PRESS_KEY(0x75);	// "MEGA" key is hold down for C64 mode
	if (inject_register_ready_status("PRG memory injection", prg_inject_callback, NULL)) // prg inject does not use the userdata ...
		goto error;
	fdc_allow_disk_access(FDC_DENY_DISK_ACCESS);	// deny now, to avoid problem on PRG load while autoboot disk is mounted
	return 0;
error:
	if (prg.stream) {
		free(prg.stream);
		prg.stream = NULL;
	}
	return 1;
}


int inject_register_ready_status ( const char *debug_msg, void (*callback)(void*), void *userdata )
{
	if (check_status) {
		DEBUGPRINT("WARNING: INJECT: cannot register 'READY.' event, already having one in progress!" NL);
		//return 1;
	}
	DEBUGPRINT("INJECT: registering 'READY.' event: %s" NL, debug_msg);
	inject_ready_userdata = userdata;
	inject_ready_callback = callback;
	check_status = 1;
	is_ready_on_screen(1);			// be sure, no READY. can be seen already on the screen
	return 0;
}


// Must be called by the main emulation loop regularly, let's say, once a frame or so.
void inject_ready_check_do ( void )
{
	if (XEMU_UNLIKELY(key2release >= 0)) {
		if (key2release_timeout <= 0) {
			DEBUGPRINT("INJECT: releasing key #%d" NL, key2release);
			KBD_RELEASE_KEY(key2release);
			key2release = -1;
		} else
			key2release_timeout--;
	}
	if (kbd_hwa_pasting) {
		static const char *now = NULL;
		static int stalling = 0;
		if (!now) {
			now = kbd_hwa_pasting;
			stalling = 0;
		}
		osd_display_console_log = 0;
		OSD(-1, 10, "Pasting in progress: %u", (unsigned int)strlen(now));
		osd_status = OSD_STATIC;
		const char *next = hwa_kbd_add_string(now, kbd_hwa_pasting_single_case);
		if (!*next)
			goto end_pasting;
		if (next == now) {
			stalling++;
			if (stalling > 60) {
				ERROR_WINDOW(
					"Pasting did not work through. Some possibilities:\n"
					"  You don't run a decent enough ROM using hardware keyboard scanning\n"
					"  You are in GO64 BASIC mode (it won't work there)\n"
					"  System is busy or (currently?) does not use hardware keyboard scanning"
				);
				goto end_pasting;
			}
			return;
		}
		now = next;
		stalling = 0;
		return;
	end_pasting:
		osd_status = 0;
		now = NULL;
		free(kbd_hwa_pasting);
		kbd_hwa_pasting = NULL;
		return;
	}
	if (XEMU_LIKELY(!check_status))
		return;
	if (check_status == 1) {		// we're in "waiting for READY." phase
		if (is_ready_on_screen(0))
			check_status = 2;
	} else if (check_status > 10) {
		check_status = 0;	// turn off "ready check" mode, we have our READY.
		inject_ready_callback(inject_ready_userdata);	// callback is activated now
	} else
		check_status++;		// we're "let's wait some time after READY." phase
}


static int basic_text_conv ( char *dst, const char *src, int len )
{
	// TODO: currently it's only a copy!!!!
	memcpy(dst, src, len);
	dst[len] = 0;
	return 0;
}


int inject_register_import_basic_text ( const char *fn )
{
	if (!fn || !*fn)
		return 0;
	const int len = xemu_load_file(fn, NULL, 1, 65535, "Could not open/load text import for BASIC65");
	if (len < 1)
		return -1;
	char *buf = xemu_malloc(len + 1);
	if (basic_text_conv(buf, xemu_load_buffer_p, len)) {
		free(buf);
		free(xemu_load_buffer_p);
		return -1;
	}
	free(xemu_load_buffer_p);
	if (xemu_save_file(IMPORT_BAS_TEXT_TEMPFILE, buf, len, "Could not save prepared text for BASIC65 import") < 0) {
		free(buf);
		return -1;
	}
	free(buf);
	if (inject_register_ready_status("BASIC65 text import", import_callback, NULL)) // prg inject does not use the userdata ...
		return -1;
	fdc_allow_disk_access(FDC_DENY_DISK_ACCESS);	// deny now, to avoid problem on PRG load while autoboot disk is mounted
	return 0;
}


int inject_hwa_pasting ( char *s, const int single_case )
{
	if (!s || !*s) {
		DEBUGPRINT("INJECT: cannot register empty text to be typed-in" NL);
		return -1;
	}
	if (kbd_hwa_pasting) {
		ERROR_WINDOW("Another pasting event is already in progress.");
		return -1;
	}
	DEBUGPRINT("INJECT: %u bytes long text is registered to be typed-in" NL, (unsigned int)strlen(s));
	kbd_hwa_pasting = s;
	kbd_hwa_pasting_single_case = single_case;
	return 0;
}
