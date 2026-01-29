/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifdef XEMU_HAS_SOCKET_API

#include "xemu/emutools.h"
#include "serialtcp.h"
#include "xemu/emutools_socketapi.h"

#define UART_CLOCK 80000000
#define BUFFER_SIZE 256

static volatile xemusock_socket_t sock = XS_INVALID_SOCKET;
static volatile bool running = false;
static volatile bool exited = false;
static volatile const char *failed = NULL;
static SDL_Thread *thread_id = NULL;
static Uint8 rx_buffer[BUFFER_SIZE];
static Uint8 tx_buffer[BUFFER_SIZE];
static SDL_atomic_t rx_fill, tx_fill;
static SDL_SpinLock lock = 0;
static Uint8 serial_regs[0x10];
static int bitrate_divisor, baudrate;
static int bitrate_divisor_reported = -1;
static char *connection_desc = NULL, *connection_string = NULL;
static volatile int tx_bytes_sum, rx_bytes_sum;


static void close_socket ( void )
{
	if (sock != XS_INVALID_SOCKET) {
		int xerr;
		xemusock_shutdown(sock, &xerr);
		xemusock_close(sock, &xerr);
		sock = XS_INVALID_SOCKET;
	}
}


static int the_thread ( void *unused )
{
	DEBUGPRINT("SERIALTCP: thread: begin" NL);
	Uint8 rx_temp[BUFFER_SIZE];
	static const char zero_transfer_error[] = "Cannot read/write, connection closed by peer?";
	while (running) {
		const int tx_size = SDL_AtomicGet(&tx_fill);
		const int rx_size = SDL_AtomicGet(&rx_fill);
		const int what = ((rx_size < BUFFER_SIZE) ? XEMUSOCK_SELECT_R : 0) | (tx_size ? XEMUSOCK_SELECT_W : 0);
		if (!what) {
			SDL_Delay(10);
			continue;
		}
		int xerr, ret_write = -1, ret_read = -1;
		const int ret = xemusock_select_1(sock, 1000, what, &xerr);
		if (!ret)
			continue;	// timeout on select
		if (ret < 0) {
			// XSEINTR is handled already inside xemusock_select_1(), so other errors are kind of fatal ...
			failed = xemusock_strerror(xerr);
			DEBUGPRINT("SERIALTCP: thread: select() error (%d): %s" NL, xerr, failed);
			break;
		}
		if ((ret & XEMUSOCK_SELECT_W)) {
			ret_write = xemusock_send(sock, tx_buffer, tx_size, &xerr);
			if (ret_write < 0) {
				if (xemusock_should_repeat_from_error(xerr))
					continue;
				failed = xemusock_strerror(xerr);
				DEBUGPRINT("SERIALTCP: thread: send error ret=%d error_code=(%d) error_str=\"%s\"" NL, ret_write, xerr, failed);
				break;
			}
			if (ret_write == 0) {
				failed = zero_transfer_error;
				break;
			}
		}
		if ((ret & XEMUSOCK_SELECT_R)) {
			ret_read = xemusock_recv(sock, rx_temp, BUFFER_SIZE - rx_size, &xerr);
			if (ret_read < 0) {
				if (xemusock_should_repeat_from_error(xerr))
					continue;
				failed = xemusock_strerror(xerr);
				DEBUGPRINT("SERIALTCP: thread: recv error ret=%d error_code=(%d) error_str=\"%s\"" NL, ret_read, xerr, failed);
				break;
			}
			if (ret_read == 0) {
				failed = zero_transfer_error;
				break;
			}
		}
		if (ret_read > 0 || ret_write > 0) {
			SDL_AtomicLock(&lock);
			if (ret_read > 0) {
				const int size = SDL_AtomicGet(&rx_fill);
				memcpy(rx_buffer + size, rx_temp, ret_read);
				SDL_AtomicSet(&rx_fill, size + ret_read);
				rx_bytes_sum += ret_read;
			}
			if (ret_write > 0) {
				const int size = SDL_AtomicGet(&tx_fill);
				if (size > ret_write)
					memmove(tx_buffer, tx_buffer + ret_write, size - ret_write);
				SDL_AtomicSet(&tx_fill, size - ret_write);
				tx_bytes_sum += ret_write;
			}
			SDL_AtomicUnlock(&lock);
		}
	}
	running = false;
	DEBUGPRINT("SERIALTCP: thread: end" NL);
	close_socket();
	exited = true;
	return 0;
}


