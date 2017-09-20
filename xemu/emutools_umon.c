/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
*  SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
*  and some Mega-65 features as well.
*  Copyright (C)2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

*  NOTE: I AM NOT a windows programmer, not even a user ...
*  These are my best tries with winsock to be usable also on the win32/64 platform ...

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

#ifdef HAVE_XEMU_UMON

#include "xemu/emutools.h"
#include "xemu/emutools_umon.h"
#include <string.h>

static char _xemunet_strerror_buffer[1024];

#ifndef _WIN32

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>
#define XEMUNET_INVALID_SOCKET -1
#define XEMUNET_SOCKET_ERROR -1
typedef int xemunet_socket_t;
#define xemunet_close_socket close
#define xemunet_errno errno
#define xemunet_perror perror
static const char *xemunet_strneterror ( void )
{
	strcpy(_xemunet_strerror_buffer, strerror(errno));
	return _xemunet_strerror_buffer;
}

#else

#include <winsock2.h>
#define XEMUNET_INVALID_SOCKET INVALID_SOCKET
#define XEMUNET_SOCKET_ERROR SOCKET_ERROR
typedef SOCKET xemunet_socket_t;
#define xemunet_close_socket closesocket
typedef int socklen_t;
#define SHUT_RD	0
#define SHUT_WR 1
#define SHUT_RDWR 2
#define xemunet_errno WSAGetLastError()
static void xemunet_perror ( const char *s )
{
	fprintf(stderr, "%s: (WSA_CODE=%d)\n", s, WSAGetLastError());
}
static const char *xemunet_strneterror ( void )
{
	sprintf(_xemunet_strerror_buffer, "(WSA_CODE=%d)", WSAGetLastError());
	return _xemunet_strerror_buffer;
}

#endif




static int socket_send_raw ( xemunet_socket_t sock, const void *data, int size )
{
	while (size > 0) {
		int w = send(sock, data, size, 0);
		if (w < 0) {
			if (xemunet_errno == EINTR)
				continue;
			return -1;
		}
		if (w == 0)
			return -1;
		data += w;
		size -= w;
	}
	return 0;
}


static int socket_send_string ( xemunet_socket_t sock, const char *s )
{
	return socket_send_raw(sock, s, strlen(s));
}



static void submit_monitor_command ( int sock, char *cmd, int is_uri_encoded )
{
	socket_send_string(sock, "test-reply: you've entered this: \"");
	socket_send_string(sock, cmd);
	socket_send_string(sock, "\"\r\n");
}


#if 0
static int http_send_chunk ( xemunet_socket_t sock, const void *data, int size )
{
	char buffer[32];
	if (size) {
		sprintf(buffer, "\r\n%X\r\n", size);
		if (socket_send_raw(sock, buffer, strlen(buffer)))
			return -1;
		return socket_send_raw(sock, data, size);
	} else {
		return socket_send_raw(sock, "\r\n0\r\n\r\n", 7);
	}
}
#endif


static int http_answer ( xemunet_socket_t sock, int error_code, const char *error_msg, int keep_alive, const char *ctype, const char *simple_answer )
{
	char buffer[4096];
	sprintf(buffer,
		"HTTP/1.1 %d %s\r\n"
		"Cache-Control: no-cache, no-store, max-age=0\r\n"
		"Pragma: no-cache\r\n"
		"Expires: Tue, 19 Sep 2017 19:08:16 GMT\r\n"
		"Content-Type: %s\r\n"
		"Connection: %s\r\n"
		"X-UA-Compatible: IE=edge\r\n"
		"X-Powered-By: Powerpuff girls\r\n"
		"X-Content-Type-Options: nosniff\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Server: %s/%s\r\n",
		error_code, error_msg,
		ctype,
		keep_alive ? "keep-alive" : "close",
		"Xemu", "Version"
	);
	if (error_code == 405)
		strcat(buffer, "Allow: GET\r\n");
	if (simple_answer) {
		sprintf(buffer + strlen(buffer), "Content-Length: %d\r\n\r\n", (int)strlen(simple_answer));
	} else
		strcat(buffer, "Transfer-Encoding: chunked\r\n");
	if (socket_send_raw(sock, buffer, strlen(buffer)))
		return -1;
	if (simple_answer) {
		if (socket_send_raw(sock, simple_answer, strlen(simple_answer)))
			return -1;
	}
	return 0;
}




