/* A work-in-progess MEGA65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2024 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#if !defined(HAS_UARTMON_SUPPORT)
#warning "Platform does not support UMON"
#else

#include "xemu/emutools.h"
#include "mega65.h"
#include "uart_monitor.h"
#include "xemu/cpu65.h"
#include "memory_mapper.h"
#include "sdcard.h"
#include "xemu/emutools_socketapi.h"
#include "hypervisor.h"
#include <string.h>

#ifndef XEMU_ARCH_WIN
#include <unistd.h>
#include <sys/un.h>
#endif

#define UMON_WRITE_BUFFER_SIZE	0x4000
#define umon_printf(...)	do { \
					if (XEMU_LIKELY(umon_write_size < UMON_WRITE_BUFFER_SIZE - 1)) \
						umon_write_size += snprintf(umon_write_buffer + umon_write_size, UMON_WRITE_BUFFER_SIZE - umon_write_size, __VA_ARGS__); \
					else \
						_umon_write_size_panic(); \
				} while(0)
#define UMON_SYNTAX_ERROR	"?SYNTAX ERROR  "
#define UNCONNECTED		XS_INVALID_SOCKET
#define PRINTF_SOCK		PRINTF_S64

static xemusock_socket_t  sock_server = UNCONNECTED;
static xemusock_socklen_t sock_len;
static xemusock_socket_t  sock_client = UNCONNECTED;

static int  umon_write_pos, umon_read_pos;
static int  umon_echo;
static char umon_read_buffer [0x1000];
static int  umon_write_size;
static int  umon_send_ok;
static char umon_write_buffer[UMON_WRITE_BUFFER_SIZE];

#define MAX_BREAKPOINTS 10
struct breakpoint_st {
	Uint16	pc;
	bool	(*callback)(const unsigned int);
	bool	temp;
	const void	*user_data;
};
static struct breakpoint_st breakpoints[MAX_BREAKPOINTS];
static unsigned int br_nums = 0;




static void _umon_write_size_panic ( void )
{
	DEBUGPRINT("UARTMON: warning: too long message (%d/%d), cannot fit into the output buffer!" NL, umon_write_pos, UMON_WRITE_BUFFER_SIZE);
}


static void do_show_regs ( void )
{
	Uint8 pf = cpu65_get_pf();
	umon_printf(
		"\r\n"
		"PC   A  X  Y  Z  B  SP   MAPL MAPH LAST-OP     P  P-FLAGS   RGP uS IO\r\n"
		"%04X %02X %02X %02X %02X %02X %04X "		// register banned message and things from PC to SP
		"%04X %04X %02X       %02X %02X "		// from MAPL to P
		"%c%c%c%c%c%c%c%c ",				// P-FLAGS
		cpu65.pc, cpu65.a, cpu65.x, cpu65.y, cpu65.z, cpu65.bphi >> 8, cpu65.sphi | cpu65.s,
		((map_mask & 0x0F) << 12) | (map_offset_low  >> 8),
		((map_mask & 0xF0) <<  8) | (map_offset_high >> 8),
		cpu65.op,
		pf, 0,	// flags
		(pf & CPU65_PF_N) ? 'N' : '-',
		(pf & CPU65_PF_V) ? 'V' : '-',
		(pf & CPU65_PF_E) ? 'E' : '-',
		'-',
		(pf & CPU65_PF_D) ? 'D' : '-',
		(pf & CPU65_PF_I) ? 'I' : '-',
		(pf & CPU65_PF_Z) ? 'Z' : '-',
		(pf & CPU65_PF_C) ? 'C' : '-'
	);
}


static void cmd_dumpmem16 ( Uint16 addr )
{
	int n = 16;
	umon_printf(":000%04X:", addr);
	while (n--)
		umon_printf("%02X", debug_read_cpu_byte(addr++));
}


static void cmd_dumpmem28 ( int addr )
{
	int n = 16;
	addr &= 0xFFFFFFF;
	umon_printf(":%07X:", addr);
	while (n--)
		umon_printf("%02X", debug_read_linear_byte(addr++));
}


#if 0
static void m65mon_set_trace ( int m )
{
	paused = m;
}


static void m65mon_do_next ( void )
{
	if (paused) {
		umon_send_ok = 0;			// delay command execution!
		delayed_callback_fptr = do_show_regs;	// register callback
		trace_next_trigger = 2;			// if JSR, then trigger until RTS to next_addr
		orig_sp = cpu65.sphi | cpu65.s;
		paused = 0;
	} else {
		umon_printf(UMON_SYNTAX_ERROR "trace can be used only in trace mode");
	}
}


static void m65mon_do_trace ( void )
{
	if (paused) {
		umon_send_ok = 0; // delay command execution!
		delayed_callback_fptr = do_show_regs; // register callback
		trace_step_trigger = 1;	// trigger one step
	} else {
		umon_printf(UMON_SYNTAX_ERROR "trace can be used only in trace mode");
	}
}


static void m65mon_do_trace_c ( void )
{
	umon_printf(UMON_SYNTAX_ERROR "command 'tc' is not implemented yet");
}


static void m65mon_next_command ( void )
{
	if (paused)
		m65mon_do_next();
}
#endif

static void m65mon_empty_command ( void )
{
#if 0
	if (paused)
		m65mon_do_trace();
#endif
}


// This function must be called when there is the chance that breakpoint-like functionality requires
// change from non-trace mode (=no CPU opcode callbacks) to trace mode or vice versa. Some possibilities
// for this (when must be called):
// * hypervisor-usermode change (because of possible hyperdebug mode)
// * breakpoint added or removed
// * emulator start-up (CLI or config controlled option takes affect for some debugging related purpose)
// The function takes care activating or de-activating trace mode (=CPU opcode callbacks) if needed by
// checking the enviconment on key momements (which means eg the above list when the function must be
// called).
void umon_opcode_callback_setup ( const char *reason )
{
	if (br_nums || (in_hypervisor && hypervisor_is_debugged) /*|| trace_next_active */) {	// all possibilities which involves using trace mode
		if (!cpu65.debug_callbacks.exec) {
			DEBUGPRINT("UMON: enabling opcode trace mode (%s)" NL, reason);
			cpu65.debug_callbacks.exec = true;
		}
	} else {
		if (cpu65.debug_callbacks.exec) {
			DEBUGPRINT("UMON: disabling opcode trace mode (%s)" NL, reason);
			cpu65.debug_callbacks.exec = false;
		}
	}
}


