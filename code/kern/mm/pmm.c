#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <slub.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &buddy_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 2412074

/*
 * Challenge helper:
 * 把一段已经确认可用的物理地址范围交给当前 PMM。
 * 起止地址会自动按页边界收缩。
 */
static size_t
init_usable_range(uintptr_t begin, uintptr_t end) {
    begin = ROUNDUP(begin, PGSIZE);
    end = ROUNDDOWN(end, PGSIZE);

    if (begin >= end) {
        return 0;
    }

    /* 当前 pages[] 只描述 [nbase, npage) 的物理页。 */
    if (begin / PGSIZE < nbase ||
        (end - 1) / PGSIZE >= npage) {
        return 0;
    }

    size_t count = (end - begin) / PGSIZE;

    init_memmap(pa2page(begin), count);

    cprintf(
        "  Usable RAM: 0x%016lx - 0x%016lx (%lu pages)\n",
        begin, end - 1, count
    );

    return count;
}

/*
 * 判断 [begin, end) 是否完整位于某个 DTB RAM region 中。
 * 主要用于确认 Page 描述符数组本身放在真实 RAM 中。
 */
static int
range_inside_dtb_ram(uintptr_t begin, uintptr_t end) {
    size_t count = get_memory_region_count();

    for (size_t i = 0; i < count; i++) {
        uint64_t base, size;

        if (get_memory_region(i, &base, &size) != 0 ||
            size == 0) {
            continue;
        }

        if (size > (uint64_t)-1 - base) {
            continue;
        }

        uint64_t region_end = base + size;

        if ((uint64_t)begin >= base &&
            (uint64_t)end <= region_end) {
            return 1;
        }
    }

    return 0;
}

/*
 * 判断 [begin, end) 是否与 DTB 保留区发生重叠。
 */
static int
range_overlaps_reserved(uintptr_t begin, uintptr_t end) {
    size_t count = get_reserved_region_count();

    for (size_t i = 0; i < count; i++) {
        uint64_t base, size;

        if (get_reserved_region(i, &base, &size) != 0 ||
            size == 0) {
            continue;
        }

        if (size > (uint64_t)-1 - base) {
            continue;
        }

        uint64_t reserved_end = base + size;

        if ((uint64_t)begin < reserved_end &&
            base < (uint64_t)end) {
            return 1;
        }
    }

    return 0;
}

/*
 * 对一段 RAM 区域减去所有 reserved region，
 * 将剩余的若干连续区间分别交给 PMM。
 *
 * reserved region 不要求预先排序。
 */
static size_t
init_ram_excluding_reserved(uintptr_t begin, uintptr_t end) {
    size_t total = 0;
    uintptr_t cursor = begin;

    while (cursor < end) {
        int found = 0;
        uintptr_t next_begin = end;
        uintptr_t next_end = end;

        size_t count = get_reserved_region_count();

        for (size_t i = 0; i < count; i++) {
            uint64_t rbase64, rsize64;

            if (get_reserved_region(
                    i, &rbase64, &rsize64) != 0 ||
                rsize64 == 0) {
                continue;
            }

            if (rsize64 > (uint64_t)-1 - rbase64) {
                continue;
            }

            uint64_t rend64 = rbase64 + rsize64;

            /*
             * 没有与 [cursor, end) 相交。
             */
            if (rend64 <= (uint64_t)cursor ||
                rbase64 >= (uint64_t)end) {
                continue;
            }

            uintptr_t overlap_begin =
                rbase64 > (uint64_t)cursor
                    ? (uintptr_t)rbase64
                    : cursor;

            uintptr_t overlap_end =
                rend64 < (uint64_t)end
                    ? (uintptr_t)rend64
                    : end;

            /*
             * 找离 cursor 最近的保留区域。
             * 如果起点相同，保留结束更晚的那个。
             */
            if (!found ||
                overlap_begin < next_begin ||
                (overlap_begin == next_begin &&
                 overlap_end > next_end)) {
                found = 1;
                next_begin = overlap_begin;
                next_end = overlap_end;
            }
        }

        /*
         * 后面已经没有 reserved region。
         */
        if (!found) {
            total += init_usable_range(cursor, end);
            break;
        }

        /*
         * reserved 前面的部分可以使用。
         */
        if (cursor < next_begin) {
            total += init_usable_range(cursor, next_begin);
        }

        /*
         * 跳过 reserved。
         */
        if (next_end <= cursor) {
            break;
        }

        cursor = next_end;
    }

    return total;
}

