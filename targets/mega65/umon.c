/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2026 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#if !defined(HAVE_XEMU_UMON)
#warning "Platform does not support UMON"
// TODO: later, we should make umon compilable without socket support, so it can be
// still usable via matrix mode at last!
#else

#include "xemu/emutools.h"
#include "xemu/emutools_umon.h"
#include "umon.h"
#include "xemu/cpu65.h"
#include "memory_mapper.h"
#include <string.h>

#define MAX_INPUT_SIZE 128

static struct {
	int	output_capacity;
	char	*output_ptr;
} umon;



static void umon_printf ( const char *fmt, ... )
{
	if (!umon.output_capacity) {
		DEBUGPRINT("UMON: umon_printf() output buffer is full!" NL);
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	const int size = vsnprintf(umon.output_ptr, umon.output_capacity, fmt, ap);
	va_end(ap);
	if (size >= umon.output_capacity) {
		umon.output_capacity = 0;
		DEBUGPRINT("UMON: umon_printf() output buffer overflow!" NL);
	} else {
		umon.output_capacity -= size;
		umon.output_ptr += size;
	}
}


static void m65mon_show_regs ( void )
{
	Uint8 pf = cpu65_get_pf();
	umon_printf(
		"\r\n"
		"PC   A  X  Y  Z  B  SP   MAPH MAPL LAST-OP     P  P-FLAGS   RGP uS IO\r\n"
		"%04X %02X %02X %02X %02X %02X %04X "		// register banned message and things from PC to SP
		"%04X %04X %02X       %02X %02X "		// from MAPL to P
		"%c%c%c%c%c%c%c%c \r\n"				// P-FLAGS
		",0777%04X\r\n", // TODO: single line of disassembly
		cpu65.pc, cpu65.a, cpu65.x, cpu65.y, cpu65.z, cpu65.bphi >> 8, cpu65.sphi | cpu65.s,
		((map_mask & 0xf0) << 8) | (map_offset_high >> 8),
		((map_mask & 0x0f) << 12)  | (map_offset_low >> 8),
		cpu65.op,
		pf, 0,	// flags
		(pf & CPU65_PF_N) ? 'N' : '-',
		(pf & CPU65_PF_V) ? 'V' : '-',
		(pf & CPU65_PF_E) ? 'E' : '-',
		'-',
		(pf & CPU65_PF_D) ? 'D' : '-',
		(pf & CPU65_PF_I) ? 'I' : '-',
		(pf & CPU65_PF_Z) ? 'Z' : '-',
		(pf & CPU65_PF_C) ? 'C' : '-',
		cpu65.pc
	);
}


// umon_main_interate() call this - see above
// also, matrix monitor can call this, to implement mega65 compatible matrix commands through this function
bool umon_execute_command ( char *output, int output_maxsize, const void *input_raw, int input_size )
{
	if (input_size >= MAX_INPUT_SIZE) {
		DEBUGPRINT("UMON: too long input to execute" NL);
		return false;
	}
	char input[MAX_INPUT_SIZE];
	memcpy(input, input_raw, input_size);
	input[input_size] = '\0';
	umon.output_capacity = output_maxsize;
	umon.output_ptr = output;
	//snprintf(output, output_maxsize, "We wanna test this :) Input size was: %d bytes. Input was: %s\n", input_size, input);
	if (input[0] == 'r')
		m65mon_show_regs();
	else
		umon_printf("We wanna test this :) Input size was: %d bytes. Input was: %s\n", input_size, input);
	output[output_maxsize - 1] = '\0';	// just to be sure in case of full buffer that it's still closed
	return true;
}


// Must be called by the main program, to dispatch queued commands from the network listener
// to execute, and then queueing back the answer.
void umon_main_iterate ( void )
{
	// TODO/FIXME: the network listener may break two messages at the wrong boundary! We may need
	// to handle the situation!
	struct xumon_com_st monres;
	while (XEMU_UNLIKELY(xumon_get_request(&monres))) {
		DEBUGPRINT("UMON: got request, %d bytes" NL, monres.size);
		char buffer[256];
		const bool ret = umon_execute_command(buffer, sizeof buffer, (char*)monres.data, monres.size);
		xumon_end_request(&monres);	// this will free the request, data of the request may be invalid after this!
		if (ret) {
			// We still need to use the same xumon_com_st structure as the answer must have the same "ptr" and "seq" values got by xumon_get_request()
			monres.data = (void*)buffer;	// set data pointer to the output data
			monres.size = strlen(buffer);	// ... and the size
			xumon_set_answer(&monres);
		}
	}
}


// Used to format the answer for_other purposes than the in-protocol usage (like for matrix use, debugging output or whatever)
void umon_format_answer_text ( char *output )
{
	char *s = output, *t = output;
	while (*s == '\n' || *s == '\r')
		s++;
	while (*s)
		if (s[0] == '\r' && s[1] == '\n')
			*t++ = '\n', s += 2;
		else
			*t++ = *s++;
	while (t > output && strchr("\n\r.,", *--t))
		;
	t[1] = '\0';
}

#endif