// REMOVE a breakpoint. Warning: there is no need to call this on breakpoints which are added as "temp" (see add_breakpoint() function) _AND_ that breakpoint was hit
// Warning2: removing a breakpoint other than the last added one causes the indices in the breakpoint array to be changed!
static void remove_breakpoint ( const unsigned int n )
{
	if (XEMU_UNLIKELY(n >= br_nums)) {
		ERROR_WINDOW("Refusing impossible breakpoint (#%u) to be removed (current number of breakpoints: %u)!", n, br_nums);
	}
	br_nums--;
	if (!br_nums)
		umon_opcode_callback_setup("removing last active breakpoint");
	else if (n != br_nums - 2)
		memmove(breakpoints + n, breakpoints + n + 1, (br_nums - n) * sizeof(struct breakpoint_st));
}


// Parameters:
//	pc:		PC value to assign breakpoint for
//	callback:	callback function pointer, will be called when breakpoint is hit, it must return with false to stop the CPU on breakpoint hit
//			(stopper_breakpoint_callback is a simple one, it causes to stop the CPU, but you can provide your own, if needed)
//	temp:		true = temporary breakpoint, automatically removed when hit (can be useful on step over opcodes mode for example)
//	user_data:	FIXME
static void add_breakpoint ( const Uint16 pc, bool (*callback)(const unsigned int), const bool temp, const void *user_data )
{
	if (XEMU_UNLIKELY(!callback))
		FATAL("Adding breakpoint with callback = NULL");
	if (XEMU_UNLIKELY(br_nums == MAX_BREAKPOINTS - 1)) {
		ERROR_WINDOW("Too many existing (%u) breakpoints, cannot add another one!", br_nums);
		return;
	}
	breakpoints[br_nums].pc = pc;
	breakpoints[br_nums].callback = callback;
	breakpoints[br_nums].temp = temp;
	breakpoints[br_nums].user_data = user_data;
	br_nums++;
	umon_opcode_callback_setup("adding breakpoint");
}


static bool breakpoint_hit_callback ( const unsigned int n )
{
	do_show_regs();
	uartmon_finish_command();
	return false;	// stop the CPU!
}


// FIXME: This is a HACK, to allow to use the old API to set an (only) breakpoint.
// FIXME: this function must die, and add_breakpoint() should be used instead directly in the future
static void set_breakpoint ( const Uint16 pc )
{
	br_nums = 0;	// FIXME: this is a hack!! I shouldn't done this! However afaik there is no simple way yet to present multiple breakpoint management
	add_breakpoint(pc, breakpoint_hit_callback, false, NULL);
}


