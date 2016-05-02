#include <stdio.h>
#include <SDL.h>
#include "emutools.h"


// Called by CPU emulation code
void  cpu_write(Uint16 addr, Uint8 data)
{
}


// Called by CPU emulation code
Uint8 cpu_read(Uint16 addr)
{
	return 0xFF;
}




int main ( int argc, char **argv )
{
	FATAL("This emulator has to be written :-)");
	return 1;
}

