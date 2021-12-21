/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_files.h"
#include "mega65.h"
#include "hypervisor.h"
#include "xemu/cpu65.h"
#include "vic4.h"
#include "dma65.h"
#include "memory_mapper.h"
#include "io_mapper.h"
#include "xemu/emutools_config.h"
#include "configdb.h"
#include "rom.h"

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


int  in_hypervisor;			// mega65 hypervisor mode
char hyppo_version_string[64];
int  hickup_is_overriden = 0;
int  hypervisor_is_debugged = 0;

static int   resolver_ok = 0;

static char  hypervisor_monout[0x10000];
static char *hypervisor_monout_p = hypervisor_monout;

static int   hypervisor_serial_out_asciizer;

static int   hypervisor_is_first_call;
static int   execution_range_check_gate;
static int   trap_current;

static int   hypervisor_queued_trap = -1;

static char *rep_file = NULL;
static struct {
	const char *src_fn;
	int src_ln;
	const char *sym_name;
	int sym_offs;
	int exec;
	const char *line;
} rep_info[0x4000];

static struct {
	int   func;
	Uint8 in_x, in_y, in_z;
	char  setnam_fn[64];
	const char  *rootdir;
} hdos;

#define DEBUGHDOS(...) DEBUG(__VA_ARGS__)




int hypervisor_queued_enter ( int trapno )
{
	if (!in_hypervisor) {
		DEBUG("HYPERVISOR: no need to queue trap, can be executed now #$%02X" NL, trapno);
		hypervisor_enter(trapno);
		return 0;
	} else if (hypervisor_queued_trap < 0) {
		hypervisor_queued_trap = trapno;
		DEBUG("HYPERVISOR: queueing trap #$%02X" NL, trapno);
		return 0;
	} else {
		DEBUGPRINT("HYPERVISOR: cannot queue trap #$%02X, already have a queued one #$%02X" NL, trapno, hypervisor_queued_trap);
		return 1;
	}
}


// Very same as hypervisor_enter() - actually it calls that
// **BUT** we check here, if next opcode is NOP.
// it should be, as on real hardware this is a relability problem.
// (sometimes one byte is skipped on execution after a trap caused by writing D640-D67F)
void hypervisor_enter_via_write_trap ( int trapno )
{
	if (XEMU_UNLIKELY(dma_is_in_use())) {
		static int do_warn = 1;
		if (do_warn) {
			WARNING_WINDOW("DMA operation would trigger hypervisor trap.\nThis is totally ignored!\nThere will be no future warning before you restart Xemu");
			do_warn = 0;
		}
		return;
	}
	static int do_nop_check = 1;
	if (do_nop_check) {
		// FIXME: for real there should be a memory reading function independent to the one used by the CPU, since
		// this has some side effects to just fetch a byte to check something, which is otherwise used normally to fetch CPU opcodes and such
		Uint8 skipped_byte = cpu65_read_callback(cpu65.pc);
		if (XEMU_UNLIKELY(skipped_byte != 0xEA && skipped_byte != 0xB8)) {	// $EA = opcode for NOP, $B8 = opcode for CLV
			char msg[256];
			snprintf(msg, sizeof msg,
				"Writing hypervisor trap $%02X must be followed by NOP/CLV\n"
				"but found opcode $%02X at PC=$%04X\n\n"
				"This will cause problems on a real MEGA65!"
				,
				0xD640 + trapno, skipped_byte, cpu65.pc
			);
			if (QUESTION_WINDOW(
				"Ignore now|Ignore all",
				msg
			)) {
				do_nop_check = 0;
				INFO_WINDOW("There will be no further warnings on this issue\nuntil you restart Xemu");
			}
		}
	}
	hypervisor_enter(trapno);
}


static int copy_mem_from_user ( Uint8 *target, int max_size, const int terminator_byte, int source_cpu_addr )
{
	int len = 0;
	if (terminator_byte >= 0)
		max_size--;
	for (;;) {
		const Uint8 byte = memory_debug_read_cpu_addr(source_cpu_addr++);
		*target++ = byte;
		len++;
		if (len >= max_size) {
			if (terminator_byte >= 0)
				*target = terminator_byte;
			break;
		}
		if ((int)byte == terminator_byte)
			break;
	}
	return len;
}