static void
page_init(void) {
    size_t region_count = get_memory_region_count();

    assert(region_count > 0);

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    /*
     * 当前实验内核的 Page 编号仍以 DRAM_BASE 为起点。
     * 因此先从 DTB 找出当前内核物理地址窗口中最高的 RAM 末端。
     *
     * DTB 可以发现窗口以外的 RAM，但在不重构
     * nbase / 内核直接映射之前，不把它交给 PMM。
     */
    uintptr_t highest_end = DRAM_BASE;

    for (size_t i = 0; i < region_count; i++) {
        uint64_t base, size;

        if (get_memory_region(i, &base, &size) != 0 ||
            size == 0) {
            continue;
        }

        if (size > (uint64_t)-1 - base) {
            continue;
        }

        uint64_t end = base + size;

        /*
         * 与当前内核支持窗口求交集。
         */
        uint64_t clipped_begin =
            base < DRAM_BASE ? DRAM_BASE : base;

        uint64_t clipped_end =
            end > PHYSICAL_MEMORY_END
                ? PHYSICAL_MEMORY_END
                : end;

        if (clipped_begin >= clipped_end) {
            cprintf(
                "PMM skips RAM region %lu outside current kernel window\n",
                i
            );
            continue;
        }

        if (clipped_begin != base ||
            clipped_end != end) {
            cprintf(
                "PMM clips RAM region %lu to current kernel window\n",
                i
            );
        }

        if (clipped_end > highest_end) {
            highest_end = (uintptr_t)clipped_end;
        }
    }

    assert(highest_end > DRAM_BASE);

    /*
     * pages[] 覆盖 DRAM_BASE 到最高可管理物理页。
     * 中间如果有地址洞，其 Page 描述符保持 Reserved。
     */
    npage = ROUNDDOWN(highest_end, PGSIZE) / PGSIZE;

    assert(npage > nbase);

    extern char end[];

    pages = (struct Page *)ROUNDUP(
        (uintptr_t)end,
        PGSIZE
    );

    size_t page_count = npage - nbase;

    uintptr_t freemem = ROUNDUP(
        (uintptr_t)pages +
            page_count * sizeof(struct Page),
        PGSIZE
    );

    uintptr_t page_array_begin_pa =
        PADDR((uintptr_t)pages);

    uintptr_t free_start_pa =
        PADDR(freemem);

    /*
     * pages[] 自身必须位于真实 RAM 中，而且不能占用 DTB
     * 明确声明的 reserved-memory / memreserve 区域。
     */
    assert(
        range_inside_dtb_ram(
            page_array_begin_pa,
            free_start_pa
        )
    );

    assert(
        !range_overlaps_reserved(
            page_array_begin_pa,
            free_start_pa
        )
    );

    /*
     * 先把整个描述符跨度全部标为 Reserved。
     * 地址洞、reserved-memory 和内核占用区域因此默认都不会分配。
     */
    for (size_t i = 0; i < page_count; i++) {
        struct Page *p = &pages[i];

        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        SetPageReserved(p);
    }

    cprintf(
        "PMM physical memory regions from DTB: %lu\n",
        region_count
    );

    cprintf(
        "Page metadata: 0x%016lx - 0x%016lx\n",
        page_array_begin_pa,
        free_start_pa - 1
    );

    /*
     * 逐个处理 DTB RAM region。
     *
     * 只有满足：
     *   1. 位于当前内核可管理物理窗口；
     *   2. 位于 Page 描述符数组之后；
     *   3. 不属于 reserved region；
     *
     * 的物理页才真正交给 PMM。
     */
    size_t total_free = 0;

    for (size_t i = 0; i < region_count; i++) {
        uint64_t base64, size64;

        if (get_memory_region(
                i, &base64, &size64) != 0 ||
            size64 == 0) {
            continue;
        }

        if (size64 > (uint64_t)-1 - base64) {
            continue;
        }

        uint64_t end64 = base64 + size64;

        uintptr_t begin =
            base64 < DRAM_BASE
                ? DRAM_BASE
                : (uintptr_t)base64;

        uintptr_t end_pa =
            end64 > PHYSICAL_MEMORY_END
                ? PHYSICAL_MEMORY_END
                : (uintptr_t)end64;

        if (begin >= end_pa) {
            continue;
        }

        /*
         * 内核、Page 数组以及它们之前的空间均不作为空闲页。
         */
        if (begin < free_start_pa) {
            begin = free_start_pa;
        }

        if (begin >= end_pa) {
            continue;
        }

        cprintf(
            "RAM region %lu candidate: "
            "0x%016lx - 0x%016lx\n",
            i, begin, end_pa - 1
        );

        total_free +=
            init_ram_excluding_reserved(
                begin,
                end_pa
            );
    }

    assert(total_free > 0);

    cprintf(
        "Free physical pages from DTB regions: %lu\n",
        total_free
    );
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    /*
     * Lab2 Bonus: initialize the SLUB small-object allocator
     * after the underlying physical page allocator is ready.
     */
    slub_init();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
