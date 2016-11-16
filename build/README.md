# Xemu build-specific files

This directory contains various files needed (or can be used) to build
and test Xemu.

## Directories:

* `bin`: the result binaries after compilation will be here
* `deploy`: used by the deploy mechanism on github/travis/bintray
* `objs`: object file "cache", you should not bother these too much
* `tests`: various tests/utils, you may want to **look at these**
* `travis`: used by github/travis for automatical build tests/deployments

## Files:

* Makefile.*something* various files: used by the build system for the
given 'architecture' (ie: Makefile.win32 is for Windows 32 bit builds). If you
know what you are doing, you can modify architecture specific parameters in these
files.