static int copy_string_from_user ( char *target, const int max_size, int source_cpu_addr )
{
	return copy_mem_from_user((Uint8*)target, max_size, 0, source_cpu_addr);
}


static void hdos_enter ( void )
{
	hdos.in_x = cpu65.x;
	hdos.in_y = cpu65.y;
	hdos.in_z = cpu65.z;
	DEBUGHDOS("HDOS: entering function #$%02X" NL, hdos.func);
}


static void hdos_leave ( void )
{
	DEBUGHDOS("HDOS: leaving function #$%02X" NL, hdos.func);
	if (hdos.func == 0x2E && cpu65.pf_c) {	// HDOS setnam function. Also check carry set (which means "ok" by HDOS trap)
		copy_string_from_user(hdos.setnam_fn, sizeof hdos.setnam_fn, hdos.in_x + (hdos.in_y << 8));
		for (char *p = hdos.setnam_fn; *p; p++) {
			if (*p < 0x20 || *p >= 0x7F) {
				DEBUGHDOS("HDOS: setnam(): invalid character in filename $%02X" NL, *p);
				hdos.setnam_fn[0] = '\0';
				break;
			}
			if (*p >= 'A' && *p <= 'Z')
				*p = *p - 'A' + 'a';
		}
		DEBUGHDOS("HDOS: setnam(): selected filename is [%s]" NL, hdos.setnam_fn);
		return;
	}
	if (hdos.func == 0x40) {
		DEBUGHDOS("HDOS: d81attach(): setnam=\"%s\", result=%s" NL, hdos.setnam_fn, cpu65.pf_c ? "OK" : "FAILED");
		return;
	}
}


