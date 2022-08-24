/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2017-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_files.h"
#include <string.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>


#define LISTEN_BACKLOG		SOMAXCONN
#define MAX_CLIENT_SLOTS	32

//#define UNCONNECTED	XS_INVALID_SOCKET
static xemusock_socket_t sock_server;

static SDL_atomic_t thread_counter;
static SDL_atomic_t thread_stop_trigger;
static SDL_atomic_t thread_client_seq;
static jmp_buf jmp_finish_client_thread;

static char *docroot = NULL;
static const char *generic_http_headers = NULL;
static const char html_footer[] = "<br><br><hr>From your Xemu acting as a webserver now ;)";

int xumon_running = 0;

#define DOCROOT_SUBDIR	"webserver-docroot"

#define END_CLIENT_THREAD(n)	do { longjmp(jmp_finish_client_thread, n); XEMU_UNREACHABLE(); } while(0)
#define CHECK_STOP_TRIGGER()	do { \
					if (XEMU_UNLIKELY(SDL_AtomicGet(&thread_stop_trigger))) \
						END_CLIENT_THREAD(1); \
				} while(0)

struct linked_fifo_st {
	struct linked_fifo_st *next;
	int size;
	Uint8 *data;
};

struct client_st {
	xemusock_socket_t	sock;
	int			fd;		// used when file access is needed (like built-in HTTP server, file streaming)
	int			seq;
	struct linked_fifo_st	*read_head;
	struct linked_fifo_st	*read_tail;
	struct linked_fifo_st	*write_head;
	struct linked_fifo_st	*write_tail;
};

static SDL_SpinLock clients_lock;
#define CLIENTS_LOCK()		SDL_AtomicLock (&clients_lock)
#define CLIENTS_UNLOCK()	SDL_AtomicUnlock (&clients_lock)

static struct client_st clients[MAX_CLIENT_SLOTS];





