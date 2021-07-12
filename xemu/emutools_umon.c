/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2017-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

!!  NOTE: I AM NOT a windows programmer, not even a user ...
!!  These are my best tries with winsock to be usable also on the win32/64 platform ...

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
#include "xemu/emutools_socketapi.h"
#include <string.h>
#include <setjmp.h>


//#define UNCONNECTED	XS_INVALID_SOCKET
static xemusock_socket_t sock_server;

static SDL_atomic_t thread_counter;
static SDL_atomic_t thread_stop_trigger;
static jmp_buf jmp_finish_client_thread;

#define END_CLIENT_THREAD(n)	do { longjmp(jmp_finish_client_thread, n); XEMU_UNREACHABLE(); } while(0)
#define CHECK_STOP_TRIGGER()	do { \
					if (XEMU_UNLIKELY(SDL_AtomicGet(&thread_stop_trigger))) \
						END_CLIENT_THREAD(1); \
				} while(0)

#if 0


#include <string.h>
#include <setjmp.h>

static char _xemunet_strerror_buffer[1024];

#ifndef XEMU_ARCH_WIN

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


volatile int xumon_is_running = 0;
static volatile int thread_stop_trigger;
static SDL_Thread *xumon_thread_id = NULL;

static xemunet_socket_t server_sock = XEMUNET_INVALID_SOCKET;
static const char content_type_text[] = "text/plain; charset=utf-8";
static jmp_buf jmp_finish_thread;

struct xumon_message_queue {
	volatile int provider_pos;
	volatile int consumer_pos;
	volatile char buffer[MESSAGE_QUEUE_SIZE];
	SDL_mutex *mutex;
};


#define XEMUNET_SELECT_R 1
#define XEMUNET_SELECT_W 2
#define XEMUNET_SELECT_E 4


static int xemunet_select_1 ( xemunet_socket_t sock, int msec, int what )
{
	msec *= 1000;
	for (;;) {
		int ret;
		struct timeval timeout;
		fd_set fds_r, fds_w, fds_e;
		FD_ZERO(&fds_r);
		FD_ZERO(&fds_w);
		FD_ZERO(&fds_e);
		if (what & XEMUNET_SELECT_R)
			FD_SET(sock, &fds_r);
		if (what & XEMUNET_SELECT_W)
			FD_SET(sock, &fds_w);
		if (what & XEMUNET_SELECT_E)
			FD_SET(sock, &fds_e);
		timeout.tv_sec = 0;
		timeout.tv_usec = msec;
		ret = select(sock + 1, &fds_r, &fds_w, &fds_e, msec >= 0 ? &timeout : NULL);
		if (ret == XEMUNET_SOCKET_ERROR) {
			if (xemunet_errno == EINTR)
				continue;
			return -1;
		}
		if (ret == 0)
			return 0;
		return (FD_ISSET(sock, &fds_r) ? XEMUNET_SELECT_R : 0) | (FD_ISSET(sock, &fds_w) ? XEMUNET_SELECT_W : 0) | (FD_ISSET(sock, &fds_e) ? XEMUNET_SELECT_E : 0);
	}
}




/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! *
 * BEGIN CRITICAL PART: these part (BELOW) of the code runs in a *THREAD*. *
 * That is, it's important what can do and what you MUST NOT do here ...   *
 * Even not so OK to call Xemu API related stuffs in general, only         *
 * if you considered to be safe (most of the Xemu API is not thread-safe,  *
 * this is even true for dialog boxes, file operations, etc ...)           *
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

#define FINISH_THREAD(n) longjmp(jmp_finish_thread, n)


static void queue_push ( struct xumon_message_queue *queue, const void *data, int size )
{
	int pos = queue->push_pos;
	while (size > 0) {
		while (pos == queue->pull_pos)
			;	// burn CPU ...
		queue->buffer[pos] = *data++;
		pos = (pos + 1) & MESSAGE_QUEUE_SIZE_MASK;
	}
	queue->push_pos = pos;
}



