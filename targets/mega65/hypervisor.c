/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "mega65.h"
#include "hypervisor.h"
#include "xemu/cpu65c02.h"
#include "vic3.h"
#include "xemu/f018_core.h"
#include "memory_mapper.h"
#include "io_mapper.h"

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>


#define INFO_MAX_SIZE	32


int in_hypervisor;			// mega65 hypervisor mode

static char debug_lines[0x4000][2][INFO_MAX_SIZE];		// I know. UGLY! and wasting memory. But this is only a HACK :)
static int resolver_ok = 0;

static char  hypervisor_monout[0x10000];
static char *hypervisor_monout_p = hypervisor_monout;

static int debug_on = 0;



int hypervisor_debug_init ( const char *fn, int hypervisor_debug )
{
	char buffer[1024];
	FILE *fp;
	int fd;
	if (!fn || !*fn) {
		DEBUG("MEGADEBUG: feature is not enabled, null file name for list file" NL);
		return 1;
	}
	for (fd = 0; fd < 0x4000; fd++) {
		debug_lines[fd][0][0] = 0;
	}
	fd = emu_load_file(fn, NULL, -1);
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



void hypervisor_enter ( int trapno )
{
	// Sanity checks
	if (trapno > 0x7F || trapno < 0)
		FATAL("FATAL: got invalid trap number %d", trapno);
	if (in_hypervisor)
		FATAL("FATAL: already in hypervisor mode while calling hypervisor_enter()");
	// First, save machine status into hypervisor registers, TODO: incomplete, can be buggy!
	gs_regs[0x640] = cpu_a;
	gs_regs[0x641] = cpu_x;
	gs_regs[0x642] = cpu_y;
	gs_regs[0x643] = cpu_z;
	gs_regs[0x644] = cpu_bphi >> 8;	// "B" register
	gs_regs[0x645] = cpu_sp;
	gs_regs[0x646] = cpu_sphi >> 8;	// stack page register
	gs_regs[0x647] = cpu_get_p();
	gs_regs[0x648] = cpu_pc & 0xFF;
	gs_regs[0x649] = cpu_pc >> 8;
	gs_regs[0x64A] = ((map_offset_low  >> 16) & 0x0F) | ((map_mask & 0x0F) << 4);
	gs_regs[0x64B] = ( map_offset_low  >>  8) & 0xFF  ;
	gs_regs[0x64C] = ((map_offset_high >> 16) & 0x0F) | ( map_mask & 0xF0);
	gs_regs[0x64D] = ( map_offset_high >>  8) & 0xFF  ;
	gs_regs[0x64E] = map_megabyte_low  >> 20;
	gs_regs[0x64F] = map_megabyte_high >> 20;
	gs_regs[0x650] = memory_get_cpu_io_port(0);
	gs_regs[0x651] = memory_get_cpu_io_port(1);
	gs_regs[0x652] = vic_iomode;
	gs_regs[0x653] = dma_registers[5];	// GS $D653 - Hypervisor DMAgic source MB
	gs_regs[0x654] = dma_registers[6];	// GS $D654 - Hypervisor DMAgic destination MB
	gs_regs[0x655] = dma_registers[0];	// GS $D655 - Hypervisor DMAGic list address bits 0-7
	gs_regs[0x656] = dma_registers[1];	// GS $D656 - Hypervisor DMAGic list address bits 15-8
	gs_regs[0x657] = (dma_registers[2] & 15) | ((dma_registers[4] & 15) << 4);	// GS $D657 - Hypervisor DMAGic list address bits 23-16
	gs_regs[0x658] = dma_registers[4] >> 4;	// GS $D658 - Hypervisor DMAGic list address bits 27-24
	// Now entering into hypervisor mode
	in_hypervisor = 1;	// this will cause apply_memory_config to map hypervisor RAM, also for checks later to out-of-bound execution of hypervisor RAM, etc ...
	vic_iomode = VIC4_IOMODE;
	memory_set_cpu_io_port_ddr_and_data(0x3F, 0x35); // sets all-RAM + I/O config up!
	cpu_pfd = 0;		// clear decimal mode ... according to Paul, punnishment will be done, if it's removed :-)
	cpu_pfi = 1;		// disable IRQ in hypervisor mode
	cpu_pfe = 1;		// 8 bit stack in hypervisor mode
	cpu_sphi = 0xBE00;	// set a nice shiny stack page
	cpu_bphi = 0xBF00;	// ... and base page (aka zeropage)
	cpu_sp = 0xFF;
	// Set mapping for the hypervisor
	map_mask = (map_mask & 0xF) | 0x30;	// mapping: 0011XXXX (it seems low region map mask is not changed by hypervisor entry)
	map_megabyte_high = 0xFF << 20;
	map_offset_high = 0xF0000;
	memory_set_do_map();	// now the memory mapping is changed
	machine_set_speed(0);	// set machine speed (hypervisor always runs at M65 fast ... ??) FIXME: check this!
	cpu_pc = 0x8000 | (trapno << 2);	// load PC with the address assigned for the given trap number
	DEBUG("MEGA65: entering into hypervisor mode, trap=$%02X PC=$%04X" NL, trapno, cpu_pc);
	fprintf(stderr, "HYPERVISOR: entering into hypervisor mode @ $%04X -> $%04X" NL, gs_regs[0x648] | (gs_regs[0x649] << 8), cpu_pc);
}


void hypervisor_leave ( void )
{
	// Sanity checks
	if (!in_hypervisor)
		FATAL("FATAL: not in hypervisor mode while calling hypervisor_leave()");
	// First, restore machine status from hypervisor registers
	fprintf(stderr, "HYPERVISOR: leaving hypervisor mode @ $%04X -> $%04X" NL, cpu_pc, gs_regs[0x648] | (gs_regs[0x649] << 8));
	cpu_a    = gs_regs[0x640];
	cpu_x    = gs_regs[0x641];
	cpu_y    = gs_regs[0x642];
	cpu_z    = gs_regs[0x643];
	cpu_bphi = gs_regs[0x644] << 8;	// "B" register
	cpu_sp   = gs_regs[0x645];
	cpu_sphi = gs_regs[0x646] << 8;	// stack page register
	cpu_set_p(gs_regs[0x647]);
	cpu_pfe = gs_regs[0x647] & 32;	// cpu_set_p() does NOT set 'E' bit by design, so we do at our own
	cpu_pc   = gs_regs[0x648] | (gs_regs[0x649] << 8);
	map_offset_low  = ((gs_regs[0x64A] & 0xF) << 16) | (gs_regs[0x64B] << 8);
	map_offset_high = ((gs_regs[0x64C] & 0xF) << 16) | (gs_regs[0x64D] << 8);
	map_mask = (gs_regs[0x64A] >> 4) | (gs_regs[0x64C] & 0xF0);
	map_megabyte_low =  gs_regs[0x64E] << 20;
	map_megabyte_high = gs_regs[0x64F] << 20;
	memory_set_cpu_io_port_ddr_and_data(gs_regs[0x650], gs_regs[0x651]);
	vic_iomode = gs_regs[0x652] & 3;
	if (vic_iomode == VIC_BAD_IOMODE)
		vic_iomode = VIC3_IOMODE;	// I/O mode "2" (binary: 10) is not used, I guess
	dma_registers[5] = gs_regs[0x653];	// GS $D653 - Hypervisor DMAgic source MB
	dma_registers[6] = gs_regs[0x654];	// GS $D654 - Hypervisor DMAgic destination MB
	dma_registers[0] = gs_regs[0x655];	// GS $D655 - Hypervisor DMAGic list address bits 0-7
	dma_registers[1] = gs_regs[0x656];	// GS $D656 - Hypervisor DMAGic list address bits 15-8
	dma_registers[2] = gs_regs[0x657] & 15;	//
	dma_registers[4] = (gs_regs[0x657] >> 4) | (gs_regs[0x658] << 4);
	// Now leaving hypervisor mode ...
	in_hypervisor = 0;
	machine_set_speed(0);	// restore speed ...
	memory_set_do_map();
	DEBUG("MEGA65: leaving hypervisor mode, (user) PC=$%04X" NL, cpu_pc);
}



void hypervisor_serial_monitor_push_char ( Uint8 chr )
{
	if (hypervisor_monout_p >= hypervisor_monout - 1 + sizeof hypervisor_monout)
		return;
	if (hypervisor_monout_p == hypervisor_monout && (chr == 13 || chr == 10))
		return;
	if (chr == 13 || chr == 10) {
		*hypervisor_monout_p = 0;
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
	if (unlikely((cpu_pc & 0xFF00) == 0x3000)) {	// this area is used by kickstart upgrade
		DEBUG("HYPERVISOR-DEBUG: allowed to run outside of hypervisor memory, no debug info, PC = $%04X" NL, cpu_pc);
		return;
	}
	if (unlikely((cpu_pc & 0xC000) != 0x8000)) {
		DEBUG("HYPERVISOR-DEBUG: execution outside of the hypervisor memory, PC = $%04X" NL, cpu_pc);
		FATAL("Hypervisor fatal error: execution outside of the hypervisor memory, PC=$%04X SP=$%04X", cpu_pc, cpu_sphi | cpu_sp);
		return;
	}
	if (!resolver_ok) {
		return;	// no debug info loaded from kickstart.list ...
	}
	if (unlikely(!debug_lines[cpu_pc - 0x8000][0][0])) {
		DEBUG("HYPERVISOR-DEBUG: execution address not found in list file (out-of-bound code?), PC = $%04X" NL, cpu_pc);
		FATAL("Hypervisor fatal error: execution address not found in list file (out-of-bound code?), PC = $%04X", cpu_pc);
		return;
	}
	// WARNING: as it turned out, using stdio I/O to log every opcodes even "only" at ~3.5MHz rate makes emulation _VERY_ slow ...
	if (unlikely(debug_on)) {
		if (debug_fp)
			fprintf(
				debug_fp,
				"HYPERVISOR-DEBUG: %-32s PC=%04X SP=%04X B=%02X A=%02X X=%02X Y=%02X Z=%02X P=%c%c%c%c%c%c%c%c IO=%d OPC=%02X @ %s" NL,
				debug_lines[cpu_pc - 0x8000][0],
				cpu_pc, cpu_sphi | cpu_sp, cpu_bphi >> 8, cpu_a, cpu_x, cpu_y, cpu_z,
				cpu_pfn ? 'N' : 'n',
				cpu_pfv ? 'V' : 'v',
				cpu_pfe ? 'E' : 'e',
				'-',
				cpu_pfd ? 'D' : 'd',
				cpu_pfi ? 'I' : 'i',
				cpu_pfz ? 'Z' : 'z',
				cpu_pfc ? 'C' : 'c',
				vic_iomode,
				cpu_read(cpu_pc),
				debug_lines[cpu_pc - 0x8000][1]
			);
	}
}
