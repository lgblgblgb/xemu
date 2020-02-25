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


#ifndef	XEMU_BUILD
#define	HAVE_XEMU_SOCKET_API
#endif

#ifdef	HAVE_XEMU_SOCKET_API

#include <stdio.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "socketapi.h"

#ifndef	WINSOCK_VERSION_MAJOR
#define	WINSOCK_VERSION_MAJOR	2
#endif
#ifndef	WINSOCK_VERSION_MINOR
#define	WINSOCK_VERSION_MINOR	2
#endif


#ifdef XEMU_BUILD
#	include <xemu/emutools_basicdefs.h>
#else
#	define	DEBUGPRINT	printf
#	define	DEBUG		printf
#	define	NL		"\n"
#endif

#ifdef	XEMU_ARCH_WIN
#	define	SOCK_ERR()		WSAGetLastError()
#else
#	define	SOCK_ERR()		(errno)
#endif

#ifndef XEMU_BUILD
#if 0
// Test google DNS with UDP
static const unsigned char TARGET_IP[4] = {8,8,8,8};
#define TARGET_PORT 53
#define TARGET_PROTOCOL XEMUSOCK_UDP
static const unsigned char message[] = {0,1,1,0,0,1,0,0,0,0,0,0,3,'l','g','b',2,'h','u',0,0,1,0,1};
#endif

#if 1
// Test LGB.HU webserver with TCP
static const unsigned char TARGET_IP[4] = {172,104,143,43};
#define TARGET_PORT 80
#define TARGET_PROTOCOL XEMUSOCK_TCP
static const unsigned char message[] = "GET / HTTP/1.0\r\nHost: lgb.hu\r\n\r\n";
#endif
#endif


// NOTE: Xemu framework has some networking even for WIN. However Enterprise-128 emulator is not yet
// fully integrated into the framework :( So for now, let's implement everything here. Later it's a
// TODO to re-factor the whole Enterprise-128 target within Xemu anyway, and this will go away as well then.


#ifdef XEMU_ARCH_WIN
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
	if (msg)
		strcpy(msg, "NO-MSG");
	if (_winsock_init_status <= 0)
		return _winsock_init_status;
#ifdef XEMU_ARCH_WIN
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(WINSOCK_VERSION_MAJOR, WINSOCK_VERSION_MINOR), &wsa)) {
		if (msg)
			sprintf(msg, "WINSOCK: ERROR: Failed to initialize winsock2, error code: %d", WSAGetLastError());
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


void xemusock_fill_servaddr_for_inet ( struct sockaddr_in *servaddr, const unsigned char ip[4], int port )
{
	char ipstr[32];
	sprintf(ipstr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	memset(servaddr, 0, sizeof(struct sockaddr_in));
	servaddr->sin_addr.s_addr = inet_addr(ipstr);
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


int xemusock_sendto ( xemusock_socket_t sock, const void *message, int message_length, int *xerrno )
{
	int ret = sendto(sock, (void*)message, message_length, 0, (struct sockaddr*)NULL, sizeof(struct sockaddr_in));
	if (ret == XS_SOCKET_ERROR) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return ret;
}


int xemusock_recvfrom ( xemusock_socket_t sock, void *buffer, int buffer_max_length, int *xerrno )
{
	int ret = recvfrom(sock, buffer, buffer_max_length, 0, (struct sockaddr*)NULL, NULL);
	if (ret == XS_SOCKET_ERROR) {
		if (xerrno)
			*xerrno = SOCK_ERR();
		return -1;
	} else
		return ret;
}

#ifndef XEMU_BUILD
int main()
{
	int xerrno;
	char buffer[1000];
	//char *message = "Hello Server";
	xemusock_socket_t sockfd;
	struct sockaddr_in servaddr;
	if (xemusock_init(buffer)) {
		fprintf(stderr, "ERROR: %s\n", buffer);
		exit(1);
	}
	printf("INIT: %s\n", buffer);
#if 0
	// clear servaddr
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_addr.s_addr = inet_addr("8.8.8.8");
	servaddr.sin_port = htons(PORT);
	servaddr.sin_family = AF_INET;
#endif
	xemusock_fill_servaddr_for_inet(&servaddr, TARGET_IP, TARGET_PORT);
#if 0
	// create datagram socket
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd == XS_INVALID_SOCKET)
		perror("socket()");
	// setsockopt(sockfd, SOL_SOCKET
#ifdef XEMU_ARCH_WIN
	u_long mode = 1;  // 1 to enable non-blocking socket
	ioctlsocket(sockfd, FIONBIO, &mode);
#else
	int flags = fcntl(sockfd, F_GETFL);
	if (flags == -1)
		perror("ERROR: could not get flags on TCP listening socket");
	if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
		perror("ERROR: could not set TCP listening socket to be non-blocking");
#endif
#endif
	sockfd = xemusock_create_for_inet ( TARGET_PROTOCOL, XEMUSOCK_NONBLOCKING, &xerrno );
	if (sockfd == XS_INVALID_SOCKET) {
		fprintf(stderr, "Cannot create socket, because: %s.\n", xemusock_strerror(xerrno));
		exit(0);
	}
#if 0
	// connect to server
	for (int a = 0 ;; a++) {
		if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)) == XS_SOCKET_ERROR) {
			int err = SOCK_ERR();
			if (err == XSEINTR)
				continue;
			if (err == XSEAGAIN || err == XSEWOULDBLOCK || err == XSEINPROGRESS) {
				usleep(1);
				continue;
			}
			perror("ERROR: connect()");
			exit(0);
		} else {
			printf("connect() was ok after %d iterations\n", a);
			break;
		}
	}
