/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2019-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef	XEMU_COMMON_EMUTOOLS_SOCKETAPI_H_INCLUDED
#define XEMU_COMMON_EMUTOOLS_SOCKETAPI_H_INCLUDED
#ifdef	XEMU_HAS_SOCKET_API

#ifdef XEMU_ARCH_WIN
#	include <winsock2.h>
	typedef	SOCKET			xemusock_socket_t;
	typedef	int			xemusock_socklen_t;
	// it seems Windows has no EAGAIN thing ...
#	define	XSEAGAIN		WSAEWOULDBLOCK
#	define	XSEWOULDBLOCK		WSAEWOULDBLOCK
#	define	XSEINPROGRESS		WSAEINPROGRESS
#	define	XSEALREADY		WSAEALREADY
#	define	XSEINTR			WSAEINTR
#	define	XS_INVALID_SOCKET	INVALID_SOCKET
#	define	XS_SOCKET_ERROR		SOCKET_ERROR
#	define	SHUT_RDWR		SD_BOTH
	extern	const char *xemusock_strerror ( int err );
#else
#	include <arpa/inet.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
	typedef	int			xemusock_socket_t;
	typedef	socklen_t		xemusock_socklen_t;
#	define	XSEAGAIN		EAGAIN
#	define	XSEWOULDBLOCK		EWOULDBLOCK
#	define	XSEINPROGRESS		EINPROGRESS
#	define	XSEALREADY		EALREADY
#	define	XSEINTR			EINTR
#	define	XS_INVALID_SOCKET	-1
#	define	XS_SOCKET_ERROR		-1
#	define	xemusock_strerror(_n)	strerror(_n)
#endif

#define	XEMUSOCK_SELECT_R	1
#define	XEMUSOCK_SELECT_W	2
#define	XEMUSOCK_SELECT_E	4

#define	XEMUSOCK_UDP		0
#define	XEMUSOCK_TCP		1
#define	XEMUSOCK_BLOCKING	0
#define	XEMUSOCK_NONBLOCKING	1

extern int  xemusock_close	( xemusock_socket_t sock, int *xerrno );
extern int  xemusock_init	( char *msg );
extern void xemusock_uninit	( void );
extern int  xemusock_select_1   ( xemusock_socket_t sock, int usec, int what );
extern void xemusock_fill_servaddr_for_inet ( struct sockaddr_in *servaddr, unsigned int ip_netlong, int port );
extern int  xemusock_set_nonblocking	( xemusock_socket_t sock, int is_nonblock, int *xerrno );
extern int  xemusock_connect		( xemusock_socket_t sock, struct sockaddr_in *servaddr, int *xerrno );
extern int  xemusock_send		( xemusock_socket_t sock, const void *buffer, int length, int *xerrno );
extern int  xemusock_sendto		( xemusock_socket_t sock, const void *buffer, int length, struct sockaddr_in *servaddr, int *xerrno );
extern int  xemusock_recv		( xemusock_socket_t sock, void *buffer, int length, int *xerrno );
extern int  xemusock_recvfrom		( xemusock_socket_t sock, void *buffer, int length, struct sockaddr_in *servaddr, int *xerrno );
extern xemusock_socket_t xemusock_create_for_inet ( int is_tcp, int is_nonblock, int *xerrno );

static inline int xemusock_should_repeat_from_error ( int err ) {
	return (err == XSEAGAIN || err == XSEWOULDBLOCK || err == XSEINPROGRESS || err == XSEINTR || err == XSEALREADY);
}
static inline unsigned int xemusock_ipv4_octetarray_to_netlong ( const unsigned char ip[4] ) {
	return htonl((unsigned int)((((unsigned int)ip[0]) << 24) + (((unsigned int)ip[1]) << 16) + (((unsigned int)ip[2]) << 8) + ((unsigned int)ip[3])));
}
static inline unsigned int xemusock_ipv4_netoctetarray_to_netlong ( const unsigned char ip[4] ) {
	return ((unsigned int)((((unsigned int)ip[0]) << 24) + (((unsigned int)ip[1]) << 16) + (((unsigned int)ip[2]) << 8) + ((unsigned int)ip[3])));
}
static inline unsigned int xemusock_ipv4_octetstring_to_netlong ( const char *ipstr ) {
	return inet_addr(ipstr);
}

#endif
#endif