// *** This is the function called by the CPU emulator before every opcode exection, if cpu65.debug_callbacks.exec = true
//     This is the place where various breakpoints, hypervisor debugging and "trace next" should be handled
void cpu65_execution_debug_callback ( void )
{
	if (in_hypervisor && hypervisor_is_debugged)
		hypervisor_debug();
	/*if (trace_next_active) {
	}*/
	for (unsigned int n = 0; n < br_nums; n++) {
		if (cpu65.pc == breakpoints[n].pc) {
			DEBUGPRINT("DEBUGGER: hitting breakpoint #%u at PC=$%04X" NL, n, cpu65.pc);
			cpu65.running = breakpoints[n].callback(n);
			if (breakpoints[n].temp)
				remove_breakpoint(n);
			return;
		}
	}
}


// *** This is the function called by the CPU emulator before accepting NMI, if cpu65.debug_callbacks.nmi = true
//     Currently it's not used here, but the callback must be defined.
void cpu65_nmi_debug_callback ( void )
{
}


// *** This is the function called by the CPU emulator before acceptint IRQ, if cpu65.debug_callbacks.irq = true
//     Currently it's not used here, but the callback must be defined.
void cpu65_irq_debug_callback ( void )
{
}


// *** This is the function called by the CPU emulator before executing BRK, if cpu65.debug_callbacks.brk = true
//     Currently it's not used here (other than for warning about a BRK ...), but the callback must be defined.
void cpu65_brk_debug_callback ( void )
{
	DEBUGPRINT("DEBUGGER: BRK is executing at $%04X" NL, cpu65.pc);
}


// *** This is the function called by the CPU emulator on CPU RESET, if cpu65.debug_callbacks.reset = true
//     Currently it's not used here, but the callback must be defined.
void cpu65_reset_debug_callback ( void )
{
}


static void subsystem_init ( void )
{
	static bool done = false;
	if (done)
		return;
	done = true;
	cpu65.debug_callbacks.brk = 1;

}


static char *parse_hex_arg ( char *p, int *val, int min, int max )
{
	while (*p == 32)
		p++;
	*val = -1;
	if (!*p) {
		umon_printf(UMON_SYNTAX_ERROR "unexpected end of command (no parameter)");
		return NULL;
	}
	int r = 0;
	for (;;) {
		if (*p >= 'a' && *p <= 'f')
			r = (r << 4) | (*p - 'a' + 10);
		else if (*p >= 'A' && *p <= 'F')
			r = (r << 4) | (*p - 'A' + 10);
		else if (*p >= '0' && *p <= '9')
			r = (r << 4) | (*p - '0');
		else if (*p == 32 || *p == 0)
			break;
		else {
			umon_printf(UMON_SYNTAX_ERROR "invalid data as hex digit '%c'", *p);
			return NULL;
		}
		p++;
	}
	*val = r;
	if (r < min || r > max) {
		umon_printf(UMON_SYNTAX_ERROR "command parameter's value is outside of the allowed range for this command %X (%X...%X)", r, min, max);
		return NULL;
	}
	return p;
}


static int check_end_of_command ( char *p, int error_out )
{
	while (*p == 32)
		p++;
	if (*p) {
		if (error_out)
			umon_printf(UMON_SYNTAX_ERROR "unexpected command parameter");
		return 0;
	}
	return 1;
}


static void cmd_setmem ( char *param, int addr )
{
	char *orig_param = param;
	int cnt = 0;
	// get param count
	while (param && !check_end_of_command(param, 0)) {
		int val;
		param = parse_hex_arg(param, &val, 0, 0xFF);
		cnt++;
	}
	param = orig_param;
	for (int idx = 0; idx < cnt; idx++) {
		int val;
		param = parse_hex_arg(param, &val, 0, 0xFF);
		if ((addr >> 16) != 0x777)
			debug_write_linear_byte(addr & 0xFFFFFFF, val);
		else
			debug_write_cpu_byte(addr & 0xFFFF, val);
		addr++;
	}
}


