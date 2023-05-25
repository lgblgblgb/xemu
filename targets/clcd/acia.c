/* Commodore LCD emulator (rewrite of my world's first working Commodore LCD emulator)
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Part of the Xemu project: https://github.com/lgblgblgb/xemu

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
#include "acia.h"

#define ACIADEBUG DEBUGPRINT
//#define ACIADEBUG DEBUG

static Uint8 status_register;
static Uint8 control_register;
static Uint8 command_register;
static Uint8 rx_data_register;
static Uint8 tx_data_register;
static acia_interrupt_setter_t interrupt_setter;

#define BIT0	0x01
#define BIT1	0x02
#define BIT2	0x04
#define BIT3	0x08
#define BIT4	0x10
#define BIT5	0x20
#define BIT6	0x40
#define BIT7	0x80



static void redistribute_interrupt ( void )
{
	//return;	// REMOVE THIS FIXME TODO
	static Uint8 old_status = 0;
	//ACIADEBUG("ACIA: interrupt change %d->%d" NL, old_status & BIT7, status_register & BIT7);
	if ((old_status ^ status_register) & BIT7) {
		ACIADEBUG("ACIA: interrupt goes %s" NL, status_register & BIT7 ? "ON" : "off");
		interrupt_setter(!!(status_register & BIT7));
		old_status = status_register;
	}
}


static void recalc_interrupts ( void )
{
	if (
		((status_register & BIT4) && (((command_register >> 2) & 3) == 1)) ||	// TX interrupt
		((status_register & BIT3) && (command_register & BIT1))			// RX interrupt
	) {
		status_register |= BIT7;
		redistribute_interrupt();
	}
}


void acia_reset ( void )
{
	ACIADEBUG("ACIA: reset" NL);
	rx_data_register = 0;
	status_register = BIT4;	// TX is empty
	command_register = 0;
	control_register = 0;
	recalc_interrupts();	// needed, otherwise ACIA in reset wouldn't stop emitting IRQ
}


void acia_init ( acia_interrupt_setter_t int_setter )
{
	interrupt_setter = int_setter;
	acia_reset();
}


static void submit_for_rx ( const Uint8 data )
{
	if ((status_register & BIT3)) {	// RX buffer is already full
		// FIXME-TODO
	}
	rx_data_register = data;
	status_register |= BIT3;
}


static void SIMULATE ( void )
{
	status_register |= BIT4;	// TX is "done" (TX is empty)
	submit_for_rx(tx_data_register);	// simulate loopback
}


void acia_write_reg ( const int reg, const Uint8 data )
{
	switch (reg) {
		case 0:
			ACIADEBUG("ACIA: writing register 0: transmit data (data=$%02X)" NL, data);
			status_register &= ~BIT4;		// bit 4 of status register is cleared when writing new data (WARNING: it seems on 65C51N - but not on S - this is buggy and never cleared????)
			tx_data_register = data;
			SIMULATE();
			recalc_interrupts();
			return;
		case 1:	// status register, BUT on writing, it means "programmed reset", which is NOT the very same as hardware reset
			ACIADEBUG("ACIA: writing register 1: programmed reset" NL);
			status_register &= 4;		// clear bit 2 (overrun)
			command_register &= 0xE0;	// only keep upper three bits
			recalc_interrupts();
			return;
		case 2:	// command register
			ACIADEBUG("ACIA: writing register 2: command (data=$%02X)" NL, data);
			command_register = data;
			recalc_interrupts();
			return;
		case 3:	// control register
			ACIADEBUG("ACIA: writing register 3: control (data=$%02X)" NL, data);
			control_register = data;
			return;
		default:
			FATAL("%s(): invalid register: %d", __func__, reg);
	}
}


Uint8 acia_read_reg ( const int reg )
{
	Uint8 data;
	switch (reg) {
		case 0:
			ACIADEBUG("ACIA: reading register 0: received data (data=$%02X)" NL, rx_data_register);
			status_register &= ~BIT3;	// bit 3 of status register is cleared on reading received data register
			recalc_interrupts();
			return rx_data_register;
		case 1:	// status register
			data = status_register;
			status_register &= ~BIT7;	// clear interrupt (status bit 7) on reading status register
			if (data)
				ACIADEBUG("ACIA: reading register 1: status (status=$%02X)" NL, data);
			redistribute_interrupt();	// do NOT recalc interrupts, only distribute the change!
			return data | BIT5 | BIT6;	// FIXME: the bits 5/6 setting is for test only!
		case 2:	// command register
			ACIADEBUG("ACIA: reading register 2: command (value=$%02X)" NL, command_register);
			return command_register;
		case 3:	// control register
			ACIADEBUG("ACIA: reading register 3: control (value=$%02X)" NL, control_register);
			return control_register;
		default:
			FATAL("%s(): invalid register: %d", __func__, reg);
	}
}


void acia_polling ( void )
{
}
