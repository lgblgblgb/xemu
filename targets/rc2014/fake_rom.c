/* RC2014 and generic Z80 SBC emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "hardware.h"
#include "fake_rom.h"
#include "console.h"
#include "uart.h"



static inline void start_shell ( const char *reason, int code )
{
	if (reason) {
		if (code != -1)
			conprintf("ABORT: %s %d\r\n", reason, code);
		else
			conprintf("ABORT: %s\r\n", reason);
	}
	Z80_PC = 2;
	Z80_SP = 0xFFFF;
}


static void api_call ( Uint8 sysnum )
{
	switch (sysnum) {
		case 0:
			conputs("System RESET requested via API call.\r\n");
			Z80_PC = 0;
			break;
		case 1:
			{
			int r = console_input();
			if (!r)
				Z80_PC = 0x30;	// still waiting for character, loop back to the api call RST
			else
				Z80_A = r;
			}
			break;
		case 2:
			console_output(Z80_A);
			break;
		case 7:
			conputs("\r\n");
			break;
		default:
			start_shell("Unknown/not-yet implemented API call C=", Z80_C);
			break;
	}
}


void xrcrom_rst ( int n )
{
	console_io_traffic = 0;
	switch (n) {
		case 0:	// RST 00
			conputs("RESET, Xemu/RC2014 internal ROM\r\n(C)2020 LGB Gabor Lenart, part of the Xemu project.\r\n");
			start_shell(NULL, -1);
			break;
		case 6: // RST 30
			api_call(Z80_C);
			break;
		case 1:	// RST 08
		case 2: // RST 10
		case 3: // RST 18
		case 4: // RST 20
		case 5: // RST 28
		case 7: // RST 38
			start_shell("not implemented rst handler", n * 8);
			break;
		default:
			start_shell("UNKNOWN rst handler", n);
			break;
	}
	io_cycles = console_io_traffic * cpu_cycles_per_uart_transfer;
	DEBUGPRINT("TIMING: %d I/O cycles" NL, io_cycles);
}


static void shell_execute ( char *cmd )
{
	conprintf("?%s\r\n", cmd);
}


static char command_buffer[127];
static int shell_mode;

void xrcrom_begin ( void )
{
	command_buffer[0] = 0;
	console_output('*');
	shell_mode = 0;
}

void xrcrom_run ( void )
{
	int r = console_input();
	Z80_PC = 4;
	if (r == 0)
		return;
	int command_size = strlen(command_buffer);
	if ((r == 8 || r == 127) && command_size) {
		conputs("\010 \010");
		command_buffer[command_size - 1] = 0;
		return;
	}
	if (command_size < 30 && r >= 32 && r < 127) {
		command_buffer[command_size] = r;
		command_buffer[command_size + 1] = 0;
		console_output(r);
		return;
	}
	if ((r == 13 || r == 10) && command_size) {
		conputs("\r\n");
		shell_execute(command_buffer);
		start_shell(NULL, -1);
		return;
	}
}
