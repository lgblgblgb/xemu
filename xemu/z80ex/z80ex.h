/*
 * Z80Ex, ZILoG Z80 CPU emulator.
 *
 * by Pigmaker57 aka boo_boo [pigmaker57@kahoh57.info]
 * modified by Gabor Lenart LGB [lgblgblgb@gmail.com] for Xep128
 *
 * contains some code from the FUSE project (http://fuse-emulator.sourceforge.net)
 * Released under GNU GPL v2
 *
 */

#ifndef _Z80EX_H_INCLUDED
#define _Z80EX_H_INCLUDED

#ifdef Z80EX_USER_HEADER
#include Z80EX_USER_HEADER
#endif

#ifdef Z80EX_TSTATE_CALLBACK_ALWAYS
#	define IS_TSTATE_CB 1
#	define Z80EX_TSTATE_CALLBACK
#else
#	define IS_TSTATE_CB z80ex.tstate_cb
#	ifdef Z80EX_TSTATE_CALLBACK
#		define Z80EX_HAVE_TSTATE_CB_VAR
#	endif
#endif

#ifndef Z80EX_TYPES_DEFINED
#define Z80EX_TYPES_DEFINED
#warning "User did not define Z80EX types ..."
#if defined(__SYMBIAN32__)
typedef unsigned char Z80EX_BYTE;
typedef signed char Z80EX_SIGNED_BYTE;
typedef unsigned short Z80EX_WORD;
typedef unsigned int Z80EX_DWORD;
#elif defined(__GNUC__)
#include <stdint.h>
typedef uint8_t Z80EX_BYTE;
typedef int8_t Z80EX_SIGNED_BYTE;
typedef uint16_t Z80EX_WORD;
typedef uint32_t Z80EX_DWORD;
#elif defined(_MSC_VER)
typedef unsigned __int8 Z80EX_BYTE;
typedef signed __int8 Z80EX_SIGNED_BYTE;
typedef unsigned __int16 Z80EX_WORD;
typedef unsigned __int32 Z80EX_DWORD;
#else
typedef unsigned char Z80EX_BYTE;
typedef signed char Z80EX_SIGNED_BYTE;
typedef unsigned short Z80EX_WORD;
typedef unsigned int Z80EX_DWORD;
#endif
#endif

/* Union allowing a register pair to be accessed as bytes or as a word */
typedef union {
#ifdef Z80EX_WORDS_BIG_ENDIAN
	struct { Z80EX_BYTE h,l; } b;
#warning "Z80Ex big endian support is not tested with the modified Z80Ex by me!"
#else
	struct { Z80EX_BYTE l,h; } b;
#endif
	Z80EX_WORD w;
} Z80EX_REGPAIR_T;

typedef enum { IM0 = 0, IM1 = 1, IM2 = 2 } IM_MODE;

/* Macros used for accessing the registers
   This is the "external" set for the application.
   Internally, z80ex uses its own ones from macros.h
   but it would collide with some other stuff because
   of being too generic names ...  */

#define Z80_A   z80ex.af.b.h
#define Z80_F   z80ex.af.b.l
#define Z80_AF  z80ex.af.w

#define Z80_B   z80ex.bc.b.h
#define Z80_C   z80ex.bc.b.l
#define Z80_BC  z80ex.bc.w

#define Z80_D   z80ex.de.b.h
#define Z80_E   z80ex.de.b.l
#define Z80_DE  z80ex.de.w

#define Z80_H   z80ex.hl.b.h
#define Z80_L   z80ex.hl.b.l
#define Z80_HL  z80ex.hl.w

#define Z80_A_  z80ex.af_.b.h
#define Z80_F_  z80ex.af_.b.l
#define Z80_AF_ z80ex.af_.w

#define Z80_B_  z80ex.bc_.b.h
#define Z80_C_  z80ex.bc_.b.l
#define Z80_BC_ z80ex.bc_.w

#define Z80_D_  z80ex.de_.b.h
#define Z80_E_  z80ex.de_.b.l
#define Z80_DE_ z80ex.de_.w

#define Z80_H_  z80ex.hl_.b.h
#define Z80_L_  z80ex.hl_.b.l
#define Z80_HL_ z80ex.hl_.w

#define Z80_IXH z80ex.ix.b.h
#define Z80_IXL z80ex.ix.b.l
#define Z80_IX  z80ex.ix.w

#define Z80_IYH z80ex.iy.b.h
#define Z80_IYL z80ex.iy.b.l
#define Z80_IY  z80ex.iy.w

#define Z80_SPH z80ex.sp.b.h
#define Z80_SPL z80ex.sp.b.l
#define Z80_SP  z80ex.sp.w

#define Z80_PCH z80ex.pc.b.h
#define Z80_PCL z80ex.pc.b.l
#define Z80_PC  z80ex.pc.w

#define Z80_I  z80ex.i
#define Z80_R  z80ex.r
#define Z80_R7 z80ex.r7

#define Z80_IFF1 z80ex.iff1
#define Z80_IFF2 z80ex.iff2
#define Z80_IM   z80ex.im

/* The flags */

#define Z80_FLAG_C	0x01
#define Z80_FLAG_N	0x02
#define Z80_FLAG_P	0x04
#define Z80_FLAG_V	Z80_FLAG_P
#define Z80_FLAG_3	0x08
#define Z80_FLAG_H	0x10
#define Z80_FLAG_5	0x20
#define Z80_FLAG_Z	0x40
#define Z80_FLAG_S	0x80

