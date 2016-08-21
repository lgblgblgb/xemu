/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


#include "uart_monitor.h"

int  umon_write_size;
char umon_write_buffer[UMON_WRITE_BUFFER_SIZE];


#ifdef _WIN32
// Windows is not supported currently, as it does not have POSIX-standard socket interface (?).
int  uartmon_init   ( const char *fn ) { return 1; }
void uartmon_update ( void ) {}
void uartmon_close  ( void ) {}
void uartmon_finish_command ( void ) {}
#else


#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#ifdef MEGA65
#include <SDL.h>
#include "emutools.h"
#include "mega65.h"
#else
#define ERROR_WINDOW printf
#define FATAL(...)
#define NL "\n"
#endif


static int  sock_server, sock_client;
static int  umon_write_pos, umon_read_pos;
static int  umon_echo, umon_send_ok;
static char umon_read_buffer [0x1000];


// WARNING: This source is pretty ugly, ie not so much check of overflow of the output (write) buffer.


#ifdef MEGA65
static void syntax_error ( const char *err )
{
	umon_printf("?SYNTAX ERROR %s", err);
}


static char *parse_hex_arg ( char *p, int *val, int min, int max )
{
	int r;
	while (*p == 32 || *p == '\t')
		p++;
	*val = -1;
	if (!*p) {
		syntax_error("unexpected end of command (no parameter)");
		return NULL;
	}
	for (r = 0;;) {
		if (*p >= 'a' && *p <= 'f')
			r = (r << 4) | (*p - 'a' + 10);
		else if (*p >= 'A' && *p <= 'F')
			r = (r << 4) | (*p - 'A' + 10);
		else if (*p >= '0' && *p <= '9')
			r = (r << 4) | (*p - '0');
		else if (*p == 32 || *p == '\t' || *p == 0)
			break;
		else {
			syntax_error("invalid data as hex value");
			return NULL;
		}
		p++;
	}
	*val = r;
	if (r < min || r > max) {
		syntax_error("command parameter's value is outside of the allowed range for this command");
		return NULL;
	}
	return p;
}



static int check_end_of_command ( char *p )
{
	while (*p == 32 || *p == '\t')
		p++;
	if (*p) {
		syntax_error("unexpected command parameter");
		return 0;
	}
	return 1;
}
#endif



static void execute_command ( char *cmd )
{
	int par1;
	char *p;
	// handle backspace (ie, char code 8)
	p = cmd;
	while (*p)
		if (*p == 8 && p > cmd)
			memmove(p - 1, p + 1, strlen(p + 1) + 1);
		else
			p++;
	// chop special characters and spaces off from the beginning
	while (*cmd && *cmd <= 32)
		cmd++;
	// chop special characters and spaces off from the end
	p = cmd + strlen(cmd) - 1;
	while (p >= cmd && *p <= 32)
		*(p--) = 0;
	printf("UARTMON: command got \"%s\" (%d bytes)." NL, cmd, (int)strlen(cmd));
#ifndef MEGA65
	umon_printf("This is a demo only in test mode, you've issued command \"%s\" (%d bytes)", cmd, (int)strlen(cmd));
#else
	switch (*(cmd++)) {
		case 'h':
		case 'H':
		case '?':
			if (check_end_of_command(cmd))
				umon_printf("Xemu/Mega65 Serial Monitor\r\nWarning: not 100%% compatible with UART monitor of a *real* Mega65 ...");
			break;
		case 'r':
		case 'R':
			if (check_end_of_command(cmd))
				m65mon_show_regs();
			break;
		case 'd':
			if (parse_hex_arg(cmd, &par1, 0, 0xFFFF))
				m65mon_disassembe16(par1);
			break;
		case 0:	// empty line: TODO, in trace mode it does a step (I guess)
			break;
		default:
			syntax_error("unknown command");
			break;
	}
#endif
}



/* ------------------------- SOCKET HANDLING, etc ------------------------- */


static int set_nonblock ( int fd )
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1)
		return 1;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
		return 1;
	return 0;
}



#ifndef MEGA65
static inline void debug_buffer ( const char *p )
{
	printf("BUFFER: ");
	while (*p) {
		printf((*p >= 32 && *p < 127) ? "%c" : "<%d>", *p);
		p++;
	}
	printf("\n");
}
static inline void debug_buffer_slice ( const char *p, int n )
{
	printf("BUFFER-SLICE: ");
	while (n--) {
		printf((*p >= 32 && *p < 127) ? "%c" : "<%d>", *p);
		p++;
	}
	printf("\n");
}
#else
#define debug_buffer(a)
#define debug_buffer_slice(a, b)
#endif