void hypervisor_enter ( int trapno )
{
	trap_current = trapno;
	// Sanity checks
	if (XEMU_UNLIKELY(trapno > 0x7F || trapno < 0))
		FATAL("FATAL: got invalid trap number %d", trapno);
	if (XEMU_UNLIKELY(in_hypervisor))
		FATAL("FATAL: already in hypervisor mode while calling hypervisor_enter()");
	// First, save machine status into hypervisor registers, TODO: incomplete, can be buggy!
	D6XX_registers[0x40] = cpu65.a;
	D6XX_registers[0x41] = cpu65.x;
	D6XX_registers[0x42] = cpu65.y;
	D6XX_registers[0x43] = cpu65.z;
	D6XX_registers[0x44] = cpu65.bphi >> 8;	// "B" register
	D6XX_registers[0x45] = cpu65.s;
	D6XX_registers[0x46] = cpu65.sphi >> 8;	// stack page register
	D6XX_registers[0x47] = cpu65_get_pf();
	D6XX_registers[0x48] = cpu65.pc & 0xFF;
	D6XX_registers[0x49] = cpu65.pc >> 8;
	D6XX_registers[0x4A] = ((map_offset_low  >> 16) & 0x0F) | ((map_mask & 0x0F) << 4);
	D6XX_registers[0x4B] = ( map_offset_low  >>  8) & 0xFF  ;
	D6XX_registers[0x4C] = ((map_offset_high >> 16) & 0x0F) | ( map_mask & 0xF0);
	D6XX_registers[0x4D] = ( map_offset_high >>  8) & 0xFF  ;
	D6XX_registers[0x4E] = map_megabyte_low  >> 20;
	D6XX_registers[0x4F] = map_megabyte_high >> 20;
	D6XX_registers[0x50] = memory_get_cpu_io_port(0);
	D6XX_registers[0x51] = memory_get_cpu_io_port(1);
	D6XX_registers[0x52] = vic_iomode;
	D6XX_registers[0x53] = dma_registers[5];	// GS $D653 - Hypervisor DMAgic source MB
	D6XX_registers[0x54] = dma_registers[6];	// GS $D654 - Hypervisor DMAgic destination MB
	D6XX_registers[0x55] = dma_registers[0];	// GS $D655 - Hypervisor DMAGic list address bits 0-7
	D6XX_registers[0x56] = dma_registers[1];	// GS $D656 - Hypervisor DMAGic list address bits 15-8
	D6XX_registers[0x57] = (dma_registers[2] & 15) | ((dma_registers[4] & 15) << 4);	// GS $D657 - Hypervisor DMAGic list address bits 23-16
	D6XX_registers[0x58] = dma_registers[4] >> 4;	// GS $D658 - Hypervisor DMAGic list address bits 27-24
	// Now entering into hypervisor mode
	in_hypervisor = 1;	// this will cause apply_memory_config to map hypervisor RAM, also for checks later to out-of-bound execution of hypervisor RAM, etc ...
	vic_iomode = VIC4_IOMODE;
	memory_set_cpu_io_port_ddr_and_data(0x3F, 0x35); // sets all-RAM + I/O config up!
	cpu65.pf_d = 0;		// clear decimal mode ... according to Paul, punnishment will be done, if it's removed :-)
	cpu65.pf_i = 1;		// disable IRQ in hypervisor mode
	cpu65.pf_e = 1;		// 8 bit stack in hypervisor mode
	cpu65.sphi = 0xBE00;	// set a nice shiny stack page
	cpu65.bphi = 0xBF00;	// ... and base page (aka zeropage)
	cpu65.s = 0xFF;
	// Set mapping for the hypervisor
	map_mask = (map_mask & 0xF) | 0x30;	// mapping: 0011XXXX (it seems low region map mask is not changed by hypervisor entry)
	map_megabyte_high = 0xFF << 20;
	map_offset_high = 0xF0000;
	memory_set_vic3_rom_mapping(0);	// for VIC-III rom mapping disable in hypervisor mode
	memory_set_do_map();	// now the memory mapping is changed
	machine_set_speed(0);	// set machine speed (hypervisor always runs at M65 fast ... ??) FIXME: check this!
	cpu65.pc = 0x8000 | (trapno << 2);	// load PC with the address assigned for the given trap number
	DEBUG("HYPERVISOR: entering into hypervisor mode, trap=$%02X (A=$%02X) @ $%04X -> $%04X" NL, trapno, cpu65.a, D6XX_registers[0x48] | (D6XX_registers[0x49] << 8), cpu65.pc);
	if (trapno == TRAP_DOS) {
		hdos.func = cpu65.a & 0x7E;
		hdos_enter();
	} else
		hdos.func = -1;
	if ((trapno == TRAP_FREEZER_RESTORE_PRESS || trapno == TRAP_FREEZER_USER_CALL) && !configdb.allowfreezer) {
		// If freezer is not enabled I warn the user, also return from the hypervisor trap now, without doing anything
		WARNING_WINDOW("FREEZER is not enabled in Xemu currently.");
		// Leave hypervisor mode now, do not allow Hyppo to get this trap.
		if (trapno == TRAP_FREEZER_USER_CALL) {
			D6XX_registers[0x47] &= 0xFE;	// clear carry flag (bit zero)
			D6XX_registers[0x40] = 0xFF;	// set A register to $FF
		}
		hypervisor_leave();
	}
#ifdef TRAP_XEMU
	if (trapno == TRAP_XEMU) {
		// Xemu's own trap.
		ERROR_WINDOW("XEMU TRAP feature is not yet implemented :(");
		// Leave hypervisor mode now, do not allow Hyppo to get this trap.
		D6XX_registers[0x47] &= 0xFE;	// clear carry flag (bit zero)
		D6XX_registers[0x40] = 0xFF;	// set A register to $FF
		hypervisor_leave();
	}
#endif
}


static void extract_version_string ( char *target, int target_max_size )
{
	static const char marker[] = "GIT: ";
	int a = 0;
	// Note: memmem() would be handy, but a bit problematic, some platforms don't have it,
	// others may require special macros to be defined to be able to use it.
	for (;;) {
		if (a == 0x4000 - 0x10) {
			strcpy(target, "???");
			return;
		}
		if (!memcmp(hypervisor_ram + a, marker, strlen(marker)))
			break;
		a++;
	}
	a += strlen(marker);
	int l = 0;
	while (a < 0x4000 && hypervisor_ram[a + l])
		l++;
	if (l >= target_max_size)
		l = target_max_size - 1;
	memcpy(target, hypervisor_ram + a, l);
	target[l] = '\0';
}


