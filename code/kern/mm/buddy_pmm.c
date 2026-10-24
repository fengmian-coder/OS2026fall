#include <buddy_pmm.h>
#include <list.h>
#include <pmm.h>

#define BUDDY_MAX_ORDER 15

static list_entry_t buddy_free_lists[BUDDY_MAX_ORDER + 1];
static size_t buddy_nr_free;

static struct Page *buddy_managed_base;
static struct Page *buddy_managed_end;

/* 返回某个 order 对应的页数，例如 order=3 -> 8 pages */
static size_t
buddy_order_pages(unsigned int order) {
    return ((size_t)1) << order;
}

/* 把申请的页数向上取整到最小的 2 的幂，并返回对应 order */
static unsigned int
buddy_order_for_pages(size_t n) {
    unsigned int order = 0;
    size_t pages = 1;

    while (pages < n && order < BUDDY_MAX_ORDER) {
        pages <<= 1;
        order ++;
    }

    if (pages < n) {
        return BUDDY_MAX_ORDER + 1;
    }
    return order;
}

/* 将一个空闲块加入指定 order 的空闲链表 */
static void
buddy_add_block(struct Page *base, unsigned int order) {
    size_t block_pages = buddy_order_pages(order);

    base->property = block_pages;
    SetPageProperty(base);

    list_add(&buddy_free_lists[order], &(base->page_link));
}

/* 从指定 order 的空闲链表中删除一个块 */
static void
buddy_remove_block(struct Page *base) {
    list_del(&(base->page_link));
    ClearPageProperty(base);
}

/*
 * 判断一个块在当前 order 下的 buddy。
 *
 * Page 在 pages 数组中的下标可以看成物理页编号相对于
 * DRAM 起点的偏移。对于大小为 2^order 的块，
 * 将对应 order 位翻转即可得到 buddy 的起始下标。
 */
static struct Page *
buddy_find_buddy(struct Page *base, unsigned int order) {
    size_t block_pages = buddy_order_pages(order);
    size_t index = base - pages;
    size_t buddy_index = index ^ block_pages;

    struct Page *buddy = pages + buddy_index;

    if (buddy < buddy_managed_base) {
        return NULL;
    }
    if (buddy + block_pages > buddy_managed_end) {
        return NULL;
    }

    return buddy;
}

/* 初始化 Buddy 内部数据结构 */
static void
buddy_init(void) {
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order ++) {
        list_init(&buddy_free_lists[order]);
    }

    buddy_nr_free = 0;
    buddy_managed_base = NULL;
    buddy_managed_end = NULL;
}

/*
 * 找到从 base 开始、既不超过 n 页又满足地址对齐要求的
 * 最大 2 的幂块。
 */
static unsigned int
buddy_largest_init_order(struct Page *base, size_t n) {
    unsigned int order = 0;

    while (order < BUDDY_MAX_ORDER) {
        size_t next_pages = buddy_order_pages(order + 1);
        size_t index = base - pages;

        if (next_pages > n) {
            break;
        }

        /*
         * Buddy 块必须按照自己的块大小对齐。
         * 例如 8 页块的起点下标必须能被 8 整除。
         */
        if ((index % next_pages) != 0) {
            break;
        }

        order ++;
    }

    return order;
}

/*
 * 接管 page_init() 提供的一段连续可用物理页。
 * 这段长度 n 不一定本身就是 2 的幂，所以需要拆成
 * 若干个合法、对齐的 Buddy 块。
 */
static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(base != NULL);
    assert(n > 0);

    buddy_managed_base = base;
    buddy_managed_end = base + n;

    /* 先清理这一整段 Page 描述符的旧状态 */
    for (struct Page *p = base; p != base + n; p ++) {
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&(p->page_link));
    }

    struct Page *current = base;
    size_t remain = n;

    /*
     * 每次取当前位置能够容纳的最大、并且正确对齐的
     * 2 的幂块，直到把整个区域划分完。
     */
    while (remain > 0) {
        unsigned int order = buddy_largest_init_order(current, remain);
        size_t block_pages = buddy_order_pages(order);

        buddy_add_block(current, order);

        buddy_nr_free += block_pages;
        current += block_pages;
        remain -= block_pages;
    }

    assert(buddy_nr_free == n);
}

/*
 * 分配至少 n 页。
 * Buddy 必须实际分配能够容纳 n 的最小 2 的幂块。
 */
static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);

    unsigned int target_order = buddy_order_for_pages(n);

    if (target_order > BUDDY_MAX_ORDER) {
        return NULL;
    }

    size_t target_pages = buddy_order_pages(target_order);

    if (target_pages > buddy_nr_free) {
        return NULL;
    }

    /*
     * 从目标 order 开始向上寻找第一个非空链表。
     */
    unsigned int current_order = target_order;

    while (current_order <= BUDDY_MAX_ORDER &&
           list_empty(&buddy_free_lists[current_order])) {
        current_order ++;
    }

    if (current_order > BUDDY_MAX_ORDER) {
        return NULL;
    }

    /*
     * 从找到的高阶空闲链表中取出第一个块。
     */
    list_entry_t *le = list_next(&buddy_free_lists[current_order]);
    struct Page *block = le2page(le, page_link);

    buddy_remove_block(block);

    /*
     * 如果块太大，就逐级二分。
     *
     * 每次：
     *     [一个大块]
     *        ↓
     * [左半] [右半]
     *
     * 左半继续拆或最终分配；
     * 右半放回低一阶空闲链表。
     */
    while (current_order > target_order) {
        current_order --;

        size_t half_pages = buddy_order_pages(current_order);
        struct Page *right_half = block + half_pages;

        buddy_add_block(right_half, current_order);
    }

    /*
     * property 保留实际分配块的大小，方便 free_pages()
     * 根据原始申请 n 找回 Buddy 实际分配的 2 的幂块。
     *
     * 但该块已经不是空闲块，所以清除 PG_property。
     */
    block->property = target_pages;
    ClearPageProperty(block);

    buddy_nr_free -= target_pages;

    return block;
}

