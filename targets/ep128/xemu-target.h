#define TARGET_NAME "ep128"
#define TARGET_DESC "Enterprise 128"
#define CONFIG_Z180

#ifdef CONFIG_Z180
#define Z80EX_Z180_SUPPORT
#endif

#define Z80EX_ED_TRAPPING_SUPPORT
#define Z80EX_CALLBACK_PROTOTYPE extern
#define CONFIG_SDEXT_SUPPORT

#ifdef XEMU_ARCH_HTML
#define NO_CONSOLE
#endif

#ifndef XEMU_ARCH_HTML
#define XEMU_USE_LODEPNG
#define XEMU_FILES_SCREENSHOT_SUPPORT
#define HAVE_XEMU_EXEC_API
#endif

//FIXME: let's repair ep128 with emscripten and re-allow it!
//#define CONFIG_EMSCRIPTEN_OK

#define VARALIGN MAXALIGNED

#define XEMU_CONFIGDB_SUPPORT
#define XEMU_OSD_SUPPORT