struct _z80_cpu_context {
	Z80EX_REGPAIR_T af,bc,de,hl;
	Z80EX_REGPAIR_T af_,bc_,de_,hl_;
	Z80EX_REGPAIR_T ix,iy;
	Z80EX_BYTE i;
	Z80EX_WORD r;
	Z80EX_BYTE r7; /* The high bit of the R register */
	Z80EX_REGPAIR_T sp,pc;
	Z80EX_BYTE iff1, iff2; /*interrupt flip-flops*/
	Z80EX_REGPAIR_T memptr; /*undocumented internal register*/
	IM_MODE im;
	int halted;

	unsigned long tstate; /*t-state clock of current/last step*/
	unsigned char op_tstate; /*clean (without WAITs and such) t-state of currently executing instruction*/

	int noint_once; /*disable interrupts before next opcode?*/
	int reset_PV_on_int; /*reset P/V flag on interrupt? (for LD A,R / LD A,I)*/
	int doing_opcode; /*is there an opcode currently executing?*/
	char int_vector_req; /*opcode must be fetched from IO device? (int vector read)*/
	Z80EX_BYTE prefix;

#ifdef Z80EX_HAVE_TSTATE_CB_VAR
	int tstate_cb;  /* use tstate callback? */
#endif

	/*other stuff*/
	Z80EX_REGPAIR_T tmpword;
	Z80EX_REGPAIR_T tmpaddr;
	Z80EX_BYTE tmpbyte;
	Z80EX_SIGNED_BYTE tmpbyte_s;

	int nmos; /* NMOS Z80 mode if '1', CMOS if '0' */
	/* Z180 related - LGB */
#ifdef Z80EX_Z180_SUPPORT
	int z180;
	int internal_int_disable;
#endif
};

typedef struct _z80_cpu_context Z80EX_CONTEXT;

extern Z80EX_CONTEXT z80ex;

/* statically linked callbacks */

#ifdef Z80EX_CALLBACK_PROTOTYPE
#ifdef Z80EX_TSTATE_CALLBACK
Z80EX_CALLBACK_PROTOTYPE void z80ex_tstate_cb ( void );
#endif
Z80EX_CALLBACK_PROTOTYPE Z80EX_BYTE z80ex_mread_cb ( Z80EX_WORD addr, int m1_state );
Z80EX_CALLBACK_PROTOTYPE void z80ex_mwrite_cb ( Z80EX_WORD addr, Z80EX_BYTE value );
Z80EX_CALLBACK_PROTOTYPE Z80EX_BYTE z80ex_pread_cb ( Z80EX_WORD port );
Z80EX_CALLBACK_PROTOTYPE void z80ex_pwrite_cb ( Z80EX_WORD port, Z80EX_BYTE value);
Z80EX_CALLBACK_PROTOTYPE Z80EX_BYTE z80ex_intread_cb(void);
Z80EX_CALLBACK_PROTOTYPE void z80ex_reti_cb ( void );
#ifdef Z80EX_ED_TRAPPING_SUPPORT
Z80EX_CALLBACK_PROTOTYPE int z80ex_ed_cb (Z80EX_BYTE opcode);
#endif
#ifdef Z80EX_Z180_SUPPORT
Z80EX_CALLBACK_PROTOTYPE void z80ex_z180_cb (Z80EX_WORD pc, Z80EX_BYTE prefix, Z80EX_BYTE series, Z80EX_BYTE opcode, Z80EX_BYTE itc76);
#endif
#endif

/*create and initialize CPU*/
extern void z80ex_init(void);

/*do next opcode (instruction or prefix), return number of T-states*/
extern int z80ex_step(void);

/*returns type of the last opcode, processed with z80ex_step.
type will be 0 for complete instruction, or dd/fd/cb/ed for opcode prefix.*/
#define z80ex_last_op_type() z80ex.prefix

/*maskable interrupt*/
/*returns number of T-states if interrupt was accepted, otherwise 0*/
extern int z80ex_int();

/*non-maskable interrupt*/
/*returns number of T-states (11 if interrupt was accepted, or 0 if processor
is doing an instruction right now)*/
extern int z80ex_nmi();

/*reset CPU*/
extern void z80ex_reset(void);

extern void z80ex_init(void);

/*returns 1 if CPU doing HALT instruction now*/
#define z80ex_doing_halt() z80ex.halted

/*when called from callbacks, returns current T-state of the executing opcode (instruction or prefix),
else returns T-states taken by last opcode executed*/
#define z80ex_op_tstate() z80ex.tstate

/*generate <w_states> Wait-states. (T-state callback will be called <w_states> times, when defined).
should be used to simulate WAIT signal or disabled CLK*/
extern void z80ex_w_states(unsigned w_states);

/*spend one T-state doing nothing (often IO devices cannot handle data request on
the first T-state at which RD/WR goes active).
for I/O callbacks*/
extern void z80ex_next_t_state();

/*returns 1 if maskable interrupts are possible in current z80 state*/
extern int z80ex_int_possible(void);

/*returns 1 if non-maskable interrupts are possible in current z80 state*/
extern int z80ex_nmi_possible(void);

#endif
