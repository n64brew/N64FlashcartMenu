#ifndef TESTS_LIBDRAGON_H
#define TESTS_LIBDRAGON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t pi_addr_t;
typedef struct kthread_s kthread_t;
typedef enum {
    INTERRUPTS_UNINITIALIZED,
    INTERRUPTS_DISABLED,
    INTERRUPTS_ENABLED,
} interrupt_state_t;

#define C0_STATUS_IE 0x01u
#define C0_STATUS_EXL 0x02u
#define C0_STATUS_ERL 0x04u
#define C0_STATUS() mock_cp0_status()
#define TICKS_PER_SECOND 46875000u
#define TICKS_FROM_MS(ms) ((ms) * (TICKS_PER_SECOND / 1000u))
#define TICKS_READ() mock_ticks_read()
#define MEMORY_BARRIER() mock_memory_barrier()

#define PhysicalAddr(addr) ((uintptr_t)(addr) > UINT32_MAX ? 0u : ((uintptr_t)(addr) & 0x1FFFFFFFu))

typedef struct {
    int (*disk_initialize)(void);
    int (*disk_status)(void);
    int (*disk_read)(uint8_t *buffer, int64_t sector, int count);
    int (*disk_write)(const uint8_t *buffer, int64_t sector, int count);
    int (*disk_ioctl)(uint8_t command, void *buffer);
} fat_disk_t;

int fat_mount (const char *prefix, const fat_disk_t *disk, int flags);
int fat_unmount (int volume_id);

#define PI_STATUS_DMA_BUSY 1u
#define PI_STATUS_IO_BUSY 2u
#define PI_STATUS_ERROR 4u
#define PI_STATUS (&mock_pi_status)

extern volatile uint32_t mock_pi_status;
uint32_t mock_cp0_status (void);
uint32_t mock_ticks_read (void);
void mock_memory_barrier (void);
void disable_interrupts (void);
void enable_interrupts (void);
interrupt_state_t get_interrupts_state (void);
kthread_t *kthread_current (void);
int dma_busy (void);
uint64_t dma_read_raw_async (void *ram, pi_addr_t pi, unsigned long length);
uint64_t dma_write_raw_async (const void *ram, pi_addr_t pi, unsigned long length);
bool dma_finished (uint64_t ticket);
void data_cache_hit_writeback_invalidate (volatile void *ram, unsigned long length);
void data_cache_hit_writeback (const volatile void *ram, unsigned long length);
void data_cache_hit_invalidate (volatile void *ram, unsigned long length);

bool sys_bbplayer (void);
int bbfs_init (void);
bool debug_init_sdfs (const char *prefix, int npart);
bool debug_init_isviewer (void);
bool debug_init_usblog (void);

#endif
