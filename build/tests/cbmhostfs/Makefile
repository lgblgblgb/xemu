PROGRAMS	= cbmhostfs wedge
DISKIMAGE	= cbmhostfs.d81
TARGETS		= $(PROGRAMS) $(DISKIMAGE)

all:	$(TARGETS) Makefile

$(DISKIMAGE): $(PROGRAMS) Makefile
	cbmconvert -v2 -D8o cbmhostfs.d81 $(PROGRAMS)

cbmhostfs: cbmhostfs.c Makefile
	cl65 -t c64 cbmhostfs.c

wedge: wedge.asm Makefile
	cl65 -t none wedge.asm

clean:
	rm -f $(TARGETS) *.o *.d81
