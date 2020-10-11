#define TARGET_NAME "ep128"
#define TARGET_DESC "Enterprise 128"
#define CONFIG_Z180
#if defined(XEMU_ARCH_UNIX) && !defined(__arm__)
#define XEP128_GTK
#endif
#ifdef CONFIG_Z180
#define Z80EX_Z180_SUPPORT
#endif
#define Z80EX_ED_TRAPPING_SUPPORT
#define Z80EX_CALLBACK_PROTOTYPE extern
#define CONFIG_SDEXT_SUPPORT
#ifdef __EMSCRIPTEN__
#define NO_CONSOLE
#endif

#ifndef __EMSCRIPTEN__
#define CONFIG_USE_LODEPNG
#endif

#define CONFIG_EMSCRIPTEN_OK

// EP128 is not well integrated into Xemu framework, we need this:
#define XEMU_NO_SDL_DIALOG_OVERRIDE
// FIXME: very ugly hack, EP128 emulator sill uses its own things, we have to deal with ...
//#define DO_NOT_INCLUDE_EMUTOOLS

#define EMUTOOLS_GUI_INCLUDE_HACK "xep128.h"
