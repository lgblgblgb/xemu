/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "mega65.h"
#include "uart_monitor.h"


#if !defined(HAS_UARTMON_SUPPORT)
// Windows is not supported currently, as it does not have POSIX-standard socket interface (?).
// Also, it's pointless for emscripten, for sure.
#warning "Platform does not support UMON"
#else

#include "xemu/emutools_socketapi.h"

//#include <sys/types.h>
//#include <sys/stat.h>
//#include <fcntl.h>
//#include <errno.h>
#include <string.h>
//#include <limits.h>

#ifndef XEMU_ARCH_WIN
#include <unistd.h>
#include <sys/un.h>
//#include <sys/socket.h>
//#include <netinet/in.h>
//#include <netdb.h>
#endif


#define UNCONNECTED	XS_INVALID_SOCKET

#ifdef XEMU_ARCH_WIN
#	define PRINTF_SOCK	"%I64d"
#else
#	define PRINTF_SOCK	"%d"
#endif

typedef struct
{
	int  umon_write_size;
	int  umon_send_ok;
	char umon_write_buffer[UMON_WRITE_BUFFER_SIZE];

	xemusock_socket_t  sock_server;
	xemusock_socklen_t sock_len;
	xemusock_socket_t  sock_client;

	int  umon_write_pos, umon_read_pos;
	int  umon_echo;
	char umon_read_buffer[0x1000];

	int loadcmdflag;
	int loadcmdcurraddr;
	int loadcmdendaddr;

	char * locptr;
} comms_details_type;

#define MAXPORTS  2

static comms_details_type comdet[MAXPORTS] =
{
	[0].sock_server = UNCONNECTED,
	[0].sock_client = UNCONNECTED,
	[0].loadcmdflag = 0,
	[0].loadcmdcurraddr = 0,
	[0].loadcmdendaddr = 0,
	[0].locptr = 0,
	[1].sock_server = UNCONNECTED,
	[1].sock_client = UNCONNECTED,
	[1].loadcmdflag = 0,
	[1].loadcmdcurraddr = 0,
	[1].loadcmdendaddr = 0,
	[1].locptr = 0
};

// WARNING: This source is pretty ugly, ie not so much check of overflow of the output (write) buffer.


static char *parse_hex_arg ( char *p, int *val, int min, int max )
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



static int check_end_of_command ( char *p, int error_out )
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


static void setmem28 ( char *param, int addr )
{
	char *orig_param = param;
	int cnt = 0;
	// get param count
	while (param && !check_end_of_command(param, 0)) {
		int val;
		param = parse_hex_arg(param, &val, 0, 0xFF);
		cnt++;
	}
	param = orig_param;
	for (int idx = 0; idx < cnt; idx++) {
		int val;
		param = parse_hex_arg(param, &val, 0, 0xFF);
		m65mon_setmem28(addr & 0xFFFFFFF, 1, (Uint8*)&val);
		addr++;
	}
}


