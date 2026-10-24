#ifndef __KERN_DRIVER_DTB_H__
#define __KERN_DRIVER_DTB_H__

#include <defs.h>

// Defined in entry.S
extern uint64_t boot_hartid;
extern uint64_t boot_dtb;

#define DTB_MAX_MEMORY_REGIONS   8
#define DTB_MAX_RESERVED_REGIONS 16

struct dtb_memory_region {
    uint64_t base;
    uint64_t size;
};

void dtb_init(void);

/* 兼容原 Lab2 接口 */
uint64_t get_memory_base(void);
uint64_t get_memory_size(void);

/* Challenge: 查询 DTB 中的所有 RAM 区域 */
size_t get_memory_region_count(void);
int get_memory_region(size_t index, uint64_t *base, uint64_t *size);

/* Challenge: 查询 DTB 中的保留物理内存区域 */
size_t get_reserved_region_count(void);
int get_reserved_region(size_t index, uint64_t *base, uint64_t *size);

#endif /* !__KERN_DRIVER_DTB_H__ */