static void fancy_print ( const char *s )
{
	printf("[");
	while (*s) {
		if (*s >= 32 && *s <= 127)
			printf("%c", *s);
		else
			printf("<%d>", *s);
		s++;
	}
	printf("]\n");
}


static int http_client_handler ( xemunet_socket_t sock, const char *method, const char *uri, const char *http_headers)
{
	if (strcasecmp(method, "GET"))
		return http_answer(sock, 405, "Method Not Allowed", 0, "text/plain; charset=utf-8", "This service only supports GET method!");
	else {
		char buffer[0x4000];
		char *p = buffer + sprintf(buffer, "%s", "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=utf-8>\n<title>Xemu</title>\n</head>\n<body>\n<H1>XEMU</H1>\n<p>Wannabe XEMU web-debug ...</p>\n<pre>\n");
		printf("http_headers = %s\n", http_headers);
		if (http_headers)
			while (*http_headers != 1) {
				char c = *http_headers++;
				if (c == '<')
					p += sprintf(p, "%s", "&lt;");
				else if (c == '>')
					p += sprintf(p, "%s", "&gt;");
				else if (c == 0) {
					*p++ = '\n';
					while (!*http_headers)
						http_headers++;
				} else
					*p++ = c;
			}
		sprintf(p, "%s", "</pre></body></html>");
		return http_answer(sock, 200, "Ok", 0, "text/html; charset=utf-8", buffer);
	}
	return 0;
}



static int generic_client_handler ( xemunet_socket_t sock )
{
	char buffer[8192];
	int buffer_fill = 0;	// current used buffer space (including the already not used space before buffer_start)
	int buffer_start = 0;	// acutal offset in buffer have the new data from
	char line_ending = 0;	// used in line-ending auto detection (last \r or \n at the next char, or zero if not the situation)
	int link_mode = 0;	// 0=to-be-detected, 1=umon-std, 2=http
	char *http_uri, *http_method, *http_headers = NULL;
	for (;;) {
		int a;
		if (buffer_start && link_mode == 1) {
			// buffer space "normalization" is not allowed in http mode, as we need all the request in once, unlike in text mode
			printf("memmove(buffer, buffer + %d, %d - %d);\n", buffer_start, buffer_fill, buffer_start);
			memmove(buffer, buffer + buffer_start, buffer_fill - buffer_start);
			buffer_fill -= buffer_start;
			buffer_start = 0;
		}
		if (buffer_fill >= sizeof(buffer)) {
			printf("Too long request ... aborting\n");
			if (link_mode == 2)
				http_answer(sock, 400, "Bad Request", 0, "text/plain; charset=utf-8", "Request is too large!");
			else
				socket_send_string(sock, "?TOO LONG LINE\r\n");
			return -1;
		}
		for (;;) {
			// TODO: at this point, we may need to introduce non-blocking I/O or something (pre-write) to allow send back queued answer (especially in link_mode=1)
			// since this runs as a thread, the emulator can queue an answer in an async way but this read() will block ...
			a = recv(sock, buffer + buffer_fill, sizeof(buffer) - buffer_fill, 0);
			if (!a) {
				printf("Connection closed by remote peer?\n");
				return -1;
			}
			if (a > 0)
				break;
			if (xemunet_errno != EINTR) {
				xemunet_perror("Connection failed at read()");
				return -1;
			}
		}
		buffer_fill += a;
		a = buffer_start;
		printf("BEFORE: buffer_start=%d buffer_fill=%d\n", buffer_start, buffer_fill);
		while (a < buffer_fill) {
			if (buffer[a] == '\r' || buffer[a] == '\n') {
				if (line_ending && buffer[a] != line_ending) {
					// the line_ending logic tries to catch all possible line endings, \n and \r and \n\r and \r\n too
					printf("Ignoring extra byte: %d\n", buffer[a]);
					buffer_start++;
					line_ending = 0;
					buffer[a] = 0;
					a++;
				} else {
					char *line = buffer + buffer_start;
					line_ending = buffer[a];	// set-up marker for line ending detection
					buffer[a] = 0;			// terminate the new line found
					buffer_start = ++a;		// prepare for the next line to parse
					fancy_print(line);
					if (link_mode == 0) {
						char *p;
						// No link mode is detected yet, try to detect HTTP first-line
						http_uri = strchr(line, ' ');
						p = http_uri ? strchr(http_uri + 1, ' ') : NULL;
						if (http_uri && p && p - http_uri > 1 && http_uri[1] == '/' && !strncasecmp(p, " http/1.", 8)) {
							*http_uri++ = 0;
							*p = 0;
							http_method = line;
							http_headers = NULL;
							link_mode = 2;
							printf("HTTP mode detected! Method=\"%s\" URI=\"%s\"\n", http_method, http_uri);
						} else {
							link_mode = 1;
							submit_monitor_command(sock, line, 0);
						}
					} else if (link_mode == 1) {
						submit_monitor_command(sock, line, 0);
					} else {
						if (!*line) {
							// empty line terminates the HTTP request
							*line = 1;
							return http_client_handler(sock, http_method, http_uri, http_headers);
						} else if (!http_headers) {
							http_headers = line;
							printf("http_headers set to %s\n", http_headers);
						}
					}
				}
			} else {
				line_ending = 0;
				a++;
			}
		}
	}
	return 0;
}




