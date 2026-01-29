/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifdef	XEMU_HAS_SOCKET_API

#include <stdio.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include "xemu/emutools_basicdefs.h"
#include "xemu/emutools_socketapi.h"

#ifndef	WINSOCK_VERSION_MAJOR
#define	WINSOCK_VERSION_MAJOR	2
#endif
#ifndef	WINSOCK_VERSION_MINOR
#define	WINSOCK_VERSION_MINOR	2
#endif

#ifdef	XEMU_ARCH_WIN
#	define	SOCK_ERR()	WSAGetLastError()
#else
#	define	SOCK_ERR()	(errno+0)
#	include <netdb.h>
#endif

#ifdef XEMU_ARCH_WIN
const char *xemusock_strerror ( const int err )
{
	wchar_t wbuf[512];
	static char buf[512];
	const DWORD len = FormatMessageW(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		err,
		MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),	// English, if possible
		wbuf,
		sizeof(wbuf) / sizeof(wbuf[0]),
		NULL
	);
	if (!len)
		goto unknown;
	const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, sizeof(buf), NULL, NULL);
	if (!utf8_len)
		goto unknown;
	for (char *p = buf + strlen(buf); --p >= buf && (*p <= 32); )
		*p = '\0';
	return buf;
unknown:
	snprintf(buf, sizeof buf, "Unknown WSA error %d", err);
	return buf;
}
#endif


static int  _winsock_init_status = 1;	// 1 = todo, 0 = was OK, -1 = error!
static char _winsock_errmsg[512];
static const xemusock_sock_opt_bool_t sock_opt_on = 1;


const char *xemusock_init ( void )
{
	if (_winsock_init_status == 0)
		return NULL;
	if (_winsock_init_status < 0)
		return _winsock_errmsg;
#ifdef XEMU_ARCH_WIN
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(WINSOCK_VERSION_MAJOR, WINSOCK_VERSION_MINOR), &wsa)) {
		int err = SOCK_ERR();
		snprintf(_winsock_errmsg, sizeof _winsock_errmsg, "WINSOCK: ERROR: Failed to initialize winsock2, [%d]: %s", err, xemusock_strerror(err));
		_winsock_init_status = -1;
		DEBUGPRINT("%s" NL, _winsock_errmsg);
		return _winsock_errmsg;
	}
	if (LOBYTE(wsa.wVersion) != WINSOCK_VERSION_MAJOR || HIBYTE(wsa.wVersion) != WINSOCK_VERSION_MINOR) {
		WSACleanup();
		snprintf(_winsock_errmsg, sizeof _winsock_errmsg,
			"WINSOCK: ERROR: No suitable winsock API in the implemantion DLL (we need v%d.%d, we got: v%d.%d), windows system error ...",
			WINSOCK_VERSION_MAJOR, WINSOCK_VERSION_MINOR,
			HIBYTE(wsa.wVersion), LOBYTE(wsa.wVersion)
		);
		_winsock_init_status = -1;
		DEBUGPRINT("%s" NL, _winsock_errmsg);
		return _winsock_errmsg;
	}
	DEBUGPRINT("WINSOCK: OK: initialized, version %d.%d" NL, HIBYTE(wsa.wVersion), LOBYTE(wsa.wVersion));
#endif
	_winsock_init_status = 0;
	return NULL;
}


void xemusock_uninit ( void )
{
#ifdef XEMU_ARCH_WIN
	if (_winsock_init_status == 0) {
		WSACleanup();
		_winsock_init_status = 1;
		DEBUGPRINT("WINSOCK: uninitialized." NL);
	}
#endif
}


unsigned long xemusock_resolve_ipv4 ( const char *host )
{
	if (inet_addr(host) != INADDR_NONE)	// First try dotted-decimal string
		return inet_addr(host);
	struct hostent *he = gethostbyname(host);
	if (!he || he->h_addrtype != AF_INET)	// Otherwise, do DNS lookup
		return 0;			// ... error
	struct in_addr addr;
	memcpy(&addr, he->h_addr, sizeof(addr));
	return addr.s_addr;			// netlong format!
}


