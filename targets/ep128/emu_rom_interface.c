/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "enterprise128.h"
#include "emu_rom_interface.h"
#include "xemu/../rom/ep128/xep_rom_syms.h"
#include "xemu/z80.h"
#include "cpu.h"
#include "roms.h"
#include "emu_monitor.h"
#include "configuration.h"
#include "fileio.h"

#include <unistd.h>
#include <time.h>

#define XEPSYM_ADDR(sym) (xep_rom_addr + (sym) - 0xC000)
#define XEPSYM_P(sym) (memory + XEPSYM_ADDR(sym))
#define COBUF ((char*)XEPSYM_P(xepsym_cobuf))
#define SET_XEPSYM_BYTE(sym, value) *XEPSYM_P(sym) = (value)
#define SET_XEPSYM_WORD(sym, value) do {	\
	SET_XEPSYM_BYTE(sym, (value) & 0xFF);	\
	SET_XEPSYM_BYTE((sym) + 1, (value) >> 8);	\
} while(0)
#define BIN2BCD(bin) ((((bin) / 10) << 4) | ((bin) % 10))

static const char EXOS_NEWLINE[] = "\r\n";
Uint8 exos_version = 0;
Uint8 exos_info[8];

#define EXOS_ADDR(n)		(0x3FC000 | ((n) & 0x3FFF))
#define EXOS_BYTE(n)		memory[EXOS_ADDR(n)]
#define EXOS_GET_WORD(n)	(EXOS_BYTE(n) | (EXOS_BYTE((n) + 1) << 8))



void exos_get_status_line ( char *buffer )
{
	Uint8 *s = memory + EXOS_ADDR(EXOS_GET_WORD(0xBFF6));
	int a = 40;
	while (a--)
		*(buffer++) = *(s++) & 0x7F;
	*buffer = '\0';
}


void xep_set_error ( const char *msg )
{
	int l = strlen(msg);
	if (l > 63)
		l = 63;
	SET_XEPSYM_BYTE(xepsym_error_message_buffer, l);
	memcpy(XEPSYM_P(xepsym_error_message_buffer + 1), msg, l);
	DEBUG("XEP: error msg set len=%d len_stored=%d \"%s\"" NL, l, *XEPSYM_P(xepsym_error_message_buffer), msg);
}


void xep_set_time_consts ( char *descbuffer )
{
	struct tm *t = localtime(&unix_time);
	SET_XEPSYM_BYTE(xepsym_settime_hour,    BIN2BCD(t->tm_hour));
	SET_XEPSYM_WORD(xepsym_settime_minsec,  (BIN2BCD(t->tm_min) << 8) | BIN2BCD(t->tm_sec));
	SET_XEPSYM_BYTE(xepsym_setdate_year,    BIN2BCD(t->tm_year - 80));
	SET_XEPSYM_WORD(xepsym_setdate_monday,  (BIN2BCD(t->tm_mon + 1) << 8) | BIN2BCD(t->tm_mday));
	if (descbuffer)
		sprintf(descbuffer, "%04d-%02d-%02d %02d:%02d:%02d",
			t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
			t->tm_hour, t->tm_min, t->tm_sec
		);
	SET_XEPSYM_WORD(xepsym_jump_on_rom_entry, xepsym_set_time);
}



/* Sets XEP ROM jump to the code fragment to utilize EXOS 19 call
   to set default device name provided, with also storing the given
   name */
void xep_set_default_device_name ( const char *name )
{
	int l;
	if (!name)
		name = config_getopt_str("ddn");
	if (!strcasecmp(name, "none"))
		name = "";
	l = strlen(name);
	if (l < 16) {
		if (l)
			memcpy(XEPSYM_P(xepsym_default_device_name_string + 1), name, l);
		SET_XEPSYM_BYTE(xepsym_default_device_name_string, l);
		SET_XEPSYM_WORD(xepsym_jump_on_rom_entry, xepsym_set_default_device_name);
		SET_XEPSYM_BYTE(xepsym_set_default_device_name_is_file_handler, strncasecmp(name, "TAPE", 4) ? 1 : 0);
	} else
		ERROR_WINDOW("Too long default device name is tried to be set, ignoring!");
}



static int exos_cmd_name_match ( const char *that, Uint16 addr )
{
	if (strlen(that) != Z80_B) return 0;
	while (*that)
		if (*(that++) != read_cpu_byte(addr++))
			return 0;
	return 1;
}



