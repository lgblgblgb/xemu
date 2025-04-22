#!/bin/bash

OUT1="/tmp/sysinfo-output-$$-$RANDOM.stdout"
OUT2="/tmp/sysinfo-output-$$-$RANDOM.stderr"

echo 'cat /etc/debian_version
lsb_release -a
sw_vers
lscpu
sysctl -n machdep.cpu.brand_string
sysctl -n machdep.cpu.core_count
sysctl -n machdep.cpu.thread_count
uname -a
id -a
echo $PATH
yacc --version | head -n 1
gcc  --version | head -n 1
g++  --version | head -n 1
x86_64-w64-mingw32-gcc --version | head -n 1
i686-w64-mingw32-gcc   --version | head -n 1
make --version | head -n 1
git  --version | head -n 1
gawk --version | head -n 1
awk  --version | head -n 1
bash --version | head -n 1
pwd
hostname
/sbin/ip a
ifconfig
uptime
df -h .' | while read line ; do
	rm -f "$OUT1" "$OUT2"
	eval $line </dev/zero >$OUT1 2>$OUT2
	if [ -s "$OUT1" ]; then
		echo "$line"
		cat "$OUT1" | sed 's/^/\t/'
	fi
	rm -f "$OUT1" "$OUT2"
done

exit 0