const char *xemusock_parse_string_connection_parameters ( const char *str, unsigned int *ip, unsigned int *port )
{
	if (ip)
		*ip = 0;
	if (port)
		*port = 0;
	if (!str || !*str)
		return NULL;
	char *s = strdup(str);	// xemu_strdup() is something, I don't want to use here, as this source does not use emutools.h ...
	if (!s)
		return "Memory allocation error";
	char *s_ip = NULL, *s_port = NULL;
	if (ip && port) {
		s_ip = s;
		s_port = strchr(s, ':');
		if (!s_port) {
			free(s);
			return "Missing ':' from HOST:PORT definition";
		}
		*s_port++ = '\0';
	} else if (ip) {
		s_ip = s;
	} else if (port) {
		s_port = s;
	}
	if (s_ip) {
		*ip = xemusock_resolve_ipv4(s_ip);	// *ip = xemusock_ipv4_octetstring_to_netlong(s_ip);
		if (!*ip) {
			free(s);
			return "Bad IP or host (cannot be resolved?)";
		}
	}
	if (s_port) {
		char *endptr;
		*port = strtol(s_port, &endptr, 10);
		if (*s_port == '\0' || (*endptr != '\0') || (*port < 1 || *port > 65535)) {
			free(s);
			return "Bad port number";
		}
	}
	free(s);
	return NULL;
}


void xemusock_fill_servaddr_for_inet_ip_netlong ( struct sockaddr_in *servaddr, unsigned int ip_netlong, int port )
{
	memset(servaddr, 0, sizeof(struct sockaddr_in));
	if (ip_netlong == 0)
		ip_netlong = INADDR_ANY;
	servaddr->sin_addr.s_addr = ip_netlong;
	servaddr->sin_port = htons(port);
	servaddr->sin_family = AF_INET;
}


void xemusock_fill_servaddr_for_inet_ip_native ( struct sockaddr_in *servaddr, unsigned int ip_native, int port )
{
	xemusock_fill_servaddr_for_inet_ip_netlong(servaddr, htonl(ip_native), port);
}


int xemusock_set_nonblocking ( xemusock_socket_t sock, int is_nonblock, int *xerrno )
{
#ifdef XEMU_ARCH_WIN
	u_long mode = !!is_nonblock;	// 1 = use non-blocking sockets
	if (ioctlsocket(sock, FIONBIO, &mode)) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	}
		return 0;
#else
	int flags = fcntl(sock, F_GETFL);
	if (flags == -1) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	}
	flags = (is_nonblock ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
	if (fcntl(sock, F_SETFL, flags) == -1) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	}
	return 0;
#endif
}


xemusock_socket_t xemusock_create_for_inet ( int is_tcp, int is_nonblock, int *xerrno )
{
	xemusock_socket_t sock = socket(AF_INET, is_tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
	if (sock == XS_INVALID_SOCKET) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return XS_INVALID_SOCKET;
	}
	if (is_nonblock == 1) {
		int ret = xemusock_set_nonblocking(sock, 1, xerrno);
		if (ret) {
			xemusock_close(sock, NULL);
			return XS_INVALID_SOCKET;
		} else
			return sock;
	}
		return sock;
}


int xemusock_close ( xemusock_socket_t sock, int *xerrno )
{
#ifdef XEMU_ARCH_WIN
	int ret = closesocket(sock);
#else
	int ret = close(sock);
#endif
	if (ret) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return XS_SOCKET_ERROR;
	} else
		return 0;
}


int xemusock_connect ( xemusock_socket_t sock, struct sockaddr_in *servaddr, int *xerrno )
{
	if (connect(sock, (struct sockaddr *)servaddr, sizeof(struct sockaddr_in)) == XS_SOCKET_ERROR) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return 0;
}


int xemusock_shutdown ( xemusock_socket_t sock, int *xerrno )
{
	if (shutdown(sock, SHUT_RDWR) == XS_SOCKET_ERROR) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return 0;
}