int uartmon_init ( const char *fn )
{
	struct sockaddr_un sock_st;
	int sock;
	sock_server = -1;
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		ERROR_WINDOW("Cannot create named socket %s, UART monitor cannot be used: %s\n", fn, strerror(errno));
		return 1;
	}
	sock_st.sun_family = AF_UNIX;
	strcpy(sock_st.sun_path, fn);
        unlink(sock_st.sun_path);
	if (bind(sock, (struct sockaddr*)&sock_st, sizeof(struct sockaddr_un))) {
		ERROR_WINDOW("Cannot bind named socket %s, UART monitor cannot be used: %s\n", fn, strerror(errno));
		close(sock);
		return 1;
	}
	if (listen(sock, 5)) {
		ERROR_WINDOW("Cannot listen with named socket %s, UART monitor cannot be used: %s\n", fn, strerror(errno));
		close(sock);
		return 1;
	}
	if (set_nonblock(sock)) {
		ERROR_WINDOW("Cannot set named socket %s into non-blocking mode, UART monitor cannot be used: %s\n", fn, strerror(errno));
		close(sock);
		return 1;
	}
	printf("UARTMON: monitor is listening on socket %s" NL, fn);
	sock_client = -1;	// no client connection yet
	sock_server = sock;	// now set the socket
	umon_echo = 1;
	umon_send_ok = 1;
	return 0;
}



void uartmon_close  ( void )
{
	if (sock_server >= 0) {
		close(sock_server);
		if (sock_client >= 0)
			close(sock_client);
	}
	sock_server = -1;
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
	int ret;
	// If there is no server socket, we can't do anything!
	if (sock_server < 0)
		return;
	// Try to accept new connection, if not yet have one (we handle only *ONE* connection!!!!)
	if (sock_client < 0) {
		struct sockaddr_un sock_st;
		socklen_t len = sizeof(struct sockaddr_un);
		ret = accept(sock_server, (struct sockaddr *)&sock_st, &len);
		if (ret >=0 || (errno != EAGAIN && errno != EWOULDBLOCK))
			printf("UARTMON: accept()=%d error=%s" NL,
				ret,
				ret >= 0 ? "OK" : strerror(errno)
			);
		if (ret >= 0) {
			if (set_nonblock(ret)) {
				close(ret);
			} else {
				sock_client = ret;	// "publish" new client socket
				// Reset reading/writing information
				umon_write_size = 0;
				umon_read_pos = 0;
				printf("UARTMON: new connection established on socket %d" NL, sock_client);
			}
		}
	}
	// If no established connection, return
	if (sock_client < 0)
		return;
	// If there is data to write, try to write
	if (umon_write_size) {
		if (!umon_send_ok)
			return;
		ret = write(sock_client, umon_write_buffer + umon_write_pos, umon_write_size);
		if (ret >=0 || (errno != EAGAIN && errno != EWOULDBLOCK))
			printf("UARTMON: write(%d,buffer+%d,%d)=%d (%s)" NL,
				sock_client, umon_write_pos, umon_write_size,
				ret, ret < 0 ? strerror(errno) : "OK"
			);
		if (ret == 0) { // client socket closed
			close(sock_client);
			sock_client = -1;
			printf("UARTMON: connection closed by peer while writing" NL);
			return;
		}
		if (ret > 0) {
			debug_buffer_slice(umon_write_buffer + umon_write_pos, ret);
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
	ret = read(sock_client, umon_read_buffer + umon_read_pos, sizeof(umon_read_buffer) - umon_read_pos - 1);
	if (ret >=0 || (errno != EAGAIN && errno != EWOULDBLOCK))
		printf("UARTMON: read(%d,buffer+%d,%d)=%d (%s)" NL,
			sock_client, umon_read_pos, (int)sizeof(umon_read_buffer) - umon_read_pos - 1,
			ret, ret < 0 ? strerror(errno) : "OK"
		);
	if (ret == 0) { // client socket closed
		close(sock_client);
		sock_client = -1;
		printf("UARTMON: connection closed by peer while reading" NL);
		return;
	}
	if (ret > 0) {
		char *p1, *p2;
		/* ECHO: provide echo for the client */
		if (umon_echo) {
			int n = 0;
			p1 = umon_read_buffer + umon_read_pos;
			while (n < ret) {
				if (*p1 != 13 && *p1 != 10) {
					umon_write_buffer[umon_write_size++] = *(p1++);
				} else {
					umon_echo = 0;
					break;
				}
				n++;
			}
		}
		/* ECHO: end */
		umon_read_pos += ret;
		umon_read_buffer[umon_read_pos] = 0;
		debug_buffer(umon_read_buffer);
		p1 = strchr(umon_read_buffer, '\n');
		p2 = strchr(umon_read_buffer, '\r');
		if ((!p1 && p2) || (p2 && p2 < p1))
			p1 = p2;
		if (p1 || sizeof(umon_read_buffer) - umon_read_pos - 1 == 0) {
			if (p1)
				*p1 = 0;
			else
				umon_read_buffer[sizeof(umon_read_buffer) - 1] = 0;
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


#ifndef MEGA65
int main ( void )
{
	uartmon_init(UARTMON_SOCKET);
	for (;;) {
		uartmon_update();
		usleep(1000);
	}
	return 0;
}
#endif

#endif
