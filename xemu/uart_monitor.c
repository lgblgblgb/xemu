/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include "xemu/emutools.h"
#ifdef MEGA65
#include "mega65.h"
#else
#include "commodore_65.h"
#endif
#include "cpu65c02.h"
#include "uart_monitor.h"

int  umon_write_size;
int  umon_send_ok;
char umon_write_buffer[UMON_WRITE_BUFFER_SIZE];

const char emulator_paused_title[] = "TRACE/PAUSE";

#ifdef _WIN32
// Windows is not supported currently, as it does not have POSIX-standard socket interface (?).
int  uartmon_init   ( const char *fn ) { return 1; }
void uartmon_update ( void ) {}
void uartmon_close  ( void ) {}
void uartmon_finish_command ( void ) {}
int  m65mon_update(void){ return 0;}
#else


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>


static int  sock_server, sock_client;
static int  umon_write_pos, umon_read_pos;
static int  umon_echo;
static char umon_read_buffer [0x1000];

#include "cpu65ce02_disasm_tables.c"
static const char  opcode_adm_off [] = {0    ,1     ,2     ,1   ,2        ,1      ,1      ,2      ,2        ,2        ,3    ,4      ,1        ,1        ,1           ,1        ,2        ,2          ,0  };
static const char *opcode_adm_pre [] = {""   ,"#$"  ,"#$"  ,"$" ,"$"      ,"$"    ,"$"    ,"$"    ,"$"      ,"$"      ,"$"  ,"$"    ,"($"     ,"($"     ,"($"        ,"($"     ,"($"     ,"($"       ," "};
static const char *opcode_adm_post[] = {""   ,""    ,""    ,""  ,",$rr"   ,",X"   ,",Y"   ,""     ,",X"     ,",Y"     ,""    ,"" ,"),Y"    ,"),Z"    ,",SP),Y"    ,",X)"    ,")"      ,",X)"      ,"A"};
                                     //{"","#$nn","#$nnnn","$nn","$nn,$rr","$nn,X","$nn,Y","$nnnn","$nnnn,X","$nnnn,Y","$rr","$rrrr","($nn),Y","($nn),Z","($nn,SP),Y","($nn,X)","($nnnn)","($nnnn,X)","A"};


/* Variables controlling behaviourt of EMU main-loop*/
static int   paused = 0, paused_old = 0;
static int   breakpoint_pc = -1;
static int   trace_step_trigger = 0;
static void (*m65mon_callback)(void) = NULL;





// WARNING: This source is pretty ugly, ie not so much check of overflow of the output (write) buffer.


static char *parse_hex_arg ( char *p, int *val, int min, int max )
{
	int r;
	while (*p == 32)
		p++;
	*val = -1;
	if (!*p) {
		umon_printf(SYNTAX_ERROR "unexpected end of command (no parameter)");
		return NULL;
	}
	for (r = 0;;) {
		if (*p >= 'a' && *p <= 'f')
			r = (r << 4) | (*p - 'a' + 10);
		else if (*p >= 'A' && *p <= 'F')
			r = (r << 4) | (*p - 'A' + 10);
		else if (*p >= '0' && *p <= '9')
			r = (r << 4) | (*p - '0');
		else if (*p == 32 || *p == 0)
			break;
		else {
			umon_printf(SYNTAX_ERROR "invalid data as hex digit '%c'", *p);
			return NULL;
		}
		p++;
	}
	*val = r;
	if (r < min || r > max) {
		umon_printf(SYNTAX_ERROR "command parameter's value is outside of the allowed range for this command %X (%X...%X)", r, min, max);
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
			umon_printf(SYNTAX_ERROR "unexpected command parameter");
		return 0;
	}
	return 1;
}



