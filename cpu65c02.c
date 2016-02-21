/* Commodore LCD emulator, C version.
 * (C)2013,2014 LGB Gabor Lenart
 * Visit my site (the better, JavaScript version of the emu is here too): http://commodore-lcd.lgb.hu/
 * Can be distributed/used/modified under the terms of GNU/GPL 3 (or later), please see file COPYING
 * or visit this page: http://www.gnu.org/licenses/gpl-3.0.html
 */

#include "nemesys.h"
#include "cpu65c02.h"

#ifdef DTV_CPU_HACK
	ubyte cpu_regs[16];
	int cpu_a_sind, cpu_a_tind, cpu_x_ind, cpu_y_ind;
#	define SP_HI (cpu_regs[11]<<8)
#	define ZP_HI (cpu_regs[10]<<8)
#	define CPU_A_INC(n) cpu_regs[cpu_a_tind] = ((ubyte)cpu_regs[cpu_a_sind] + n)
#	define CPU_A_GET() cpu_regs[cpu_a_sind]
#	define CPU_A_SET(d) cpu_regs[cpu_a_tind]=d
#	define cpu_x cpu_regs[cpu_x_ind]
#	define cpu_y cpu_regs[cpu_y_ind]
#	define CPU_TYPE "65C02+DTV"
#	warning "DTV_CPU_HACK is incorrect, still used 65C02 opcodes, only three ops are redefined: SAC, SIR, BRA"
#	define A_OP(op,dat) cpu_regs[cpu_a_tind] = cpu_regs[cpu_a_sind] op dat
#else
	ubyte cpu_a, cpu_x, cpu_y;
#	define SP_HI 0x100
#	define ZP_HI 0
#	define CPU_A_INC(n) cpu_a = ((ubyte)cpu_a + n)
#	define CPU_A_GET() cpu_a
#	define CPU_A_SET(d) cpu_a=d
#	define CPU_TYPE "65C02"
#	define A_OP(op,dat) cpu_a = cpu_a op dat
#endif


ubyte cpu_sp, cpu_op;
uword cpu_pc, cpu_old_pc;
int cpu_pfn,cpu_pfv,cpu_pfb,cpu_pfd,cpu_pfi,cpu_pfz,cpu_pfc;
int cpu_fatal = 0, cpu_irqLevel = 0, cpu_nmiEdge = 0;
int cpu_cycles;

static const ubyte opcycles[] = {7,6,2,2,5,3,5,5,3,2,2,2,6,4,6,2,2,5,5,2,5,4,6,5,2,4,2,2,6,4,7,2,6,6,2,2,3,3,5,5,4,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,2,2,4,4,7,2,6,6,2,2,3,3,5,5,3,2,2,2,3,4,6,2,2,5,5,2,4,4,6,5,2,4,3,2,2,4,7,2,6,6,2,2,3,3,5,5,4,2,2,2,5,4,6,2,2,5,5,2,4,4,6,5,2,4,4,2,6,4,7,2,3,6,2,2,3,3,3,5,2,2,2,2,4,4,4,2,2,6,5,2,4,4,4,5,2,5,2,2,4,5,5,2,2,6,2,2,3,3,3,5,2,2,2,2,4,4,4,2,2,5,5,2,4,4,4,5,2,4,2,2,4,4,4,2,2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,3,2,2,4,7,2,2,6,2,2,3,3,5,5,2,2,2,2,4,4,6,2,2,5,5,2,4,4,6,5,2,4,4,2,2,4,7,2};

static inline uword readWord(uword addr) {
	return cpu_read(addr) | (cpu_read(addr + 1) << 8);
}

#define push(data) cpu_write(((ubyte)(cpu_sp--)) | SP_HI, data)
#define pop() cpu_read(((ubyte)(++cpu_sp)) | SP_HI)

static inline void  pushWord(uword data) { push(data >> 8); push(data & 0xFF); }
static inline uword popWord() { uword temp = pop(); return temp | (pop() << 8); }


static void setP(ubyte st) {
	cpu_pfn = st & 128;
	cpu_pfv = st &  64;
	cpu_pfb = st &  16;
	cpu_pfd = st &   8;
	cpu_pfi = st &   4;
	cpu_pfz = st &   2;
	cpu_pfc = st &   1;
}

static ubyte getP() {
	return  (cpu_pfn ? 128 : 0) |
	(cpu_pfv ?  64 : 0) |
	/* begin ins block: GETPU for 65C02 */ 32 /* end ins block */ |
	(cpu_pfb ?  16 : 0) |
	(cpu_pfd ?   8 : 0) |
	(cpu_pfi ?   4 : 0) |
	(cpu_pfz ?   2 : 0) |
	(cpu_pfc ?   1 : 0);
}

void cpu_reset() {
	setP(20);
	cpu_sp = 0xFF;
	cpu_irqLevel = cpu_nmiEdge = 0;
	cpu_fatal = 0;
#ifdef DTV_CPU_HACK
	cpu_a_sind = 0;
	cpu_a_tind = 0;
	cpu_y_ind = 1;
	cpu_x_ind = 2;
	memset(cpu_regs, 0, 16);
	cpu_regs[11] = 1; // set page#1 for stack by default
#else
	CPU_A_SET(0);
	cpu_x = 0;
	cpu_y = 0;
#endif
	cpu_pc = readWord(0xFFFC);
	printf("CPU[" CPU_TYPE "]: RESET, PC=%04X\n", cpu_pc);
}