static void execute_command ( char *cmd )
{
	int bank;
	int par1;
	char *p = cmd;
	while (*p)
		if (p == cmd && (*cmd == 32 || *cmd == '\t' || *cmd == 8))
			cmd = ++p;
		else if (*p == '\t')
			*(p++) = 32;
		else if (*p == 8)
			memmove(p - 1, p + 1, strlen(p + 1) + 1);
		else if ((unsigned char)*p > 127 || (unsigned char)*p < 32) {
			umon_printf(UMON_SYNTAX_ERROR "invalid character in the command (ASCII=%d)", *p);
			return;
		} else
			p++;
	p--;
	while (p >= cmd && *p <= 32)
		*(p--) = 0;
	DEBUG("UARTMON: command got \"%s\" (%d bytes)." NL, cmd, (int)strlen(cmd));
	switch (*(cmd++)) {
		case 'h':
		case 'H':
		case '?':
			if (check_end_of_command(cmd, 1))
				umon_printf("Xemu/MEGA65 Serial Monitor\r\nWarning: not 100%% compatible with UART monitor of a *real* MEGA65 ...");
			break;
		case 'r':
		case 'R':
			if (check_end_of_command(cmd, 1))
				do_show_regs();
			break;
		case 'm':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			bank = par1 >> 16;
			if (cmd && check_end_of_command(cmd, 1)) {
				if (bank == 0x777)
					cmd_dumpmem16(par1);
				else
					cmd_dumpmem28(par1);
			}
			break;
		case 'M':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			bank = par1 >> 16;
			if (cmd && check_end_of_command(cmd, 1)) {
				for (int k = 0; k < 32; k++) {
					if (bank == 0x777)
						cmd_dumpmem16(par1);
					else
						cmd_dumpmem28(par1);
					par1 += 16;
					umon_printf("\n");
				}
			}
			break;
		case 's':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFFFFF);
			cmd_setmem(cmd, par1);
			break;
#if 0
		case 't':
			if (!*cmd)
				m65mon_do_trace();
			else if (*cmd == 'c') {
				if (check_end_of_command(cmd, 1))
					m65mon_do_trace_c();
			} else {
				cmd = parse_hex_arg(cmd, &par1, 0, 1);
				if (cmd && check_end_of_command(cmd, 1))
					m65mon_set_trace(par1);
			}
			break;
#endif
		case 'b':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFF);
			if (cmd && check_end_of_command(cmd, 1))
				set_breakpoint(par1);
			break;
		case 'g':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFF);
			cpu65_debug_set_pc(par1);
			break;
#if 0
		case 'N':
			m65mon_next_command();
			break;
#endif
		case 0:
			m65mon_empty_command();	// emulator can use this, if it wants
			break;
		case '!':
			reset_mega65();
			break;
		case '~':
			if (!strncmp(cmd, "exit", 4)) {
				XEMUEXIT(0);
			} else if (!strncmp(cmd, "reset", 5)) {
				reset_mega65();
			} else if (!strncmp(cmd, "mount", 5)) {
				// Quite crude syntax for now:
				// 	~mount0		- unmounting image/disk in drive-0
				//	~mount1		- --""-- in drive-1
				//	~mount0disk.d81	- mounting "disk81" to drive-0 (yes, no spaces, etc ....)
				// So you got it.
				cmd += 5;
				if (*cmd && (*cmd == '0' || *cmd == '1')) {
					const int unit = *cmd - '0';
					cmd++;
					if (*cmd) {
						umon_printf("Mounting %d for: \"%s\"", unit, cmd);
						if (!sdcard_external_mount(unit, cmd, "Monitor: D81 mount failure")) {
							OSD(-1, -1, "Mounted (%d): %s", unit, cmd);
						}
					} else {
						sdcard_unmount(unit);
						OSD(-1, -1, "Unmounted (%d)", unit);
					}
				}
			} else
				umon_printf(UMON_SYNTAX_ERROR "unknown (or not implemented) Xemu special command: %s", cmd - 1);
			break;
		default:
			umon_printf(UMON_SYNTAX_ERROR "unknown (or not implemented) command '%c'", cmd[-1]);
			break;
	}
}


/* ------------------------- SOCKET HANDLING, etc ------------------------- */


int uartmon_is_active ( void )
{
	return sock_server != UNCONNECTED;
}