int serialtcp_init ( const char *connection )
{
	static const char error_prefix[] = "SerialTCP error:";
	if (!connection || !*connection)
		return 0;
	if (running || sock != XS_INVALID_SOCKET) {
		ERROR_WINDOW("%s cannot init connection as it's already on-going!", error_prefix);
		return -1;
	}
	// Network init must go first, as xemusock_parse_string_connection_parameters() use socket API already.
	// It makes a difference on Windows, where winsock initialization must be done first.
	const char *errmsg = xemusock_init();
	if (errmsg) {
		ERROR_WINDOW("%s network init problem:\n%s", error_prefix, errmsg);
		return -1;
	}
	unsigned int ip, port;
	errmsg = xemusock_parse_string_connection_parameters(connection, &ip, &port);
	if (errmsg) {
		ERROR_WINDOW("%s target parsing:\n%s", error_prefix, errmsg);
		return -1;
	}
	int xerr;
	sock = xemusock_create_for_inet(XEMUSOCK_TCP, XEMUSOCK_BLOCKING, &xerr);
	if (sock == XS_INVALID_SOCKET) {
		ERROR_WINDOW("%s cannot create TCP socket:\n%s", error_prefix, xemusock_strerror(xerr));
		return -1;
	}
	struct sockaddr_in sock_st;
	xemusock_fill_servaddr_for_inet_ip_netlong(&sock_st, ip, port);
	if (xemusock_connect(sock, &sock_st, &xerr)) {
		ERROR_WINDOW("%s cannot connect to TCP target:\n%s", error_prefix, xemusock_strerror(xerr));
		close_socket();
		return -1;
	}
	if (xemusock_setsockopt_keepalive(sock, &xerr))
		ERROR_WINDOW("%s warning, could not set KEEPALIVE:\n%s", error_prefix, xemusock_strerror(xerr));
	if (xemusock_set_nonblocking(sock, 1, &xerr)) {
		ERROR_WINDOW("%s cannot set unblock for TCP target:\n%s", error_prefix, xemusock_strerror(xerr));
		close_socket();
		return -1;
	}
	// Reset data structures
	SDL_AtomicSet(&rx_fill, 0);
	SDL_AtomicSet(&tx_fill, 0);
	SDL_AtomicUnlock(&lock);
	memset(serial_regs, 0, sizeof serial_regs);
	rx_bytes_sum = 0;
	tx_bytes_sum = 0;
	bitrate_divisor = 0;
	baudrate = 19200;
	const unsigned int ip_native = ntohl(ip);
	char ip_string[16];
	snprintf(ip_string, sizeof ip_string, "%u.%u.%u.%u", (ip_native >> 24) & 0xFF, (ip_native >> 16) & 0xFF, (ip_native >> 8) & 0xFF, ip_native & 0xFF);
	free(connection_string);
	connection_string = xemu_strdup(connection);
	if (strncmp(ip_string, connection, strlen(ip_string))) {
		const int size = strlen(connection) + strlen(ip_string) + 10;
		connection_desc = xemu_realloc(connection_desc, size);
		snprintf(connection_desc, size, "%s (%s)", connection, ip_string);
	} else {
		connection_desc = xemu_realloc(connection_desc, strlen(connection) + 1);
		strcpy(connection_desc, connection);
	}
	exited = false;
	running = true;
	failed = NULL;
	// Start thread
	DEBUGPRINT("SERIALTCP: starting connection thread to %s" NL, connection_desc);
	thread_id = SDL_CreateThread(the_thread, "Xemu-SerialTCP", NULL);
	if (!thread_id) {
		running = false;
		close_socket();
		ERROR_WINDOW("%s cannot create thread:\n%s", error_prefix, SDL_GetError());
		return -1;
	}
	OSD(-1, -1, "SerialTCP connection established to\n%s", connection_desc);
	return 0;
}


bool serialtcp_get_connection_desc ( char *param, const unsigned int param_size, char *desc, const unsigned int desc_size, int *tx, int *rx )
{
	if (param)
		snprintf(param, param_size, "%s", connection_string ? connection_string : "");
	if (desc)
		snprintf(desc, desc_size, "%s", connection_desc ? connection_desc : "");
	if (tx)
		*tx = tx_bytes_sum;
	if (rx)
		*rx = rx_bytes_sum;
	return running;
}


const char *serialtcp_get_connection_error ( const bool remove_error )
{
	if (running)
		return NULL;
	const char *err = (const char *)failed;
	if (remove_error)
		failed = NULL;
	return err;
}


