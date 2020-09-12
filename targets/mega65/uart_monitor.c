/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
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

int  umon_write_size;
int  umon_send_ok;
char umon_write_buffer[UMON_WRITE_BUFFER_SIZE];

#define UNCONNECTED	XS_INVALID_SOCKET

#ifdef XEMU_ARCH_WIN
#	define PRINTF_SOCK	"%I64d"
#else
#	define PRINTF_SOCK	"%d"
#endif


static xemusock_socket_t  sock_server = UNCONNECTED;
static xemusock_socklen_t sock_len;
static xemusock_socket_t  sock_client = UNCONNECTED;

static int  umon_write_pos, umon_read_pos;
static int  umon_echo;
static char umon_read_buffer [0x1000];


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
	//Uint8* vals = NULL;
	char *orig_param = param;
	int cnt = 0;
	// get param count
	while (param && !check_end_of_command(param, 0)) {
		int val;
		param = parse_hex_arg(param, &val, 0, 0xFF);
		cnt++;
	}
	//vals = calloc(cnt, sizeof(Uint8));
	Uint8 vals[cnt];
	param = orig_param;
	for (int idx = 0; idx < cnt; idx++) {
		int val;
		(void)parse_hex_arg(param, &val, 0, 0xFF);
		vals[idx] = (Uint8)val;
	}
	m65mon_setmem28(addr & 0xFFFFFFF, cnt, vals);
	//free(vals);
}


static void execute_command ( char *cmd )
{
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
		case 'd':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFF);
			if (cmd && check_end_of_command(cmd, 1))
				m65mon_dumpmem16(par1);
			break;
		case 'm':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			if (cmd && check_end_of_command(cmd, 1))
				m65mon_dumpmem28(par1);
			break;
		case 's':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			setmem28(cmd, par1);
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
		case 0:
			m65mon_empty_command();	// emulator can use this, if it wants
			break;
		default:
			umon_printf(UMON_SYNTAX_ERROR "unknown (or not implemented) command '%c'", cmd[-1]);
			break;
	}
}



/* ------------------------- SOCKET HANDLING, etc ------------------------- */


//static int set_nonblock ( int fd )
//{
//	int flags = fcntl(fd, F_GETFL, 0);
//	if (flags == -1)
//		return 1;
//	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
//		return 1;
//	return 0;
//}


