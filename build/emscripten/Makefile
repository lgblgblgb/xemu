all:
	cp *.html *.js ../../rom/c65-system.rom ../../rom/primo-b64.rom ../../rom/vic20-basic.rom ../../rom/vic20-chargen.rom ../../rom/vic20-emulator-tool.rom ../../rom/vic20-kernal.rom ../bin/
	dd if=/dev/zero of=../bin/empty.d81 bs=819200 count=1
	echo "format xemu,00" | c1541 ../bin/empty.d81