static void execute_command ( comms_details_type *cd, char *cmd )
{
	int bank;
	int par1;
	char *p = cmd;
	while (*p)
		if (p == cmd && (*cmd == 32 || *cmd == '\t' || *cmd == 8))
			cmd = ++p;
		else if (*p == '\t')
			*(p++) = 32;
		else if (*p == 8)
			memmove(p - 1, p + 1, strlen(p + 1) + 1);
		else if ((unsigned char)*p > 127 || (unsigned char)*p < 32) {
			umon_printf(UMON_SYNTAX_ERROR "invalid character in the command (ASCII=%d)", *p);
			return;
		} else
			p++;
	p--;
	while (p >= cmd && *p <= 32)
		*(p--) = 0;
	DEBUG("UARTMON: command got \"%s\" (%d bytes)." NL, cmd, (int)strlen(cmd));
	switch (*(cmd++)) {
		case 'h':
		case 'H':
		case '?':
			if (check_end_of_command(cmd, 1))
				umon_printf("Xemu/MEGA65 Serial Monitor\r\nWarning: not 100%% compatible with UART monitor of a *real* MEGA65 ...");
			break;
		case 'r':
		case 'R':
			if (check_end_of_command(cmd, 1))
				m65mon_show_regs();
			break;
		case 'm':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			bank = par1 >> 16;
			if (cmd && check_end_of_command(cmd, 1))
			{
				m65mon_dumpmem28(par1);
			}
			break;
		case 'M':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			bank = par1 >> 16;
			if (cmd && check_end_of_command(cmd, 1))
			{
				for (int k = 0; k < 16; k++)
				{
					if (bank == 0x777)
						m65mon_dumpmem16(par1);
					else
						m65mon_dumpmem28(par1);
					par1 += 16;
					umon_printf("\n");
				}
			}
			break;
		case 's':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			setmem28(cmd, par1);
			break;
		case 'l':
			cd->loadcmdflag = 1;
			cmd = parse_hex_arg(cmd, &cd->loadcmdcurraddr, 0, 0xFFFFFFF);
			cmd = parse_hex_arg(cmd, &cd->loadcmdendaddr, 0, 0xFFFF);
			cd->loadcmdendaddr += (cd->loadcmdcurraddr & 0xFFF0000);
			break;
		case 'g':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFF);
			m65mon_setpc(par1);
			break;
		case 't':
			if (!*cmd)
				m65mon_do_trace();
			else if (*cmd == 'c') {
				if (check_end_of_command(cmd, 1))
					m65mon_do_trace_c();
			} else {
				cmd = parse_hex_arg(cmd, &par1, 0, 1);
				if (cmd && check_end_of_command(cmd, 1))
					m65mon_set_trace(par1);
			}
			break;
		case 'b':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFF);
			if (cmd && check_end_of_command(cmd, 1))
				m65mon_breakpoint(par1);
			break;
#ifdef TRACE_NEXT_SUPPORT
		case 'N':
			m65mon_next_command();
			break;
#endif
		case 0:
			m65mon_empty_command();	// emulator can use this, if it wants
			break;
		default:
			umon_printf(UMON_SYNTAX_ERROR "unknown (or not implemented) command '%c'", cmd[-1]);
			break;
	}
}



/* ------------------------- SOCKET HANDLING, etc ------------------------- */


int uartmon_is_active ( void )
{
	for (int idx = 0; idx < MAXPORTS; idx++)
		if (comdet[idx].sock_server != UNCONNECTED)
			return 1;

	return 0;
}


