#!/bin/sh
exec gawk '
BEGIN {
	print "/* from src/vhdl/matrix_to_ascii.vhdl in mega65-core project */"
	grab = ""
}
/^\s*signal\s+matrix_(normal|shift|control|cbm)/ {
	grab = $2
	gsub(":.*$","",grab)
	grab = "static const Uint8 " grab "_to_ascii [] = {"
	comma = " "
	next
}
grab && ($1 ~ /^others|)/) {
	print grab " };"
	grab = ""
	next
}
grab && $0 ~ /^\s*[0-9]+\s*=>\s*[xX]"/ {
	val = $0;
	gsub("^.*=>[\t ]*[xX]\"","0x",val)
	gsub("\".*$","",val)
	grab = grab comma val
	comma = ","
	next
}
' < ../../../mega65-core/src/vhdl/matrix_to_ascii.vhdl
