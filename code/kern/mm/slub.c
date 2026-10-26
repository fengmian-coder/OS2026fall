#include <slub.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SLUB_MAGIC          0x534C5542UL
#define SLUB_LARGE_MAGIC    0x4C415247UL
#define SLUB_ALIGN          16

struct slub_page {
    uint64_t magic;

    struct slub_cache *cache;
    struct Page *page;

    list_entry_t slab_link;

    void *free_object;

    uint16_t inuse;
    uint16_t total;
};

struct slub_large {
    uint64_t magic;
    struct Page *page;
    size_t requested_size;
    size_t nr_pages;
};

static struct slub_cache slub_caches[SLUB_CACHE_COUNT];

static const size_t slub_sizes[SLUB_CACHE_COUNT] = {
    16, 32, 64, 128,
    256, 512, 1024, 2048
};

/*
 * 当前内核把物理内存直接映射到高虚拟地址。
 * Page -> PA -> KVA。
 */
static void *
slub_page_kva(struct Page *page) {
    return (void *)(page2pa(page) + va_pa_offset);
}

/*
 * KVA -> PA -> Page。
 */
static struct Page *
slub_kva_page(void *ptr) {
    uintptr_t kva =
        ROUNDDOWN((uintptr_t)ptr, PGSIZE);

    uintptr_t pa =
        kva - va_pa_offset;

    return pa2page(pa);
}

static size_t
slub_header_size(void) {
    return ROUNDUP(sizeof(struct slub_page), SLUB_ALIGN);
}

/*
 * 一个 slab 页中最多能存多少个对象。
 */
static size_t
slub_objects_per_page(size_t object_size) {
    size_t header = slub_header_size();

    if (header >= PGSIZE) {
        return 0;
    }

    return (PGSIZE - header) / object_size;
}

/*
 * 根据请求大小选择 size cache。
 */
static int
slub_cache_index(size_t size) {
    for (int i = 0; i < SLUB_CACHE_COUNT; i++) {
        if (size <= slub_sizes[i]) {
            return i;
        }
    }

    return -1;
}

/*
 * 在一个新物理页中建立 slab。
 */
static struct slub_page *
slub_grow(struct slub_cache *cache) {
    struct Page *page = alloc_page();

    if (page == NULL) {
        return NULL;
    }

    void *kva = slub_page_kva(page);
    memset(kva, 0, PGSIZE);

    struct slub_page *slab =
        (struct slub_page *)kva;

    slab->magic = SLUB_MAGIC;
    slab->cache = cache;
    slab->page = page;

    list_init(&slab->slab_link);

    slab->inuse = 0;
    slab->total =
        slub_objects_per_page(cache->object_size);

    assert(slab->total > 0);

    uintptr_t first =
        (uintptr_t)kva + slub_header_size();

    /*
     * 直接使用空闲 object 本身保存 next 指针。
     */
    slab->free_object = (void *)first;

    for (size_t i = 0; i < slab->total; i++) {
        void *obj =
            (void *)(first +
                     i * cache->object_size);

        void *next = NULL;

        if (i + 1 < slab->total) {
            next =
                (void *)(first +
                         (i + 1) *
                             cache->object_size);
        }

        *(void **)obj = next;
    }

    list_add(
        &cache->slabs_free,
        &slab->slab_link
    );

    return slab;
}

static void
slub_move_slab(
    struct slub_page *slab,
    list_entry_t *target
) {
    list_del(&slab->slab_link);
    list_add(target, &slab->slab_link);
}

static struct slub_page *
slub_first_slab(list_entry_t *head) {
    if (list_empty(head)) {
        return NULL;
    }

    list_entry_t *le = list_next(head);

    return to_struct(
        le,
        struct slub_page,
        slab_link
    );
}

/*
 * 从指定 cache 中分配一个小对象。
 */
static void *
slub_cache_alloc(struct slub_cache *cache) {
    struct slub_page *slab = NULL;

    /*
     * 优先使用 partial slab。
     */
    slab =
        slub_first_slab(&cache->slabs_partial);

    if (slab == NULL) {
        slab =
            slub_first_slab(&cache->slabs_free);
    }

    if (slab == NULL) {
        slab = slub_grow(cache);

        if (slab == NULL) {
            return NULL;
        }
    }

    assert(slab->free_object != NULL);
    assert(slab->inuse < slab->total);

    void *obj = slab->free_object;

    slab->free_object = *(void **)obj;
    slab->inuse++;

    /*
     * 重新确定 slab 所属链表。
     */
    if (slab->inuse == slab->total) {
        slub_move_slab(
            slab,
            &cache->slabs_full
        );
    }
    else {
        slub_move_slab(
            slab,
            &cache->slabs_partial
        );
    }

    return obj;
}

/*
 * 释放一个小对象。
 */
