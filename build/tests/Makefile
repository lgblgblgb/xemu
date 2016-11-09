PROGRAMS	= cbmhostfs
DISKIMAGES	= cbmhostfs.d81
TARGETS		= $(PROGRAMS) $(DISKIMAGES)

all:	$(TARGETS) Makefile

cbmhostfs.d81: cbmhostfs Makefile
	cbmconvert -v2 -D8o cbmhostfs.d81 cbmhostfs

cbmhostfs: cbmhostfs.c Makefile
	cl65 -t c64 cbmhostfs.c

clean:
	rm -f $(TARGETS) *.o *.d81
