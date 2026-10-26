#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>
#include <list.h>
#include <pmm.h>

#define SLUB_MIN_SIZE       16
#define SLUB_MAX_SIZE       2048
#define SLUB_CACHE_COUNT    8
#define SLUB_NAME_LEN       16

struct slub_cache {
    const char *name;
    size_t object_size;

    /* 完全没有对象被使用的 slab */
    list_entry_t slabs_free;

    /* 部分对象已经被使用 */
    list_entry_t slabs_partial;

    /* 所有对象都已经被使用 */
    list_entry_t slabs_full;
};

void slub_init(void);

void *kmalloc(size_t size);
void kfree(void *ptr);

size_t ksize(void *ptr);

size_t slub_reap(void);

void slub_check(void);

#endif
