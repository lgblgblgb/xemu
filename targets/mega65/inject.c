/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#define C64_BASIC_LOAD_ADDR	0x0801
#define C65_BASIC_LOAD_ADDR	0x2001

int inject_ready_check_status = 0;

static void *inject_ready_userdata;
static void (*inject_ready_callback)(void*);
static struct {
	Uint8 *stream;
	int   size;
	int   load_addr;
	int   c64_mode;
	int   run_it;
} prg;
static Uint8 *under_ready_p;


static XEMU_INLINE int get_screen_width ( void )
{
	// C65 $D031.7 VIC-III:H640 Enable C64 640 horizontal pixels / 80 column mode
	// Used to determine if C64 or C65 "power-on" screen
	// TODO: this should be revised in the future, as MEGA65 can other means have
	// different screens!!!
	return (vic_registers[0x31] & 0x80) ? 80 : 40;
}


static void _cbm_screen_write ( Uint8 *p, const char *s )
{
	while (*s) {
		if (*s == '@')
			*p++ = 0;
		else if (*s >= 'a' && *s <= 'z')
			*p++ = *s - 'a' + 1;
		else if (*s >= 'A' && *s <= 'Z')
			*p++ = *s - 'A' + 1;
		else
			*p++ = *s;
		s++;
	}
}


#define CBM_SCREEN_PRINTF(scrp, ...)	do {	\
	char __buffer__[80];			\
	sprintf(__buffer__, ##__VA_ARGS__);	\
	_cbm_screen_write(scrp, __buffer__);	\
	} while (0)


static void prg_inject_callback ( void *unused )
{
	DEBUGPRINT("INJECT: hit 'READY.' trigger, about to inject %d bytes from $%04X." NL, prg.size, prg.load_addr);
	fdc_allow_disk_access(FDC_ALLOW_DISK_ACCESS);
	memcpy(main_ram + prg.load_addr, prg.stream, prg.size);
	clear_emu_events();	// clear keyboard & co state, ie for C64 mode, probably had MEGA key pressed still
	CBM_SCREEN_PRINTF(under_ready_p - get_screen_width() + 7, "<$%04X-$%04X,%d bytes>", prg.load_addr, prg.load_addr + prg.size - 1, prg.size);
	if (prg.run_it) {
		// We must modify BASIC pointers ... Important to know the C64/C65 differences!
		if (prg.c64_mode) {
			main_ram[0x2D] =  prg.size + prg.load_addr;
			main_ram[0x2E] = (prg.size + prg.load_addr) >> 8;
		} else {
			main_ram[0xAE] =  prg.size + prg.load_addr;
			main_ram[0xAF] = (prg.size + prg.load_addr) >> 8;
		}
		// If program was detected as BASIC (by load-addr) we want to auto-RUN it
		CBM_SCREEN_PRINTF(under_ready_p, " ?\"@\":RUN:");
		KBD_PRESS_KEY(0x01);	// press RETURN
		under_ready_p[get_screen_width()] = 0x20;	// be sure no "@" (screen code 0) at the trigger position
		inject_ready_check_status = 100;	// go into special mode, to see "@" character printed by PRINT, to release RETURN by that trigger
	} else {
		// In this case we DO NOT press RETURN for user, as maybe the SYS addr is different, or user does not want this at all!
		CBM_SCREEN_PRINTF(under_ready_p, " SYS%d:REM **YOU CAN PRESS RETURN**", prg.load_addr);
	}
	free(prg.stream);
}


static void allow_disk_access_callback ( void *unused )
{
	DEBUGPRINT("INJECT: re-enable disk access on READY. prompt" NL);
	fdc_allow_disk_access(FDC_ALLOW_DISK_ACCESS);
}


int inject_register_ready_status ( const char *debug_msg, void (*callback)(void*), void *userdata )
{
	if (inject_ready_check_status) {
		DEBUGPRINT("WARNING: INJECT: cannot register 'READY.' event, already having one in progress!" NL);
		//return 1;
	}
	DEBUGPRINT("INJECT: registering 'READY.' event: %s" NL, debug_msg);
	inject_ready_userdata = userdata;
	inject_ready_callback = callback;
	inject_ready_check_status = 1;
	memset(main_ram + 1024, 0, 1024 * 3);	// be sure, no READY. can be seen already on the screen
	return 0;
}


void inject_register_allow_disk_access ( void )
{
	fdc_allow_disk_access(FDC_DENY_DISK_ACCESS);	// deny now!
	// register event for the READY. prompt for re-enable
	inject_register_ready_status("Disk access re-enabled", allow_disk_access_callback, NULL);
}


int inject_register_prg ( const char *prg_fn, int prg_mode )
{
	prg.stream = NULL;
	prg.size = xemu_load_file(prg_fn, NULL, 3, 0xFFFF, "Cannot load PRG to be injected");
	if (prg.size < 3)
		goto error;
	prg.stream = xemu_load_buffer_p;
	xemu_load_buffer_p = NULL;
	prg.load_addr = prg.stream[0] + (prg.stream[1] << 8);
	prg.size -= 2;
	memmove(prg.stream, prg.stream + 2, prg.size);
	// TODO: needs to be fixed to check ROM boundary (or eg from addr zero)
	if (prg.load_addr + prg.size > 0xFFFF) {
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


static int is_ready_on_screen ( void )
{
	static const Uint8 ready[] = { 0x12, 0x05, 0x01, 0x04, 0x19, 0x2E };	// "READY." in screen codes
	int width = get_screen_width();
	// TODO: this should be revised in the future, as MEGA65 can other means have
	// different screen starting addresses, and not even dependent on the 40/80 column mode!!!
	int start = (width == 80) ? 2048 : 1024;
	// Check every lines of the screen (not the "0th" line, because we need "READY." in the previous line!)
	// NOTE: I cannot rely on exact position as different ROMs can have different line position for the "READY." text!
	for (int i = 1; i < 23; i++) {
		// We need this pointer later, to "fake" a command on the screen
		under_ready_p = main_ram + start + i * width;
		// 0XA0 -> cursor is shown, and the READY. in the previous line
		if (*under_ready_p == 0xA0 && !memcmp(under_ready_p - width, ready, sizeof ready))
			return 1;
	}
	return 0;
}


void inject_ready_check_do ( void )
{
	if (XEMU_LIKELY(!inject_ready_check_status))
		return;
	if (inject_ready_check_status == 1) {		// we're in "waiting for READY." phase
		if (is_ready_on_screen())
			inject_ready_check_status = 2;
	} else if (inject_ready_check_status == 100) {	// special mode ...
		// This is used to check the @ char printed by our tricky RUN line to see it's time to release RETURN (or just simply clear all the keyboard)
		Uint8 *p = under_ready_p + get_screen_width();
		if (*p == 0x00) {
			inject_ready_check_status = 0;
			clear_emu_events();		// reset keyboard state & co
			DEBUGPRINT("INJECT: clearing keyboard status on '@' trigger." NL);
			memset(under_ready_p, ' ', 10);
			CBM_SCREEN_PRINTF(p, "RUN:");
		}
	} else if (inject_ready_check_status > 10) {
		inject_ready_check_status = 0;	// turn off "ready check" mode, we have our READY.
		inject_ready_callback(inject_ready_userdata);	// callback is activated now
	} else
		inject_ready_check_status++;		// we're "let's wait some time after READY." phase
}
