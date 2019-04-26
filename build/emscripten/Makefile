all:
	cp -a xemu-sample.html xemu-*-sample.html xemu-boot.js ../../rom/c65-system.rom ../../rom/primo-b64.rom ../../rom/vic20-basic.rom ../../rom/vic20-chargen.rom ../../rom/vic20-emulator-tool.rom ../../rom/vic20-kernal.rom ../../rom/clcd-chargen.rom ../../rom/clcd-u102.rom ../../rom/clcd-u103.rom ../../rom/clcd-u104.rom ../../rom/clcd-u105.rom ../../rom/tvc22_d4_64k.rom ../../rom/tvc22_d6_64k.rom ../../rom/tvc22_d7_64k.rom ../../rom/tvc_sddos.rom ../bin/
	test -s ../bin/empty.d81 || { dd if=/dev/zero of=../bin/empty.d81 bs=819200 count=1 ; echo "format xemu,00" | c1541 ../bin/empty.d81 ; }
