/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/f018_core.h"
#include "memory_mapper.h"
#include "io_mapper.h"
#include "xemu/emutools_config.h"

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


#define INFO_MAX_SIZE	32


int in_hypervisor;			// mega65 hypervisor mode

static char debug_lines[0x4000][2][INFO_MAX_SIZE];		// I know. UGLY! and wasting memory. But this is only a HACK :)
static int resolver_ok = 0;

static char  hypervisor_monout[0x10000];
static char *hypervisor_monout_p = hypervisor_monout;

static int debug_on = 0;
static int hypervisor_serial_out_asciizer;

static int first_hypervisor_leave;

static int hypervisor_queued_trap = -1;


int hypervisor_debug_init ( const char *fn, int hypervisor_debug, int use_hypervisor_serial_out_asciizer )
{
	char buffer[1024];
	FILE *fp;
	int fd;
	hypervisor_serial_out_asciizer = use_hypervisor_serial_out_asciizer;
	if (!fn || !*fn) {
		DEBUG("MEGADEBUG: feature is not enabled, null file name for list file" NL);
		return 1;
	}
	for (fd = 0; fd < 0x4000; fd++) {
		debug_lines[fd][0][0] = 0;
	}
	fd = xemu_open_file(fn, O_RDONLY, NULL, NULL);
	if (fd < 0) {
		INFO_WINDOW("Cannot open %s, no resolved symbols will be used.", fn);
		return 1;
	}
	fp = fdopen(fd, "rb");
	while (fgets(buffer, sizeof buffer, fp)) {
		int addr = 0xFFFF;
		char *p, *p1, *p2;
		if (sscanf(buffer, "%04X", &addr) != 1) continue;
		if (addr < 0x8000 || addr >= 0xC000) continue;
		p2  = strchr(buffer, '|');
		if (!p2) continue;			// no '|' pipe in the line, skip
		if (strchr(p2 + 1, '|')) continue;	// more than one '|' pipes, it's a hex dump, skip
		*(p2++) = 0;
		p = strrchr(p2, '/');
		if (p)
			p2 = p + 1;
		else {
			p = strrchr(p2, '\\');
			if (p)
				p2 = p + 1;
		}
		while (*p2 <= 32 && *p2)
			p2++;
		p = p2 + strlen(p2) - 1;
		while (p > p2 && *p <= 32)
			*(p--) = 0;
		if (strlen(p2) >= 32)
			FATAL("Bad list file, too long file reference part");
		// that was awful. Now the first part
		p1 = buffer + 5;
		while (*p1 && *p1 <= 32)
			p1++;
		p = p1 + strlen(p1) - 1;
		while (p > p1 && *p <= 32)
			*(p--) = 0;
		if (strlen(p2) >= 32)
			FATAL("Bad list file, too long assembly part");
		// And finally, copy line to its well deserved place(s)
		snprintf(debug_lines[addr - 0x8000][0], INFO_MAX_SIZE, "%s", p1);
		snprintf(debug_lines[addr - 0x8000][1], INFO_MAX_SIZE, "%s", p2);
	}
	fclose(fp);
	close(fd);
	resolver_ok = 1;
	debug_on = hypervisor_debug;
	return 0;
}


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



void hypervisor_enter ( int trapno )
{
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
	DEBUG("HYPERVISOR: entering into hypervisor mode, trap=$%02X @ $%04X -> $%04X" NL, trapno, D6XX_registers[0x48] | (D6XX_registers[0x49] << 8), cpu65.pc);
}


// Actual (CPU level opcode execution) emulation of Mega65 should start with calling this function (surely after initialization of every subsystems etc).
void hypervisor_start_machine ( void )
{
	in_hypervisor = 0;
	hypervisor_queued_trap = -1;
	first_hypervisor_leave = 1;
	hypervisor_enter(TRAP_RESET);
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
	if (XEMU_UNLIKELY(first_hypervisor_leave)) {
		first_hypervisor_leave = 0;
		int new_pc = refill_c65_rom_from_preinit_cache();	// this function should decide then, if it's really a (forced) thing to do ...
		if (new_pc >= 0) {
			// positive return value from the re-fill routine: we DID re-fill, we should re-initialize "user space" PC from the return value
			DEBUGPRINT("MEM: force ROM re-apply policy, PC change: $%04X -> $%04X" NL,
				cpu65.pc, new_pc
			);
			cpu65.pc = new_pc;
		} else
			DEBUGPRINT("MEM: no force ROM re-apply policy was requested" NL);
		dma_init_set_rev(xemucfg_get_num("dmarev"), main_ram + 0x20000 + 0x16);
	}
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
	if (resolver_ok) {
		resolver_ok = 0;
		INFO_WINDOW("Hypervisor debug feature is asked to be disabled: %s", reason);
	}
}



void hypervisor_debug ( void )
{
	if (!in_hypervisor)
		return;
	// TODO: better hypervisor upgrade check, maybe with checking the exact range kickstart uses for upgrade outside of the "normal" hypervisor mem range
	if (XEMU_UNLIKELY((cpu65.pc & 0xFF00) == 0x3000)) {	// this area is used by kickstart upgrade
		DEBUG("HYPERVISOR-DEBUG: allowed to run outside of hypervisor memory, no debug info, PC = $%04X" NL, cpu65.pc);
		return;
	}
	if (XEMU_UNLIKELY((cpu65.pc & 0xC000) != 0x8000)) {
		DEBUG("HYPERVISOR-DEBUG: execution outside of the hypervisor memory, PC = $%04X" NL, cpu65.pc);
		ERROR_WINDOW("Hypervisor fatal error: execution outside of the hypervisor memory, PC=$%04X SP=$%04X", cpu65.pc, cpu65.sphi | cpu65.s);
		if (QUESTION_WINDOW("Reset|Exit Xemu", "What to do now?"))
			XEMUEXIT(1);
		else
			hypervisor_start_machine();
		return;
	}
	if (!resolver_ok) {
		return;	// no debug info loaded from kickstart.list ...
	}
	if (XEMU_UNLIKELY(!debug_lines[cpu65.pc - 0x8000][0][0])) {
		DEBUG("HYPERVISOR-DEBUG: execution address not found in list file (out-of-bound code?), PC = $%04X" NL, cpu65.pc);
		FATAL("Hypervisor fatal error: execution address not found in list file (out-of-bound code?), PC = $%04X", cpu65.pc);
		return;
	}
	// WARNING: as it turned out, using stdio I/O to log every opcodes even "only" at ~3.5MHz rate makes emulation _VERY_ slow ...
	if (XEMU_UNLIKELY(debug_on)) {
		if (debug_fp) {
			Uint8 pf = cpu65_get_pf();
			fprintf(
				debug_fp,
				"HYPERVISOR-DEBUG: %-32s PC=%04X SP=%04X B=%02X A=%02X X=%02X Y=%02X Z=%02X P=%c%c%c%c%c%c%c%c IO=%d OPC=%02X @ %s" NL,
				debug_lines[cpu65.pc - 0x8000][0],
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
				debug_lines[cpu65.pc - 0x8000][1]
			);
		}
	}
}
