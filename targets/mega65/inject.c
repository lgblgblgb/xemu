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


int inject_ready_check_status = 0;

static void *inject_ready_userdata;
static void (*inject_ready_callback)(void*);

static struct {
	Uint8 *stream;
	int   size;
	int   load_addr;
	int   c64_mode;
	int   basic;
} prg;

static void prg_inject_callback ( void *unused )
{
	DEBUGPRINT("!! INJECT NOW !! %d bytes from $%04X" NL, prg.size, prg.load_addr);
	memcpy(main_ram + prg.load_addr, prg.stream + 2, prg.size - 2);
	OSD(-1,-1,"INJECTED!!!");
	free(prg.stream);
}


int inject_register_ready_status ( const char *debug_msg, void (*callback)(void*), void *userdata )
{
	if (inject_ready_check_status) {
		DEBUGPRINT("ERROR: INJECT: cannot register READY. event, already having one in progress!" NL);
		return 1;
	}
	DEBUGPRINT("INJECT: registering READY. event: %s" NL, debug_msg);
	inject_ready_userdata = userdata;
	inject_ready_callback = callback;
	inject_ready_check_status = 1;
	memset(main_ram + 1024, 0, 1024 * 3);	// be sure, no READY. can be seen already on the screen
	return 0;
}


int inject_register_prg ( const char *prg_fn )
{
	prg.stream = NULL;
	prg.size = xemu_load_file(prg_fn, NULL, 3, 0xFFFF, "Cannot load PRG to be injected");
	if (prg.size <= 0)
		goto error;
	prg.stream = xemu_load_buffer_p;
	xemu_load_buffer_p = NULL;
	prg.load_addr = prg.stream[0] + (prg.stream[1] << 8);
	if (prg.load_addr + prg.size > 0xFFFF) {
		ERROR_WINDOW("Program to be injected is too large (%d bytes, load address: $%04X)\nFile: %s",
			prg.size,
			prg.load_addr,
			prg_fn
		);
		goto error;
	}
	if (prg.load_addr == 0x0801) {
		prg.c64_mode = 1;
		prg.basic = 1;
	} else if (prg.load_addr == 0x1001) {
		prg.c64_mode = 0;
		prg.basic = 1;
	} else {
		char msg[512];
		snprintf(msg, sizeof msg, "PRG to load: %s\nCannot detect C64/C65 mode for non-BASIC (load address: $%04X) PRG, please specify:", prg_fn, prg.load_addr);
		prg.c64_mode = QUESTION_WINDOW("C64|C65|Ouch, CANCEL!", msg);
		if (prg.c64_mode == 2)
			goto error;
		prg.basic = 0;
	}
	


	if (inject_register_ready_status("PRG memory injection", prg_inject_callback, NULL)) // prg inject does not use the userdata ...
		goto error;
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
	static const Uint8 ready[] = { 0x12, 0x05, 0x01, 0x04, 0x19, 0x2E };
	// C65 $D031.7 VIC-III:H640 Enable C64 640 horizontal pixels / 80 column mode
	int start, width;
	if ((vic_registers[0x31] & 0x80)) {
		start = 2048;
		width = 80;
	} else {
		start = 1024;
		width = 40;
	}
	for (int i = 0; i < 25; i++)
		if (main_ram[start + (i + 1) * width] == 0xA0 && !memcmp(main_ram + start + (i * width), ready, sizeof ready)) {
			//DEBUGPRINT("CURSOR block (line=%d,start=$%04X,width=%d) is: $%02X, colour matrix is: $%02X" NL, i, start,width, main_ram[start + (i + 1) * width], colour_ram[(i + 1) * width]);
			//DEBUGPRINT(">> FOUND READY + CURSOR <<" NL);
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
	} else if (inject_ready_check_status > 10) {
		if (is_ready_on_screen()) {
			inject_ready_callback(inject_ready_userdata);
			inject_ready_check_status = 0;	// turn off "ready check" mode, we're done!
		} else {
			inject_ready_check_status = 1;	// unknown problem, let's restart the process of waiting READY. ...
		}
	} else
		inject_ready_check_status++;		// we're "let's wait some time after READY." phase
}