static void execute_command ( char *cmd )
{
	int par1;
	char *p = cmd;
	while (*p)
		if (p == cmd && (*cmd == 32 || *cmd == '\t' || *cmd == 8))
			cmd = ++p;
		else if (*p == '\t')
			*(p++) = 32;
		else if (*p == 8)
			memmove(p - 1, p + 1, strlen(p + 1) + 1);
		else if (*p > 127 || *p < 32) {
			umon_printf(SYNTAX_ERROR "invalid character in the command (ASCII=%d)", *p);
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
				umon_printf("Xemu/Mega65 Serial Monitor\r\nWarning: not 100%% compatible with UART monitor of a *real* Mega65 ...");
			break;
		case 'r':
		case 'R':
			if (check_end_of_command(cmd, 1))
				m65mon_show_regs();
			break;
		case 'd':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFF);
			if (cmd && check_end_of_command(cmd, 1))
				m65mon_dissassmem16(par1);                                
			break;
                case 'm':
                        cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFF);
                        if (cmd && check_end_of_command(cmd, 1))
                                m65mon_dumpmem16(par1);
                        break;
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
		case 'b':
			cmd = parse_hex_arg(cmd, &par1, 0, 0xFFFF);
			if (cmd && check_end_of_command(cmd, 1))
				m65mon_breakpoint(par1);
			break;
		case 0:
			m65mon_empty_command(); // emulator can use it, if it wants
			break;
		default:
			umon_printf(SYNTAX_ERROR "unknown (or not implemented) command '%c'", cmd[-1]);
			break;
	}
}

/***************************************************************************/
/*   Disassembler */
/***************************************************************************/


Uint16 umon_print_disass_line(Uint16 disass_pc, Uint8 disass_op){
    
      Uint16 addr_off=opcode_adm_off[opcode_adms[disass_op]];
  
       switch (addr_off){
         case 0:  // no extra argument
                umon_printf(
                "%04X %s %s%s         \r\n",                          // PC , disambled opcode , prefix , postfix
                disass_pc, opcode_names[disass_op], opcode_adm_pre[opcode_adms[disass_op]], opcode_adm_post[opcode_adms[disass_op]] );
                break;
         case 1: // 8-bit argument
                umon_printf(
                "%04X %s %s%02X%s     \r\n",                          // PC , disambled opcode , prefix , postfix
                disass_pc, opcode_names[disass_op], opcode_adm_pre[opcode_adms[disass_op]],cpu_read(disass_pc+1)&0xff, opcode_adm_post[opcode_adms[disass_op]] );
                break;
         case 2: // 16-bit argument
                umon_printf(
                "%04X %s %s%02X%02x%s    \r\n",                          // PC , disambled opcode , prefix , postfix
                disass_pc, opcode_names[disass_op], opcode_adm_pre[opcode_adms[disass_op]],cpu_read(disass_pc+2)&0xff,cpu_read(disass_pc+1)&0xff, opcode_adm_post[opcode_adms[disass_op]] );
                break;
         case 3: // 8-bit relative argument
                umon_printf(
                "%04X %s %s%04X%s    \r\n",                          // PC , disambled opcode , prefix , postfix
                disass_pc, opcode_names[disass_op], opcode_adm_pre[opcode_adms[disass_op]],disass_pc+(char)(cpu_read(disass_pc+1)&0xff)+2, opcode_adm_post[opcode_adms[disass_op]] );
                addr_off=1;
                break;
         case 4: // 16-bit releative argument
                umon_printf(
                "%04X %s %s%04X%s    \r\n",                          // PC , disambled opcode , prefix , postfix
                disass_pc, opcode_names[disass_op], opcode_adm_pre[opcode_adms[disass_op]],disass_pc+(int16_t)((cpu_read(disass_pc+1)&0xff)+0x100*(cpu_read(disass_pc+2)&0xff))+3, opcode_adm_post[opcode_adms[disass_op]] );
                addr_off=2;
                break;
                
         default:
           printf ( "Unkonw opcode %02X at address $%0X4 ",disass_op,disass_pc); 
           break;
      }
     
     return (addr_off);
      
}


/***************************************************************************/
/*   Monitor- commands */
/***************************************************************************/
void m65mon_dumpmem16 ( Uint16 addr )
{
        int n = 16;
        umon_printf(":000%04X", addr);
        while (n--)
                umon_printf(" %02X", cpu_read(addr++));
}