static int socket_send_raw ( xemunet_socket_t sock, const void *data, int size )
{
	while (size > 0) {
		int w;
		if (thread_stop_trigger)
			FINISH_THREAD(1);
		w = send(sock, data, size, 0);
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


static int hexstr2num ( const char *s, int len )
{
	int out = 0;
	while (len) {
		out <<= 4;
		if (*s >= '0' && *s <= '9')
			out |= *s - '0';
		else if (*s >= 'A' && *s <= 'F')
			out |= *s - 'A' + 10;
		else if (*s >= 'a' && *s <= 'f')
			out |= *s - 'a' + 10;
		else
			return -1;
		s++;
		len--;
	}
	return out;
}



static void submit_monitor_command_from_client ( int sock, char *cmd, int is_uri_encoded )
{
	if (is_uri_encoded) {
		char *s, *t;
		s = strstr(cmd, by_uri_part);
		if (!s)
			return;
		s += strlen(by_uri_part);
		t = s;
		cmd = s;
		for (;;) {
			if (*s == 0 || *s == '?' || *s == '&' || *s == '#')
				break;
			else if (*s == '+') {
				*t++ = ' ';
				s++;
			} else if (*cmd == '%') {
				int h = hexstr2num(s + 1, 2);
				if (h < 0)
					return;		// fatal error, unterminated % encoding ...
				if (h < 32)
					return;		// control character in URI??
				*t++ = h;
				s += 3;
			} else
				*t++ = *s++;
		}
		*t = 0;
	}
	/* queue data for the main thread */
	queue_push(&queue_from_client, cmd);
	SDL_AtomicLock(&queue->lock);
	SDL_AtomicUnlock(&queue->lock);

	int pos = queue_from_client.push_pos;
	while (*cmd) {
		while (pos == queue_from_client.pull_pos)
			;
		queue_from_client.buffer[pos] = *cmd++;
		pos = (pos + 1) & (MSG_QUEUE_SIZE - 1);
	}
	queue_from_client.push_pos = pos;

	queue_pos(&our_queue, cmd, strlen(cmd));
	// LOCK command queue!
	SDL_LockMutex(queue_input.mutex);

	if (strlen(cmd) + queue_input
	strcpy(queue_input.provider_pos, cmd);
	queue_input.provider_pos += strlen(cmd);
	SDL_UnlockMutex(queue_input.mutex);

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
		"Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
		"Cache-Control: post-check=0, pre-check=0\r\n"
		"Pragma: no-cache\r\n"
		"Expires: Tue, 19 Sep 2017 19:08:16 GMT\r\n"
		"Content-Type: %s\r\n"
		"Connection: %s\r\n"
		"X-UA-Compatible: IE=edge\r\n"
		"X-Powered-By: The Powerpuff Girls\r\n"
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
		return http_answer(sock, 405, "Method Not Allowed", 0, content_type_text, "This service only supports GET method!");
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
	//int link_mode = 0;	// 0=to-be-detected, 1=umon-std, 2=http
	enum { DETECT_LINK, TEXT_LINK, HTTP_LINK } link_mode = DETECT_LINK;
	char *http_uri, *http_method, *http_headers = NULL;
	for (;;) {
		int a;
		if (buffer_start && link_mode == TEXT_LINK) {
			// buffer space "normalization" is not allowed in http mode, as we need all the request in once, unlike in text mode
			printf("memmove(buffer, buffer + %d, %d - %d);\n", buffer_start, buffer_fill, buffer_start);
			memmove(buffer, buffer + buffer_start, buffer_fill - buffer_start);
			buffer_fill -= buffer_start;
			buffer_start = 0;
		}
		if (buffer_fill >= sizeof(buffer)) {
			printf("Too long request ... aborting\n");
			if (link_mode == HTTP_LINK)
				http_answer(sock, 400, "Bad Request", 0, content_type_text, "Request is too large!");
			else
				socket_send_string(sock, "?TOO LONG LINE OR TOO MANY QUEUED REQUESTS\r\n");
			return -1;
		}
		for (;;) {
			// TODO: at this point, we may need to introduce non-blocking I/O or something (pre-write) to allow send back queued answer (especially in link_mode=TEXT)
			// since this runs as a thread, the emulator can queue an answer in an async way but this read() will block ...
			// Also: it's possible that we need to introduce timeout and fall back into TEXT_LINK mode if no info got, thus we can produce a greeter message on the
			// monitor. If really needed ...
			if (thread_stop_trigger)
				FINISH_THREAD(1);
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
					if (link_mode == DETECT_LINK) {
						char *p;
						// No link mode is detected yet, try to detect HTTP first-line
						http_uri = strchr(line, ' ');
						p = http_uri ? strchr(http_uri + 1, ' ') : NULL;
						if (http_uri && p && p - http_uri > 1 && http_uri[1] == '/' && !strncasecmp(p, " http/1.", 8)) {
							*http_uri++ = 0;
							*p = 0;
							http_method = line;
							http_headers = NULL;
							link_mode = HTTP_LINK;
							printf("HTTP mode detected! Method=\"%s\" URI=\"%s\"\n", http_method, http_uri);
						} else {
							link_mode = TEXT_LINK;
							submit_monitor_command_from_client(sock, line, 0);
						}
					} else if (link_mode == TEXT_LINK) {
						submit_monitor_command_from_client(sock, line, 0);
					} else {	// so link_mode is HTTP_LINK ...
						if (!*line) {
							// empty line terminates the HTTP request
							*line = 1;	// this odd stuff marks end of the http header for header parser functions
							return http_client_handler(sock, http_method, http_uri, http_headers); // currently we don't support HTTP keep-alive, hence the return ...
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




static int xumon_main ( void *user_data_unused )
{
	int ret_on;
	xemunet_socket_t client_sock = XEMUNET_INVALID_SOCKET;
	printf("UMON: linking thread is running\n");
	ret_on = setjmp(jmp_finish_thread);
	if (ret_on) {
		printf("UMON: finish jump\n");
		if (client_sock != XEMUNET_INVALID_SOCKET) {
			printf("UMON: close client socket on stopping\n");
			shutdown(client_sock, SHUT_RDWR);
			xemunet_close_socket(client_sock);
		}
	} else {
		printf("UMON: entering into the main accept() loop ...\n");
		thread_stop_trigger = 0;
		xumon_is_running = 1;
		while (xumon_is_running) {
			int ret;
			struct sockaddr_in client_addr;
			socklen_t addrlen = sizeof client_addr;
			do {
				int ret = xemunet_select_1(server_sock, 50, XEMUNET_SELECT_R);
				if (thread_stop_trigger)
					FINISH_THREAD(1);
			} while (ret != 1);
			client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addrlen);
			if (client_sock == XEMUNET_INVALID_SOCKET) {
				if (xemunet_errno == EINTR)
					continue;
				xemunet_perror("Server: accept");
				ret_on = -1;
				break;
			}
			printf("Connection: connected\n");
			generic_client_handler(client_sock);
			printf("Connection: closing ...\n");
			shutdown(client_sock, SHUT_RDWR);
			if (xemunet_close_socket(client_sock) < 0) {
				xemunet_perror("Server: close client connection");
			}
			client_sock = XEMUNET_INVALID_SOCKET;
		}
		printf("UMON: leaving main accept() loop ...\n");
	}
	printf("UMON: close server socket\n");
	xemunet_close_socket(server_sock);
	server_sock = XEMUNET_INVALID_SOCKET;
	xumon_is_running = 0;
	printf("UMON: good bye!\n");
	return ret_on;
}



#undef FINISH_THREAD

#endif


static int recv_raw ( xemusock_socket_t sock, void *buffer, int min_size, int max_size )
{
	int size = 0;
	while (size < min_size && max_size > 0) {
		CHECK_STOP_TRIGGER();
		int xerr;
		int ret = xemusock_select_1(sock, 100000, XEMUSOCK_SELECT_R, &xerr);
		if (ret < 0) {
			if (xerr == XSEINTR)
				continue;
			DEBUGPRINT("UMON: client: recv_raw: select() returned with error: %s" NL, xemusock_strerror(xerr));
			END_CLIENT_THREAD(1);
		}
		if (!ret)
			continue;
		ret = xemusock_recv(sock, buffer, max_size, &xerr);
		if (ret == 0) {
			// successfull receiving of zero bytes usually (?? FIXME ??) means socket has been closed
			DEBUGPRINT("UMON: client: recv_raw: recv() returned with zero" NL);
			END_CLIENT_THREAD(1);
		}
		if (ret < 0) {
			if (xemusock_should_repeat_from_error(xerr))
				continue;
			DEBUGPRINT("UMON: client: send_raw: recv() returned with error: %s" NL, xemusock_strerror(xerr));
			END_CLIENT_THREAD(1);
		}
		buffer += ret;
		size += ret;
		max_size -= ret;
	}
	return size;
}


static void send_raw ( xemusock_socket_t sock, const void *buffer, int size )
{
	while (size > 0) {
		CHECK_STOP_TRIGGER();
		int xerr;
		int ret = xemusock_select_1(sock, 100000, XEMUSOCK_SELECT_W, &xerr);
		if (ret < 0) {
			if (xerr == XSEINTR)
				continue;
			DEBUGPRINT("UMON: client: send_raw: select() returned with error: %s" NL, xemusock_strerror(xerr));
			END_CLIENT_THREAD(1);
		}
		if (!ret)
			continue;
		ret = xemusock_send(sock, buffer, size, &xerr);
		if (ret == 0) {
			// successfull sending of zero bytes usually (?? FIXME ??) means socket has been closed
			DEBUGPRINT("UMON: client: send_raw: send() returned with zero" NL);
			END_CLIENT_THREAD(1);
		}
		if (ret < 0) {
			if (xemusock_should_repeat_from_error(xerr))
				continue;
			DEBUGPRINT("UMON: client: send_raw: send() returned with error: %s" NL, xemusock_strerror(xerr));
			END_CLIENT_THREAD(1);
		}
		buffer += ret;
		size -= ret;
	}
}


static inline void send_string ( xemusock_socket_t sock, const char *p )
{
	send_raw(sock, p, strlen(p));
}


static void client_run ( xemusock_socket_t sock )
{
	char buffer[8192];
	int read_size = 0;
	int xerr;
	for (;;) {
		CHECK_STOP_TRIGGER();
		if (read_size >= sizeof(buffer) - 1)
			break;
		int ret = xemusock_recv(sock, buffer + read_size, sizeof(buffer) - read_size - 1, &xerr);
		DEBUGPRINT("UMON: client: result of recv() = %d, error = %s" NL, ret, ret == -1 ? xemusock_strerror(xerr) : "OK");
		if (ret == 0)
			break;
		if (ret > 0) {
			read_size += ret;
			buffer[read_size] = 0;
			const char *p = strstr(buffer, "\r\n\r\n");
			if (p) {

				char outbuffer[8192];
				sprintf(outbuffer,
					"HTTP/1.1 200 OK\r\n"
					"Host: 127.0.0.1\r\n"
					"Content-Type: text/plain; charset=UTF-8\r\n"
					"Connection: close\r\n"
					"Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
					"Cache-Control: post-check=0, pre-check=0\r\n"
					"Pragma: no-cache\r\n"
					"Expires: Tue, 19 Sep 2017 19:08:16 GMT\r\n"
					"X-UA-Compatible: IE=edge\r\n"
					"X-Powered-By: The Powerpuff Girls\r\n"
					"X-Content-Type-Options: nosniff\r\n"
					"Access-Control-Allow-Origin: *\r\n"
					"Server: Xemu/0.1\r\n"
					"\r\n"
					"Hello, world ;)\r\n"
				);
				send_string(sock, outbuffer);
				//p = outbuffer;
				//while (*p) {
				//	ret = xemusock_send(sock, p, strlen(p), &xerr);
				//	DEBUGPRINT("UMON: client: result of send() = %d, error = %s" NL, ret, ret == -1 ? xemusock_strerror(xerr) : "OK");
				//	if (ret == 0)
				//		break;
				//	if (ret > 0)
				//		p += ret;
				//	SDL_Delay(100);
				//}
				return;
			}
		}
		SDL_Delay(100);
	}
}


#undef END_CLIENT_THREAD
#undef CHECK_STOP_TRIGGER


// Client handling thread, for the life-time of a given connection only.
// It calls function client_run() to actually handle the connection.
// It's a very trivial function, just "extracts" the client socket from the thread parameter,
// and sets client socket to non-blocking mode, besides calling the mentioned real handler function.
// Also, this function sets a jump point for longjmp() thus, it's possible to return and finish thread
// without "walking backwards" in the call chain to be able to return from the thread handler itself.
static int client_thread_initiate ( void *user_param )
{
	int num_of_threads = SDL_AtomicAdd(&thread_counter, 1) + 1;	// increment thread counter (and remember)
	xemusock_socket_t sock = (xemusock_socket_t)(uintptr_t)user_param;
	DEBUGPRINT("UMON: client: new connection on socket %d" NL, (int)sock);
	if (num_of_threads > XUMON_MAX_THREADS) {
		DEBUGPRINT("UMON: client: too many threads (%d > %d), aborting connection." NL, num_of_threads, XUMON_MAX_THREADS);
	} else {
		int xerr;
		if (xemusock_set_nonblocking(sock, XEMUSOCK_NONBLOCKING, &xerr)) {
			DEBUGPRINT("UMON: client: Cannot set socket %d into non-blocking mode:\n%s" NL, (int)sock, xemusock_strerror(xerr));
		} else {
			if (!setjmp(jmp_finish_client_thread))
				client_run(sock);
			xemusock_set_nonblocking(sock, XEMUSOCK_BLOCKING, NULL);
		}
	}
	xemusock_shutdown(sock, NULL);
	xemusock_close(sock, NULL);
	(void)SDL_AtomicAdd(&thread_counter, -1);			// decrement thread counter
	return 0;
}


// Main server thread, running during the full life-time of UMON subsystem.
// It accepts incoming connections and creating new threads to handle the given connection then.
static int main_thread ( void *user_param )
{
	int client_seq = 0;
	SDL_AtomicSet(&thread_counter, 1);	// the main thread counts as the first one already
	while (!SDL_AtomicGet(&thread_stop_trigger)) {
		struct sockaddr_in sock_st;
		int xerr;
		// Wait for socket event with select, with 0.1sec timeout
		// We need timeout, to check thread_stop_trigger condition
		int select_result = xemusock_select_1(sock_server, 100000, XEMUSOCK_SELECT_R | XEMUSOCK_SELECT_E, &xerr);
		if (!select_result)
			continue;
		if (select_result < 0) {
			if (xerr == XSEINTR)
				continue;
			DEBUGPRINT("UMON: client: select() error: %s" NL, xemusock_strerror(xerr));
			SDL_Delay(100);
			continue;
		}
		xemusock_socklen_t len = sizeof(struct sockaddr_in);
		xemusock_socket_t sock = xemusock_accept(sock_server, (struct sockaddr *)&sock_st, &len, &xerr);
		if (sock != XS_INVALID_SOCKET && sock != XS_SOCKET_ERROR) {	// FIXME: both conditions needed? maybe others as well?
			char thread_name[64];
			sprintf(thread_name, "Xemu-Umon-%d-%d", SDL_AtomicGet(&thread_counter), client_seq);
			SDL_Thread *thread = SDL_CreateThread(client_thread_initiate, thread_name, (void*)(uintptr_t)sock);
			if (thread) {
				client_seq++;
				SDL_DetachThread(thread);
			} else {
				DEBUGPRINT("UMON: client: cannot create thread for incomming connection" NL);
				xemusock_shutdown(sock, NULL);
				xemusock_close(sock, NULL);
			}
		} else {
			DEBUGPRINT("UMON: client: accept() error: %s" NL, xemusock_strerror(xerr));
			SDL_Delay(10);
		}
	}
	(void)SDL_AtomicAdd(&thread_counter, -1);	// for the main thread itself
	return 0;
}



/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! *
 * END CRITICAL PART: these part ABOVE of the code runs in a *THREAD*.            *
 * The rest of this file is about creating the thread and it's enivornment first, *
 * and it will run in the main context of the execution.                          *
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */


int xumon_init ( int port )
{
	SDL_AtomicSet(&thread_counter, 0);
	sock_server = XS_INVALID_SOCKET;
	if (!port) {
		DEBUGPRINT("UMON: not enabled" NL);
		return 0;
	}
	static const char err_msg[] = "UMON initialization problem, UMON won't be available:\n";
	if (port < 1024 || port > 0xFFFF) {
		ERROR_WINDOW("%sInvalid port (must be between 1024 and 65535): %d", err_msg, port);
		goto error;
	}
	const char *sock_init_status = xemusock_init();
	if (sock_init_status) {
		ERROR_WINDOW("%sCannot initialize network library:\n%s", err_msg, sock_init_status);
		goto error;
	}
	int xerr;
	sock_server = xemusock_create_for_inet(XEMUSOCK_TCP, XEMUSOCK_BLOCKING, &xerr);
	if (sock_server == XS_INVALID_SOCKET) {
		ERROR_WINDOW("%sCannot create TCP socket:\n%s", err_msg, xemusock_strerror(xerr));
		goto error;
	}
	if (xemusock_setsockopt_reuseaddr(sock_server, &xerr)) {
		ERROR_WINDOW("UMON setsockopt for SO_REUSEADDR failed:\n%s", xemusock_strerror(xerr));
		goto error;
	}
	struct sockaddr_in sock_st;
	xemusock_fill_servaddr_for_inet_ip_native(&sock_st, 0, port);
	xemusock_socklen_t sock_len = sizeof(struct sockaddr_in);
	if (xemusock_bind(sock_server, (struct sockaddr*)&sock_st, sock_len, &xerr)) {
		ERROR_WINDOW("%sCannot bind TCP socket %d:\n%s", err_msg, port, xemusock_strerror(xerr));
		goto error;
	}
	if (xemusock_listen(sock_server, 5, &xerr)) {
		ERROR_WINDOW("%sCannot listen socket %d:\n%s", err_msg, (int)sock_server, xemusock_strerror(xerr));
		goto error;
	}
	if (xemusock_set_nonblocking(sock_server, XEMUSOCK_NONBLOCKING, &xerr)) {
		ERROR_WINDOW("%sCannot set socket %d into non-blocking mode:\n%s", err_msg, (int)sock_server, xemusock_strerror(xerr));
		goto error;
	}
	// Create thread to handle incoming connections on our brand new server socket we've just created for this purpose
	SDL_AtomicSet(&thread_stop_trigger, 0);
	Uint32 passed_time = 0, start_time = SDL_GetTicks();
	SDL_Thread *thread = SDL_CreateThread(main_thread, "Xemu-Umon-Main", NULL);
	if (!thread) {
		ERROR_WINDOW("%sCannot create monitor thread:\n%s", err_msg, SDL_GetError());
		goto error;
	}
	SDL_DetachThread(thread);
	while (!SDL_AtomicGet(&thread_counter)) {
		SDL_Delay(1);
		passed_time = SDL_GetTicks() - start_time;
		if (passed_time > 500) {
			DEBUGPRINT("UMON: timeout while waiting for thread to start! UMON won't be available!" NL);
			goto error;
		}
	}
	// Everything is OK, return with success.
	DEBUGPRINT("UMON: has been initialized for TCP/IP port %d, on-line within %d msecs." NL, port, passed_time);
	return 0;
error:
	SDL_AtomicSet(&thread_stop_trigger, 1);
	if (sock_server != XS_INVALID_SOCKET) {
		int xerr;
		if (xemusock_close(sock_server, &xerr))
			DEBUGPRINT("UMON: warning, could not close server socket after error: %s" NL, xemusock_strerror(xerr));
		sock_server = XS_INVALID_SOCKET;
	}
	return 1;
}


int xumon_stop ( void )
{
	int count = SDL_AtomicGet(&thread_counter);
	if (!count)
		return 0;
	Uint32 passed_time = 0, start_time = SDL_GetTicks();
	SDL_AtomicSet(&thread_stop_trigger, 1);
	while (SDL_AtomicGet(&thread_counter) > 0) {
		SDL_Delay(1);
		passed_time = SDL_GetTicks() - start_time;
		if (passed_time > 500) {
			DEBUGPRINT("UMON: timeout while waiting for threads to stop!" NL);
			break;
		}
	}
	xemusock_set_nonblocking(sock_server, XEMUSOCK_BLOCKING, NULL);
	xemusock_shutdown(sock_server, NULL);
	xemusock_close(sock_server, NULL);
	sock_server = XS_INVALID_SOCKET;
	int count2 = SDL_AtomicGet(&thread_counter);
	DEBUGPRINT("UMON: shutdown, %d thread(s) (%d client) exited, %d thread(s) has timeout condition, %d msecs." NL, count - count2, count - count2 - 1, count2, passed_time);
	return 0;
}

#endif
