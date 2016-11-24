all:
	$(MAKE) xep_rom.hex logo.ico

logo.ico: logo.png
	convert $< $@

xep_rom.hex: xep_rom.asm
	sjasm -s xep_rom.asm xep_rom.rom || { rm -f xep_rom.rom xep_rom.lst xep_rom.sym ; false; }
	awk '$$1 ~ /xepsym_[^:. ]+:/ { gsub(":$$","",$$1); gsub("h$$","",$$3); print "#define " $$1 " 0x" $$3 }' xep_rom.sym > xep_rom_syms.h
	../build/bin2values.py xep_rom.rom xep_rom.hex

clean:
	rm -f xep_rom.sym xep_rom.rom xep_rom.lst