void m65mon_dissassmem16 ( Uint16 addr )
{
        int n = 16;
        Uint16 addr_off;
        do {
          addr_off=umon_print_disass_line(addr,cpu_read(addr)); 
          addr++; // at least to next opcode
          addr+=addr_off; // skip arguments
        }while (n--);
                
}

void m65mon_show_disam ( void )
{
   umon_print_disass_line(cpu_old_pc,cpu_op);
}



void m65mon_show_regs ( void )
{
        umon_printf(
                "PC   A  X  Y  Z  B  SP   MAPL MAPH LAST-OP     P  P-FLAGS   RGP uS IO\r\n"
                "%04X %02X %02X %02X %02X %02X %04X "           // register banned message and things from PC to SP
                "%04X %04X %02X       %02X %02X "               // from MAPL to P
                "%c%c%c%c%c%c%c%c ",                            // P-FLAGS
                cpu_pc, cpu_a, cpu_x, cpu_y, cpu_z, cpu_bphi >> 8, cpu_sphi | cpu_sp,
                map_offset_low >> 8, map_offset_high >> 8, cpu_op,
                cpu_get_p(), 0, // flags
                cpu_pfn ? 'N' : '-',
                cpu_pfv ? 'V' : '-',
                cpu_pfe ? 'E' : '-',
                cpu_pfb ? 'B' : '-',
                cpu_pfd ? 'D' : '-',
                cpu_pfi ? 'I' : '-',
                cpu_pfz ? 'Z' : '-',
                cpu_pfc ? 'C' : '-'
        );
}

    

void m65mon_set_trace ( int m )
{
        paused = m;
}

void m65mon_do_trace ( void )
{
        if (paused) {
                umon_send_ok = 0; // delay command execution!
                m65mon_callback = m65mon_show_disam ; //m65mon_show_regs; // register callback
                trace_step_trigger = 1; // trigger one step
        } else {
                umon_printf(SYNTAX_ERROR "trace can be used only in trace mode");
        }
}

void m65mon_do_trace_c ( void )
{
        umon_printf(SYNTAX_ERROR "command 'tc' is not implemented yet");
}

void m65mon_empty_command ( void )
{
        if (paused)
                m65mon_do_trace();
}

void m65mon_breakpoint ( int brk )
{
        breakpoint_pc = brk;
}

/**************************************************************************/
/*       m65mon_update is called from emulator-mainloop returns pause-mode */
/**************************************************************************/

int m65mon_update ( void )
{

    if (paused){
     if (m65mon_callback) {  // delayed uart monitor command should be finished ...
            m65mon_callback();
            m65mon_callback = NULL;
            uartmon_finish_command();
     }
     // we still need to feed our emulator with update events ... It also slows this pause-busy-loop down to every full frames (~25Hz)
     // note, that it messes timing up a bit here, as there is update_emulator() calls later in the "normal" code as well
     // this can be a bug, but real-time emulation is not so much an issue if you eg doing trace of your code ...
     update_emulator();
     if (trace_step_trigger) {
          // if monitor trigges a step, break the pause loop, however we will get back the control on the next
          // iteration of the infinite "for" loop, as "paused" is not altered
          trace_step_trigger = 0;
          return (0);
     }
     if (paused != paused_old) {
             paused_old = paused;
             if (paused){
                     fprintf(stderr, "TRACE: entering into trace mode @ $%04X" NL, cpu_pc);
                     m65mon_show_regs();       
             }else{
                     fprintf(stderr, "TRACE: leaving trace mode @ $%04X" NL, cpu_pc);
             }
     }
    }else{
      if (breakpoint_pc == cpu_pc) {
         fprintf(stderr, "Breakpoint @ $%04X hit, Xemu moves to trace mode after the execution of this opcode." NL, cpu_pc);
         paused = 1;
      }
    }
        
   
    return (paused);
}

/**************************************************************************/

/* ------------------------- SOCKET HANDLING, etc ------------------------- */


static int set_nonblock ( int fd )
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1)
		return 1;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
		return 1;
	return 0;
}