static inline void hypervisor_xemu_init ( void )
{
	hyppo_version_string[0] = '\0';
	if (configdb.hdosvirt) {
		// Setup root for the "faked" HDOS traps
		// First build our default one. We may need this anyway, if overriden one invalid
		char dir[PATH_MAX + 1];
		strcpy(dir, sdl_pref_dir);
		strcat(dir, "hdosroot" DIRSEP_STR);
		MKDIR(dir);	// try to make that directory, maybe hasn't existed yet
		if (configdb.hdosdir) {
			XDIR *dirhandle = xemu_os_opendir(configdb.hdosdir);
			if (dirhandle) {
				xemu_os_closedir(dirhandle);
				strcpy(dir, configdb.hdosdir);
				if (dir[strlen(dir) - 1] != DIRSEP_CHR)
					strcat(dir, DIRSEP_STR);
			} else
				ERROR_WINDOW("HYPERVISOR: bad HDOS virtual directory given (cannot open as directory): %s\nUsing the default instead: %s", configdb.hdosdir, dir);
		}
		hdos.rootdir = xemu_strdup(dir);
		DEBUGPRINT("HYPERVISOR: HDOS virtual directory: %s" NL, hdos.rootdir);
	} else {
		hdos.rootdir = NULL;
		DEBUGPRINT("HYPERVISOR: HDOS trap virtualization is not enabled." NL);
	}
}


// Actual (CPU level opcode execution) emulation of MEGA65 should start with calling this function (surely after initialization of every subsystems etc).
void hypervisor_start_machine ( void )
{
	static int init_done = 0;
	if (XEMU_UNLIKELY(!init_done)) {
		init_done = 1;
		hypervisor_xemu_init();
	}
	hdos.setnam_fn[0] = '\0';
	hdos.func = -1;
	in_hypervisor = 0;
	hypervisor_queued_trap = -1;
	hypervisor_is_first_call = 1;
	execution_range_check_gate = 0;
	extract_version_string(hyppo_version_string, sizeof hyppo_version_string);
	DEBUGPRINT("HYPERVISOR: HYPPO version \"%s\" (%s) starting with TRAP reset (#$%02X)" NL, hyppo_version_string, hickup_is_overriden ? "OVERRIDEN" : "built-in", TRAP_RESET);
	hypervisor_enter(TRAP_RESET);
}


static inline void first_leave ( void )
{
	DEBUGPRINT("HYPERVISOR: first return after RESET, start of processing workarounds." NL);
	execution_range_check_gate = 1;
	// Workarounds: ROM override
	// returns with new PC for reset vector _OR_ negative value if no ROM override was needed
	int new_pc = rom_do_override(main_ram + 0x20000);
	if (new_pc >= 0) {
		// if ROM was forced here, PC hypervisor would return is invalid (valid for the _original_ ROM which was overriden here!), thus we must set it now!
		DEBUGPRINT("ROM: force ROM re-apply policy, PC change: $%04X -> $%04X" NL, cpu65.pc, new_pc);
		if (new_pc < 0x8000)
			WARNING_WINDOW("ROM override has a suspect reset address! ($%04X)", new_pc);
		cpu65.pc = new_pc;
		// Since we have overriden ROM, we also must take the responsibility to do what Hyppo would also do:
		// uploading chargen from the loaded ROM into the "char WOM".
		memcpy(char_wom, main_ram + 0x2D000, 0x1000);
	} else {
		DEBUGPRINT("ROM: no custom force-ROM policy, PC remains at $%04X" NL, cpu65.pc);
	}
	// Workaround: set DMA version based on ROM version
	dma_init_set_rev(configdb.dmarev, main_ram + 0x20000);
	// Workaround: force video standard
	if (configdb.init_videostd >= 0) {
		DEBUGPRINT("VIC: setting %s mode as boot-default based on request" NL, configdb.init_videostd ? "NTSC" : "PAL");
		if (configdb.init_videostd)
			vic_registers[0x6F] |= 0x80;
		else
			vic_registers[0x6F] &= 0x7F;
	}
	DEBUGPRINT("HYPERVISOR: first return after RESET, end of processing workarounds." NL);
}


int hypervisor_level_reset ( void )
{
	if (!in_hypervisor) {
		DEBUGPRINT("HYPERVISOR: hypervisor-only reset was requested by Xemu." NL);
		hypervisor_enter(TRAP_RESET);
		last_reset_type = "HYPPO";
		return 0;
	}
	DEBUGPRINT("HYPERVISOR: hypervisor-only reset requested by Xemu **FAILED**: already in hypervisor mode!" NL);
	return 1;
}


