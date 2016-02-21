#ifndef _NEMESYS_65C02_H
#define _NEMESYS_65C02_H

extern int cpu_fatal;
extern uword cpu_pc, cpu_old_pc;
extern ubyte cpu_op;

void  cpu_write(uword addr, ubyte data);
ubyte cpu_read(uword addr);

void cpu_reset(void);
int  cpu_step (void);

#endif
