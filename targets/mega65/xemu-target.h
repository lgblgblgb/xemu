#define TARGET_NAME "mega65"
#define TARGET_DESC "MEGA65"
#define CPU_65CE02
#define MEGA65
#define CPU65_65CE02_6502NMOS_TIMING_EMULATION
#define XEMU_SNAPSHOT_SUPPORT "Mega-65"
#define CPU_STEP_MULTI_OPS
//#define DEBUG_CPU
#define CPU_CUSTOM_MEMORY_FUNCTIONS_H "cpu_custom_functions.h"
#define CPU65 cpu65
//#define CPU65_DISCRETE_PF_NZ

// #define DO_NOT_FORCE_UNREACHABLE

#define HAVE_XEMU_EXEC_API

#ifdef HAVE_SOCKET_OS_API
//#define HAVE_XEMU_SOCKET_API
//#define HAVE_XEMU_UMON
#endif
#define HAVE_XEMU_INSTALLER

#ifndef XEMU_ARCH_HTML
#define CONFIG_DROPFILE_CALLBACK
#define VIRTUAL_DISK_IMAGE_SUPPORT
//#define BASIC_TEXT_SUPPORT
#define SD_CONTENT_SUPPORT
#endif

/* Globally: XEMU_INLINE hints gcc to always inline a function. Using this define switches that behaviour off, defaulting to standard "inline" (as it would be without using gcc as well) */
//#define DO_NOT_FORCE_INLINE

// CPU emulation has always has these (originally NMOS) bugs, regardless of the CPU mode (1 = yes, 0 = no-or-mode-dependent)
#define M65_CPU_ALWAYS_BUG_JMP_INDIRECT			0
#define M65_CPU_ALWAYS_BUG_NO_RESET_PFD_ON_INT		0
#define M65_CPU_ALWAYS_BUG_BCD				0
// CPU emulation has only these NMOS-only bugs, if the CPU is in NMOS-persona mode (1=yes-only-in-nmos, 0=ALWAYS-setting-counts-for-this-bug-not-this-setting)
// To be able to use these, the corresponding ALWAYS setting above should be 0!
#define M65_CPU_NMOS_ONLY_BUG_JMP_INDIRECT		1
#define M65_CPU_NMOS_ONLY_BUG_NO_RESET_PFD_ON_INT	1
#define M65_CPU_NMOS_ONLY_BUG_BCD			1

// Currently only Linux-TAP device is supported to have emulated ethernet controller
#ifdef XEMU_ARCH_LINUX
#define HAVE_ETHERTAP
#endif

#define DMA_SOURCE_IOREADER_FUNC	io_dma_reader
#define DMA_SOURCE_MEMREADER_FUNC	memory_dma_source_mreader
#define DMA_TARGET_IOREADER_FUNC	io_dma_reader
#define DMA_TARGET_MEMREADER_FUNC	memory_dma_target_mreader
#define DMA_SOURCE_IOWRITER_FUNC	io_dma_writer
#define DMA_SOURCE_MEMWRITER_FUNC	memory_dma_source_mwriter
#define DMA_TARGET_IOWRITER_FUNC	io_dma_writer
#define DMA_TARGET_MEMWRITER_FUNC	memory_dma_target_mwriter
#define DMA_LIST_READER_FUNC		memory_dma_list_reader

#define FAKE_TYPING_SUPPORT
#define C65_FAKE_TYPING_LOAD_SEQS
#define C65_KEYBOARD
#define HID_KBD_MAP_CFG_SUPPORT

#define CONFIG_EMSCRIPTEN_OK