int uartmon_init ( const char *fn )
{
	static char fn_stored[PATH_MAX] = "";
	int xerr;
	xemusock_socket_t sock;
	if (*fn_stored) {
		ERROR_WINDOW("UARTMON: already activated on %s", fn_stored);
		return 1;
	}
	for (int idx = 0; idx < MAXPORTS; idx++)
	{
		comdet[idx].sock_server = UNCONNECTED;
		comdet[idx].sock_client = UNCONNECTED;
	}
	if (!fn || !*fn) {
		DEBUGPRINT("UARTMON: disabled, no name is specified to bind to." NL);
		return 0;
	}
	if (xemusock_init(NULL)) {
		ERROR_WINDOW("Cannot initialize network, uart_mon won't be availbale");
		return 1;
	}
	for (int idx = 0; idx < MAXPORTS; idx++)
	{
		if (fn[0] == ':') {
			int port = atoi(fn + 1);
			if (port < 1024 || port > 65535) {
				ERROR_WINDOW("uartmon: invalid port specification %d (1024-65535 is allowed) from string %s", port, fn);
				return 1;
			}
			struct sockaddr_in sock_st;
			comdet[idx].sock_len = sizeof(struct sockaddr_in);
			//sock = socket(AF_INET, SOCK_STREAM, 0);

			sock = xemusock_create_for_inet(XEMUSOCK_TCP, XEMUSOCK_BLOCKING, &xerr);
			if (sock == XS_INVALID_SOCKET) {
				ERROR_WINDOW("Cannot create TCP socket: %s", xemusock_strerror(xerr));
				return 1;
			}
			if (xemusock_setsockopt_reuseaddr(sock, &xerr)) {
				ERROR_WINDOW("UARTMON: setsockopt for SO_REUSEADDR failed with %s", xemusock_strerror(xerr));
			}
			//sock_st.sin_family = AF_INET;
			//sock_st.sin_addr.s_addr = htonl(INADDR_ANY);
			//sock_st.sin_port = htons(curport);
			xemusock_fill_servaddr_for_inet_ip_native(&sock_st, 0, port+idx);
			if (xemusock_bind(sock, (struct sockaddr*)&sock_st, comdet[idx].sock_len, &xerr)) {
				ERROR_WINDOW("Cannot bind TCP socket %d, UART monitor cannot be used: %s", port+idx, xemusock_strerror(xerr));
				xemusock_close(sock, NULL);
				return 1;
			}
		} else {
#if !defined(XEMU_ARCH_UNIX)
			ERROR_WINDOW("On non-UNIX systems, you must use TCP/IP sockets, so uartmon parameter must be in form of :n (n=port number to bind to)\nUARTMON is not available because of bad syntax.");
			return 1;
#else
			if (idx != 0) // just open one socket for unix
				continue;
			// This is UNIX specific code (UNIX named socket) thus it's OK not use Xemu socket API calls here.
			// Note: on longer term, we want to drop this, and allow only TCP sockets to be more unified and simple.
			struct sockaddr_un sock_st;
			comdet[idx].sock_len = sizeof(struct sockaddr_un);
			sock = socket(AF_UNIX, SOCK_STREAM, 0);
			if (sock < 0) {
				ERROR_WINDOW("Cannot create named socket %s, UART monitor cannot be used: %s", fn, strerror(errno));
				return 1;
			}
			sock_st.sun_family = AF_UNIX;
			strcpy(sock_st.sun_path, fn);
			unlink(sock_st.sun_path);
			if (bind(sock, (struct sockaddr*)&sock_st, comdet[idx].sock_len)) {
				ERROR_WINDOW("Cannot bind named socket %s, UART monitor cannot be used: %s", fn, strerror(errno));
				xemusock_close(sock, NULL);
				return 1;
			}
#endif
		}
		if (xemusock_listen(sock, 5, &xerr)) {
			ERROR_WINDOW("Cannot listen socket %s, UART monitor cannot be used: %s", fn, xemusock_strerror(xerr));
			xemusock_close(sock, NULL);
			return 1;
		}
		if (xemusock_set_nonblocking(sock, XEMUSOCK_NONBLOCKING, &xerr)) {
			ERROR_WINDOW("Cannot set socket %s into non-blocking mode, UART monitor cannot be used: %s", fn, xemusock_strerror(xerr));
			xemusock_close(sock, NULL);
			return 1;
		}
		DEBUG("UARTMON: monitor is listening on socket %s" NL, fn);
		comdet[idx].sock_client = UNCONNECTED;	// no client connection yet
		comdet[idx].sock_server = sock;		// now set the server socket visible outside of this function too
		comdet[idx].umon_echo = 1;
		comdet[idx].umon_send_ok = 1;
		strcpy(fn_stored, fn);
	}
	return 0;
}


void uartmon_close  ( void )
{
	for (int idx = 0; idx < MAXPORTS; idx++)
	{
		if (comdet[idx].sock_server != UNCONNECTED) {
			xemusock_close(comdet[idx].sock_server, NULL);
			comdet[idx].sock_server = UNCONNECTED;
		}
		if (comdet[idx].sock_client != UNCONNECTED) {
			xemusock_close(comdet[idx].sock_client, NULL);
			comdet[idx].sock_client = UNCONNECTED;
		}
	}
}

