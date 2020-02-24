// Socket test program.
// Should be evolved into Xemu socket API ;-P

#include <stdio.h> 
#include <strings.h> 
#include <sys/types.h> 
#include <unistd.h> 
#include <stdlib.h> 
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef XEMU_ARCH_WIN
#	include <winsock2.h>
#	include <windows.h>
	typedef	SOCKET			xemusock_socket_t;
#	define	xemusock_close(_n)	closesocket(_n)
#	define	xemusock_errno()	WSAGetLastError()
	// it seems Windows has no EAGAIN thing ...
#	define	XSEAGAIN		WSAEWOULDBLOCK
#	define	XSEWOULDBLOCK		WSAEWOULDBLOCK
#	define	XSEINPROGRESS		WSAEINPROGRESS
#	define	XSEINTR			WSAEINTR
#	define	XS_INVALID_SOCKET	INVALID_SOCKET
#	define	XS_SOCKET_ERROR		SOCKET_ERROR
#else
#	include <arpa/inet.h> 
#	include <sys/socket.h> 
#	include <netinet/in.h>
	typedef	int			xemusock_socket_t;
#	define	xemusock_close(_n)	close(_n)
#	define	xemusock_errno()	errno
#	define	XSEAGAIN		EAGAIN
#	define	XSEWOULDBLOCK		EWOULDBLOCK
#	define	XSEINPROGRESS		EINPROGRESS
#	define	XSEINTR			EINTR
#	define	XS_INVALID_SOCKET	-1
#	define	XS_SOCKET_ERROR		-1
#	define	xs_strerror(_n)		strerror(_n)
#endif


//#ifndef XEMU_BUILD
#	define	ERROR_WINDOW	printf
#	define	DEBUGPRINT	printf
#	define	NL		"\n"
//#endif



// NOTE: Xemu framework has some networking even for WIN. However Enterprise-128 emulator is not yet
// fully integrated into the framework :( So for now, let's implement everything here. Later it's a
// TODO to re-factor the whole Enterprise-128 target within Xemu anyway, and this will go away as well then.


#ifdef XEMU_ARCH_WIN
static int _winsock_init_status = 1;	// 1 = todo, 0 = was OK, -1 = error!
#endif
int xemusock_init ( void )
{
#ifdef XEMU_ARCH_WIN
	WSADATA wsa;
	if (_winsock_init_status <= 0)
		return _winsock_init_status;
	if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
		ERROR_WINDOW("Failed to initialize winsock2, error code: %d", WSAGetLastError());
		_winsock_init_status = -1;
		return -1;
	}
	if (LOBYTE(wsa.wVersion) != 2 || HIBYTE(wsa.wVersion) != 2) {
		WSACleanup();
		ERROR_WINDOW("No suitable winsock API in the implemantion DLL (we need v2.2, we got: v%d.%d), windows system error ...", HIBYTE(wsa.wVersion), LOBYTE(wsa.wVersion));
		_winsock_init_status = -1;
		return -1;
	}
	DEBUGPRINT("WINSOCK: initialized, version %d.%d\n", HIBYTE(wsa.wVersion), LOBYTE(wsa.wVersion));
	_winsock_init_status = 0;
#endif
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


#define PORT 53
#define MAXLINE 1000 

/*
HEXDUMP @ 0000  00  [?]
HEXDUMP @ 0001  01  [?]
HEXDUMP @ 0002  01  [?]
HEXDUMP @ 0003  00  [?]
HEXDUMP @ 0004  00  [?]
HEXDUMP @ 0005  01  [?]
HEXDUMP @ 0006  00  [?]
HEXDUMP @ 0007  00  [?]
HEXDUMP @ 0008  00  [?]
HEXDUMP @ 0009  00  [?]
HEXDUMP @ 000A  00  [?]
HEXDUMP @ 000B  00  [?]
HEXDUMP @ 000C  03  [?]
HEXDUMP @ 000D  6C  [l]
HEXDUMP @ 000E  67  [g]
HEXDUMP @ 000F  62  [b]
HEXDUMP @ 0010  02  [?]
HEXDUMP @ 0011  68  [h]
HEXDUMP @ 0012  75  [u]
HEXDUMP @ 0013  00  [?]
HEXDUMP @ 0014  00  [?]
HEXDUMP @ 0015  01  [?]
HEXDUMP @ 0016  00  [?]
HEXDUMP @ 0017  01  [?]
*/

static const unsigned char message[] = {0,1,1,0,0,1,0,0,0,0,0,0,3,'l','g','b',2,'h','u',0,0,1,0,1};



// Driver code 
#ifdef XEMU_BUILD
int main_notsomuch_socket()
#else
int main() 
#endif
{    
    char buffer[100]; 
    //char *message = "Hello Server"; 
    xemusock_socket_t sockfd; 
    struct sockaddr_in servaddr; 

    xemusock_init();


    // clear servaddr 
    memset(&servaddr, 0, sizeof(servaddr)); 
    servaddr.sin_addr.s_addr = inet_addr("8.8.8.8"); 
    servaddr.sin_port = htons(PORT); 
    servaddr.sin_family = AF_INET; 
      
	// create datagram socket 
	sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
	if (sockfd == XS_INVALID_SOCKET)
		perror("socket()");

 //	setsockopt(sockfd, SOL_SOCKET
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

	int ret;

	// connect to server
	for (int a = 0 ;; a++) {
		if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == XS_SOCKET_ERROR) {
			int err = xemusock_errno();
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
  
	// request to send datagram 
	// no need to specify server address in sendto 
	// connect stores the peers IP and port
	for (int a = 0 ;; a++) {
		ret = sendto(sockfd, (void*)message, sizeof(message), 0, (struct sockaddr*)NULL, sizeof(servaddr)); 
		if (ret == XS_SOCKET_ERROR) {
			int err = xemusock_errno();
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
      
	// waiting for response
	for (int a = 0 ;; a++) {
		ret = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)NULL, NULL); 
		if (ret == XS_SOCKET_ERROR) {
			int err = xemusock_errno();
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

	// close the descriptor
	xemusock_close(sockfd);
	xemusock_uninit();
	return 0;
}
