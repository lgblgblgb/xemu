#ifndef _NEMESYS_VIA_H
#define _NEMESYS_VIA_H

struct Via65c22 {
	void (*outa)(ubyte mask, ubyte data);
	void (*outb)(ubyte mask, ubyte data);
	void (*outsr)(ubyte data);
	ubyte (*ina)(ubyte mask);
	ubyte (*inb)(ubyte mask);
	ubyte (*insr)(void);
	void (*setint)(int level);
	const char *name;
	ubyte DDRB, ORB, DDRA, ORA, SR, IER, IFR, ACR, PCR, T1LL, T1LH, T2LL, T2LH;
	int T1C, T2C;
	int irqLevel, SRcount, SRmode, T1run, T2run;
};

void via_init(
	struct Via65c22 *via, const char *name,
	void (*outa)(ubyte mask, ubyte data),
	void (*outb)(ubyte mask, ubyte data),
	void (*outsr)(ubyte data),
	ubyte (*ina)(ubyte mask),
	ubyte (*inb)(ubyte mask),
	ubyte (*insr)(void),
	void (*setint)(int level)
);
void  via_reset(struct Via65c22 *via);
void  via_write(struct Via65c22 *via, int addr, ubyte data);
ubyte via_read (struct Via65c22 *via, int addr);
void  via_tick (struct Via65c22 *via, int ticks);




#endif