static int recv_raw ( xemusock_socket_t sock, void *buffer, int min_size, int max_size )
{
	int size = 0;
	while (size < min_size && max_size > 0) {
		CHECK_STOP_TRIGGER();
		int xerr;
		int ret = xemusock_select_1(sock, 100000, XEMUSOCK_SELECT_R, &xerr);
		if (ret < 0) {
			if (xerr == XSEINTR) {
				DEBUGPRINT("UMON: client: recv_raw: select() got EINTR, restarting" NL);
				continue;
			}
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
			if (xemusock_should_repeat_from_error(xerr)) {
				DEBUGPRINT("UMON: client: recv_raw: recv() non-fatal error, restarting: %s" NL, xemusock_strerror(xerr));
				continue;
			}
			DEBUGPRINT("UMON: client: recv_raw: recv() returned with error: %s" NL, xemusock_strerror(xerr));
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
			if (xerr == XSEINTR) {
				DEBUGPRINT("UMON: client: send_raw: select() got EINTR, restarting" NL);
				continue;
			}
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
			if (xemusock_should_repeat_from_error(xerr)) {
				DEBUGPRINT("UMON: client: send_raw: send() non-fatal error, restarting: %s" NL, xemusock_strerror(xerr));
				continue;
			}
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


// err_code = 0 is a special mode about the same as "200 OK" just with slightly modified text inside the <h1>...</h1>
static void http_page_and_exit ( xemusock_socket_t sock, int err_code, const char *err1, const char *err2, const char *err3, const char *extra_headers )
{
	const char *err_text;
	switch (err_code) {
		case 0:
			err_text = "Xemu Webserver OK";
			err_code = 200;
			break;
		case 302:
			err_text = "Found";
			break;
		case 400:
			err_text = "Bad Request";
			break;
		case 403:
			err_text = "Forbidden";
			break;
		case 404:
			err_text = "Not Found";
			break;
		case 405:
			err_text = "Method Not Allowed";
			break;
		default:
			DEBUGPRINT("UMON: client: unknown HTTP status code (%d), reverting to 500" NL, err_code);
			err_text = "Internal Server Error";
			err_code = 500;
			break;
	}
	if (!extra_headers) extra_headers = "";
	if (!err1) err1 = "";
	if (!err2) err2 = "";
	if (!err3) err3 = "";
	int l = strlen(err_text) * 2 + strlen(html_footer) + strlen(err1) + strlen(err2) + strlen(err3) + 512;
	char page[l];
	snprintf(page, l,
		"<!DOCTYPE html><html><head><title>%s</title></head><body style=\"background-color: white; color: darkblue;\"><h1>%d %s</h1><p>%s</p><p>%s</p><p>%s</p>%s</body></html>",
		err_text,
		err_code, err_text,
		err1, err2, err3,
		html_footer
	);
	l += strlen(err_text) + strlen(generic_http_headers) + strlen(extra_headers) + 512;
	char buffer[l];
	snprintf(buffer, l,
		"HTTP/1.1 %d %s\r\n"
		"Host: 127.0.0.1\r\n"
		"Content-Type: text/html; charset=UTF-8\r\n"
		"Connection: close\r\n"
		"%s"
		"Content-Length: %u\r\n"
		"%s"
		"\r\n%s"
		,
		err_code, err_code == 200 ? "OK" : err_text,
		generic_http_headers,
		(unsigned int)strlen(page),
		extra_headers,
		page
	);
	if (err_code != 200)
		DEBUGPRINT("UMON: client: http error answer %d %s %s %s %s" NL, err_code, err_text, err1, err2, err3);
	send_string(sock, buffer);
	END_CLIENT_THREAD(1);
}


static void http_main_page_and_exit ( struct client_st *client )
{
	char td_stat_str[XEMU_CPU_STAT_INFO_BUFFER_SIZE];
	xemu_get_timing_stat_string(td_stat_str, sizeof td_stat_str);
	char page[2048];
	snprintf(page, sizeof page,
		"<table style=\"background-color: coral; border: 2px solid black;\"><tr><th>Emulation:</th><td>%s</td></tr>"
		"<tr><th>Version/date:</th><td>%s</td></tr>"
		"<tr><th>GIT info:</th><td>%s</td></tr>"
		"<tr><th>CPU stat:</th><td>%s</td></tr>"
		"<tr><th>OS:</th><td>%s</td></tr>"
		"<tr><th>System:</th><td>%d x CPU (<i style=\"font-size: 66%%;\">%s%s%s%s%s%s%s%s%s%s%s</i>), cache-line %d, ~%dM RAM</td></tr>"
		"<tr><th>SDL:</th><td>%d.%d.%d %s (<i>video:%s audio:%s</i>)</td></tr>"
		"<tr><th>Copyright:</th><td><pre>%s</pre></td></tr>"
		"<table>",
		TARGET_DESC, XEMU_BUILDINFO_CDATE, XEMU_BUILDINFO_GIT,
		td_stat_str,
                xemu_get_uname_string(),
		SDL_GetCPUCount(),
			SDL_Has3DNow() ? "3Dnow " : "", SDL_HasAVX() ? "AVX " : "", SDL_HasAVX2() ? "AVX2 " : "" ,
			SDL_HasAltiVec() ? "AltiVec " : "",SDL_HasMMX() ? "MMX " : "", SDL_HasRDTSC() ? "RDTSC ": "",
			SDL_HasSSE() ? "SSE " : "", SDL_HasSSE2() ? "SSE2 " : "", SDL_HasSSE3() ? "SSE3 " : "", SDL_HasSSE41() ? "SSE41 " : "",SDL_HasSSE42() ? "SSE42 " : "",
		SDL_GetCPUCacheLineSize(), SDL_GetSystemRAM(),
		sdlver_linked.major, sdlver_linked.minor, sdlver_linked.patch, SDL_GetRevision(), SDL_GetCurrentVideoDriver(), SDL_GetCurrentAudioDriver(),
		emulators_disclaimer
	);
	http_page_and_exit(client->sock, 0, page, NULL, NULL, NULL);
}


static void http_serve_file_and_exit ( struct client_st *client, const char *uri )
{
	while (*uri == '/')
		uri++;
	if (*uri == '.')
		http_page_and_exit(client->sock, 403, "URI starts with '.'", NULL, NULL, NULL);
	int l = 0;
	// Rather lame, currently file URIs should not contain special characters, so no resolving %XX and anything like that at all
	for (const char *p = uri; *p > 32 && *p < 127 && *p != '?' && *p != '#'; p++, l++)
		if (*p == '/' || *p == '\\')
			http_page_and_exit(client->sock, 403, "URI contains directory separator", NULL, NULL, NULL);
	if (!l) {
		static const char default_index[] = "index.html";
		uri = default_index;
		l = strlen(default_index);
	}
	char path[strlen(docroot) + l + 1];
	strcpy(path, docroot);
	memcpy(path + strlen(docroot), uri, l);
	path[strlen(docroot) + l] = 0;
	client->fd = open(path, O_RDONLY | O_BINARY);
	if (client->fd < 0)
		http_page_and_exit(client->sock, 404, "Cannot open specified file", path, strerror(errno), NULL);
	// Guess mime-type
	const char *mime = strrchr(path, '.');
	if (!mime)
		http_page_and_exit(client->sock, 403, "Filename does not contain extension to guess mime-type", path, NULL, NULL);
	mime++;
	if (!strcasecmp(mime, "html"))		mime = "text/html; charset=UTF-8";
	else if (!strcasecmp(mime, "txt"))	mime = "text/plain; charset=UTF-8";
	else if (!strcasecmp(mime, "png") ||
		!strcasecmp(mime, "ico"))	mime = "image/png";
	else if (!strcasecmp(mime, "jpg"))	mime = "image/jpeg";
	else if (!strcasecmp(mime, "js"))	mime = "application/javascript";
	else if (!strcasecmp(mime, "css"))	mime = "text/css";
	else if (!strcasecmp(mime, "json"))	mime = "application/json";
	else if (!strcasecmp(mime, "bin"))	mime = "application/octet-stream";
	else
		http_page_and_exit(client->sock, 403, path, "Cannot guess mime-type from filename extension:", mime - 1, NULL);
	DEBUGPRINT("UMON: client: trying to serve file \"%s\" (fd %d), mime-type is \"%s\"" NL, path, client->fd, mime);
	// Chunked transfer encoding ...
	char buffer[4096];
	sprintf(buffer,
		"HTTP/1.1 200 OK\r\n"
		"Transfer-Encoding: chunked\r\n"
		"Host: 127.0.0.1\r\n"
		"Content-Type: %s\r\n"
		"Connection: close\r\n"
		"%s"
		"\r\n",
		mime, generic_http_headers
	);
	send_string(client->sock, buffer);
	for (int total_len = 0;;) {
		char chunk_head[16];
		int ret = xemu_safe_read(client->fd, buffer + 16, sizeof(buffer) - 2 - 16);
		if (ret < 0) {	// this shouldn't happen ...
			DEBUGPRINT("UMON: client: ERROR, read error during file serving!" NL);
			END_CLIENT_THREAD(1);
		}
		buffer[ret + 16] = '\r';
		buffer[ret + 17] = '\n';
		const int chunk_head_size = sprintf(chunk_head, "%X\r\n", ret);
		char *p_to_send = buffer + 16 - chunk_head_size;
		memcpy(p_to_send, chunk_head, chunk_head_size);
		send_raw(client->sock, p_to_send, ret + 2 + chunk_head_size);
		if (!ret)	// We're done :D
			END_CLIENT_THREAD(1);
		total_len += ret;
		if (total_len > (256 << 20)) {
			DEBUGPRINT("UMON: client: ERROR, too long file tried to be streamed!" NL);
			END_CLIENT_THREAD(1);
		}
	}
}




static void client_run ( struct client_st *client )
{
	char buffer[8192];
	int read_size = 0;
	for (;;) {
		CHECK_STOP_TRIGGER();
		if (read_size >= sizeof(buffer) - 1)
			break;
		int xerr, ret = xemusock_recv(client->sock, buffer + read_size, sizeof(buffer) - read_size - 1, &xerr);
		DEBUGPRINT("UMON: client: result of recv() = %d, error = %s" NL, ret, ret == -1 ? xemusock_strerror(xerr) : "OK");
		if (ret == 0)
			break;
		if (ret > 0) {
			read_size += ret;
			buffer[read_size] = 0;
			const char *p = strstr(buffer, "\r\n\r\n");
			if (p) {
				http_main_page_and_exit(client);
				http_page_and_exit(client->sock, 0, "We will rock you ;)", NULL, NULL, NULL);
				http_serve_file_and_exit(client, "index.html");
				return;
				char buffer[8192];
				sprintf(buffer,
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
				send_string(client->sock, buffer);
				//p = buffer;
				//while (*p) {
				//	ret = xemusock_send(client->sock, p, strlen(p), &xerr);
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
#	define CLIENT_SOCK (xemusock_socket_t)(uintptr_t)user_param
	const int num_of_threads = SDL_AtomicAdd(&thread_counter, 1);	// increment thread counter
	const int client_seq = SDL_AtomicAdd(&thread_client_seq, 1) + 1;	// generate a monotone sequence of ID about the connection to be identified without doubts. Avoid using zero! [thus the +1]
	struct client_st *client = NULL;
	int xerr;
	DEBUGPRINT("UMON: client: new connection on socket %d, thread %d/%d, seq %d" NL, (int)CLIENT_SOCK, num_of_threads, MAX_CLIENT_SLOTS, client_seq);
	// Trying to allocate slot multiple times (with time-out), since some HTTP client may overloaded us for a moment only.
	for (const Uint32 start = SDL_GetTicks();;) {
		if (XEMU_UNLIKELY(SDL_AtomicGet(&thread_stop_trigger)))
			goto finish;
		CLIENTS_LOCK();
		for (struct client_st *c = clients; c < clients + MAX_CLIENT_SLOTS; c++)
			if (!c->seq) {
				c->seq = client_seq;
				c->sock = CLIENT_SOCK;
				c->fd = -1;
				client = c;
				CLIENTS_UNLOCK();
				goto slot_found;
			}
		CLIENTS_UNLOCK();
		if (SDL_GetTicks() - start >= 100) {
			DEBUGPRINT("UMON: client: cannot allocate slot for new connection: aborting connection for now" NL);
			goto finish;
		}
		SDL_Delay(10);
	}
slot_found:
	// OK. We have our slot now.
	if (XEMU_UNLIKELY(xemusock_set_nonblocking(CLIENT_SOCK, XEMUSOCK_NONBLOCKING, &xerr))) {
		DEBUGPRINT("UMON: client: Cannot set socket %d into non-blocking mode:\n%s" NL, (int)CLIENT_SOCK, xemusock_strerror(xerr));
	} else {
		if (!setjmp(jmp_finish_client_thread))
			client_run(client);
		// longjmp() in END_CLIENT_THREAD() will bring us back here. Also if client_run() returns, for sure.
		xemusock_set_nonblocking(CLIENT_SOCK, XEMUSOCK_BLOCKING, NULL);
	}
finish:
	DEBUGPRINT("UMON: client: about closing connection on %d, seq %d" NL, (int)CLIENT_SOCK, client_seq);
	if (client) {
		if (client->fd >= 0)
			close(client->fd);
		CLIENTS_LOCK();
		client->seq = 0;
		struct linked_fifo_st *l[2] = { client->read_head, client->write_head };
		client->read_head  = NULL;
		client->write_head = NULL;
		client->read_tail  = NULL;
		client->write_tail = NULL;
		CLIENTS_UNLOCK();
		client = NULL;
		for (int i = 0; i < 2; i++)
			while (l[i]) {
				free(l[i]->data);
				free(l[i]);
				l[i] = l[i]->next;
			}
	}
	xemusock_shutdown(CLIENT_SOCK, NULL);
	xemusock_close(CLIENT_SOCK, NULL);
	(void)SDL_AtomicAdd(&thread_counter, -1);			// decrement thread counter
	return 0;
#	undef CLIENT_SOCK
}


// Main server thread, running during the full life-time of UMON subsystem.
// It accepts incoming connections and creating new threads to handle the given connection then.
static int main_thread ( void *user_param )
{
	SDL_AtomicSet(&thread_counter, 1);	// the main thread counts as the first one already
	while (!SDL_AtomicGet(&thread_stop_trigger)) {
		struct sockaddr_in sock_st;
		int xerr;
		// Wait for socket event with select, with 0.1sec timeout
		// We need timeout, to check thread_stop_trigger condition
		const int select_result = xemusock_select_1(sock_server, 100000, XEMUSOCK_SELECT_R | XEMUSOCK_SELECT_E, &xerr);
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
			sprintf(thread_name, "Xemu-Umon-%d-%d", SDL_AtomicGet(&thread_counter), SDL_AtomicGet(&thread_client_seq));
			SDL_Thread *thread = SDL_CreateThread(client_thread_initiate, thread_name, (void*)(uintptr_t)sock);
			if (thread) {
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





// Called by the emulator! Returns with the "seq" number of the client connection.
// Proper locking should be applied to avoid ugly things happening ...
// RETURN: non-zero: *res is filled, there is data, otherwise invalid!
// res->data must be free()'ed up by the caller!
// Answering a request must be issued with filling the res->ptr and res->seq with the
// values, this function returned, to identify the connection (there can be more!)
int xumon_get_request ( struct xumon_com_st *res )
{
	if (XEMU_LIKELY(!xumon_running))
		return 0;
	CLIENTS_LOCK();
	for (struct client_st *c = clients; c < clients + MAX_CLIENT_SLOTS; c++) {
		if (c->read_head) {
			struct linked_fifo_st *o = c->read_head;
			c->read_head = o->next;
			if (!o->next)
				c->read_tail = NULL;
			res->data = o->data;
			res->size = o->size;
			res->seq = c->seq;
			res->ptr = (const void*)c;
			CLIENTS_UNLOCK();
			free(o);
			return res->seq;
		}
	}
	CLIENTS_UNLOCK();
	return 0;
}


int xumon_set_answer ( struct xumon_com_st *res )
{
	if (XEMU_LIKELY(!xumon_running))
		return 0;
	struct linked_fifo_st *p = malloc(sizeof(struct linked_fifo_st));
	Uint8 *d = malloc(res->size);
	if (!p || !d) {
		free(p);
		free(d);
		return 1;
	}
	struct client_st *client = (struct client_st*)res->ptr;
	CLIENTS_LOCK();
	if (client->seq == res->seq) {

	} else {
		free(p);
		free(d);
	}
	CLIENTS_UNLOCK();
}

#if 0

// Called by umon to store a request
static void store_request ( struct client_st *client, const void *buffer, int size )
{
	struct linked_fifo_st *p = malloc(sizeof(struct linked_fifo_st));
	Uint8 *d = malloc(size);
	if (XEMU_UNLIKELY(!p || !d)) {
		DEBUGPRINT("UMON: client: malloc() failed to store request! Aborting connection." NL);
		END_CLIENT_THREAD(1);
	}
	memcpy(d, buffer, size);
	p->data = d;
	p->next = NULL;
	p->size = size;
	CLIENTS_LOCK();
	if (client->read_tail)
		client->read_tail->next = p;
	client->read_tail = p;
	CLIENTS_UNLOCK();
}





#endif


/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! *
 * END CRITICAL PART: these part ABOVE of the code runs in a *THREAD*.            *
 * The rest of this file is about creating the thread and it's enivornment first, *
 * and it will run in the main context of the execution.                          *
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */


int xumon_init ( const int port )
{
	if (xumon_running) {
		ERROR_WINDOW("UMON is already running!");
		return 1;
	}
	for (int i = 0; i < MAX_CLIENT_SLOTS; i++) {
		clients[i].seq = 0;
		clients[i].read_head = NULL;
		clients[i].read_tail = NULL;
		clients[i].write_head = NULL;
		clients[i].write_tail = NULL;
	}
	SDL_AtomicSet(&thread_counter, 0);
	static char first_time = 1;
	if (first_time) {
		first_time = 0;
		SDL_AtomicSet(&thread_client_seq, 0);
	}
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
	if (xemusock_listen(sock_server, LISTEN_BACKLOG, &xerr)) {
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
	// document root ("docroot") for the built-in webserver
	if (!docroot) {
		docroot = xemu_malloc(strlen(sdl_pref_dir) + strlen(DOCROOT_SUBDIR) + 2);
		sprintf(docroot, "%s%s%c", sdl_pref_dir, DOCROOT_SUBDIR, DIRSEP_CHR);
		MKDIR(docroot);
	}
	// generic http headers
	if (!generic_http_headers) {
		const char *p = strstr(XEMU_BUILDINFO_GIT, "https://");
		if (!p)
			p = strstr(XEMU_BUILDINFO_GIT, "http://");
		if (!p)
			p = XEMU_BUILDINFO_GIT;
		char buffer[4096];
		snprintf(buffer, sizeof buffer,
			"Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
			"Cache-Control: post-check=0, pre-check=0\r\n"
			"Pragma: no-cache\r\n"
			"Expires: Tue, 19 Sep 2017 19:08:16 GMT\r\n"
			"X-UA-Compatible: IE=edge\r\n"
			"X-Powered-By: The Powerpuff Girls ;)\r\n"
			"X-Content-Type-Options: nosniff\r\n"
			"Access-Control-Allow-Origin: *\r\n"
			"Server: Xemu;%s/%s %s\r\n",
			TARGET_DESC,
			XEMU_BUILDINFO_CDATE,
			p
		);
		generic_http_headers = xemu_strdup(buffer);
	}
	// Everything is OK, return with success.
	xumon_running = 1;
	DEBUGPRINT("UMON: has been initialized for TCP/IP port %d backlog %d (web-docroot: %s) within %d msecs." NL, port, LISTEN_BACKLOG, docroot, passed_time);
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
	if (!xumon_running) {
		DEBUGPRINT("UMON: was not running, no need to stop it" NL);
		return 1;
	}
	xumon_running = 0;
	const int count = SDL_AtomicGet(&thread_counter);
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
	const int count2 = SDL_AtomicGet(&thread_counter);
	DEBUGPRINT("UMON: shutdown, %d thread(s) (%d client) exited, %d thread(s) has timeout condition, backlog %d, %d msecs." NL, count - count2, count - count2 - 1, count2, LISTEN_BACKLOG, passed_time);
	return 0;
}

#endif