void hypervisor_leave ( void )
{
	// Sanity check
	if (XEMU_UNLIKELY(!in_hypervisor))
		FATAL("FATAL: not in hypervisor mode while calling hypervisor_leave()");
	// First, restore machine status from hypervisor registers
	DEBUG("HYPERVISOR: leaving hypervisor mode @ $%04X -> $%04X" NL, cpu65.pc, D6XX_registers[0x48] | (D6XX_registers[0x49] << 8));
	cpu65.a    = D6XX_registers[0x40];
	cpu65.x    = D6XX_registers[0x41];
	cpu65.y    = D6XX_registers[0x42];
	cpu65.z    = D6XX_registers[0x43];
	cpu65.bphi = D6XX_registers[0x44] << 8;	// "B" register
	cpu65.s    = D6XX_registers[0x45];
	cpu65.sphi = D6XX_registers[0x46] << 8;	// stack page register
	cpu65_set_pf(D6XX_registers[0x47]);
	cpu65.pf_e = D6XX_registers[0x47] & CPU65_PF_E;	// cpu65_set_pf() does NOT set 'E' bit by design, so we do at our own
	cpu65.pc   = D6XX_registers[0x48] | (D6XX_registers[0x49] << 8);
	if (XEMU_UNLIKELY(hdos.func >= 0))
		hdos_leave();
	map_offset_low  = ((D6XX_registers[0x4A] & 0xF) << 16) | (D6XX_registers[0x4B] << 8);
	map_offset_high = ((D6XX_registers[0x4C] & 0xF) << 16) | (D6XX_registers[0x4D] << 8);
	map_mask = (D6XX_registers[0x4A] >> 4) | (D6XX_registers[0x4C] & 0xF0);
	map_megabyte_low =  D6XX_registers[0x4E] << 20;
	map_megabyte_high = D6XX_registers[0x4F] << 20;
	memory_set_cpu_io_port_ddr_and_data(D6XX_registers[0x50], D6XX_registers[0x51]);
	vic_iomode = D6XX_registers[0x52] & 3;
	if (vic_iomode == VIC_BAD_IOMODE)
		vic_iomode = VIC3_IOMODE;	// I/O mode "2" (binary: 10) is not used, I guess
	dma_registers[5] = D6XX_registers[0x53];	// GS $D653 - Hypervisor DMAgic source MB
	dma_registers[6] = D6XX_registers[0x54];	// GS $D654 - Hypervisor DMAgic destination MB
	dma_registers[0] = D6XX_registers[0x55];	// GS $D655 - Hypervisor DMAGic list address bits 0-7
	dma_registers[1] = D6XX_registers[0x56];	// GS $D656 - Hypervisor DMAGic list address bits 15-8
	dma_registers[2] = D6XX_registers[0x57] & 15;	//
	dma_registers[4] = (D6XX_registers[0x57] >> 4) | (D6XX_registers[0x58] << 4);
	// Now leaving hypervisor mode ...
	in_hypervisor = 0;
	machine_set_speed(0);	// restore speed ...
	memory_set_vic3_rom_mapping(vic_registers[0x30]);	// restore possible active VIC-III mapping
	memory_set_do_map();	// restore mapping ...
	if (XEMU_UNLIKELY(hypervisor_is_first_call)) {
		if (trap_current != TRAP_RESET)
			ERROR_WINDOW("First hypervisor TRAP is not RESET?!");
		first_leave();
	}
	// Catch the event when it's a reset TRAP but not part of the initial call (not "cold" reset)
	if (XEMU_UNLIKELY(trap_current == TRAP_RESET && !hypervisor_is_first_call)) {
		// If Xemu does a "hyppo level reset" only, we can have problems with knowing the
		// exact ROM type, thus, make sure, to do it here.
		// FIXME: really, should we care about this minor detail so much?! ;)
		rom_clear_reports();
		rom_detect_date(main_ram + 0x20000);
	}
	hypervisor_is_first_call = 0;
	if (XEMU_UNLIKELY(hypervisor_queued_trap >= 0)) {
		// Not so much used currently ...
		DEBUG("HYPERVISOR: processing queued trap on leaving hypervisor: trap #$%02X" NL, hypervisor_queued_trap);
		hypervisor_enter(hypervisor_queued_trap);
		hypervisor_queued_trap = -1;
	}
}