static inline void setNZ(ubyte st) {
	cpu_pfn = st & 128;
	cpu_pfz = !st;
}
#define _imm() (cpu_pc++)
static inline uword _abs() {
	uword o = cpu_read(cpu_pc++);
	return o | (cpu_read(cpu_pc++) << 8);
}
#define _absx() ((uword)(_abs() + cpu_x))
#define _absy() ((uword)(_abs() + cpu_y))
#define _absi() readWord(_abs())
#define _absxi() readWord(_absx())
#define _zp() cpu_read(cpu_pc++)
static inline uword _zpi() {
	ubyte a = cpu_read(cpu_pc++);
	return cpu_read(a) | (cpu_read((ubyte)(a + 1)) << 8);
}
#define _zpx() ((ubyte)(cpu_read(cpu_pc++) + cpu_x))
#define _zpy() ((ubyte)(cpu_read(cpu_pc++) + cpu_y))
#define _zpiy() ((uword)(_zpi() + cpu_y))
static inline uword _zpxi() {
	ubyte a = cpu_read(cpu_pc++) + cpu_x;
	return cpu_read(a) | (cpu_read((ubyte)(a + 1)) << 8);
}
static inline void _BRA(int cond) {
	 if (cond) {
		int temp = cpu_read(cpu_pc);
		if (temp & 128) temp = cpu_pc - (temp ^ 0xFF);
		else temp = cpu_pc + temp + 1;
		if ((temp & 0xFF00) != (cpu_pc & 0xFF00)) cpu_cycles++;
		cpu_pc = temp;
		cpu_cycles++;
	} else
		cpu_pc++;
}
static inline void _CMP(ubyte reg, ubyte data) {
	uword temp = reg - data;
	cpu_pfc = temp < 0x100;
	setNZ(temp);
}
static inline void _TSB(int addr) {
	ubyte m = cpu_read(addr);
	cpu_pfz = (!(m & CPU_A_GET()));
	cpu_write(addr, m | CPU_A_GET());
}
static inline void _TRB(int addr) {
	ubyte m = cpu_read(addr);
	cpu_pfz = (!(m & CPU_A_GET()));
	cpu_write(addr, m & (255 - CPU_A_GET()));
}
static inline void _ASL(int addr) {
	ubyte t = (addr == -1 ? CPU_A_GET() : cpu_read(addr));
	cpu_pfc = t & 128;
	//t = (t << 1) & 0xFF;
	t <<= 1;
	setNZ(t);
	if (addr == -1) CPU_A_SET(t); else cpu_write(addr, t);
}
static inline void _LSR(int addr) {
	ubyte t = (addr == -1 ? CPU_A_GET() : cpu_read(addr));
	cpu_pfc = t & 1;
	//t = (t >> 1) & 0xFF;
	t >>= 1;
	setNZ(t);
	if (addr == -1) CPU_A_SET(t); else cpu_write(addr, t);
}
static inline void _BIT(ubyte data) {
	cpu_pfn = data & 128;
	cpu_pfv = data & 64;
	cpu_pfz = (!(CPU_A_GET() & data));
}
static inline void _ADC(data) {
	if (cpu_pfd) {
		uword temp  = (CPU_A_GET() & 0x0F) + (data & 0x0F) + (cpu_pfc ? 1 : 0);
		uword temp2 = (CPU_A_GET() & 0xF0) + (data & 0xF0);
		if (temp > 9) { temp2 += 0x10; temp += 6; }
		cpu_pfv = (~(CPU_A_GET() ^ data) & (CPU_A_GET() ^ temp) & 0x80);
		if (temp2 > 0x90) temp2 += 0x60;
		cpu_pfc = (temp2 & 0xFF00);
		CPU_A_SET((temp & 0x0F) + (temp2 & 0xF0));
		setNZ(CPU_A_GET());
	} else {
		uword temp = data + CPU_A_GET() + (cpu_pfc ? 1 : 0);
		cpu_pfc = temp > 0xFF;
		cpu_pfv = (!((CPU_A_GET() ^ data) & 0x80) && ((CPU_A_GET() ^ temp) & 0x80));
		CPU_A_SET(temp & 0xFF);
		setNZ(CPU_A_GET());
	}
}
static inline void _SBC(data) {
	if (cpu_pfd) {
		uword temp = CPU_A_GET() - (data & 0x0F) - (cpu_pfc ? 0 : 1);
		if ((temp & 0x0F) > (CPU_A_GET() & 0x0F)) temp -= 6;
		temp -= (data & 0xF0);
		if ((temp & 0xF0) > (CPU_A_GET() & 0xF0)) temp -= 0x60;
		cpu_pfv = (!(temp > CPU_A_GET()));
		cpu_pfc = (!(temp > CPU_A_GET()));
		CPU_A_SET(temp & 0xFF);
		setNZ(CPU_A_GET());
	} else {
		uword temp = CPU_A_GET() - data - (cpu_pfc ? 0 : 1);
		cpu_pfc = temp < 0x100;
		cpu_pfv = ((CPU_A_GET() ^ temp) & 0x80) && ((CPU_A_GET() ^ data) & 0x80);
		CPU_A_SET(temp & 0xFF);
		setNZ(CPU_A_GET());
	}
}
static inline void _ROR(int addr) {
	uword t = addr == -1 ? CPU_A_GET() : cpu_read(addr);
	if (cpu_pfc) t |= 0x100;
	cpu_pfc = t & 1;
	t >>= 1;
	setNZ(t);
	if (addr == -1) CPU_A_SET(t); else cpu_write(addr, t);
}
static inline void _ROL(int addr) {
	uword t = addr == -1 ? CPU_A_GET() : cpu_read(addr);
	t = (t << 1) | (cpu_pfc ? 1 : 0);
	cpu_pfc = t & 0x100;
	t &= 0xFF;
	setNZ(t);
	if (addr == -1) CPU_A_SET(t); else cpu_write(addr, t);
}
/*	static inline void unknownOpCode() {
		cpu_fatal = true;
		alert("Unknown opcode _" + cpu_op.toString(16) + " at " + cpu_old_pc.toString(16));
	}*/