void uartmon_finish_command ( comms_details_type *cd )
{
	cd->umon_send_ok = 1;
	if (cd->umon_write_buffer[cd->umon_write_size - 1] != '\n') {
		// if generated message wasn't closed with CRLF (well, only LF is checked), we do so here
		cd->umon_write_buffer[cd->umon_write_size++] = '\r';
		cd->umon_write_buffer[cd->umon_write_size++] = '\n';
	}
	// umon_trigger_end_of_answer = 1;
	cd->umon_write_buffer[cd->umon_write_size++] = '.';	// add the 'dot prompt'! (m65dbg seems to check LF + dot for end of the answer)
	cd->umon_write_buffer[cd->umon_write_size++] = '\r';  // I can't seem to see the dot over tcp unless I add a CRLF...
	cd->umon_write_buffer[cd->umon_write_size++] = '\n';
	cd->umon_read_pos = 0;
	cd->umon_echo = 1;
}

void uartmons_finish_command (void)
{
	for (int idx = 0; idx < MAXPORTS; idx++)
		if (comdet[idx].sock_server != UNCONNECTED)
			uartmon_finish_command(&comdet[idx]);
}

void echo_command(comms_details_type* cd, char* command, int ret);

int connect_unix_socket(comms_details_type *cd)
{
	int xerr;
	//struct sockaddr_un sock_st;
	xemusock_socklen_t len = cd->sock_len;
	union {
#ifdef XEMU_ARCH_UNIX
		struct sockaddr_un un;
#endif
		struct sockaddr_in in;
	} sock_st;
	xemusock_socket_t ret_sock = xemusock_accept(cd->sock_server, (struct sockaddr *)&sock_st, &len, &xerr);
	if (ret_sock != XS_INVALID_SOCKET || (ret_sock == XS_INVALID_SOCKET && !xemusock_should_repeat_from_error(xerr)))
		DEBUG("UARTMON: accept()=" PRINTF_SOCK " error=%s" NL,
					ret_sock,
					ret_sock != XS_INVALID_SOCKET ? "OK" : xemusock_strerror(xerr)
		);
	if (ret_sock != XS_INVALID_SOCKET) {
		if (xemusock_set_nonblocking(ret_sock, 1, &xerr)) {
			DEBUGPRINT("UARTMON: error, cannot make socket non-blocking %s", xemusock_strerror(xerr));
			xemusock_close(ret_sock, NULL);
			return 0;
		} else {
			cd->sock_client = ret_sock;	// "publish" new client socket
			// Reset reading/writing information
			cd->umon_write_size = 0;
			cd->umon_read_pos = 0;
			DEBUGPRINT("UARTMON: new connection established on socket " PRINTF_SOCK NL, cd->sock_client);
			return 1;
		}
	}
	return 0;
}

void umon_printf(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	for (int idx = 0; idx < MAXPORTS; idx++)
		if (comdet[idx].sock_server != UNCONNECTED)
			comdet[idx].umon_write_size += vsprintf(comdet[idx].umon_write_buffer + comdet[idx].umon_write_size, format, args);

	va_end(args);
}

void write_hypervisor_byte(char byte)
{
	for (int idx = 0; idx < MAXPORTS; idx++)
		if (comdet[idx].sock_server != UNCONNECTED)
		{
			//DEBUGPRINT("UARTMON: write_hypervisor(%d) - umon_send_ok=%d\n", byte, umon_send_ok);
			if (!comdet[idx].umon_send_ok)
				continue;

			int xerr;

			xemusock_send(comdet[idx].sock_client, &byte, 1, &xerr);
		}
}

