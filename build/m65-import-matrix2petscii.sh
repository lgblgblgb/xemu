#!/bin/sh
exec gawk '
BEGIN {
	print "/* from src/vhdl/matrix_to_ascii.vhdl in mega65-core project */"
	grab = ""
}
/^\s*signal\s+matrix_petscii_(normal|shift|control|mega|capslock|cbm|alt)/ {
	grab = $2
	gsub(":.*$","",grab)
	if (grab == "matrix_petscii_capslock")
		ifdef = "#if 0\n"
	else
		ifdef = ""
	grab = ifdef "static const Uint8 " grab "_to_petscii[KBD_MATRIX_SIZE] = {\n\t"
	ct = 0
	lin = 0
	next
}
grab && ($1 ~ /^others|)/) {
	print grab "};"
	grab = ""
	if (ifdef != "")
		print "#endif"
	next
}
grab && $0 ~ /^\s*[0-9]+\s*=>\s*[xX]"/ {
	val = $0;
	gsub("^.*=>[\t ]*[xX]\"","0x",val)
	gsub("\".*$","",val)
	grab = grab val
	if (ct < 7)
		grab = grab ","
	ct = ct + 1
	if (ct == 8) {
		ct = 0
		lin = lin + 1
		if (lin < 9)
			grab = grab ",\n\t"
		else
			grab = grab "\n"
	}

	next
}
' | sed 's/matrix_petscii_/matrix_/g;s/_normal_/_base_/g;s/_shifted_/_shift_/g;s/_mega_/_cbm_/g;s/_control_/_ctrl_/g'