#endif
	for (int a = 0 ;; a++) {
		if (xemusock_connect(sockfd, &servaddr, &xerrno)) {
			if (xemusock_should_repeat_from_error(xerrno)) {
				usleep(1);
				continue;
			}
			fprintf(stderr, "Error at connect(): %s\n", xemusock_strerror(xerrno));
			exit(1);
		} else {
			printf("connect() was ok after %d iterations\n", a);
			break;
		}
	}
	// request to send datagram
	// no need to specify server address in sendto
	// connect stores the peers IP and port
#if 0
	for (int a = 0 ;; a++) {
		ret = sendto(sockfd, (void*)message, sizeof(message), 0, (struct sockaddr*)NULL, sizeof(struct sockaddr_in));
		if (ret == XS_SOCKET_ERROR) {
			int err = SOCK_ERR();
			if (err == XSEINTR)
				continue;
			if (err == XSEAGAIN || err == XSEWOULDBLOCK || err == XSEINPROGRESS) {
				usleep(1);
				continue;
			}
			perror("ERROR: sendto()");
			exit(0);
		} else {
			printf("sendto() was ok after %d iterations, sent %d bytes\n", a, ret);
			break;
		}
	}
#endif
	for (int a = 0 ;; a++) {
		int ret = xemusock_sendto ( sockfd, message, sizeof(message), &xerrno );
		if (ret == XS_SOCKET_ERROR) {
			if (xemusock_should_repeat_from_error(xerrno)) {
				usleep(1);
				continue;
			}
			fprintf(stderr, "Error at sendto(): %s\n", xemusock_strerror(xerrno));
			exit(1);
		} else {
			printf("sendto() was ok after %d iterations, sent %d bytes\n", a, ret);
			break;
		}
	}
	// waiting for response
#if 0
	for (int a = 0 ;; a++) {
		ret = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)NULL, NULL);
		if (ret == XS_SOCKET_ERROR) {
			int err = SOCK_ERR();
			if (err == XSEINTR)
				continue;
			if (err == XSEAGAIN || err == XSEWOULDBLOCK || err == XSEINPROGRESS) {
				usleep(1);
				continue;
			}
			perror("ERROR: recvfrom()");
			printf("revform() error code: %d\n", err);
			exit(0);
		} else {
			printf("recvfrom() was ok after %d iterations, received %d bytes\n", a, ret);
			break;
		}
	}
#endif
	for (int a = 0 ;; a++) {
		int ret = xemusock_recvfrom(sockfd, buffer, sizeof(buffer), &xerrno);
		if (ret == XS_SOCKET_ERROR) {
			if (xemusock_should_repeat_from_error(xerrno)) {
				usleep(1);
				continue;
			}
			fprintf(stderr, "Error at recvfrom(): %s\n", xemusock_strerror(xerrno));
			exit(1);
		} else {
			printf("recvfrom() was ok after %d iterations, recieved %d bytes\n", a, ret);
			break;
		}
	}
	xemusock_close(sockfd, NULL);
	xemusock_uninit();
	return 0;
}
#endif
#endif