int write_to_socket(comms_details_type *cd)
{
	int xerr, ret;

	if (!cd->umon_send_ok)
		return 0;
	ret = xemusock_send(cd->sock_client, cd->umon_write_buffer + cd->umon_write_pos, cd->umon_write_size, &xerr);
	if (ret != XS_SOCKET_ERROR || (ret == XS_SOCKET_ERROR && !xemusock_should_repeat_from_error(xerr))) {
		DEBUG("UARTMON: write(" PRINTF_SOCK ",buffer+%d,%d)=%d (%s)" NL,
					cd->sock_client, cd->umon_write_pos, cd->umon_write_size,
					ret, ret == XS_SOCKET_ERROR ? xemusock_strerror(xerr) : "OK"
		);
	}
	if (ret == 0 || xerr == XSECONNRESET || xerr == XSECONNABORTED) { // client socket closed
		xemusock_close(cd->sock_client, NULL);
		cd->sock_client = UNCONNECTED;
		DEBUGPRINT("UARTMON: connection closed by peer while writing" NL);
		return 0;
	}
	if (ret > 0) {
		//debug_buffer_slice(umon_write_buffer + umon_write_pos, ret);
		cd->umon_write_pos += ret;
		cd->umon_write_size -= ret;
		if (cd->umon_write_size < 0)
			FATAL("FATAL: negative umon_write_size!");
	}
	if (cd->umon_write_size)
		return 0;	// if we still have bytes to write, return and leave the work for the next update

	return 1;
}

int is_received_string_fully_parsed(comms_details_type* cd, int ret)
{
	return cd->locptr == &cd->umon_read_buffer[cd->umon_read_pos+ret];
}

int get_unparsed_bytes_remaining_count(comms_details_type* cd, int ret)
{
	return (int)(&cd->umon_read_buffer[cd->umon_read_pos+ret] - cd->locptr);
}

// had to create my own strtok() equivalent that would tokenise on *either* '\r' or '\n'
char * find_next_cmd(comms_details_type* cd, char *loc)
{

	if (loc != NULL)
		cd->locptr = loc;

	loc = cd->locptr;

	char *p = loc;
	// assure that looking forward into this string, we locate a '\r' or '\n'
	while (*p != 0)
	{
		if (*p == '\r' || *p == '\n')
		{
			cd->locptr = p+1;
			return loc;
		}
		p++;
	}

	return 0;
}

int read_loadcmd_data(comms_details_type *cd, char* buff, int count)
{
	char *p = buff;

	while (count != 0)
	{
		m65mon_setmem28(cd->loadcmdcurraddr, 1, (Uint8*)p);
		cd->loadcmdcurraddr++;
		p++;
		count--;
		if (cd->loadcmdcurraddr == cd->loadcmdendaddr)
		{
			cd->loadcmdflag = 0;
			uartmon_finish_command(cd);
			break;
		}
	}

	return count;
}

// return: 1=we loaded stuff into memory
//         0=we didn't
int check_loadcmd(comms_details_type* cd, char* buff, int ret)
{
	if (cd->loadcmdflag)
	{
		int remaining = read_loadcmd_data(cd, buff, ret);
		// TODO: I should probably see if there is a command immediately after this, but for now, I'll assume there isn't.
		// NOTE2: Yes, this is critical, I need to fix this for tools like mega65_ftp, as the pump out commands very fast
		// (especially as xemu comms is so fast compared to real hardware)
		return 1;
	}

	return 0;
}

