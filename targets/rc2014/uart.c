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
#include "uart.h"

#define UART_START_BITS 1
#define UART_STOP_BITS 1
#define UART_HAS_PARITY_BIT 0

int cpu_cycles_per_uart_transfer;

void uart_init ( int baud_crystal_hz, int baud_rate, int cpu_hz )
{
	cpu_cycles_per_uart_transfer = (8 + UART_START_BITS + UART_STOP_BITS + UART_HAS_PARITY_BIT) * cpu_hz / baud_rate;
	DEBUGPRINT("UART: %d CPU cycles (CPU @ %.2fMHz) per UART character transfer" NL, cpu_cycles_per_uart_transfer, (double)cpu_hz / 1000000.0);
}