static void
slub_cache_free(void *ptr) {
    struct Page *page =
        slub_kva_page(ptr);

    void *kva =
        slub_page_kva(page);

    struct slub_page *slab =
        (struct slub_page *)kva;

    assert(slab->magic == SLUB_MAGIC);
    assert(slab->cache != NULL);
    assert(slab->inuse > 0);

    struct slub_cache *cache =
        slab->cache;

    uintptr_t object_begin =
        (uintptr_t)kva +
        slub_header_size();

    uintptr_t object_end =
        object_begin +
        slab->total * cache->object_size;

    uintptr_t addr =
        (uintptr_t)ptr;

    /*
     * 必须属于对象区域，并且正好位于对象边界。
     */
    assert(addr >= object_begin);
    assert(addr < object_end);
    assert(
        (addr - object_begin) %
            cache->object_size == 0
    );

    *(void **)ptr =
        slab->free_object;

    slab->free_object = ptr;
    slab->inuse--;

    if (slab->inuse == 0) {
        slub_move_slab(
            slab,
            &cache->slabs_free
        );
    }
    else {
        slub_move_slab(
            slab,
            &cache->slabs_partial
        );
    }
}

void *
kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    int index =
        slub_cache_index(size);

    /*
     * 2048 字节以内走 SLUB。
     */
    if (index >= 0) {
        return slub_cache_alloc(
            &slub_caches[index]
        );
    }

    /*
     * 大对象直接向 PMM 申请连续页。
     * 第一页开头保存大对象元数据。
     */
    size_t total =
        size + sizeof(struct slub_large);

    size_t nr_pages =
        ROUNDUP(total, PGSIZE) /
        PGSIZE;

    struct Page *page =
        alloc_pages(nr_pages);

    if (page == NULL) {
        return NULL;
    }

    void *kva =
        slub_page_kva(page);

    struct slub_large *large =
        (struct slub_large *)kva;

    large->magic = SLUB_LARGE_MAGIC;
    large->page = page;
    large->requested_size = size;
    large->nr_pages = nr_pages;

    return (void *)(large + 1);
}

void
kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    struct Page *page =
        slub_kva_page(ptr);

    void *kva =
        slub_page_kva(page);

    uint64_t magic =
        *(uint64_t *)kva;

    if (magic == SLUB_MAGIC) {
        slub_cache_free(ptr);
        return;
    }

    if (magic == SLUB_LARGE_MAGIC) {
        struct slub_large *large =
            (struct slub_large *)kva;

        size_t nr_pages =
            large->nr_pages;

        struct Page *base =
            large->page;

        /*
         * 防止释放后继续把旧 magic 当成有效对象。
         */
        large->magic = 0;

        free_pages(base, nr_pages);
        return;
    }

    panic("kfree: invalid pointer");
}

size_t
ksize(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }

    struct Page *page =
        slub_kva_page(ptr);

    void *kva =
        slub_page_kva(page);

    uint64_t magic =
        *(uint64_t *)kva;

    if (magic == SLUB_MAGIC) {
        struct slub_page *slab =
            (struct slub_page *)kva;

        return slab->cache->object_size;
    }

    if (magic == SLUB_LARGE_MAGIC) {
        struct slub_large *large =
            (struct slub_large *)kva;

        return large->requested_size;
    }

    return 0;
}

/*
 * 回收所有完全空闲的 slab 页。
 */
size_t
slub_reap(void) {
    size_t pages_reaped = 0;

    for (int i = 0;
         i < SLUB_CACHE_COUNT;
         i++) {

        struct slub_cache *cache =
            &slub_caches[i];

        list_entry_t *head =
            &cache->slabs_free;

        list_entry_t *le =
            list_next(head);

        while (le != head) {
            list_entry_t *next =
                list_next(le);

            struct slub_page *slab =
                to_struct(
                    le,
                    struct slub_page,
                    slab_link
                );

            assert(slab->inuse == 0);

            list_del(le);

            struct Page *page =
                slab->page;

            slab->magic = 0;

            free_page(page);

            pages_reaped++;

            le = next;
        }
    }

    return pages_reaped;
}

void
slub_check(void) {
    size_t free_before =
        nr_free_pages();

    /*
     * 24 -> 32-byte cache。
     */
    void *a = kmalloc(24);
    void *b = kmalloc(24);

    assert(a != NULL);
    assert(b != NULL);
    assert(a != b);

    assert(ksize(a) == 32);
    assert(ksize(b) == 32);

    memset(a, 0x11, 24);
    memset(b, 0x22, 24);

    /*
     * 最大的小对象 size class。
     */
    void *c = kmalloc(2000);

    assert(c != NULL);
    assert(ksize(c) == 2048);

    /*
     * 大于 2048，测试连续页路径。
     */
    void *large = kmalloc(3000);

    assert(large != NULL);
    assert(ksize(large) == 3000);

    kfree(a);
    kfree(b);
    kfree(c);
    kfree(large);

    /*
     * 小对象所在的空 slab 页此时仍缓存着，
     * reap 后应全部返回 PMM。
     */
    size_t reaped =
        slub_reap();

    assert(reaped > 0);
    assert(nr_free_pages() == free_before);

    cprintf(
        "slub_check() succeeded!\n"
    );
}

void
slub_init(void) {
    for (int i = 0;
         i < SLUB_CACHE_COUNT;
         i++) {

        struct slub_cache *cache =
            &slub_caches[i];

        cache->name = "slub-sized";
        cache->object_size =
            slub_sizes[i];

        list_init(&cache->slabs_free);
        list_init(&cache->slabs_partial);
        list_init(&cache->slabs_full);
    }

    cprintf(
        "SLUB: initialized size caches "
        "16..2048 bytes\n"
    );

    slub_check();
}