static void xep_exos_command_trap ( void )
{
	Uint8 c = Z80_C, b = Z80_B;
	Uint16 de = Z80_DE;
	int size;
	*COBUF = 0; // no ans by def
	DEBUG("XEP: COMMAND TRAP: C=%02Xh, B=%02Xh, DE=%04Xh" NL, c, b, de);
	/* restore exos command handler jump address */
	SET_XEPSYM_WORD(xepsym_jump_on_rom_entry, xepsym_print_xep_buffer);
	switch (c) {
		case 2: // EXOS command
			if (exos_cmd_name_match("XEP", de + 1)) {
				char buffer[256];
				char *p = buffer;
				b = read_cpu_byte(de) - 3;
				de += 4;
				while (b--)
					*(p++) = read_cpu_byte(de++);
				*p = '\0';
				monitor_execute(
					buffer,			// input buffer
					1,			// source system (XEP ROM)
					COBUF,			// output buffer (directly into the co-buffer area!)
					xepsym_cobuf_size - 1,	// max allowed output size
					EXOS_NEWLINE		// newline delimiter requested (for EXOS we use this fixed value! unlike with console/monitor where it's host-OS dependent!)
				);
				Z80_A = 0;
				Z80_C = 0;
			}
			break;
		case 3: // EXOS help
			if (!b) {
				// eg on :HELP (ROM list) we patch the request as ROMNAME monitor command ...
				monitor_execute("ROMNAME", 1, COBUF, xepsym_cobuf_size - 1, EXOS_NEWLINE);
				Z80_A = 0;
			} else if (exos_cmd_name_match("XEP", de + 1)) {
				monitor_execute("HELP", 1, COBUF, xepsym_cobuf_size - 1, EXOS_NEWLINE);
				Z80_A = 0;
				Z80_C = 0;
			}
			break;
		case 8:	// Initialization
			// Tell XEP ROM to set EXOS date/time with setting we will provide here
			xep_set_time_consts(NULL);
			SET_XEPSYM_WORD(xepsym_jump_on_rom_entry, xepsym_system_init);
			break;
		case 1:	// Cold reset (app program can take control here) after the copyright msg ...
			SET_XEPSYM_WORD(xepsym_jump_on_rom_entry, xepsym_cold_reset);
			break;
		case 5:	// explain error code ...
			DEBUG("XEP: explain error code of %02Xh our=%d" NL, Z80_B, Z80_B == XEP_ERROR_CODE);
			if (Z80_B == XEP_ERROR_CODE) {
				Z80_B = xep_rom_seg;
				Z80_DE = xepsym_error_message_buffer;
				Z80_C = 0; // signal that we recognized the error code
			}
			break;
	}
	size = strlen(COBUF);
	if (size)
		DEBUG("XEP: ANSWER: [%d bytes] = \"%s\"" NL, size, COBUF);
	// just a sanity check, monitor_execute() would not allow - in theory ... - to store more data than specified (by MPRINTF)
	if (size > xepsym_cobuf_size - 1)
		FATAL("FATAL: XEP ROM answer is too large, %d bytes.", size);
	SET_XEPSYM_WORD(xepsym_print_size, size);	// set print-out size (0 = no print)
}



void xep_rom_trap ( Uint16 pc, Uint8 opcode )
{
	xep_rom_write_support(0);	// to be safe, let's switch writable XEP ROM off (maybe it was enabled by previous trap?)
	DEBUG("XEP: ROM trap at PC=%04Xh OPC=%02Xh" NL, pc, opcode);
	if (opcode != xepsym_ed_trap_opcode)
		FATAL("FATAL: Unknown ED-trap opcode in XEP ROM: PC=%04Xh ED_OP=%02Xh", pc, opcode);
	switch (pc) {
		case xepsym_trap_enable_rom_write:
			DEBUG("XEP: write access to XEP ROM was requested" NL);
			xep_rom_write_support(1);	// special ROM request to enable ROM write ... Danger Will Robinson!!
			break;
		case xepsym_trap_exos_command:
			xep_exos_command_trap();
			break;
		case xepsym_trap_on_system_init:
			exos_version = Z80_B;	// store EXOS version number we got ...
			memcpy(exos_info, memory + ((xepsym_exos_info_struct & 0x3FFF) | (xep_rom_seg << 14)), 8);
			if (config_getopt_int("skiplogo")) {
				DEBUG("XEP: skiplogo option requested logo skip, etting EXOS variable 0xBFEF to 1 on system init ROM call" NL);
				EXOS_BYTE(0xBFEF) = 1; // use this, to skip Enterprise logo when it would come :-)
			}
			break;
		default:
			FATAL("FATAL: Unknown ED-trap location in XEP ROM: PC=%04Xh (ED_OP=%02Xh)", pc, opcode);
			break;
		//
		/* ---- FILEIO RELATED TRAPS ---- */
		//
		case xepsym_trap_set_default_device_name_feedback:
			// TODO: handle this :)
			break;
		case xepsym_fileio_open_channel_remember:
			fileio_func_open_channel_remember();
			break;
		case xepsym_fileio_no_used_call:
			fileio_func_not_used_call();
			break;
		case xepsym_fileio_open_channel:
			fileio_func_open_or_create_channel(0);
			break;
		case xepsym_fileio_create_channel:
			fileio_func_open_or_create_channel(1);
			break;
		case xepsym_fileio_close_channel:
			fileio_func_close_channel();
			break;
		case xepsym_fileio_destroy_channel:
			fileio_func_destroy_channel();
			break;
		case xepsym_fileio_read_character:
			fileio_func_read_character();
			break;
		case xepsym_fileio_read_block:
			fileio_func_read_block();
			break;
		case xepsym_fileio_write_character:
			fileio_func_write_character();
			break;
		case xepsym_fileio_write_block:
			fileio_func_write_block();
			break;
		case xepsym_fileio_channel_read_status:
			fileio_func_channel_read_status();
			break;
		case xepsym_fileio_set_channel_status:
			fileio_func_set_channel_status();
			break;
		case xepsym_fileio_special_function:
			fileio_func_special_function();
			break;
		case xepsym_fileio_init:
			fileio_func_init();
			break;
		case xepsym_fileio_buffer_moved:
			fileio_func_buffer_moved();
			break;
	}
}

