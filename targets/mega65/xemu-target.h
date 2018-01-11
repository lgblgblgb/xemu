#define TARGET_NAME "mega65"
#define TARGET_DESC "MEGA65"
#define CPU_65CE02
#define MEGA65
#define XEMU_SNAPSHOT_SUPPORT "Mega-65"
#define CPU_STEP_MULTI_OPS
//#define DEBUG_CPU
#define CPU_CUSTOM_MEMORY_FUNCTIONS_H "cpu_custom_functions.h"
#define HAVE_XEMU_EXEC_API
#ifdef HAVE_SOCKET_OS_API
//#define HAVE_XEMU_SOCKET_API
//#define HAVE_XEMU_UMON
#endif
#define HAVE_XEMU_INSTALLER
#define CPU65 cpu65
//#define DO_NOT_FORCE_INLINE
//#define CPU65_DISCRETE_PF_NZ

// CPU emulation has always has these (originally NMOS) bugs, regardless of the CPU mode (1 = yes, 0 = no-or-mode-dependent)
#define M65_CPU_ALWAYS_BUG_JMP_INDIRECT			0
#define M65_CPU_ALWAYS_BUG_NO_RESET_PFD_ON_INT		0
#define M65_CPU_ALWAYS_BUG_BCD				0
// CPU emulation has only these NMOS-only bugs, if the CPU is in NMOS-persona mode (1=yes-only-in-nmos, 0=ALWAYS-setting-counts-for-this-bug-not-this-setting)
// To be able to use these, the corresponding ALWAYS setting above should be 0!
#define M65_CPU_NMOS_ONLY_BUG_JMP_INDIRECT		1
#define M65_CPU_NMOS_ONLY_BUG_NO_RESET_PFD_ON_INT	1
#define M65_CPU_NMOS_ONLY_BUG_BCD			1
