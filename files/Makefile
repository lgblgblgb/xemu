FILES = $(shell ls | sort | grep -wv Makefile | grep -wv checksum.md5 | grep -wv checksum.sha1)

all:
	md5sum $(FILES) > checksum.md5
	sha1sum $(FILES) > checksum.sha1
