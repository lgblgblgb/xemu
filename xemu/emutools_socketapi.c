/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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
#endif

#ifdef XEMU_ARCH_WIN
// FIXME: maybe migrate this to Windows' FormatMessage() at some point?
const char *xemusock_strerror ( int err )
{
	switch (err) {
		case 0:				return "Success (0)";
		case WSA_INVALID_HANDLE:	return "Invalid handle (INVALID_HANDLE)";
		case WSA_NOT_ENOUGH_MEMORY:	return "Not enough memory (NOT_ENOUGH_MEMORY)";
		case WSA_INVALID_PARAMETER:	return "Invalid parameter (INVALID_PARAMETER)";
		case WSA_OPERATION_ABORTED:	return "Operation aborted (OPERATION_ABORTED)";
		case WSA_IO_INCOMPLETE:		return "IO incomplete (IO_INCOMPLETE)";
		case WSA_IO_PENDING:		return "IO pending (IO_PENDING)";
		case WSAEINTR:			return "Interrupted (EINTR)";
		case WSAEBADF:			return "Invalid handle (EBADF)";
		case WSAEACCES:			return "Permission denied (EACCES)";
		case WSAEFAULT:			return "Bad address (EFAULT)";
		case WSAEINVAL:			return "Invaild argument (EINVAL)";
		case WSAEMFILE:			return "Too many files (EMFILE)";
		case WSAEWOULDBLOCK:		return "Resource temporarily unavailable (EWOULDBLOCK)";
		case WSAEINPROGRESS:		return "Operation now in progress (EINPROGRESS)";
		case WSAEALREADY:		return "Operation already in progress (EALREADY)";
		case WSAENOTSOCK:		return "Socket operation on nonsocket (ENOTSOCK)";
		case WSAEDESTADDRREQ:		return "Destination address required (EDESTADDRREQ)";
		case WSAEMSGSIZE:		return "Message too long (EMSGSIZE)";
		case WSAEPROTOTYPE:		return "Protocol wrong type for socket (EPROTOTYPE)";
		case WSAENOPROTOOPT:		return "Bad protocol option (ENOPROTOOPT)";
		case WSAEPROTONOSUPPORT:	return "Protocol not supported (EPROTONOSUPPORT)";
		case WSAESOCKTNOSUPPORT:	return "Socket type not supported (ESOCKTNOSUPPORT)";
		case WSAEOPNOTSUPP:		return "Operation not supported (EOPNOTSUPP)";
		case WSAEPFNOSUPPORT:		return "Protocol family not supported (EPFNOSUPPORT)";
		case WSAEAFNOSUPPORT:		return "Address family not supported by protocol family (EAFNOSUPPORT)";
		case WSAEADDRINUSE:		return "Address already in use (EADDRINUSE)";
		case WSAEADDRNOTAVAIL:		return "Cannot assign requested address (EADDRNOTAVAIL)";
		case WSAENETDOWN:		return "Network is down (ENETDOWN)";
		case WSAENETUNREACH:		return "Network is unreachable (ENETUNREACH)";
		case WSAENETRESET:		return "Network dropped connection on reset (ENETRESET)";
		case WSAECONNABORTED:		return "Software caused connection abort (ECONNABORTED)";
		case WSAECONNRESET:		return "Connection reset by peer (ECONNRESET)";
		case WSAENOBUFS:		return "No buffer space available (ENOBUFS)";
		case WSAEISCONN:		return "Socket is already connected (EISCONN)";
		case WSAENOTCONN:		return "Socket is not connected (ENOTCONN)";
		case WSAESHUTDOWN:		return "Cannot send after socket shutdown (ESHUTDOWN)";
		case WSAETOOMANYREFS:		return "Too many references (ETOOMANYREFS)";
		case WSAETIMEDOUT:		return "Connection timed out (ETIMEDOUT)";
		case WSAECONNREFUSED:		return "Connection refused (ECONNREFUSED)";
		case WSAELOOP:			return "Cannot translate name (ELOOP)";
		case WSAENAMETOOLONG:		return "Name too long (ENAMETOOLONG)";
		case WSAEHOSTDOWN:		return "Host is down (EHOSTDOWN)";
		case WSAEHOSTUNREACH:		return "No route to host (EHOSTUNREACH)";
		case WSAENOTEMPTY:		return "Directory not empty (ENOTEMPTY)";
		case WSAEPROCLIM:		return "Too many processes (EPROCLIM)";
		case WSAEUSERS:			return "User quota exceeded (EUSERS)";
		case WSAEDQUOT:			return "Disk quota exceeded (EDQUOT)";
		case WSAESTALE:			return "Stale file handle reference (ESTALE)";
		case WSAEREMOTE:		return "Item is remote (EREMOTE)";
		case WSASYSNOTREADY:		return "Network subsystem is unavailable (SYSNOTREADY)";
		case WSAVERNOTSUPPORTED:	return "Winsock.dll version out of range (VERNOTSUPPORTED)";
		case WSANOTINITIALISED:		return "Successful WSAStartup not yet performed (NOTINITIALISED)";
		case WSAEDISCON:		return "Graceful shutdown in progress (EDISCON)";
		case WSAENOMORE:		return "No more results (ENOMORE)";
		case WSAECANCELLED:		return "Call has been canceled (WSAECANCELLED)";
		case WSAEINVALIDPROCTABLE:	return "Procedure call table is invalid (EINVALIDPROCTABLE)";
		case WSAEINVALIDPROVIDER:	return "Service provider is invalid (EINVALIDPROVIDER)";
		case WSAEPROVIDERFAILEDINIT:	return "Service provider failed to initialize (EPROVIDERFAILEDINIT)";
		case WSASYSCALLFAILURE:		return "System call failure (SYSCALLFAILURE)";
		case WSASERVICE_NOT_FOUND:	return "Service not found (SERVICE_NOT_FOUND)";
		case WSATYPE_NOT_FOUND:		return "Class type not found (TYPE_NOT_FOUND)";
		case WSA_E_NO_MORE:		return "No more results (E_NO_MORE)";
		case WSA_E_CANCELLED:		return "Call was canceled (E_CANCELLED)";
		case WSAEREFUSED:		return "Database query was refused (EREFUSED)";
		case WSAHOST_NOT_FOUND:		return "Host not found (HOST_NOT_FOUND)";
		case WSATRY_AGAIN:		return "Nonauthoritative host not found (TRY_AGAIN)";
		case WSANO_RECOVERY:		return "This is a nonrecoverable error (NO_RECOVERY)";
		case WSANO_DATA:		return "Valid name, no data record of requested type (NO_DATA)";
		default:			return "Unknown Winsock error";
	}
}
#endif