void hypervisor_serial_monitor_push_char ( Uint8 chr )
{
	if (hypervisor_monout_p >= hypervisor_monout - 1 + sizeof hypervisor_monout)
		return;
	int flush = (chr == 0x0A || chr == 0x0D || chr == 0x8A || chr == 0x8D);
	if (hypervisor_monout_p == hypervisor_monout && flush)
		return;
	if (flush) {
		*hypervisor_monout_p = 0;
		if (hypervisor_serial_out_asciizer) {
			unsigned char *p = (unsigned char*)hypervisor_monout;
			while (*p) {
				if      (*p >= 0x61 && *p <= 0x7A)
					*p -= 0x20;
				else if (*p >= 0xC1 && *p <= 0xDA)
					*p -= 0x80;
				else if (*p < 0x20 || *p >= 0x80)
					*p = '?';
				p++;
			}
		}
		fprintf(stderr, "Hypervisor serial output: \"%s\"." NL, hypervisor_monout);
		DEBUG("MEGA65: Hypervisor serial output: \"%s\"." NL, hypervisor_monout);
		hypervisor_monout_p = hypervisor_monout;
		return;
	}
	*(hypervisor_monout_p++) = (char)chr;
}


void hypervisor_debug_invalidate ( const char *reason )
{
	DEBUGPRINT("HYPERVISOR: disabling debug ability: %s" NL, reason);
	if (resolver_ok) {
		resolver_ok = 0;
		INFO_WINDOW("Hypervisor debug feature is asked to be disabled: %s", reason);
	}
}