int cpu_step () {
	if (cpu_nmiEdge) {
		cpu_nmiEdge = 0;
		pushWord(cpu_pc);
		push(getP());
		cpu_pfi = 1;
		cpu_pfd = 0;
		cpu_pc = readWord(0xFFFA);
		return 7;
	}
	if (cpu_irqLevel && (!cpu_pfi)) {
		pushWord(cpu_pc);
		cpu_pfb = 0;
		push(getP());
		cpu_pfi = 1;
		cpu_pfd = 0;
		cpu_pc = readWord(0xFFFE);
		return 7;
	}
	cpu_old_pc = cpu_pc;
	cpu_op = cpu_read(cpu_pc++);
	cpu_cycles = opcycles[cpu_op];
	switch (cpu_op) {
	case 0x00:  pushWord(cpu_pc + 1); cpu_pfb = 1 ; push(getP()) ; cpu_pfd = 0 ; cpu_pfi = 1 ; cpu_pc = readWord(0xFFFE) ; break; /* 0x0 BRK Implied */
	//case 0x01:  setNZ(A_OP(|,cpu_read(_zpxi())) ; break; /* 0x1 ORA (Zero_Page,X) */
	case 0x01:  setNZ(A_OP(|,cpu_read(_zpxi()))); break; /* 0x1 ORA (Zero_Page,X) */
	case 0x02:  cpu_pc++; break; /* 0x2 NOP imm (non-std NOP with addr mode) */
	case 0x03:  break; /* 0x3 NOP (nonstd loc, implied) */
	case 0x04:  _TSB(_zp()) ; break; /* 0x4 TSB Zero_Page */
	case 0x05:  setNZ(A_OP(|,cpu_read(_zp()))) ; break; /* 0x5 ORA Zero_Page */
	case 0x06:  _ASL(_zp()) ; break; /* 0x6 ASL Zero_Page */
	case 0x07:  { int a = _zp() ; cpu_write(a, cpu_read(a) & 254);  } break; /* 0x7 RMB Zero_Page */
	case 0x08:  push(getP() | 0x10) ; break; /* 0x8 PHP Implied */
	case 0x09:  setNZ(A_OP(|,cpu_read(_imm()))) ; break; /* 0x9 ORA Immediate */
	case 0x0a:  _ASL(-1) ; break; /* 0xa ASL Accumulator */
	case 0x0b:   break; /* 0xb NOP (nonstd loc, implied) */
	case 0x0c:  _TSB(_abs()) ; break; /* 0xc TSB Absolute */
	case 0x0d:  setNZ(A_OP(|,cpu_read(_abs()))) ; break; /* 0xd ORA Absolute */
	case 0x0e:  _ASL(_abs()) ; break; /* 0xe ASL Absolute */
	case 0x0f:  _BRA(!(cpu_read(_zp()) & 1)) ; break; /* 0xf BBR Relative */
	case 0x10:  _BRA(!cpu_pfn) ; break; /* 0x10 BPL Relative */
	case 0x11:  setNZ(A_OP(|,cpu_read(_zpiy()))) ; break; /* 0x11 ORA (Zero_Page),Y */
#ifdef DTV_CPU_HACK
	case 0x12:   _BRA(1) ; break; /* 0x12: DTV specific BRA */
#else
	case 0x12:   setNZ(A_OP(|,cpu_read(_zpi()))) ; break; /* 0x12 ORA (Zero_Page) */
#endif
	case 0x13:   break; /* 0x13 NOP (nonstd loc, implied) */
	case 0x14:   _TRB(_zp()) ; break; /* 0x14 TRB Zero_Page */
	case 0x15:   setNZ(A_OP(|,cpu_read(_zpx()))) ; break; /* 0x15 ORA Zero_Page,X */
	case 0x16:   _ASL(_zpx()) ; break; /* 0x16 ASL Zero_Page,X */
	case 0x17:   { int a = _zp() ; cpu_write(a, cpu_read(a) & 253); } break; /* 0x17 RMB Zero_Page */
	case 0x18:   cpu_pfc = 0 ; break; /* 0x18 CLC Implied */
	case 0x19:   setNZ(A_OP(|,cpu_read(_absy()))) ; break; /* 0x19 ORA Absolute,Y */
	case 0x1a:   setNZ(CPU_A_INC(1)); break; /* 0x1a INA Accumulator */
	case 0x1b:   break; /* 0x1b NOP (nonstd loc, implied) */
	case 0x1c:   _TRB(_abs()) ; break; /* 0x1c TRB Absolute */
	case 0x1d:   setNZ(A_OP(|,cpu_read(_absx()))) ; break; /* 0x1d ORA Absolute,X */
	case 0x1e:   _ASL(_absx()) ; break; /* 0x1e ASL Absolute,X */
	case 0x1f:   _BRA(!(cpu_read(_zp()) & 2)) ; break; /* 0x1f BBR Relative */
	case 0x20:   pushWord(cpu_pc + 1) ; cpu_pc = _abs() ; break; /* 0x20 JSR Absolute */
	case 0x21:   setNZ(A_OP(&,cpu_read(_zpxi()))) ; break; /* 0x21 AND (Zero_Page,X) */
	case 0x22:   cpu_pc++; break; /* 0x22 NOP imm (non-std NOP with addr mode) */
	case 0x23:   break; /* 0x23 NOP (nonstd loc, implied) */
	case 0x24:   _BIT(cpu_read(_zp())) ; break; /* 0x24 BIT Zero_Page */
	case 0x25:   setNZ(A_OP(&,cpu_read(_zp()))) ; break; /* 0x25 AND Zero_Page */
	case 0x26:   _ROL(_zp()) ; break; /* 0x26 ROL Zero_Page */
	case 0x27:   { int a = _zp() ; cpu_write(a, cpu_read(a) & 251); } break; /* 0x27 RMB Zero_Page */
	case 0x28:   setP(pop() | 0x10) ; break; /* 0x28 PLP Implied */
	case 0x29:   setNZ(A_OP(&,cpu_read(_imm()))) ; break; /* 0x29 AND Immediate */
	case 0x2a:   _ROL(-1) ; break; /* 0x2a ROL Accumulator */
	case 0x2b:   break; /* 0x2b NOP (nonstd loc, implied) */
	case 0x2c:   _BIT(cpu_read(_abs())) ; break; /* 0x2c BIT Absolute */
	case 0x2d:   setNZ(A_OP(&,cpu_read(_abs()))) ; break; /* 0x2d AND Absolute */
	case 0x2e:   _ROL(_abs()) ; break; /* 0x2e ROL Absolute */
	case 0x2f:   _BRA(!(cpu_read(_zp()) & 4)) ; break; /* 0x2f BBR Relative */
	case 0x30:   _BRA(cpu_pfn) ; break; /* 0x30 BMI Relative */
	case 0x31:   setNZ(A_OP(&,cpu_read(_zpiy()))) ; break; /* 0x31 AND (Zero_Page),Y */
#ifdef DTV_CPU_HACK
	case 0x32:   cpu_a_sind = cpu_a_tind = cpu_read(cpu_pc++); cpu_a_sind &= 15; cpu_a_tind >>= 4; break; /* 0x32: DTV specific: SAC */
#else
	case 0x32:   setNZ(A_OP(&,cpu_read(_zpi()))) ; break; /* 0x32 AND (Zero_Page) */
#endif
	case 0x33:   break; /* 0x33 NOP (nonstd loc, implied) */
	case 0x34:   _BIT(cpu_read(_zpx())) ; break; /* 0x34 BIT Zero_Page,X */
	case 0x35:   setNZ(A_OP(&,cpu_read(_zpx()))) ; break; /* 0x35 AND Zero_Page,X */
	case 0x36:   _ROL(_zpx()) ; break; /* 0x36 ROL Zero_Page,X */
	case 0x37:   { int a = _zp() ; cpu_write(a, cpu_read(a) & 247); } break; /* 0x37 RMB Zero_Page */
	case 0x38:   cpu_pfc = 1 ; break; /* 0x38 SEC Implied */
	case 0x39:   setNZ(A_OP(&,cpu_read(_absy()))) ; break; /* 0x39 AND Absolute,Y */
	case 0x3a:   setNZ(CPU_A_INC(-1)); break; /* 0x3a DEA Accumulator */
	case 0x3b:   break; /* 0x3b NOP (nonstd loc, implied) */
	case 0x3c:   _BIT(cpu_read(_absx())) ; break; /* 0x3c BIT Absolute,X */
	case 0x3d:   setNZ(A_OP(&,cpu_read(_absx()))) ; break; /* 0x3d AND Absolute,X */
	case 0x3e:   _ROL(_absx()) ; break; /* 0x3e ROL Absolute,X */
	case 0x3f:   _BRA(!(cpu_read(_zp()) & 8)) ; break; /* 0x3f BBR Relative */
	case 0x40:   setP(pop() | 0x10) ; cpu_pc = popWord() ; break; /* 0x40 RTI Implied */
	case 0x41:   setNZ(A_OP(^,cpu_read(_zpxi()))) ; break; /* 0x41 EOR (Zero_Page,X) */
#ifdef DTV_CPU_HACK
	case 0x42:   cpu_x_ind = cpu_y_ind = cpu_read(cpu_pc++); cpu_x_ind &= 15; cpu_y_ind >>= 4; break; /* 0x42: DTV specific: SIR */
#else
	case 0x42:   cpu_pc++ ; break; /* 0x42 NOP imm (non-std NOP with addr mode) */
#endif
	case 0x43:   break; /* 0x43 NOP (nonstd loc, implied) */
	case 0x44:   cpu_pc++ ; break; /* 0x44 NOP zp (non-std NOP with addr mode) */
	case 0x45:   setNZ(A_OP(^,cpu_read(_zp()))) ; break; /* 0x45 EOR Zero_Page */
	case 0x46:   _LSR(_zp()) ; break; /* 0x46 LSR Zero_Page */
	case 0x47:   { int a = _zp() ; cpu_write(a, cpu_read(a) & 239); } break; /* 0x47 RMB Zero_Page */
	case 0x48:   push(CPU_A_GET()) ; break; /* 0x48 PHA Implied */
	case 0x49:   setNZ(A_OP(^,cpu_read(_imm()))) ; break; /* 0x49 EOR Immediate */
	case 0x4a:   _LSR(-1) ; break; /* 0x4a LSR Accumulator */
	case 0x4b:   break; /* 0x4b NOP (nonstd loc, implied) */
	case 0x4c:   cpu_pc = _abs() ; break; /* 0x4c JMP Absolute */
	case 0x4d:   setNZ(A_OP(^,cpu_read(_abs()))) ; break; /* 0x4d EOR Absolute */
	case 0x4e:   _LSR(_abs()) ; break; /* 0x4e LSR Absolute */
	case 0x4f:   _BRA(!(cpu_read(_zp()) & 16)) ; break; /* 0x4f BBR Relative */
	case 0x50:   _BRA(!cpu_pfv) ; break; /* 0x50 BVC Relative */
	case 0x51:   setNZ(A_OP(^,cpu_read(_zpiy()))) ; break; /* 0x51 EOR (Zero_Page),Y */
	case 0x52:   setNZ(A_OP(^,cpu_read(_zpi()))) ; break; /* 0x52 EOR (Zero_Page) */
	case 0x53:   break; /* 0x53 NOP (nonstd loc, implied) */
	case 0x54:   cpu_pc++ ; break; /* 0x54 NOP zpx (non-std NOP with addr mode) */
	case 0x55:   setNZ(A_OP(^,cpu_read(_zpx()))) ; break; /* 0x55 EOR Zero_Page,X */
	case 0x56:   _LSR(_zpx()) ; break; /* 0x56 LSR Zero_Page,X */
	case 0x57:   { int a = _zp() ; cpu_write(a, cpu_read(a) & 223); } break; /* 0x57 RMB Zero_Page */
	case 0x58:   cpu_pfi = 0 ; break; /* 0x58 CLI Implied */
	case 0x59:   setNZ(A_OP(^,cpu_read(_absy()))) ; break; /* 0x59 EOR Absolute,Y */
	case 0x5a:   push(cpu_y) ; break; /* 0x5a PHY Implied */
	case 0x5b:   break; /* 0x5b NOP (nonstd loc, implied) */
	case 0x5c:   break; /* 0x5c NOP (nonstd loc, implied) */
	case 0x5d:   setNZ(A_OP(^,cpu_read(_absx()))) ; break; /* 0x5d EOR Absolute,X */
	case 0x5e:   _LSR(_absx()) ; break; /* 0x5e LSR Absolute,X */
	case 0x5f:   _BRA(!(cpu_read(_zp()) & 32)) ; break; /* 0x5f BBR Relative */
	case 0x60:   cpu_pc = popWord() + 1; break; /* 0x60 RTS Implied */
	case 0x61:   _ADC(cpu_read(_zpxi())) ; break; /* 0x61 ADC (Zero_Page,X) */
	case 0x62:   cpu_pc++ ; break; /* 0x62 NOP imm (non-std NOP with addr mode) */
	case 0x63:   break; /* 0x63 NOP (nonstd loc, implied) */
	case 0x64:   cpu_write(_zp(), 0) ; break; /* 0x64 STZ Zero_Page */
	case 0x65:   _ADC(cpu_read(_zp())) ; break; /* 0x65 ADC Zero_Page */
	case 0x66:   _ROR(_zp()) ; break; /* 0x66 ROR Zero_Page */
	case 0x67:   { int a = _zp() ; cpu_write(a, cpu_read(a) & 191); } break; /* 0x67 RMB Zero_Page */
	case 0x68:   setNZ(CPU_A_SET(pop())) ; break; /* 0x68 PLA Implied */
	case 0x69:   _ADC(cpu_read(_imm())) ; break; /* 0x69 ADC Immediate */
	case 0x6a:   _ROR(-1) ; break; /* 0x6a ROR Accumulator */
	case 0x6b:   break; /* 0x6b NOP (nonstd loc, implied) */
	case 0x6c:   cpu_pc = _absi() ; break; /* 0x6c JMP (Absolute) */
	case 0x6d:   _ADC(cpu_read(_abs())) ; break; /* 0x6d ADC Absolute */
	case 0x6e:   _ROR(_abs()) ; break; /* 0x6e ROR Absolute */
	case 0x6f:   _BRA(!(cpu_read(_zp()) & 64)) ; break; /* 0x6f BBR Relative */
	case 0x70:   _BRA(cpu_pfv) ; break; /* 0x70 BVS Relative */
	case 0x71:   _ADC(cpu_read(_zpiy())) ; break; /* 0x71 ADC (Zero_Page),Y */
	case 0x72:   _ADC(cpu_read(_zpi())) ; break; /* 0x72 ADC (Zero_Page) */
	case 0x73:   break; /* 0x73 NOP (nonstd loc, implied) */
	case 0x74:   cpu_write(_zpx(), 0) ; break; /* 0x74 STZ Zero_Page,X */
	case 0x75:   _ADC(cpu_read(_zpx())) ; break; /* 0x75 ADC Zero_Page,X */
	case 0x76:   _ROR(_zpx()) ; break; /* 0x76 ROR Zero_Page,X */
	case 0x77:   { int a = _zp() ; cpu_write(a, cpu_read(a) & 127); } break; /* 0x77 RMB Zero_Page */
	case 0x78:   cpu_pfi = 1 ; break; /* 0x78 SEI Implied */
	case 0x79:   _ADC(cpu_read(_absy())) ; break; /* 0x79 ADC Absolute,Y */
	case 0x7a:   setNZ(cpu_y = pop()) ; break; /* 0x7a PLY Implied */
	case 0x7b:   break; /* 0x7b NOP (nonstd loc, implied) */
	case 0x7c:   cpu_pc = _absxi() ; break; /* 0x7c JMP (Absolute,X) */
	case 0x7d:   _ADC(cpu_read(_absx())) ; break; /* 0x7d ADC Absolute,X */
	case 0x7e:   _ROR(_absx()) ; break; /* 0x7e ROR Absolute,X */
	case 0x7f:   _BRA(!(cpu_read(_zp()) & 128)) ; break; /* 0x7f BBR Relative */
	case 0x80:   _BRA(1) ; break; /* 0x80 BRA Relative */
	case 0x81:   cpu_write(_zpxi(), CPU_A_GET()) ; break; /* 0x81 STA (Zero_Page,X) */
	case 0x82:   cpu_pc++ ; break; /* 0x82 NOP imm (non-std NOP with addr mode) */
	case 0x83:   break; /* 0x83 NOP (nonstd loc, implied) */
	case 0x84:   cpu_write(_zp(), cpu_y) ; break; /* 0x84 STY Zero_Page */
	case 0x85:   cpu_write(_zp(), CPU_A_GET()) ; break; /* 0x85 STA Zero_Page */
	case 0x86:   cpu_write(_zp(), cpu_x) ; break; /* 0x86 STX Zero_Page */
	case 0x87:   { int a = _zp() ; cpu_write(a, cpu_read(a) | 1); } break; /* 0x87 SMB Zero_Page */
	case 0x88:   setNZ(--cpu_y); break; /* 0x88 DEY Implied */
	case 0x89:   cpu_pfz = (!(CPU_A_GET() & cpu_read(_imm()))) ; break; /* 0x89 BIT+ Immediate */
	case 0x8a:   setNZ(CPU_A_SET(cpu_x)) ; break; /* 0x8a TXA Implied */
	case 0x8b:   break; /* 0x8b NOP (nonstd loc, implied) */
	case 0x8c:   cpu_write(_abs(), cpu_y) ; break; /* 0x8c STY Absolute */
	case 0x8d:   cpu_write(_abs(), CPU_A_GET()) ; break; /* 0x8d STA Absolute */
	case 0x8e:   cpu_write(_abs(), cpu_x) ; break; /* 0x8e STX Absolute */
	case 0x8f:   _BRA( cpu_read(_zp()) & 1 ) ; break; /* 0x8f BBS Relative */
	case 0x90:   _BRA(!cpu_pfc) ; break; /* 0x90 BCC Relative */
	case 0x91:   cpu_write(_zpiy(), CPU_A_GET()) ; break; /* 0x91 STA (Zero_Page),Y */
	case 0x92:   cpu_write(_zpi(), CPU_A_GET()) ; break; /* 0x92 STA (Zero_Page) */
	case 0x93:   break; /* 0x93 NOP (nonstd loc, implied) */
	case 0x94:   cpu_write(_zpx(), cpu_y) ; break; /* 0x94 STY Zero_Page,X */
	case 0x95:   cpu_write(_zpx(), CPU_A_GET()) ; break; /* 0x95 STA Zero_Page,X */
	case 0x96:   cpu_write(_zpy(), cpu_x) ; break; /* 0x96 STX Zero_Page,Y */
	case 0x97:   { int a = _zp() ; cpu_write(a, cpu_read(a) | 2); } break; /* 0x97 SMB Zero_Page */
	case 0x98:   setNZ(CPU_A_SET(cpu_y)) ; break; /* 0x98 TYA Implied */
	case 0x99:   cpu_write(_absy(), CPU_A_GET()) ; break; /* 0x99 STA Absolute,Y */
	case 0x9a:   cpu_sp = cpu_x ; break; /* 0x9a TXS Implied */
	case 0x9b:   break; /* 0x9b NOP (nonstd loc, implied) */
	case 0x9c:   cpu_write(_abs(), 0) ; break; /* 0x9c STZ Absolute */
	case 0x9d:   cpu_write(_absx(), CPU_A_GET()) ; break; /* 0x9d STA Absolute,X */
	case 0x9e:   cpu_write(_absx(), 0) ; break; /* 0x9e STZ Absolute,X */
	case 0x9f:   _BRA( cpu_read(_zp()) & 2 ) ; break; /* 0x9f BBS Relative */
	case 0xa0:   setNZ(cpu_y = cpu_read(_imm())) ; break; /* 0xa0 LDY Immediate */
	case 0xa1:   setNZ(CPU_A_SET(cpu_read(_zpxi()))) ; break; /* 0xa1 LDA (Zero_Page,X) */
	case 0xa2:   setNZ(cpu_x = cpu_read(_imm())) ; break; /* 0xa2 LDX Immediate */
	case 0xa3:   break; /* 0xa3 NOP (nonstd loc, implied) */
	case 0xa4:   setNZ(cpu_y = cpu_read(_zp())) ; break; /* 0xa4 LDY Zero_Page */
	case 0xa5:   setNZ(CPU_A_SET(cpu_read(_zp()))) ; break; /* 0xa5 LDA Zero_Page */
	case 0xa6:   setNZ(cpu_x = cpu_read(_zp())) ; break; /* 0xa6 LDX Zero_Page */
	case 0xa7:   { int a = _zp() ; cpu_write(a, cpu_read(a) | 4); } break; /* 0xa7 SMB Zero_Page */
	case 0xa8:   setNZ(cpu_y = CPU_A_GET()) ; break; /* 0xa8 TAY Implied */
	case 0xa9:   setNZ(CPU_A_SET(cpu_read(_imm()))) ; break; /* 0xa9 LDA Immediate */
	case 0xaa:   setNZ(cpu_x = CPU_A_GET()) ; break; /* 0xaa TAX Implied */
	case 0xab:   break; /* 0xab NOP (nonstd loc, implied) */
	case 0xac:   setNZ(cpu_y = cpu_read(_abs())) ; break; /* 0xac LDY Absolute */
	case 0xad:   setNZ(CPU_A_SET(cpu_read(_abs()))) ; break; /* 0xad LDA Absolute */
	case 0xae:   setNZ(cpu_x = cpu_read(_abs())) ; break; /* 0xae LDX Absolute */
	case 0xaf:   _BRA( cpu_read(_zp()) & 4 ) ; break; /* 0xaf BBS Relative */
	case 0xb0:   _BRA(cpu_pfc) ; break; /* 0xb0 BCS Relative */
	case 0xb1:   setNZ(CPU_A_SET(cpu_read(_zpiy()))) ; break; /* 0xb1 LDA (Zero_Page),Y */
	case 0xb2:   setNZ(CPU_A_SET(cpu_read(_zpi()))) ; break; /* 0xb2 LDA (Zero_Page) */
	case 0xb3:   break; /* 0xb3 NOP (nonstd loc, implied) */
	case 0xb4:   setNZ(cpu_y = cpu_read(_zpx())) ; break; /* 0xb4 LDY Zero_Page,X */
	case 0xb5:   setNZ(CPU_A_SET(cpu_read(_zpx()))) ; break; /* 0xb5 LDA Zero_Page,X */
	case 0xb6:   setNZ(cpu_x = cpu_read(_zpy())) ; break; /* 0xb6 LDX Zero_Page,Y */
	case 0xb7:   { int a = _zp() ; cpu_write(a, cpu_read(a) | 8); } break; /* 0xb7 SMB Zero_Page */
	case 0xb8:   cpu_pfv = 0 ; break; /* 0xb8 CLV Implied */
	case 0xb9:   setNZ(CPU_A_SET(cpu_read(_absy()))) ; break; /* 0xb9 LDA Absolute,Y */
	case 0xba:   setNZ(cpu_x = cpu_sp) ; break; /* 0xba TSX Implied */
	case 0xbb:   break; /* 0xbb NOP (nonstd loc, implied) */
	case 0xbc:   setNZ(cpu_y = cpu_read(_absx())) ; break; /* 0xbc LDY Absolute,X */
	case 0xbd:   setNZ(CPU_A_SET(cpu_read(_absx()))) ; break; /* 0xbd LDA Absolute,X */
	case 0xbe:   setNZ(cpu_x = cpu_read(_absy())) ; break; /* 0xbe LDX Absolute,Y */
	case 0xbf:   _BRA( cpu_read(_zp()) & 8 ) ; break; /* 0xbf BBS Relative */
	case 0xc0:   _CMP(cpu_y, cpu_read(_imm())) ; break; /* 0xc0 CPY Immediate */
	case 0xc1:   _CMP(CPU_A_GET(), cpu_read(_zpxi())) ; break; /* 0xc1 CMP (Zero_Page,X) */
	case 0xc2:   cpu_pc++ ; break; /* 0xc2 NOP imm (non-std NOP with addr mode) */
	case 0xc3:   break; /* 0xc3 NOP (nonstd loc, implied) */
	case 0xc4:   _CMP(cpu_y, cpu_read(_zp())) ; break; /* 0xc4 CPY Zero_Page */
	case 0xc5:   _CMP(CPU_A_GET(), cpu_read(_zp())) ; break; /* 0xc5 CMP Zero_Page */
	case 0xc6:   { int addr = _zp() ; ubyte data = cpu_read(addr) - 1; setNZ(data) ; cpu_write(addr, data); } break; /* 0xc6 DEC Zero_Page */
	case 0xc7:   { int a = _zp() ; cpu_write(a, cpu_read(a) | 16); } break; /* 0xc7 SMB Zero_Page */
	case 0xc8:   setNZ(++cpu_y); break; /* 0xc8 INY Implied */
	case 0xc9:   _CMP(CPU_A_GET(), cpu_read(_imm())) ; break; /* 0xc9 CMP Immediate */
	case 0xca:   setNZ(--cpu_x); break; /* 0xca DEX Implied */
	case 0xcb:   break; /* 0xcb NOP (nonstd loc, implied) */
	case 0xcc:   _CMP(cpu_y, cpu_read(_abs())) ; break; /* 0xcc CPY Absolute */
	case 0xcd:   _CMP(CPU_A_GET(), cpu_read(_abs())) ; break; /* 0xcd CMP Absolute */
	case 0xce:   { int addr = _abs() ; ubyte data = cpu_read(addr) - 1; setNZ(data) ; cpu_write(addr, data); } break; /* 0xce DEC Absolute */
	case 0xcf:   _BRA( cpu_read(_zp()) & 16 ) ; break; /* 0xcf BBS Relative */
	case 0xd0:   _BRA(!cpu_pfz) ; break; /* 0xd0 BNE Relative */
	case 0xd1:   _CMP(CPU_A_GET(), cpu_read(_zpiy())) ; break; /* 0xd1 CMP (Zero_Page),Y */
	case 0xd2:   _CMP(CPU_A_GET(), cpu_read(_zpi())) ; break; /* 0xd2 CMP (Zero_Page) */
	case 0xd3:   break; /* 0xd3 NOP (nonstd loc, implied) */
	case 0xd4:   cpu_pc++ ; break; /* 0xd4 NOP zpx (non-std NOP with addr mode) */
	case 0xd5:   _CMP(CPU_A_GET(), cpu_read(_zpx())) ; break; /* 0xd5 CMP Zero_Page,X */
	case 0xd6:   { int addr = _zpx() ; ubyte data = cpu_read(addr) - 1; setNZ(data) ; cpu_write(addr, data); } break; /* 0xd6 DEC Zero_Page,X */
	case 0xd7:   { int a = _zp() ; cpu_write(a, cpu_read(a) | 32); } break; /* 0xd7 SMB Zero_Page */
	case 0xd8:   cpu_pfd = 0 ; break; /* 0xd8 CLD Implied */
	case 0xd9:   _CMP(CPU_A_GET(), cpu_read(_absy())) ; break; /* 0xd9 CMP Absolute,Y */
	case 0xda:   push(cpu_x) ; break; /* 0xda PHX Implied */
	case 0xdb:   break; /* 0xdb NOP (nonstd loc, implied) */
	case 0xdc:   break; /* 0xdc NOP (nonstd loc, implied) */
	case 0xdd:   _CMP(CPU_A_GET(), cpu_read(_absx())) ; break; /* 0xdd CMP Absolute,X */
	case 0xde:   { int addr = _absx() ; ubyte data = cpu_read(addr) - 1; setNZ(data) ; cpu_write(addr, data); } break; /* 0xde DEC Absolute,X */
	case 0xdf:   _BRA( cpu_read(_zp()) & 32 ) ; break; /* 0xdf BBS Relative */
	case 0xe0:   _CMP(cpu_x, cpu_read(_imm())) ; break; /* 0xe0 CPX Immediate */
	case 0xe1:   _SBC(cpu_read(_zpxi())) ; break; /* 0xe1 SBC (Zero_Page,X) */
	case 0xe2:   cpu_pc++ ; break; /* 0xe2 NOP imm (non-std NOP with addr mode) */
	case 0xe3:   break; /* 0xe3 NOP (nonstd loc, implied) */
	case 0xe4:   _CMP(cpu_x, cpu_read(_zp())) ; break; /* 0xe4 CPX Zero_Page */
	case 0xe5:   _SBC(cpu_read(_zp())) ; break; /* 0xe5 SBC Zero_Page */
	case 0xe6:   { int addr = _zp() ; ubyte data = cpu_read(addr) + 1; setNZ(data) ; cpu_write(addr, data); } break; /* 0xe6 INC Zero_Page */
	case 0xe7:   { int a = _zp() ; cpu_write(a, cpu_read(a) | 64); } break; /* 0xe7 SMB Zero_Page */
	case 0xe8:   setNZ(++cpu_x); break; /* 0xe8 INX Implied */
	case 0xe9:   _SBC(cpu_read(_imm())) ; break; /* 0xe9 SBC Immediate */
	case 0xea:   break; /* 0xea NOP Implied */
	case 0xeb:   break; /* 0xeb NOP (nonstd loc, implied) */
	case 0xec:   _CMP(cpu_x, cpu_read(_abs())) ; break; /* 0xec CPX Absolute */
	case 0xed:   _SBC(cpu_read(_abs())) ; break; /* 0xed SBC Absolute */
	case 0xee:   { int addr = _abs(); ubyte data = cpu_read(addr) + 1; setNZ(data) ; cpu_write(addr, data); } break; /* 0xee INC Absolute */
	case 0xef:   _BRA( cpu_read(_zp()) & 64 ) ; break; /* 0xef BBS Relative */
	case 0xf0:   _BRA(cpu_pfz) ; break; /* 0xf0 BEQ Relative */
	case 0xf1:   _SBC(cpu_read(_zpiy())) ; break; /* 0xf1 SBC (Zero_Page),Y */
	case 0xf2:   _SBC(cpu_read(_zpi())) ; break; /* 0xf2 SBC (Zero_Page) */
	case 0xf3:   break; /* 0xf3 NOP (nonstd loc, implied) */
	case 0xf4:   cpu_pc++ ; break; /* 0xf4 NOP zpx (non-std NOP with addr mode) */
	case 0xf5:   _SBC(cpu_read(_zpx())) ; break; /* 0xf5 SBC Zero_Page,X */
	case 0xf6:   { int addr = _zpx() ; ubyte data = cpu_read(addr) + 1; setNZ(data) ; cpu_write(addr, data); } break; /* 0xf6 INC Zero_Page,X */
	case 0xf7:   { int a = _zp() ; cpu_write(a, cpu_read(a) | 128); } break; /* 0xf7 SMB Zero_Page */
	case 0xf8:   cpu_pfd = 1 ; break; /* 0xf8 SED Implied */
	case 0xf9:   _SBC(cpu_read(_absy())) ; break; /* 0xf9 SBC Absolute,Y */
	case 0xfa:   setNZ(cpu_x = pop()) ; break; /* 0xfa PLX Implied */
	case 0xfb:   break; /* 0xfb NOP (nonstd loc, implied) */
	case 0xfc:   break; /* 0xfc NOP (nonstd loc, implied) */
	case 0xfd:   _SBC(cpu_read(_absx())) ; break; /* 0xfd SBC Absolute,X */
	case 0xfe:   { int addr = _absx() ; ubyte data = cpu_read(addr) + 1; setNZ(data) ; cpu_write(addr, data); } break; /* 0xfe INC Absolute,X */
	case 0xff:   _BRA( cpu_read(_zp()) & 128 ) ; break; /* 0xff BBS Relative */
	}
	return cpu_cycles;
}