int xemusock_sendto ( xemusock_socket_t sock, const void *buffer, int length, struct sockaddr_in *servaddr, int *xerrno )
{
	int ret = sendto(sock, buffer, length, 0, (struct sockaddr*)servaddr, sizeof(struct sockaddr_in));
	if (ret == XS_SOCKET_ERROR) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return ret;
}


int xemusock_send ( xemusock_socket_t sock, const void *buffer, int length, int *xerrno )
{
	int ret = send(sock, buffer, length, 0);
	if (ret == XS_SOCKET_ERROR) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return ret;
}


int xemusock_recvfrom ( xemusock_socket_t sock, void *buffer, int length, struct sockaddr_in *servaddr, int *xerrno )
{
	xemusock_socklen_t addrlen = sizeof(struct sockaddr_in);
	int ret = recvfrom(sock, buffer, length, 0, (struct sockaddr*)servaddr, &addrlen);
	if (ret == XS_SOCKET_ERROR) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return ret;
}


int xemusock_recv ( xemusock_socket_t sock, void *buffer, int length, int *xerrno )
{
	int ret = recv(sock, buffer, length, 0);
	if (ret == XS_SOCKET_ERROR) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return ret;
}


int xemusock_bind ( xemusock_socket_t sock, struct sockaddr *addr, xemusock_socklen_t addrlen, int *xerrno )
{
	int ret = bind(sock, addr, addrlen);
	if (ret) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return 0;
}


int xemusock_listen ( xemusock_socket_t sock, int backlog, int *xerrno )
{
	int ret = listen(sock, backlog);
	if (ret) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return 0;
}


xemusock_socket_t xemusock_accept ( xemusock_socket_t sock, struct sockaddr *addr, xemusock_socklen_t *addrlen, int *xerrno )
{
	xemusock_socket_t ret = accept(sock, addr, addrlen);
	if (ret == XS_INVALID_SOCKET) {
		if (xerrno)
			*xerrno = SOCK_ERR();
	}
	return ret;
}


int xemusock_setsockopt ( xemusock_socket_t sock, int level, int option, const void *value, int len, int *xerrno )
{
	if (setsockopt(sock, level, option, (const char*)value, len)) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return 0;
}


int xemusock_setsockopt_reuseaddr ( xemusock_socket_t sock, int *xerrno )
{
	return xemusock_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &sock_opt_on, sizeof sock_opt_on, xerrno);
}


int xemusock_setsockopt_keepalive ( xemusock_socket_t sock, int *xerrno )
{
	return xemusock_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &sock_opt_on, sizeof sock_opt_on, xerrno);
}


int xemusock_select_1 ( xemusock_socket_t sock, int usec, int what, int *xerrno )
{
	for (;;) {
		int ret;
		struct timeval timeout;
		fd_set fds_r, fds_w, fds_e;
		FD_ZERO(&fds_r);
		FD_ZERO(&fds_w);
		FD_ZERO(&fds_e);
		if (what & XEMUSOCK_SELECT_R)
			FD_SET(sock, &fds_r);
		if (what & XEMUSOCK_SELECT_W)
			FD_SET(sock, &fds_w);
		if (what & XEMUSOCK_SELECT_E)
			FD_SET(sock, &fds_e);
		timeout.tv_sec = 0;
		timeout.tv_usec = usec;
		ret = select(sock + 1, &fds_r, &fds_w, &fds_e, usec >= 0 ? &timeout : NULL);
		if (ret == XS_SOCKET_ERROR) {
			int err = SOCK_ERR();
			if (err == XSEINTR)
				continue;
			if (xerrno)
				*xerrno = SOCK_ERR();
			return -1;
		}
		if (ret == 0)
			return 0;
		return (FD_ISSET(sock, &fds_r) ? XEMUSOCK_SELECT_R : 0) | (FD_ISSET(sock, &fds_w) ? XEMUSOCK_SELECT_W : 0) | (FD_ISSET(sock, &fds_e) ? XEMUSOCK_SELECT_E : 0);
	}
}

#endif