int hypervisor_debug_init ( const char *fn, int hypervisor_debug, int use_hypervisor_serial_out_asciizer )
{
	hypervisor_serial_out_asciizer = use_hypervisor_serial_out_asciizer;
	if (!fn || !*fn) {
		DEBUG("HYPERVISOR-DEBUG: feature is not enabled, null file name for list file" NL);
		return 1;
	}
	if (rep_file) {
		free(rep_file);
		rep_file = NULL;
	}
	// Load REP file into a memory buffer. Yes, it's maybe a waste of memory a bit, but we can
	// use then to reference various parts of it with pointers, with some mangling though (like inserting field
	// terminator '\0's to have valid C-strings). It also eliminates the need to build up storage for symbols, lines
	// and managing that. Though a bit ugly solution, I admit.
	int ret = xemu_load_file(fn, NULL, 10, 4 << 20, "Failed to load the REP file");	// hopefully 4Mbyte max size is enough
	if (ret < 0)
		return -1;
	rep_file = xemu_realloc(xemu_load_buffer_p, ret + 1);	// reallocate buffer to have space for the '\0' terminator
	xemu_load_buffer_p = NULL;
	rep_file[ret] = '\0';	// terminate the file content, to form a valid C-string
	if (strlen(rep_file) != ret) {
		ERROR_WINDOW("The loaded REP file has NULL character inside, thus it's invalid!");
		goto failure;
	}
	DEBUGPRINT("HYPERVISOR-DEBUG: loaded REP file with %d bytes" NL, ret);
	static const char unknown_source_name[] = "UNKNOWN_SRC";
	static const char unknown_line[] = "<UNKNOWN LINE>";
	for (int a = 0; a < 0x4000; a++) {
		rep_info[a].src_fn = unknown_source_name;
		rep_info[a].src_ln = 0;
		rep_info[a].sym_name = NULL;
		rep_info[a].sym_offs = 0;
		rep_info[a].exec = 0;
		rep_info[a].line = unknown_line;
	}
	// The big loop, which walks through the file and parses it
	const char *current_source_file_name = unknown_source_name;
	const char *unresolved_symbol = NULL;
	static const char valid_label_characters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";
	int total_symbols = 0;
	int total_lines = 0;
	for (char *r = rep_file ;*r ;) {
		// search the end of line marker, with trying to guess line ending policy
		//DEBUGPRINT("HYPERVISOR-DEBUG: processing line at offset %d" NL, (int)(r - rep_file));
		char *nl = r;
		while (*nl && *nl != '\r' && *nl != '\n')
			nl++;
		if (*nl) {
			*nl++ = '\0';	// terminate string here
			while (*nl == '\r' || *nl == '\n')	// skip single or multiple '\r'/'\n' combinations (various OS line endings)
				nl++;
		}	// now 'nl' pointer points to the start of the next line, and current line at 'r' should be '\0' terminated C-string
		if (*r == ';') {	// comment at the beginning of the line, must be acme's own, about the Source file name
			char *p = strchr(r, ':');	// acme syntax: "Source: FILENAME", thus search for ':'
			if (p) {
				p++;
				if (*p == ' ')
					p++;	// skip space after ':'
				char *q = p + strlen(p) - 1;
				// do not use the path part of the file name
				while (q >= p && *q != '/' && *q != '\\')
					q--;
				current_source_file_name = q > p ? q + 1 : p;
			} else
				DEBUGPRINT("HYPERVISOR-DEBUG: unknown acme-comment line: %s" NL, r);
			goto next_line;
		}
		if (strlen(r) < 32) {
			//DEBUGPRINT("HYPERVISOR-DEBUG: too short line!" NL);
			goto next_line;
		}
		r[31] = '\0';		// terminate the 'acme' apart from the 'user' part of the line
		int line_number, addr;
		// try to sscanf() the acme part of the line. We can say things on the meaning of the line
		// based on the return value (how many entities could got). The first is line number in the
		// current source, second is hex memory address (if there is no address, it means, that
		// no code is generated by that line in the REP file, though, we still want to check
		// if there is a label definition within the 'user' part of the line there).
		switch (sscanf(r, "%d %x", &line_number, &addr)) {
			case 0:
				goto next_line;
			case 1:
				addr = -1;	// no address in this line (still interesting for labels, but should be resolved later on the next known address line ...)
				break;
			case 2:
				if (addr < 0x8000 || addr >= 0xC000) {
					ERROR_WINDOW("Bad REP file contains address outside of $8000...$BFFF area!");
					goto failure;
				}
				break;
		}
		r += 32;		// now "r" points to the "user" part of the line
		while (*r && *r <= 0x20)// skip spaces/tabs/odd things though
			r++;
		//DEBUGPRINT("HYPERVISOR-DEBUG: REP-PARSER: (%s:%d) $%X \"%s\"" NL, current_source_file_name, line_number, addr, r);
		// Now search if there is some "label-like" entity in the 'user' part of the line
		char *l = r;
		while (strchr(valid_label_characters, *l))
			l++;
		if (*l == ':') {
			// wow we seems to have found a label!
			*l = '\0'; // terminate string here for the label
			//DEBUGPRINT("HYPERVISOR-DEBUG: found label!!! \"%s\"" NL, r);
			if (addr == -1) {
				if (unresolved_symbol)
					DEBUGPRINT("HYPERVISOR-DEBUG: warning, multiple unresolved syms collide %s -> %s" NL, unresolved_symbol, r);
				unresolved_symbol = r;
			} else
				rep_info[addr - 0x8000].sym_name = r;
			total_symbols++;
			r = l + 1;
		}
		if (addr != -1) {
			rep_info[addr - 0x8000].src_fn = current_source_file_name;
			rep_info[addr - 0x8000].src_ln = line_number;
			rep_info[addr - 0x8000].exec = 1;
			if (unresolved_symbol) {
				rep_info[addr - 0x8000].sym_name = unresolved_symbol;
				unresolved_symbol = NULL;

			}
			while (*r && *r <= 0x20)	// skip spaces/tabs/odd things
				r++;
			rep_info[addr - 0x8000].line = r;
			total_lines++;
		}
	next_line:
		r = nl;
	}
	DEBUGPRINT("HYPERVISOR-DEBUG: imported %d symbols and %d code/data lines" NL, total_symbols, total_lines);
	// for debug info entries not having any symbol, let's populate those with offsets compared to the last known one
	if (!rep_info[0].sym_name) {
		static const char first_fake_label[] = "<HYPPO_BASE>";
		rep_info[0].sym_name = first_fake_label;
	}
	for (int a = 0, last_symi = 0; a < 0x4000; a++) {
		if (!rep_info[a].sym_name) {
			rep_info[a].sym_name = rep_info[last_symi].sym_name;
			rep_info[a].sym_offs = a - last_symi;
		} else
			last_symi = a;
	}
#if 0
	for (int a = 0; a < 0x4000; a++) {
		DEBUGPRINT("HYPERVISOR-DEBUG: FINAL: $%04X %s:%d %s+%d%s%s" NL, a + 0x8000, rep_info[a].src_fn, rep_info[a].src_ln, rep_info[a].sym_name, rep_info[a].sym_offs, rep_info[a].exec ? " [EXEC] " : " ", rep_info[a].line);
	}
	exit(1);
#endif
	// Now, it's really the end
	resolver_ok = 1;
	hypervisor_is_debugged = hypervisor_debug;
	return 0;
failure:
	if (rep_file) {
		free(rep_file);
		rep_file = NULL;
	}
	return -1;
}