int uartmon_init ( const char *fn )
{
	subsystem_init();
	static char fn_stored[PATH_MAX] = "";
	int xerr;
	xemusock_socket_t sock;
	if (*fn_stored) {
		ERROR_WINDOW("UARTMON: already activated on %s", fn_stored);
		return 1;
	}
	sock_server = UNCONNECTED;
	sock_client = UNCONNECTED;
	if (!fn || !*fn) {
		DEBUGPRINT("UARTMON: disabled, no name is specified to bind to." NL);
		return 0;
	}
	const char *init_status = xemusock_init();
	if (init_status) {
		ERROR_WINDOW("Cannot initialize network, uart_mon won't be availbale\n%s", init_status);
		return 1;
	}
	if (fn[0] == ':') {
		int port = atoi(fn + 1);
		if (port < 1024 || port > 65535) {
			ERROR_WINDOW("uartmon: invalid port specification %d (1024-65535 is allowed) from string %s", port, fn);
			return 1;
		}
		struct sockaddr_in sock_st;
		sock_len = sizeof(struct sockaddr_in);
		//sock = socket(AF_INET, SOCK_STREAM, 0);
		sock = xemusock_create_for_inet(XEMUSOCK_TCP, XEMUSOCK_BLOCKING, &xerr);
		if (sock == XS_INVALID_SOCKET) {
			ERROR_WINDOW("Cannot create TCP socket: %s", xemusock_strerror(xerr));
			return 1;
		}
		if (xemusock_setsockopt_reuseaddr(sock, &xerr)) {
			ERROR_WINDOW("UARTMON: setsockopt for SO_REUSEADDR failed with %s", xemusock_strerror(xerr));
		}
		//sock_st.sin_family = AF_INET;
		//sock_st.sin_addr.s_addr = htonl(INADDR_ANY);
		//sock_st.sin_port = htons(port);
		xemusock_fill_servaddr_for_inet_ip_native(&sock_st, 0, port);
		if (xemusock_bind(sock, (struct sockaddr*)&sock_st, sock_len, &xerr)) {
			ERROR_WINDOW("Cannot bind TCP socket %d, UART monitor cannot be used: %s", port, xemusock_strerror(xerr));
			xemusock_close(sock, NULL);
			return 1;
		}
	} else {
#if !defined(XEMU_ARCH_UNIX)
		ERROR_WINDOW("On non-UNIX systems, you must use TCP/IP sockets, so uartmon parameter must be in form of :n (n=port number to bind to)\nUARTMON is not available because of bad syntax.");
		return 1;
#else
		// This is UNIX specific code (UNIX named socket) thus it's OK not use Xemu socket API calls here.
		// Note: on longer term, we want to drop this, and allow only TCP sockets to be more unified and simple.
		struct sockaddr_un sock_st;
		sock_len = sizeof(struct sockaddr_un);
		sock = socket(AF_UNIX, SOCK_STREAM, 0);
		if (sock < 0) {
			ERROR_WINDOW("Cannot create named socket %s, UART monitor cannot be used: %s", fn, strerror(errno));
			return 1;
		}
		sock_st.sun_family = AF_UNIX;
		strcpy(sock_st.sun_path, fn);
		unlink(sock_st.sun_path);
		if (bind(sock, (struct sockaddr*)&sock_st, sock_len)) {
			ERROR_WINDOW("Cannot bind named socket %s, UART monitor cannot be used: %s", fn, strerror(errno));
			xemusock_close(sock, NULL);
			return 1;
		}
#endif
	}
	if (xemusock_listen(sock, 5, &xerr)) {
		ERROR_WINDOW("Cannot listen socket %s, UART monitor cannot be used: %s", fn, xemusock_strerror(xerr));
		xemusock_close(sock, NULL);
		return 1;
	}
	if (xemusock_set_nonblocking(sock, XEMUSOCK_NONBLOCKING, &xerr)) {
		ERROR_WINDOW("Cannot set socket %s into non-blocking mode, UART monitor cannot be used: %s", fn, xemusock_strerror(xerr));
		xemusock_close(sock, NULL);
		return 1;
	}
	DEBUG("UARTMON: monitor is listening on socket %s" NL, fn);
	sock_client = UNCONNECTED;	// no client connection yet
	sock_server = sock;		// now set the server socket visible outside of this function too
	umon_echo = 1;
	umon_send_ok = 1;
	strcpy(fn_stored, fn);
	return 0;
}


