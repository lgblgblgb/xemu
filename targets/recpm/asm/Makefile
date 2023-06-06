all: bios_hard.cdata

%.rel: %.asm Makefile
	@echo "----[ $< -> $@ ]------------------------------------------"
	@echo "[[pass1]]"
	rasm -xr -DORGI=0x100 -ob $@.p1 $<
	@echo "[[pass2]]"
	rasm -DORGI=0x200 -ob $@.p2 $<
	@echo "[[merge]]"
	cat $@.p1 $@.p2 > $@
	rm -f $@.p1 $@.p2

%.cdata: %.rel
	@echo "----[ $< -> $@ ]----------------------------------------"
	python ../../../build/bin2values.py $< $@

clean:
	rm -f *.rel *.rel.p1 *.rel.p2 *.cdata

.PRECIOUS: %.rel
.PHONY: clean all