void read_from_socket(comms_details_type *cd)
{
	int xerr, ret;
	ret = xemusock_recv(cd->sock_client, cd->umon_read_buffer + cd->umon_read_pos, sizeof(cd->umon_read_buffer) - cd->umon_read_pos - 1, &xerr);
	if (ret != XS_SOCKET_ERROR || (ret == XS_SOCKET_ERROR && !xemusock_should_repeat_from_error(xerr)))
		DEBUG("UARTMON: read(" PRINTF_SOCK ",buffer+%d,%d)=%d (%s)" NL,
					cd->sock_client, cd->umon_read_pos, (int)sizeof(cd->umon_read_buffer) - cd->umon_read_pos - 1,
					ret, ret == XS_SOCKET_ERROR ? xemusock_strerror(xerr) : "OK"
		);
	if (ret == 0 || xerr == XSECONNRESET || xerr == XSECONNABORTED) { // client socket closed
		xemusock_close(cd->sock_client, NULL);
		cd->sock_client = UNCONNECTED;
		DEBUGPRINT("UARTMON: connection closed by peer while reading" NL);
		return;
	}
	if (ret > 0) {

		// assure a null terminator at end of data
		cd->umon_read_buffer[cd->umon_read_pos+ret] = 0;


		if (check_loadcmd(cd, &cd->umon_read_buffer[cd->umon_read_pos], ret))
			return;

		char *p;
		if (cd->umon_read_pos == 0)
			p = find_next_cmd(cd, cd->umon_read_buffer);
		else
			p = find_next_cmd(cd, NULL);

		while (p)
		{
			DEBUG("UARTMON: find_next_cmd p = %08X\n", (unsigned int)p);
			cd->umon_echo = 1;
			echo_command(cd, p, ret);

			//debug_buffer(umon_read_buffer);
			if (!cd->umon_echo || sizeof(cd->umon_read_buffer) - cd->umon_read_pos - 1 == 0) {
				cd->umon_read_buffer[sizeof(cd->umon_read_buffer) - 1] = 0; // just in case of a "mega long command" with filled rx buffer ...
				cd->umon_write_buffer[cd->umon_write_size++] = '\r'; // mega65_ftp seemed to prefer only '\n' (it choked on '\r')
				cd->umon_write_buffer[cd->umon_write_size++] = '\n';
				cd->umon_send_ok = 1;	// by default, command is finished after the execute_command()
				execute_command(cd, p);	// Execute our command!
				// command may delay (like with trace) the finish of the command with
				// setting umon_send_ok to zero. In this case, some need to call
				// uartmon_finish_command() some time otherwise the monitor connection
				// will just hang!
			}

			if (check_loadcmd(cd, cd->locptr, get_unparsed_bytes_remaining_count(cd, ret)))
			{
				cd->umon_read_pos = 0;
				cd->umon_read_buffer[cd->umon_read_pos] = 0;
				return;
			}
			p = find_next_cmd(cd, NULL); // prepare to read next command on next iteration (if there is one)
		}

		// only finish command if we geniunely found a carriage return as the last character
		if (is_received_string_fully_parsed(cd, ret))
			uartmon_finish_command(cd);
		else
		{
			cd->umon_read_pos += ret;
			cd->umon_read_buffer[cd->umon_read_pos] = 0;
		}
	}
}


// Non-blocky I/O for UART monitor emulation.
// Note: you need to call it "quite often" or it will be terrible slow ...
// From emulator main update, aka etc 25Hz rate should be Okey ...
void uartmon_update ( void )
{
	for (int idx = 0; idx < MAXPORTS; idx++)
	{
		comms_details_type* cd = &comdet[idx];
		// If there is no server socket, we can't do anything!
		if (cd->sock_server == UNCONNECTED)
			continue;

		// Try to accept new connection, if not yet have one (we handle only *ONE* connection!!!!)
		if (cd->sock_client == UNCONNECTED) {
			if (!connect_unix_socket(cd))
				continue;
		}

		// If no established connection, return
		if (cd->sock_client == UNCONNECTED)
			continue;

		// If there is data to write, try to write
		if (cd->umon_write_size) {
			if (!write_to_socket(cd))
				continue;
		}

		cd->umon_write_pos = 0;

		// Try to read data
		read_from_socket(cd);
	}
}

void echo_command(comms_details_type* cd, char* command, int ret)
{
	/* ECHO: provide echo for the client */
	if (cd->umon_echo) {
		char*p = command;
		while (*p != 0 && *p != '\r' && *p != '\n') {
			cd->umon_write_buffer[cd->umon_write_size++] = *(p++);
		}
		cd->umon_echo = 0; // setting to zero avoids more input to echo, and also signs a complete command
		*p = 0; // terminate string in read buffer
	}
}

void set_umon_send_ok(int val)
{
	for (int idx = 0; idx < MAXPORTS; idx++)
		if (comdet[idx].sock_server != UNCONNECTED)
			comdet[idx].umon_send_ok = val;
}
#endif
