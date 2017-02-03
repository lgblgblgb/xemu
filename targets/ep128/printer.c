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

#include "xep128.h"
#include "printer.h"
#include "dave.h"
#include "configuration.h"

#include "main.h"


static FILE *fp = NULL;
static int fp_to_open = 1;

#define BUFFER_SIZE 1024
#define COVOX_ACTIVATION_LIMIT 0x100

static Uint8 buffer[BUFFER_SIZE];
static int buffer_pos;
static int strobes_missed = 0;
Uint8 printer_data_byte = 0xFF;
static int printer_is_covox = 0;
static int covox_to_warn = 1;
static int old_strobe_level = 0;




static void write_printer_buffer ( void )
{
	if (buffer_pos && fp != NULL) {
		if (fwrite(buffer, buffer_pos, 1, fp) != 1) {
			WARNING_WINDOW("Cannot write printer output: %s\nFurther printer I/O has been disabled.", ERRSTR());
			fclose(fp);
			fp = NULL;
		}
	}
	buffer_pos = 0;
}



void printer_close ( void )
{
	if (fp) {
		write_printer_buffer();
		fclose(fp);
		DEBUG("Closing printer output file." NL);
		fp_to_open = 1;
		fp = NULL;
	}
}



void printer_port_set_data ( Uint8 data )
{
	printer_data_byte = data;
	if (strobes_missed > COVOX_ACTIVATION_LIMIT) {
		if (!printer_is_covox) {
			DEBUG("PRINTER: COVOX: covox mode has been activated on more than %d writes without STROBE" NL, COVOX_ACTIVATION_LIMIT);
			printer_is_covox = 1;
			if (covox_to_warn) {
				covox_to_warn = 0;
				INFO_WINDOW("COVOX on printer port has been activated. There will be no further messages on this.");
			}
			audio_source = AUDIO_SOURCE_PRINTER_COVOX;
		}
	} else
		strobes_missed++;
}



static void send_data_to_printer ( Uint8 data )
{
	//DEBUG("PRINTER GOT DATA: %d" NL, data);
	if (fp_to_open) {
		const char *printfile = config_getopt_str("printfile");
		char path[PATH_MAX + 1];
		fp = open_emu_file(printfile, "ab", path);
		if (fp == NULL)
			WARNING_WINDOW("Cannot create/append printer output file \"%s\": %s.\nYou can use Xep128 but printer output will not be logged!", path, ERRSTR());
		else
			INFO_WINDOW("Printer event, file \"%s\" has been opened for the output.", path);
		fp_to_open = 0;
		buffer_pos = 0;
	}
	if (fp != NULL) {
		buffer[buffer_pos++] = data;
		if (buffer_pos == BUFFER_SIZE)
			write_printer_buffer();
		// fprintf(fp, "%c", data);
	}
}



void printer_port_check_strobe ( int level )
{
	if (old_strobe_level && !level) {
		DEBUG("PRINTER: strobe!" NL);
		//old_strobe_level = level;
		strobes_missed = 0;
		if (printer_is_covox) {
			DEBUG("PRINTER: COVOX: covox mode has been disabled on STROBE, data byte %02Xh is sent for printing" NL, printer_data_byte);
			printer_is_covox = 0;
			audio_source = AUDIO_SOURCE_DAVE;
		}
		send_data_to_printer(printer_data_byte);
	}/* else
		DEBUG("PRINTER: NOT strobe: %d -> %d" NL, old_strobe_level, level);*/
	old_strobe_level = level;
}



void printer_disable_covox ( void )
{
	if (printer_is_covox) {
		printer_is_covox = 0;
		strobes_missed = 0;
		DEBUG("PRINTER: COVOX: covox mode has been disabled on emulator event." NL);
	}
}

