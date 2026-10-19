#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
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
    pmm_manager = &default_pmm_manager;
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
static void page_init(void) {
    // 获取物理内存信息
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();

    // 检查设备树返回的内存信息是否合法
    assert(mem_base == DRAM_BASE);
    assert(mem_size > 0);
    assert(mem_size <= PHYSICAL_MEMORY_END - mem_base);

    // 初始化虚拟地址与物理地址偏移
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // 计算物理页号上界
    npage = (mem_base + mem_size) / PGSIZE;

    // 获取内核结束地址
    extern char end[];

    // 设置物理页描述符数组的起始位置
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);

    // 计算需要管理的物理页数量
    size_t page_count = npage - nbase;

    // 计算 Page 数组结束后的第一个页对齐虚拟地址
    uintptr_t freemem = ROUNDUP(
    (uintptr_t)pages + page_count * sizeof(struct Page),
    PGSIZE
    );

    // 初始化所有物理页描述符，先将它们标记为保留状态
    for (size_t i = 0; i < page_count; i++) {
        struct Page *p = &pages[i];

        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        SetPageReserved(p);
    }

    // 将可用内存起始位置转换为物理地址
    uintptr_t free_start_pa = PADDR(freemem);

    // 计算物理内存结束地址
    uintptr_t mem_end_pa = mem_base + mem_size;

    // 确保空闲内存范围合法
    assert(free_start_pa >= mem_base);
    assert(free_start_pa < mem_end_pa);

    // 将物理内存结束地址向下对齐到页边界
    uintptr_t free_end_pa = ROUNDDOWN(mem_end_pa, PGSIZE);

    // 确保至少存在一个可用物理页
    assert(free_start_pa < free_end_pa);

    // 计算可用物理页数量
    size_t nfree = (free_end_pa - free_start_pa) / PGSIZE;

    // 将可用物理页交给 First-Fit 分配器管理
    init_memmap(pa2page(free_start_pa), nfree);

    // 输出空闲页数量，方便调试
    cprintf("Free physical pages: %lu\n", nfree);

    // 输出调试信息
    cprintf("Memory base: 0x%016lx\n", mem_base);
    cprintf("Memory size: 0x%016lx\n", mem_size);
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

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
