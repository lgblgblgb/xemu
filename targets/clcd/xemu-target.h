#define TARGET_NAME "clcd"
#define TARGET_DESC "Commodore LCD"
#define CPU65_TRAP_OPCODE 0xFC
#define CPU65 cpu65

// I guess Commodore LCD since used CMOS 65C02 already, no need to emulate the RMW behaviour on NMOS 6502 (??)
#define CPU65_NO_RMW_EMULATION

#ifndef XEMU_ARCH_HTML
#define XEMU_USE_LODEPNG
#define XEMU_FILES_SCREENSHOT_SUPPORT
#define HAVE_XEMU_EXEC_API
#endif

#define CONFIG_EMSCRIPTEN_OK
#define XEMU_CONFIGDB_SUPPORT
#define XEMU_OSD_SUPPORT