void hypervisor_debug ( void )
{
	static int do_execution_range_check = 1;
	if (!in_hypervisor)
		return;
	// TODO: better hypervisor upgrade check, maybe with checking the exact range hyppo/hickup uses for upgrade outside of the "normal" hypervisor mem range
	if (XEMU_UNLIKELY((cpu65.pc & 0xFF00) == 0x3000)) {	// this area is used by HICKUP upgrade
		DEBUG("HYPERVISOR-DEBUG: allowed to run outside of hypervisor memory, no debug info, PC = $%04X" NL, cpu65.pc);
		return;
	}
	const int within_hypervisor_ram = (cpu65.pc >= 0x8000 && cpu65.pc < 0xC000);
	if (XEMU_UNLIKELY(!within_hypervisor_ram && do_execution_range_check && execution_range_check_gate)) {
		DEBUG("HYPERVISOR-DEBUG: execution outside of the hypervisor memory, PC = $%04X" NL, cpu65.pc);
		char msg[128];
		sprintf(msg, "Hypervisor fatal error: execution outside of the hypervisor memory, PC=$%04X SP=$%04X", cpu65.pc, cpu65.sphi | cpu65.s);
		switch (QUESTION_WINDOW("Reset|Exit Xemu|Ignore now|Ingore all", msg)) {
			case 0:
				hypervisor_start_machine();
				break;
			case 1:
				XEMUEXIT(1);
				break;
			case 2:
				break;
			case 3:
				do_execution_range_check = 0;
				break;
		}
		return;
	}
	if (!resolver_ok) {
		return;	// no debug info loaded from hickstart.list ...
	}
	if (XEMU_UNLIKELY(within_hypervisor_ram && !rep_info[cpu65.pc - 0x8000].exec)) {
		DEBUG("HYPERVISOR-DEBUG: execution address not found in list file (out-of-bound code?), PC = $%04X" NL, cpu65.pc);
		FATAL("Hypervisor fatal error: execution address not found in list file (out-of-bound code?), PC = $%04X", cpu65.pc);
		return;
	}
	// WARNING: as it turned out, using stdio I/O to log every opcodes even "only" at ~3.5MHz rate makes emulation _VERY_ slow ...
	if (XEMU_UNLIKELY(hypervisor_is_debugged)) {
		if (debug_fp) {
			const Uint8 pf = cpu65_get_pf();
			fprintf(
				debug_fp,
				"HYPERVISOR-DEBUG: PC=%04X SP=%04X B=%02X A=%02X X=%02X Y=%02X Z=%02X P=%c%c%c%c%c%c%c%c IO=%d OPC=%02X @ %s:%d %s+%d | %s" NL,
				cpu65.pc, cpu65.sphi | cpu65.s, cpu65.bphi >> 8, cpu65.a, cpu65.x, cpu65.y, cpu65.z,
				(pf & CPU65_PF_N) ? 'N' : 'n',
				(pf & CPU65_PF_V) ? 'V' : 'v',
				(pf & CPU65_PF_E) ? 'E' : 'e',
				'-',
				(pf & CPU65_PF_D) ? 'D' : 'd',
				(pf & CPU65_PF_I) ? 'I' : 'i',
				(pf & CPU65_PF_Z) ? 'Z' : 'z',
				(pf & CPU65_PF_C) ? 'C' : 'c',
				vic_iomode,
				cpu65_read_callback(cpu65.pc),
				within_hypervisor_ram ? rep_info[cpu65.pc - 0x8000].src_fn   : "<NOT>",
				within_hypervisor_ram ? rep_info[cpu65.pc - 0x8000].src_ln   : 0,
				within_hypervisor_ram ? rep_info[cpu65.pc - 0x8000].sym_name : "<NOT>",
				within_hypervisor_ram ? rep_info[cpu65.pc - 0x8000].sym_offs : 0,
				within_hypervisor_ram ? rep_info[cpu65.pc - 0x8000].line     : ""
			);
		}
	}
}
