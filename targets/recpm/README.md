Re-CP/M is a restricted CP/M (2.2) re-implementation from ground-up.

It implements the CP/M BIOS and BDOS and many utilities by its own,
without the need any original CP/M material/disks. It does that
by providing the native file system of the host OS (which runs the
emulator, for example Windows, Linux or MacOS) to the CP/M software,
for easier CP/M-related projects, tests.

It's important to decalre, that Re-CP/M *DOES NOT* have the following
goals:

* it does not run every CP/M apps: especially system applications which
depends on low-level disk access, which is impossible to do, since re-CP/M
provides a "virtual filesystem" can only be used on FCB-controlled operations,
but never on the block I/O (disk) level. Thus every CP/M program will probably
fail or missbehave which tries to mess disk level calls or structures (though
for the FCB, direct manipulation of the FCB is supported if it's only used
to "seek" as some tools do, but not the block pointer etc level things there).
If you need a CP/M system emulation with real low level disk emulation you
can find plenty of emulators can do that, emulating very wide spectrum of
CP/M capable hardware.
* it does not aim to have visually the same for the built-in apps
* it is not as versatile as a true CP/M, for example there is no implementation
of the so called IO-byte, thus redirections.
* it does not emulate the 8080 but Z80. In general this should be not a problem
though, in fact, many late CP/M software started to 'exploit' the features of
Z80, since almost every (later) CP/M hardware was equipped with Z80 instead of
8080 (in theory Z80 is backward compatible with 8080, however there can be some
very rare and minor issue, like the P/V flag interpretation after addition)
* it does not want to provide anything 'standard' later than CP/M 2.2, however,
it may introduce some extended features in its own way (like directories)
* re-CP/M surely not a CP/M implementation for a real hardware! it does everything
in C, by talking with the host OS. But, indeed, this can help (for me at least!)
to better understand CP/M to be able write my own little CP/M-ish system which
can run on an SBC, or such.

However some extras, re-CP/M can offer or at least *planned to offer*:

* PLANNED: able to work without SDL and own window, just in the plain terminal,
with optionally allow to use stdin/stdout of CP/M connected to your host OS
stdin/stdout, allow to "integrate" CP/M commands/programs into native shell
scripts/batch files, or whatever
* PLANNED: wide variety of CP/M console emulations with different special codes,
escape sequencies
* re-CP/M allows to start a CP/M program from your host-OS fileystem without
any preparation, *OR* it allows to use the system as it would be a "real one"
with the CLI-lookalike of a CP/M system after boot.
* no need for any installation of the CP/M system, re-CP/M contains everything
to start, and having a working CP/M-like environment, ready to run CP/M
software
* direct, real-time access to the host OS file system, no need to fiddle with
CP/M disk images, copy data between it and the host OS, etc
* non-standard way to access directories (CP/M 2 does not have directories) by
allowing to "change directory" by a special built-in command for a given CP/M
drive.
* "PATH" like feature (presents on later CP/Ms, but not in 2): allowing CP/M
drives searched for a given command invoked.
* Built-in some default CP/M apps, including the command processor itself.
* System integrity: ability to detect tampering CP/M memory areas and/or
important part of the "zero page" so it's easier to find bugs/problems with
a CP/M software (would cause a simple crash with a real CP/M without too
much information about the reason)
* Allow much higher execution speed than a real CP/M system or hardware level
implementation, can be great for more rapid testing. Though, of course there
is speed control to limit the emulated CPU's speed.
* Allow to mov(-e) (MOVCPM) the system, with a single command, without the need
to re-boot, or anything. This can be useful to test different scenarios with
different amount of memory availabe to a CP/M program.

Built in programs:

* CPP: the CPP itself, or "something like that" at least :)
* CD: re-CP/M extension, to allow select which host OS directory is mapped to
which CP/M drive, and to allow to change that dynamically run-time