static int _winsock_init_status = 1;	// 1 = todo, 0 = was OK, -1 = error!


int xemusock_init ( char *msg )
{
	*msg = '\0';
	if (_winsock_init_status <= 0)
		return _winsock_init_status;
#ifdef XEMU_ARCH_WIN
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(WINSOCK_VERSION_MAJOR, WINSOCK_VERSION_MINOR), &wsa)) {
		if (msg) {
			int err = SOCK_ERR();
			sprintf(msg, "WINSOCK: ERROR: Failed to initialize winsock2, [%d]: %s", err, xemusock_strerror(err));
		}
		_winsock_init_status = -1;
		return -1;
	}
	if (LOBYTE(wsa.wVersion) != WINSOCK_VERSION_MAJOR || HIBYTE(wsa.wVersion) != WINSOCK_VERSION_MINOR) {
		WSACleanup();
		if (msg)
			sprintf(msg,
				"WINSOCK: ERROR: No suitable winsock API in the implemantion DLL (we need v%d.%d, we got: v%d.%d), windows system error ...",
				WINSOCK_VERSION_MAJOR, WINSOCK_VERSION_MINOR,
				HIBYTE(wsa.wVersion), LOBYTE(wsa.wVersion)
			);
		_winsock_init_status = -1;
		return -1;
	}
	if (msg)
		sprintf(msg, "WINSOCK: OK: initialized, version %d.%d", HIBYTE(wsa.wVersion), LOBYTE(wsa.wVersion));
#endif
	_winsock_init_status = 0;
	return 0;
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


void xemusock_fill_servaddr_for_inet ( struct sockaddr_in *servaddr, unsigned int ip_netlong, int port )
{
	memset(servaddr, 0, sizeof(struct sockaddr_in));
	servaddr->sin_addr.s_addr = ip_netlong;
	servaddr->sin_port = htons(port);
	servaddr->sin_family = AF_INET;
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


int xemusock_select_1 ( xemusock_socket_t sock, int usec, int what )
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
			return -1;
		}
		if (ret == 0)
			return 0;
		return (FD_ISSET(sock, &fds_r) ? XEMUSOCK_SELECT_R : 0) | (FD_ISSET(sock, &fds_w) ? XEMUSOCK_SELECT_W : 0) | (FD_ISSET(sock, &fds_e) ? XEMUSOCK_SELECT_E : 0);
	}
}

#endif
