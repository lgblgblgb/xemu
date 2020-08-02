#define TARGET_NAME "c65"
#define TARGET_DESC "Commodore 65"
#define CPU_65CE02
#define CPU65_65CE02_6502NMOS_TIMING_EMULATION
#define XEMU_SNAPSHOT_SUPPORT "Commodore-65"
#define CPU_STEP_MULTI_OPS
#define CPU65 cpu65

#define DMA_SOURCE_IOREADER_FUNC        io_read
#define DMA_SOURCE_MEMREADER_FUNC       read_phys_mem_for_dma
#define DMA_TARGET_IOREADER_FUNC        io_read
#define DMA_TARGET_MEMREADER_FUNC       read_phys_mem_for_dma
#define DMA_SOURCE_IOWRITER_FUNC        io_write
#define DMA_SOURCE_MEMWRITER_FUNC       write_phys_mem_for_dma
#define DMA_TARGET_IOWRITER_FUNC        io_write
#define DMA_TARGET_MEMWRITER_FUNC       write_phys_mem_for_dma
#define DMA_LIST_READER_FUNC            read_phys_mem_for_dma

#define FAKE_TYPING_SUPPORT
#define C65_FAKE_TYPING_LOAD_SEQS
#define C65_KEYBOARD
#define HID_KBD_MAP_CFG_SUPPORT

#define CONFIG_EMSCRIPTEN_OK
