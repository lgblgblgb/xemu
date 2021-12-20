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

static struct {
	const char *name;
	int         offs;
	int         exec;
} debug_symbols[0x4000];

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
		if (XEMU_UNLIKELY(skipped_byte != 0xEA)) {	// $EA = opcode of NOP
			char msg[256];
			snprintf(msg, sizeof msg,
				"Writing hypervisor trap $%02X must be followed by NOP\n"
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
	static int init_done = 0;
	if (init_done)
		FATAL("%s() cannot be called more times!", __func__);
	init_done = 1;
	hypervisor_serial_out_asciizer = use_hypervisor_serial_out_asciizer;
	if (!fn || !*fn) {
		DEBUG("MEGADEBUG: feature is not enabled, null file name for list file" NL);
		return 1;
	}
	int fd = xemu_open_file(fn, O_RDONLY, NULL, NULL);
	if (fd < 0) {
		ERROR_WINDOW("Cannot open %s, no resolved symbols will be used.", fn);
		return 1;
	}
	FILE *fp = fdopen(fd, "rb");
	if (!fp) {
		ERROR_WINDOW("Cannot open %s, no resolved symbols will be used.", fn);
		close(fd);
		return 1;
	}
	// Preinitalize symtab
	int syms[0x4000];
	for (int a = 0; a < 0x4000; a++)
		syms[a] = -1;
	// to have something as the first one (may be overwritten later anyway)
	char *storage = xemu_strdup("start_of_hypervisor_ram_at_8000");
	int storage_size = strlen(storage) + 1;
	syms[0] = 0;
	// Start parsing the file
	int total_symbols = 1;
	char linebuf[256];
	int lineno = 0, size_now = 0;
	while (fgets(linebuf, sizeof(linebuf) - 1, fp)) {
		lineno++;
		if (strlen(linebuf) >= sizeof(linebuf) - 4) {
			ERROR_WINDOW("Bad hickup debug SYM file: contains extra long line in line %d", lineno);
			goto error;
		}
		char sym[256];
		int addr;
		if (sscanf(linebuf, "%s = $%x", sym, &addr) != 2) {
			ERROR_WINDOW("Bad hickup debug SYM file: contains badly formatted line in line %d", lineno);
			goto error;
		}
		if (addr < 0x8000 || addr >= 0xC000) {
			continue;
		}
		total_symbols++;
		size_now = strlen(sym) + 1;
		// Store symbol name in the name "storage"
		storage = xemu_realloc(storage, storage_size + size_now);
		strcpy(storage + storage_size, sym);
		// Store address + offset. Note: DO NOT use pointers, as "realloc" above could alter that!
		syms[addr - 0x8000] = storage_size;
		// Finally increate storage size offset
		storage_size += size_now;
	}
	fclose(fp);
	close(fd);
	DEBUGPRINT("HYPERVISOR-DEBUG: loaded %d symbols, using %d bytes of memory for symbol storage." NL, total_symbols, storage_size - size_now);
	// OK, now expand the symbol table for untaken positions, and "export" the result to global variable, with pointers
	for (int i = 0, lastref = 0; i < 0x4000; i++) {
		if (syms[i] >= 0) {
			debug_symbols[i].name = storage + syms[i];
			debug_symbols[i].offs = 0;
			debug_symbols[i].exec = 1;	// FIXME: later, it can be used to tell if it's OK to be executed there, needs the "rep" file
			lastref = i;
		} else {
			debug_symbols[i].name = debug_symbols[lastref].name;
			debug_symbols[i].offs = i - lastref;
			debug_symbols[i].exec = 1;	// FIXME: see the previous similar comment above.
		}
	}
	// Now, it's really the end
	resolver_ok = 1;
	hypervisor_is_debugged = hypervisor_debug;
	return 0;
error:
	fclose(fp);
	close(fd);
	return 1;
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
	if (XEMU_UNLIKELY(within_hypervisor_ram && !debug_symbols[cpu65.pc - 0x8000].exec)) {
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
				"HYPERVISOR-DEBUG: PC=%04X SP=%04X B=%02X A=%02X X=%02X Y=%02X Z=%02X P=%c%c%c%c%c%c%c%c IO=%d OPC=%02X @ %s+%d" NL,
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
				within_hypervisor_ram ? debug_symbols[cpu65.pc - 0x8000].name : "NOT_IN_HYPERVISOR_RAM",
				within_hypervisor_ram ? debug_symbols[cpu65.pc - 0x8000].offs : 0
			);
		}
	}
}