void uartmon_close  ( void )
{
	if (sock_server != UNCONNECTED) {
		xemusock_close(sock_server, NULL);
		sock_server = UNCONNECTED;
	}
	if (sock_client != UNCONNECTED) {
		xemusock_close(sock_client, NULL);
		sock_client = UNCONNECTED;
	}
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
	int xerr, ret;
	// If there is no server socket, we can't do anything!
	if (sock_server == UNCONNECTED)
		return;
	// Try to accept new connection, if not yet have one (we handle only *ONE* connection!!!!)
	if (sock_client == UNCONNECTED) {
		//struct sockaddr_un sock_st;
		xemusock_socklen_t len = sock_len;
		union {
#ifdef XEMU_ARCH_UNIX
			struct sockaddr_un un;
#endif
			struct sockaddr_in in;
		} sock_st;
		xemusock_socket_t ret_sock = xemusock_accept(sock_server, (struct sockaddr *)&sock_st, &len, &xerr);
		if (ret_sock != XS_INVALID_SOCKET || (ret_sock == XS_INVALID_SOCKET && !xemusock_should_repeat_from_error(xerr)))
			DEBUG("UARTMON: accept()=" PRINTF_SOCK " error=%s" NL,
				(Sint64)ret_sock,
				ret_sock != XS_INVALID_SOCKET ? "OK" : xemusock_strerror(xerr)
			);
		if (ret_sock != XS_INVALID_SOCKET) {
			if (xemusock_set_nonblocking(ret_sock, 1, &xerr)) {
				DEBUGPRINT("UARTMON: error, cannot make socket non-blocking %s", xemusock_strerror(xerr));
				xemusock_close(ret_sock, NULL);
				return;
			} else {
				sock_client = ret_sock;	// "publish" new client socket
				// Reset reading/writing information
				umon_write_size = 0;
				umon_read_pos = 0;
				DEBUGPRINT("UARTMON: new connection established on socket " PRINTF_SOCK NL, (Sint64)sock_client);
			}
		}
	}
	// If no established connection, return
	if (sock_client == UNCONNECTED)
		return;
	// If there is data to write, try to write
	if (umon_write_size) {
		if (!umon_send_ok)
			return;
		ret = xemusock_send(sock_client, umon_write_buffer + umon_write_pos, umon_write_size, &xerr);
		if (ret != XS_SOCKET_ERROR || (ret == XS_SOCKET_ERROR && !xemusock_should_repeat_from_error(xerr)))
			DEBUG("UARTMON: write(" PRINTF_SOCK ",buffer+%d,%d)=%d (%s)" NL,
				(Sint64)sock_client, umon_write_pos, umon_write_size,
				ret, ret == XS_SOCKET_ERROR ? xemusock_strerror(xerr) : "OK"
			);
		if (ret == 0) { // client socket closed
			xemusock_close(sock_client, NULL);
			sock_client = UNCONNECTED;
			DEBUGPRINT("UARTMON: connection closed by peer while writing" NL);
			return;
		}
		if (ret > 0) {
			//debug_buffer_slice(umon_write_buffer + umon_write_pos, ret);
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
	ret = xemusock_recv(sock_client, umon_read_buffer + umon_read_pos, sizeof(umon_read_buffer) - umon_read_pos - 1, &xerr);
	if (ret != XS_SOCKET_ERROR || (ret == XS_SOCKET_ERROR && !xemusock_should_repeat_from_error(xerr)))
		DEBUG("UARTMON: read(" PRINTF_SOCK ",buffer+%d,%d)=%d (%s)" NL,
			(Sint64)sock_client, umon_read_pos, (int)sizeof(umon_read_buffer) - umon_read_pos - 1,
			ret, ret == XS_SOCKET_ERROR ? xemusock_strerror(xerr) : "OK"
		);
	if (ret == 0) { // client socket closed
		xemusock_close(sock_client, NULL);
		sock_client = UNCONNECTED;
		DEBUGPRINT("UARTMON: connection closed by peer while reading" NL);
		return;
	}
	if (ret > 0) {
		/* ECHO: provide echo for the client */
		if (umon_echo) {
			char*p = umon_read_buffer + umon_read_pos;
			int n = ret;
			while (n--)
				if (*p != 13 && *p != 10) {
					umon_write_buffer[umon_write_size++] = *(p++);
				} else {
					umon_echo = 0; // setting to zero avoids more input to echo, and also signs a complete command
					*p = 0; // terminate string in read buffer
					break;
				}
		}
		/* ECHO: end */
		umon_read_pos += ret;
		umon_read_buffer[umon_read_pos] = 0;
		//debug_buffer(umon_read_buffer);
		if (!umon_echo || sizeof(umon_read_buffer) - umon_read_pos - 1 == 0) {
			umon_read_buffer[sizeof(umon_read_buffer) - 1] = 0; // just in case of a "mega long command" with filled rx buffer ...
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

#endif