static int xumon_main ( void *user_data )
{
	int sock = (xemunet_socket_t)(uintptr_t)user_data;
	for (;;) {
		struct sockaddr_in clientaddr;
		socklen_t addrlen = sizeof clientaddr;
		xemunet_socket_t clientsock = accept(sock, (struct sockaddr *)&clientaddr, &addrlen);
		if (clientsock == XEMUNET_INVALID_SOCKET) {
			if (xemunet_errno == EINTR)
				continue;
			xemunet_perror("Server: accept");
			break;
		}
		printf("Connection: connected\n");
		generic_client_handler(clientsock);
		printf("Connection: closing ...\n");
		shutdown(clientsock, SHUT_RDWR);
		if (xemunet_close_socket(clientsock) < 0) {
			xemunet_perror("Server: close client connection");
		}
	}
	xemunet_close_socket(sock);
	return 0;
}


int xumon_init ( int port, int threaded )
{
	int optval;
	struct sockaddr_in serveraddr;
	xemunet_socket_t sock;
	DEBUGPRINT("UMON: requested monitor service on TCP port %d (%s)" NL, port, threaded ? "in dedicated thread" : "DEVELOPER: IN-MAIN-THREAD");
	if (xemu_use_sockapi())
		return -1;
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == XEMUNET_INVALID_SOCKET) {
		ERROR_WINDOW("UMON: Cannot create TCP socket: %s", xemunet_strneterror());
		return -1;
	}
	optval = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int)) < 0) {
		xemunet_perror("setsockopt()");
		//return -1;
	}
	memset(&serveraddr, 0, sizeof serveraddr);
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)port);
	if (bind(sock, (struct sockaddr *)&serveraddr, sizeof serveraddr) < 0) {
		ERROR_WINDOW("UMON: Cannot bind TCP socket to port %d: %s", port, xemunet_strneterror());
		xemunet_close_socket(sock);
		return -1;
	}
	if (listen(sock, 32) < 0) {
		ERROR_WINDOW("UMON: Cannot listen to socket on TCP port %d: %s", port, xemunet_strneterror());
		xemunet_close_socket(sock);
		return -1;
	}
	if (threaded) {
		SDL_Thread *thread = SDL_CreateThread(xumon_main, "Xemu-uMonitor", (void*)(uintptr_t)sock);
		if (!thread) {
			ERROR_WINDOW("UMON: cannot create monitor thread: %s" NL, SDL_GetError());
			xemunet_close_socket(sock);
			return -1;
		}
		DEBUGPRINT("UMON: thread started with thread id %p: \"%s\"" NL, thread, SDL_GetThreadName(thread));
		return 0;
	} else {
		return xumon_main((void*)(uintptr_t)sock);
	}
	return 0;
}

#endif