int uartmon_init ( const char *fn )
{
	int xerr;
	xemusock_socket_t sock;
	if (sock_server != UNCONNECTED) {
		ERROR_WINDOW("UARTMON: already activated");
		return 1;
	}
	if (!fn || !*fn) {
		DEBUGPRINT("UARTMON: disabled, no name is specified to bind to." NL);
		return 0;
	}
	if (xemusock_init(NULL)) {
		ERROR_WINDOW("Cannot initialize network, uart_mon won't be availbale");
		return 1;
	}
	if (fn[0] == ':') {
		int port = atoi(fn + 1);
		if (port < 1024 || port > 65535) {
			ERROR_WINDOW("uartmon: invalid port specification %d (1024-65535 is allowed) from string %s", port, fn);
			return 1;
		}
		struct sockaddr_in sock_st;
		sock_len = sizeof(struct sockaddr_in);
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
		//sock_st.sin_port = htons(port);
		xemusock_fill_servaddr_for_inet_ip_native(&sock_st, 0, port);
		if (xemusock_bind(sock, (struct sockaddr*)&sock_st, sock_len, &xerr)) {
			ERROR_WINDOW("Cannot bind TCP socket %d, UART monitor cannot be used: %s", port, xemusock_strerror(xerr));
			xemusock_close(sock, NULL);
			return 1;
		}
	} else {
#if !defined(XEMU_ARCH_UNIX)
		ERROR_WINDOW("On non-UNIX systems, you must use TCP/IP sockets, so uartmon parameter must be in form of :n (n=port number to bind to)\nUARTMON is not available because of bad syntax.");
		return 1;
#else
		// This is UNIX specific code (UNIX named socket) thus it's OK not use Xemu socket API calls here.
		// Note: on longer term, we want to drop this, and allow only TCP sockets to be more unified and simple.
		struct sockaddr_un sock_st;
		sock_len = sizeof(struct sockaddr_un);
		sock = socket(AF_UNIX, SOCK_STREAM, 0);
		if (sock < 0) {
			ERROR_WINDOW("Cannot create named socket %s, UART monitor cannot be used: %s", fn, strerror(errno));
			return 1;
		}
		sock_st.sun_family = AF_UNIX;
		strcpy(sock_st.sun_path, fn);
		unlink(sock_st.sun_path);
		if (bind(sock, (struct sockaddr*)&sock_st, sock_len)) {
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
	sock_client = UNCONNECTED;	// no client connection yet
	sock_server = sock;		// now set the server socket visible outside of this function too
	umon_echo = 1;
	umon_send_ok = 1;
	return 0;
}


void uartmon_close  ( void )
{
	if (sock_server != UNCONNECTED) {
		xemusock_close(sock_server, NULL);
		sock_server = UNCONNECTED;
	}
	if (sock_client != UNCONNECTED) {
		xemusock_close(sock_client, NULL);
		sock_client = UNCONNECTED;
	}
}


void uartmon_finish_command ( void )
{
	umon_send_ok = 1;
	if (umon_write_buffer[umon_write_size - 1] != '\n') {
		// if generated message wasn't closed with CRLF (well, only LF is checked), we do so here
		umon_write_buffer[umon_write_size++] = '\r';
		umon_write_buffer[umon_write_size++] = '\n';
	}
	// umon_trigger_end_of_answer = 1;
	umon_write_buffer[umon_write_size++] = '.';	// add the 'dot prompt'! (m65dbg seems to check LF + dot for end of the answer)
	umon_read_pos = 0;
	umon_echo = 1;
}


// Non-blocky I/O for UART monitor emulation.
// Note: you need to call it "quite often" or it will be terrible slow ...
// From emulator main update, aka etc 25Hz rate should be Okey ...
void uartmon_update ( void )
{
	int xerr, ret;
	// If there is no server socket, we can't do anything!
	if (sock_server == UNCONNECTED)
		return;
	// Try to accept new connection, if not yet have one (we handle only *ONE* connection!!!!)
	if (sock_client == UNCONNECTED) {
		//struct sockaddr_un sock_st;
		xemusock_socklen_t len = sock_len;
		union {
#ifdef XEMU_ARCH_UNIX
			struct sockaddr_un un;
#endif
			struct sockaddr_in in;
		} sock_st;
		xemusock_socket_t ret_sock = xemusock_accept(sock_server, (struct sockaddr *)&sock_st, &len, &xerr);
		if (ret_sock != XS_INVALID_SOCKET || (ret_sock == XS_INVALID_SOCKET && !xemusock_should_repeat_from_error(xerr)))
			DEBUG("UARTMON: accept()=" PRINTF_SOCK " error=%s" NL,
				ret_sock,
				ret_sock != XS_INVALID_SOCKET ? "OK" : xemusock_strerror(xerr)
			);
		if (ret_sock != XS_INVALID_SOCKET) {
			if (xemusock_set_nonblocking(ret_sock, 1, &xerr)) {
				DEBUGPRINT("UARTMON: error, cannot make socket non-blocking %s", xemusock_strerror(xerr));
				xemusock_close(ret_sock, NULL);
				return;
			} else {
				sock_client = ret_sock;	// "publish" new client socket
				// Reset reading/writing information
				umon_write_size = 0;
				umon_read_pos = 0;
				DEBUGPRINT("UARTMON: new connection established on socket " PRINTF_SOCK NL, sock_client);
			}
		}
	}
	// If no established connection, return
	if (sock_client == UNCONNECTED)
		return;
	// If there is data to write, try to write
	if (umon_write_size) {
		if (!umon_send_ok)
			return;
		ret = xemusock_send(sock_client, umon_write_buffer + umon_write_pos, umon_write_size, &xerr);
		if (ret != XS_SOCKET_ERROR || (ret == XS_SOCKET_ERROR && !xemusock_should_repeat_from_error(xerr)))
			DEBUG("UARTMON: write(" PRINTF_SOCK ",buffer+%d,%d)=%d (%s)" NL,
				sock_client, umon_write_pos, umon_write_size,
				ret, ret == XS_SOCKET_ERROR ? xemusock_strerror(xerr) : "OK"
			);
		if (ret == 0) { // client socket closed
			xemusock_close(sock_client, NULL);
			sock_client = UNCONNECTED;
			DEBUGPRINT("UARTMON: connection closed by peer while writing" NL);
			return;
		}
		if (ret > 0) {
			//debug_buffer_slice(umon_write_buffer + umon_write_pos, ret);
			umon_write_pos += ret;
			umon_write_size -= ret;
			if (umon_write_size < 0)
				FATAL("FATAL: negative umon_write_size!");
		}
		if (umon_write_size)
			return;	// if we still have bytes to write, return and leave the work for the next update
	}
	umon_write_pos = 0;
	// Try to read data
	ret = xemusock_recv(sock_client, umon_read_buffer + umon_read_pos, sizeof(umon_read_buffer) - umon_read_pos - 1, &xerr);
	if (ret != XS_SOCKET_ERROR || (ret == XS_SOCKET_ERROR && !xemusock_should_repeat_from_error(xerr)))
		DEBUG("UARTMON: read(" PRINTF_SOCK ",buffer+%d,%d)=%d (%s)" NL,
			sock_client, umon_read_pos, (int)sizeof(umon_read_buffer) - umon_read_pos - 1,
			ret, ret == XS_SOCKET_ERROR ? xemusock_strerror(xerr) : "OK"
		);
	if (ret == 0) { // client socket closed
		xemusock_close(sock_client, NULL);
		sock_client = UNCONNECTED;
		DEBUGPRINT("UARTMON: connection closed by peer while reading" NL);
		return;
	}
	if (ret > 0) {
		/* ECHO: provide echo for the client */
		if (umon_echo) {
			char*p = umon_read_buffer + umon_read_pos;
			int n = ret;
			while (n--)
				if (*p != 13 && *p != 10) {
					umon_write_buffer[umon_write_size++] = *(p++);
				} else {
					umon_echo = 0; // setting to zero avoids more input to echo, and also signs a complete command
					*p = 0; // terminate string in read buffer
					break;
				}
		}
		/* ECHO: end */
		umon_read_pos += ret;
		umon_read_buffer[umon_read_pos] = 0;
		//debug_buffer(umon_read_buffer);
		if (!umon_echo || sizeof(umon_read_buffer) - umon_read_pos - 1 == 0) {
			umon_read_buffer[sizeof(umon_read_buffer) - 1] = 0; // just in case of a "mega long command" with filled rx buffer ...
			umon_write_buffer[umon_write_size++] = '\r';
			umon_write_buffer[umon_write_size++] = '\n';
			umon_send_ok = 1;	// by default, command is finished after the execute_command()
			execute_command(umon_read_buffer);	// Execute our command!
			// command may delay (like with trace) the finish of the command with
			// setting umon_send_ok to zero. In this case, some need to call
			// uartmon_finish_command() some time otherwise the monitor connection
			// will just hang!
			if (umon_send_ok)
				uartmon_finish_command();
		}
	}
}

#endif
