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
#define DO_NOT_FORCE_INLINE
//#define CPU65_DISCRETE_PF_NZ