/*
 * 释放一块 Buddy 分配得到的内存。
 */
static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(base != NULL);
    assert(n > 0);
    assert(base >= buddy_managed_base && base < buddy_managed_end);
    assert(!PageReserved(base));
    assert(!PageProperty(base));

    unsigned int order = buddy_order_for_pages(n);

    assert(order <= BUDDY_MAX_ORDER);

    size_t block_pages = buddy_order_pages(order);

    /*
     * alloc_pages() 会把实际分配的 2 的幂页数保存在
     * 起始 Page 的 property 中。
     */
    assert(base->property == block_pages);

    buddy_nr_free += block_pages;

    struct Page *block = base;

    /*
     * 不断尝试和同阶 buddy 合并。
     */
    while (order < BUDDY_MAX_ORDER) {
        struct Page *buddy = buddy_find_buddy(block, order);

        if (buddy == NULL) {
            break;
        }

        /*
         * buddy 必须：
         * 1. 当前是一个空闲块头；
         * 2. 大小与当前块完全相同。
         */
        if (!PageProperty(buddy)) {
            break;
        }

        if (buddy->property != block_pages) {
            break;
        }

        /*
         * buddy 已经包含在 buddy_nr_free 中，
         * 合并只改变组织形式，因此不能再次增加空闲页数。
         */
        buddy_remove_block(buddy);

        if (buddy < block) {
            block = buddy;
        }

        order ++;
        block_pages <<= 1;
    }

    block->property = block_pages;
    SetPageProperty(block);
    list_add(&buddy_free_lists[order], &(block->page_link));
}

/* 返回当前空闲物理页总数 */
static size_t
buddy_nr_free_pages(void) {
    return buddy_nr_free;
}

/*
 * 全面的 Buddy 测试将在 Prompt 2 中实现。
 * Prompt 1 阶段只保留 pmm_manager 所要求的 check 接口。
 */
/* 统计指定 order 当前有多少个空闲块 */
static size_t
buddy_count_blocks(unsigned int order) {
    assert(order <= BUDDY_MAX_ORDER);

    size_t count = 0;
    list_entry_t *head = &buddy_free_lists[order];
    list_entry_t *le = head;

    while ((le = list_next(le)) != head) {
        count ++;
    }

    return count;
}

static void
buddy_check(void) {
    size_t nr_free_store = buddy_nr_free_pages();

    /*
     * 保存测试前每个 order 的空闲块数量。
     * 测试结束后必须完全恢复。
     */
    size_t block_count_store[BUDDY_MAX_ORDER + 1];

    for (unsigned int order = 0;
         order <= BUDDY_MAX_ORDER;
         order ++) {
        block_count_store[order] = buddy_count_blocks(order);
    }

    assert(nr_free_store >= 13);

    /*
     * 边界测试：
     * 大于 Buddy 最大块大小的请求必须失败，
     * 并且不能改变空闲页数量。
     */
    size_t too_large =
        buddy_order_pages(BUDDY_MAX_ORDER) + 1;

    assert(buddy_alloc_pages(too_large) == NULL);
    assert(buddy_nr_free_pages() == nr_free_store);

    /*
     * 测试普通申请和非 2 的幂申请。
     *
     * 1 -> 1 page
     * 3 -> 4 pages
     * 5 -> 8 pages
     */
    struct Page *p1 = buddy_alloc_pages(1);
    struct Page *p3 = buddy_alloc_pages(3);
    struct Page *p5 = buddy_alloc_pages(5);

    assert(p1 != NULL);
    assert(p3 != NULL);
    assert(p5 != NULL);

    assert(p1 != p3);
    assert(p1 != p5);
    assert(p3 != p5);

    assert(p1->property == 1);
    assert(p3->property == 4);
    assert(p5->property == 8);

    assert(!PageProperty(p1));
    assert(!PageProperty(p3));
    assert(!PageProperty(p5));

    /*
     * 检查三个实际分配块之间没有重叠。
     */
    assert(p1 + 1 <= p3 || p3 + 4 <= p1);
    assert(p1 + 1 <= p5 || p5 + 8 <= p1);
    assert(p3 + 4 <= p5 || p5 + 8 <= p3);

    /*
     * 实际消耗：
     * 1 + 4 + 8 = 13 pages
     */
    assert(buddy_nr_free_pages() ==
           nr_free_store - 13);

    /*
     * 故意用不同顺序释放，
     * 检查释放顺序不会破坏 Buddy 合并逻辑。
     */
    buddy_free_pages(p3, 3);
    assert(buddy_nr_free_pages() ==
           nr_free_store - 9);

    buddy_free_pages(p1, 1);
    assert(buddy_nr_free_pages() ==
           nr_free_store - 8);

    buddy_free_pages(p5, 5);

    /*
     * 所有内存释放后，总空闲页必须恢复。
     */
    assert(buddy_nr_free_pages() ==
           nr_free_store);

    /*
     * 每个 order 的空闲块数量也应该恢复到测试前。
     * 如果拆分出的 buddy 没有正确合并，这里会失败。
     */
    for (unsigned int order = 0;
         order <= BUDDY_MAX_ORDER;
         order ++) {
        assert(buddy_count_blocks(order) ==
               block_count_store[order]);
    }
}

/* 向 uCore 的统一 PMM 框架注册 Buddy System */
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