int uartmon_init ( const char *fn )
{
	struct sockaddr_un sock_st;
	int sock;
	sock_server = -1;
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		ERROR_WINDOW("Cannot create named socket %s, UART monitor cannot be used: %s\n", fn, strerror(errno));
		return 1;
	}
	sock_st.sun_family = AF_UNIX;
	strcpy(sock_st.sun_path, fn);
        unlink(sock_st.sun_path);
	if (bind(sock, (struct sockaddr*)&sock_st, sizeof(struct sockaddr_un))) {
		ERROR_WINDOW("Cannot bind named socket %s, UART monitor cannot be used: %s\n", fn, strerror(errno));
		close(sock);
		return 1;
	}
	if (listen(sock, 5)) {
		ERROR_WINDOW("Cannot listen with named socket %s, UART monitor cannot be used: %s\n", fn, strerror(errno));
		close(sock);
		return 1;
	}
	if (set_nonblock(sock)) {
		ERROR_WINDOW("Cannot set named socket %s into non-blocking mode, UART monitor cannot be used: %s\n", fn, strerror(errno));
		close(sock);
		return 1;
	}
	DEBUG("UARTMON: monitor is listening on socket %s" NL, fn);
	sock_client = -1;	// no client connection yet
	sock_server = sock;	// now set the socket
	umon_echo = 1;
	umon_send_ok = 1;
	return 0;
}



void uartmon_close  ( void )
{
	if (sock_server >= 0) {
		close(sock_server);
		if (sock_client >= 0)
			close(sock_client);
	}
	sock_server = -1;
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
	int ret;
	// If there is no server socket, we can't do anything!
	if (sock_server < 0)
		return;
	// Try to accept new connection, if not yet have one (we handle only *ONE* connection!!!!)
	if (sock_client < 0) {
		struct sockaddr_un sock_st;
		socklen_t len = sizeof(struct sockaddr_un);
		ret = accept(sock_server, (struct sockaddr *)&sock_st, &len);
		if (ret >=0 || (errno != EAGAIN && errno != EWOULDBLOCK))
			DEBUG("UARTMON: accept()=%d error=%s" NL,
				ret,
				ret >= 0 ? "OK" : strerror(errno)
			);
		if (ret >= 0) {
			if (set_nonblock(ret)) {
				close(ret);
			} else {
				sock_client = ret;	// "publish" new client socket
				// Reset reading/writing information
				umon_write_size = 0;
				umon_read_pos = 0;
				fprintf(stderr, "UARTMON: new connection established on socket %d" NL, sock_client);
			}
		}
	}
	// If no established connection, return
	if (sock_client < 0)
		return;
	// If there is data to write, try to write
	if (umon_write_size) {
		if (!umon_send_ok)
			return;
		ret = write(sock_client, umon_write_buffer + umon_write_pos, umon_write_size);
		if (ret >=0 || (errno != EAGAIN && errno != EWOULDBLOCK))
			DEBUG("UARTMON: write(%d,buffer+%d,%d)=%d (%s)" NL,
				sock_client, umon_write_pos, umon_write_size,
				ret, ret < 0 ? strerror(errno) : "OK"
			);
		if (ret == 0) { // client socket closed
			close(sock_client);
			sock_client = -1;
			fprintf(stderr, "UARTMON: connection closed by peer while writing" NL);
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
	ret = read(sock_client, umon_read_buffer + umon_read_pos, sizeof(umon_read_buffer) - umon_read_pos - 1);
	if (ret >=0 || (errno != EAGAIN && errno != EWOULDBLOCK))
		DEBUG("UARTMON: read(%d,buffer+%d,%d)=%d (%s)" NL,
			sock_client, umon_read_pos, (int)sizeof(umon_read_buffer) - umon_read_pos - 1,
			ret, ret < 0 ? strerror(errno) : "OK"
		);
	if (ret == 0) { // client socket closed
		close(sock_client);
		sock_client = -1;
		fprintf(stderr, "UARTMON: connection closed by peer while reading" NL);
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