int serialtcp_shutdown ( void )
{
	if (!running) {
		DEBUGPRINT("SERIALTCP: not running, nothing to shut down" NL);
		return 0;
	}
	running = false;
	Uint32 ticks = SDL_GetTicks();
	bool ok = true;
	for (;;) {
		Uint32 elapsed = SDL_GetTicks() - ticks;
		if (exited) {
			DEBUGPRINT("SERIALTCP: thread has exited after %d ms" NL, elapsed);
			break;
		}
		if (elapsed > 200) {
			DEBUGPRINT("SERIALTCP: thread has timed out after %d ms" NL, elapsed);
			ok = false;
			break;
		}
		SDL_Delay(10);
	}
	close_socket();	// force to close - just in case ... (especially for thread timeout)
	return ok ? 0 : -1;
}


int serialtcp_restart ( const char *connection )
{
	if (!connection || !*connection) {
		ERROR_WINDOW("Cannot restart SerialTCP:\nno target specification");
		return -1;
	}
	DEBUGPRINT("SERIALTCP: restarting connection ..." NL);
	serialtcp_shutdown();
	return serialtcp_init(connection);
}


static int send_byte ( const Uint8 byte )
{
	SDL_AtomicLock(&lock);
	const int size = SDL_AtomicGet(&tx_fill);
	if (size >= BUFFER_SIZE) {
		SDL_AtomicUnlock(&lock);
		return -1;
	}
	tx_buffer[size] = byte;
	SDL_AtomicSet(&tx_fill, size + 1);
	SDL_AtomicUnlock(&lock);
	// DEBUGPRINT("SERIALTCP: byte sent: $%02X" NL, byte);
	return 0;
}


static int recv_byte ( const bool also_remove )
{
	SDL_AtomicLock(&lock);
	const int size = SDL_AtomicGet(&rx_fill);
	if (!size) {
		SDL_AtomicUnlock(&lock);
		// No is available (RX buffer is empty). What should I return now?
		return -1;
	}
	const Uint8 byte = rx_buffer[0];
	if (also_remove) {
		if (size > 1)
			memmove(rx_buffer, rx_buffer + 1, size - 1);
		SDL_AtomicSet(&rx_fill, size - 1);
	}
	SDL_AtomicUnlock(&lock);
	// DEBUGPRINT("SERIALTCP: byte %s $%02X" NL, also_remove ? "removed" : "received", byte);
	return byte;
}


static inline Uint8 get_status ( void )
{
	const int rx = SDL_AtomicGet(&rx_fill);
	const int tx = SDL_AtomicGet(&tx_fill);
	Uint8 status =
		(tx >= BUFFER_SIZE ? 0x08 : 0x00) |	// bit 3: TX buffer full
		(rx >= BUFFER_SIZE ? 0x10 : 0x00) |	// bit 4: RX buffer full
		(tx == 0           ? 0x20 : 0x00) |	// bit 5: TX buffer empty
		(rx == 0           ? 0x40 : 0x00);	// bit 6: RX buffer empty
	return status;
}


static void do_report_bitrate ( void )
{
	baudrate = UART_CLOCK / (bitrate_divisor + 1);
	DEBUGPRINT("SERIALTCP: bitrate is %d baud (divisior = %d) now" NL, baudrate, bitrate_divisor);
}


static XEMU_INLINE void report_bitrate ( void )
{
	if (XEMU_UNLIKELY(bitrate_divisor != bitrate_divisor_reported)) {
		bitrate_divisor_reported = bitrate_divisor;
		do_report_bitrate();
	}
}


void serialtcp_write_reg ( const int reg, const Uint8 data )
{
	serial_regs[reg] = data;
	switch (reg) {
		case 2:	// $D0E2: used to ACK received byte if this register is **WRITTEN** (normally this register is used to read the buffer)
			(void)recv_byte(true);
			break;
		case 3:	// $D0E3: transmit byte
			report_bitrate();
			send_byte(data);
			break;
			// $D0E4...$D0E6: bitrate divisor bytes
		case 4:
		case 5:
		case 6:
			bitrate_divisor = serial_regs[4] + (serial_regs[5] << 8) + (serial_regs[6] << 16);
			break;
	}
}


Uint8 serialtcp_read_reg ( const int reg )
{
	switch (reg) {
		case 1:
			return get_status() | (serial_regs[1] & (255 - (0x08|0x10|0x20|0x40)));
		case 2:	// $D0E2: read received byte (but do not remove from buffer)
			report_bitrate();
			return recv_byte(false);
		default:
			return serial_regs[reg];

	}
}

#endif
