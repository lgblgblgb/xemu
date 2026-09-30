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

#define MAX_INPUT_SIZE		128
#define UMON_SYNTAX_ERROR	"?SYNTAX ERROR  "

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

static void m65mon_dumpmem28 ( int addr )
{
	umon_printf(":%08X:", addr & 0xFFFFFFF);
	for (int k = 0; k < 16; k++, addr++)
		if ((addr & 0xFFF0000) == 0x7770000)
			umon_printf("%02X", debug_read_cpu_byte(addr & 0xFFFF));
		else
			umon_printf("%02X", debug_read_linear_byte(addr & 0xFFFFFFF));
}

static void m65mon_setmem28 ( int addr, int cnt, const Uint8 *vals )
{
	for (; cnt > 0; cnt--, addr++, vals++)
		if ((addr & 0xFFF0000) == 0x7770000)
			debug_write_cpu_byte(addr & 0xFFFF, *vals);
		else
			debug_write_linear_byte(addr & 0xFFFFFFF, *vals);
}

static void m65mon_setmem28_byte ( const int addr, const Uint8 byte )
{
	m65mon_setmem28(addr, 1, &byte);
}



static char *parse_hex_arg ( char *p, int *val, const int min, const int max )
{
	while (*p == 32)
		p++;
	*val = -1;
	if (!*p) {
		umon_printf(UMON_SYNTAX_ERROR "unexpected end of command (no parameter)");
		return NULL;
	}
	int r = 0;
	for (;;) {
		if (*p >= 'a' && *p <= 'f')
			r = (r << 4) | (*p - 'a' + 10);
		else if (*p >= 'A' && *p <= 'F')
			r = (r << 4) | (*p - 'A' + 10);
		else if (*p >= '0' && *p <= '9')
			r = (r << 4) | (*p - '0');
		else if (*p == 32 || *p == 0)
			break;
		else {
			umon_printf(UMON_SYNTAX_ERROR "invalid data as hex digit '%c'", *p);
			return NULL;
		}
		p++;
	}
	*val = r;
	if (r < min || r > max) {
		umon_printf(UMON_SYNTAX_ERROR "command parameter's value is outside of the allowed range for this command %X (%X...%X)", r, min, max);
		return NULL;
	}
	return p;
}


static int check_end_of_command ( const char *p, const bool error_out )
{
	while (*p == 32)
		p++;
	if (*p) {
		if (error_out)
			umon_printf(UMON_SYNTAX_ERROR "unexpected command parameter");
		return 0;
	}
	return 1;
}


static void cmd_setmem ( char *param, int addr )
{
	char *orig_param = param;
	int cnt = 0;
	// get param count
	while (param && !check_end_of_command(param, false)) {
		int val;
		param = parse_hex_arg(param, &val, 0, 0xFF);
		cnt++;
	}
	param = orig_param;
	for (int idx = 0; idx < cnt; idx++) {
		int val;
		param = parse_hex_arg(param, &val, 0, 0xFF);
		m65mon_setmem28_byte(addr, val);
		addr++;
	}
}


static void cmd_fillmem ( char *param, int addr )
{
	//char *orig_param = param;
	int endaddr;
	int val;
	if (param && !check_end_of_command(param, false))
		param = parse_hex_arg(param, &endaddr, 0, 0xFFFFFFF);
	else
		return;
	if (param && !check_end_of_command(param, false))
		param = parse_hex_arg(param, &val, 0, 0xFF);
	else
		return;
	for (int k = addr; k < endaddr; k++)
		m65mon_setmem28_byte(k, val);
}


// umon_main_interate() call this - see that after this function
// also, matrix monitor can call this, to implement mega65 compatible matrix commands through this function
bool umon_execute_command ( char *output, unsigned int output_maxsize, const char *input_raw, unsigned int input_size )
{
	while (input_size > 0 && *input_raw <= 32)
		input_raw++, input_size--;
	if (input_size >= MAX_INPUT_SIZE) {
		DEBUGPRINT("UMON: too long input to execute" NL);
		return false;
	}
	char cmd_buffer[input_size + 1];
	memcpy(cmd_buffer, input_raw, input_size);
	cmd_buffer[input_size] = '\0';
	// Prepare for output
	umon.output_capacity = output_maxsize;
	umon.output_ptr = output;
	// Part of old code, should be refactored at some point
	char *p = cmd_buffer;
	while (*p)
		if (*p == '\t')
			*(p++) = 32;
		else if (*p == 8 && p > cmd_buffer)
			memmove(p - 1, p + 1, strlen(p + 1) + 1);
		else if ((unsigned char)*p > 127 || (unsigned char)*p < 32) {
			umon_printf(UMON_SYNTAX_ERROR "invalid character in the command (ASCII=%d)", *p);
			return true;
		} else
			p++;
	p--;
	while (p >= cmd_buffer && *p <= 32)
		*(p--) = 0;
	char *cmd = cmd_buffer;
	DEBUG("UARTMON: command got \"%s\" (%d bytes)." NL, cmd, (int)strlen(cmd));
	int par1;
	switch (*(cmd++)) {
		case 'h':
		case 'H':
		case '?':
			if (check_end_of_command(cmd, true))
				umon_printf("Xemu/MEGA65 Serial Monitor\r\nWarning: not 100%% compatible with UART monitor of a *real* MEGA65 ...");
			break;
		case 'r':
		case 'R':
			if (check_end_of_command(cmd, true))
				m65mon_show_regs();
			break;
		case 'm':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			if (cmd && check_end_of_command(cmd, true)) {
				m65mon_dumpmem28(par1);
			}
			break;
		case 'M':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			if (cmd && check_end_of_command(cmd, true)) {
				for (int k = 0; k < 16; k++) {
					m65mon_dumpmem28(par1);
					par1 += 16;
					umon_printf("\n");
				}
			}
			break;
		case 's':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			cmd_setmem(cmd, par1);
			break;
		case 'f':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			cmd_fillmem(cmd, par1);
			break;
		default:
			DEBUGPRINT("UMON: unknown command received: %s" NL, cmd - 1);
			umon_printf(UMON_SYNTAX_ERROR "unknown (or not implemented) command '%c'", cmd[-1]);
			break;
	}
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
	int trigger_submit = 0;
	while (XEMU_UNLIKELY(xumon_get_request(&monres))) {
		DEBUGPRINT("UMON: got request, %d bytes" NL, monres.size);
		char buffer[256];
		const bool ret = umon_execute_command(buffer, sizeof buffer, (char*)monres.data, monres.size);
		xumon_free_request(&monres);	// this will free the request, data part of the request (monres.data) may be invalid after this!
		if (ret) {
			// We still need to use the same xumon_com_st structure as the answer must have the same "ptr" and "seq" values got by xumon_get_request()
			monres.data = (void*)buffer;	// set data pointer to the output data (previously it means the input request, but for calling xumon_set_answer() it's the answer already)
			monres.size = strlen(buffer);	// ... and the size
			if (monres.need_prompt && monres.size + 10 < sizeof buffer) {
				strcpy(buffer + monres.size, monres.size > 0 && buffer[monres.size - 1] == '\n' ? ".\r\n": "\r\n.\r\n");
				monres.size = strlen(buffer);
			}
			xumon_set_answer(&monres);
			trigger_submit++;
		}
	}
	if (XEMU_UNLIKELY(trigger_submit))
		xumon_trigger_sending(trigger_submit);
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


int umon_init ( const int port )
{
	return xumon_init(port, true);
}


int umon_stop ( void )
{
	return xumon_stop();
}


int umon_get_port ( void )
{
	return xumon_running ? xumon_port : 0;
}

#endif
