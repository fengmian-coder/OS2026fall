#include <dtb.h>
#include <stdio.h>
#include <string.h>
#include <defs.h>
#include <memlayout.h>

// 设备树头部结构
struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

// FDT token
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

static uint32_t
fdt32_to_cpu(uint32_t x) {
    return ((x & 0xff) << 24) |
           (((x >> 8) & 0xff) << 16) |
           (((x >> 16) & 0xff) << 8) |
           ((x >> 24) & 0xff);
}

static uint64_t
fdt64_to_cpu(uint64_t x) {
    return ((uint64_t)fdt32_to_cpu((uint32_t)(x & 0xffffffff)) << 32) |
           fdt32_to_cpu((uint32_t)(x >> 32));
}

/* Lab2 兼容接口保存第一段 RAM */
static uint64_t memory_base = 0;
static uint64_t memory_size = 0;

/* Challenge: 保存多个物理内存区间 */
static struct dtb_memory_region
    memory_regions[DTB_MAX_MEMORY_REGIONS];

static size_t memory_region_count = 0;

/* FDT memreserve / reserved-memory */
static struct dtb_memory_region
    reserved_regions[DTB_MAX_RESERVED_REGIONS];

static size_t reserved_region_count = 0;

static void
add_memory_region(uint64_t base, uint64_t size) {
    if (size == 0) {
        return;
    }

    if (memory_region_count >= DTB_MAX_MEMORY_REGIONS) {
        cprintf("Warning: too many DTB memory regions\n");
        return;
    }

    memory_regions[memory_region_count].base = base;
    memory_regions[memory_region_count].size = size;
    memory_region_count++;
}

static void
add_reserved_region(uint64_t base, uint64_t size) {
    if (size == 0) {
        return;
    }

    if (reserved_region_count >= DTB_MAX_RESERVED_REGIONS) {
        cprintf("Warning: too many DTB reserved regions\n");
        return;
    }

    reserved_regions[reserved_region_count].base = base;
    reserved_regions[reserved_region_count].size = size;
    reserved_region_count++;
}

/*
 * DTB 的地址和长度由若干个 32-bit cell 组成。
 * 本实验使用 uint64_t，因此最多支持 2 个 cell。
 */
static uint64_t
read_cells(const uint32_t *cells, uint32_t count) {
    uint64_t value = 0;

    for (uint32_t i = 0; i < count; i++) {
        value = (value << 32) | fdt32_to_cpu(cells[i]);
    }

    return value;
}

static void
parse_reg_property(const uint32_t *data, uint32_t prop_len,
                   uint32_t address_cells, uint32_t size_cells,
                   int reserved) {
    if (address_cells == 0 || address_cells > 2 ||
        size_cells == 0 || size_cells > 2) {
        cprintf("Warning: unsupported DTB address/size cell count\n");
        return;
    }

    uint32_t tuple_cells = address_cells + size_cells;
    uint32_t tuple_bytes = tuple_cells * sizeof(uint32_t);

    if (tuple_bytes == 0 || prop_len < tuple_bytes) {
        return;
    }

    uint32_t count = prop_len / tuple_bytes;

    for (uint32_t i = 0; i < count; i++) {
        const uint32_t *tuple = data + i * tuple_cells;

        uint64_t base = read_cells(tuple, address_cells);
        uint64_t size = read_cells(tuple + address_cells, size_cells);

        if (reserved) {
            add_reserved_region(base, size);
        } else {
            add_memory_region(base, size);
        }
    }
}

/*
 * 读取 FDT header 中的 memory reservation block。
 * 每个条目由 address + size 两个 64-bit 大端整数组成，
 * 以 {0, 0} 结束。
 */
static void
extract_memreserve(uintptr_t dtb_vaddr,
                   const struct fdt_header *header) {
    uint32_t offset = fdt32_to_cpu(header->off_mem_rsvmap);
    const uint64_t *entry =
        (const uint64_t *)(dtb_vaddr + offset);

    while (1) {
        uint64_t base = fdt64_to_cpu(entry[0]);
        uint64_t size = fdt64_to_cpu(entry[1]);

        if (base == 0 && size == 0) {
            break;
        }

        add_reserved_region(base, size);
        entry += 2;
    }
}

/*
 * Challenge:
 * 枚举所有 memory 节点，而不是找到第一段内存就立即返回。
 *
 * 同时解析：
 *   - 根节点 #address-cells / #size-cells
 *   - memory@... 节点中的所有 reg tuple
 *   - reserved-memory 子节点中的 reg
 */
static int
extract_memory_regions(uintptr_t dtb_vaddr,
                       const struct fdt_header *header) {
    uint32_t struct_offset =
        fdt32_to_cpu(header->off_dt_struct);
    uint32_t strings_offset =
        fdt32_to_cpu(header->off_dt_strings);

    const char *strings_base =
        (const char *)(dtb_vaddr + strings_offset);

    const uint32_t *struct_ptr =
        (const uint32_t *)(dtb_vaddr + struct_offset);

    int depth = -1;
    int memory_node_depth = -1;
    int reserved_root_depth = -1;
    int reserved_child_depth = -1;

    /* RISC-V/QEMU 常见默认值为 2/2 */
    uint32_t root_address_cells = 2;
    uint32_t root_size_cells = 2;

    uint32_t reserved_address_cells = 2;
    uint32_t reserved_size_cells = 2;

    while (1) {
        uint32_t token = fdt32_to_cpu(*struct_ptr++);

        switch (token) {
        case FDT_BEGIN_NODE: {
            const char *name = (const char *)struct_ptr;
            int name_len = strlen(name);

            depth++;

            /*
             * memory 或 memory@xxxxxxxx
             * 这里不能再像原实现一样找到第一项就返回。
             */
            if (depth == 1 &&
                (strcmp(name, "memory") == 0 ||
                 strncmp(name, "memory@", 7) == 0)) {
                memory_node_depth = depth;
            }

            if (depth == 1 &&
                strcmp(name, "reserved-memory") == 0) {
                reserved_root_depth = depth;
                reserved_address_cells = root_address_cells;
                reserved_size_cells = root_size_cells;
            } else if (reserved_root_depth >= 0 &&
                       depth == reserved_root_depth + 1) {
                reserved_child_depth = depth;
            }

            /* node name 按 4 字节对齐 */
            struct_ptr = (const uint32_t *)(
                ((uintptr_t)struct_ptr + name_len + 4) & ~3UL
            );
            break;
        }

        case FDT_END_NODE:
            if (depth == memory_node_depth) {
                memory_node_depth = -1;
            }

            if (depth == reserved_child_depth) {
                reserved_child_depth = -1;
            }

            if (depth == reserved_root_depth) {
                reserved_root_depth = -1;
            }

            depth--;
            break;

        case FDT_PROP: {
            uint32_t prop_len =
                fdt32_to_cpu(*struct_ptr++);

            uint32_t prop_nameoff =
                fdt32_to_cpu(*struct_ptr++);

            const char *prop_name =
                strings_base + prop_nameoff;

            const uint32_t *prop_data = struct_ptr;

            /*
             * 根节点定义了其直接子节点 reg 的编码方式。
             */
            if (depth == 0 &&
                strcmp(prop_name, "#address-cells") == 0 &&
                prop_len >= 4) {
                root_address_cells =
                    fdt32_to_cpu(prop_data[0]);
            }

            if (depth == 0 &&
                strcmp(prop_name, "#size-cells") == 0 &&
                prop_len >= 4) {
                root_size_cells =
                    fdt32_to_cpu(prop_data[0]);
            }

            /*
             * reserved-memory 可以覆盖自己的 cell 配置。
             */
            if (depth == reserved_root_depth &&
                strcmp(prop_name, "#address-cells") == 0 &&
                prop_len >= 4) {
                reserved_address_cells =
                    fdt32_to_cpu(prop_data[0]);
            }

            if (depth == reserved_root_depth &&
                strcmp(prop_name, "#size-cells") == 0 &&
                prop_len >= 4) {
                reserved_size_cells =
                    fdt32_to_cpu(prop_data[0]);
            }

            /*
             * 一个 memory 节点的 reg 可以包含多个
             * <base, size> tuple。
             */
            if (depth == memory_node_depth &&
                strcmp(prop_name, "reg") == 0) {
                parse_reg_property(
                    prop_data,
                    prop_len,
                    root_address_cells,
                    root_size_cells,
                    0
                );
            }

            /*
             * reserved-memory 的每个子节点都可能有 reg。
             */
            if (depth == reserved_child_depth &&
                strcmp(prop_name, "reg") == 0) {
                parse_reg_property(
                    prop_data,
                    prop_len,
                    reserved_address_cells,
                    reserved_size_cells,
                    1
                );
            }

            struct_ptr = (const uint32_t *)(
                ((uintptr_t)struct_ptr + prop_len + 3) & ~3UL
            );
            break;
        }

        case FDT_NOP:
            break;

        case FDT_END:
            return memory_region_count > 0 ? 0 : -1;

        default:
            return -1;
        }
    }
}

void
dtb_init(void) {
    cprintf("DTB Init\n");
    cprintf("HartID: %ld\n", boot_hartid);
    cprintf("DTB Address: 0x%lx\n", boot_dtb);

    memory_base = 0;
    memory_size = 0;
    memory_region_count = 0;
    reserved_region_count = 0;

    if (boot_dtb == 0) {
        cprintf("Error: DTB address is null\n");
        return;
    }

    uintptr_t dtb_vaddr =
        boot_dtb + PHYSICAL_MEMORY_OFFSET;

    const struct fdt_header *header =
        (const struct fdt_header *)dtb_vaddr;

    uint32_t magic = fdt32_to_cpu(header->magic);

    if (magic != 0xd00dfeed) {
        cprintf(
            "Error: Invalid DTB magic number: 0x%x\n",
            magic
        );
        return;
    }

    /*
     * Challenge:
     * 先解析 FDT reservation block，
     * 再解析结构树中的 memory / reserved-memory。
     */
    extract_memreserve(dtb_vaddr, header);

    if (extract_memory_regions(dtb_vaddr, header) != 0) {
        cprintf(
            "Warning: Could not extract memory regions from DTB\n"
        );
        return;
    }

    /*
     * 保留原 Lab2 API：
     * 第一段 RAM 继续作为 get_memory_base/size 的返回值。
     */
    memory_base = memory_regions[0].base;
    memory_size = memory_regions[0].size;

    cprintf(
        "Physical Memory Regions from DTB: %lu\n",
        memory_region_count
    );

    for (size_t i = 0; i < memory_region_count; i++) {
        uint64_t base = memory_regions[i].base;
        uint64_t size = memory_regions[i].size;

        cprintf(
            "  Region %lu: 0x%016lx - 0x%016lx (%lu MB)\n",
            i,
            base,
            base + size - 1,
            size / (1024 * 1024)
        );
    }

    if (reserved_region_count > 0) {
        cprintf(
            "Reserved Memory Regions from DTB: %lu\n",
            reserved_region_count
        );

        for (size_t i = 0;
             i < reserved_region_count;
             i++) {
            uint64_t base = reserved_regions[i].base;
            uint64_t size = reserved_regions[i].size;

            cprintf(
                "  Reserved %lu: 0x%016lx - 0x%016lx\n",
                i,
                base,
                base + size - 1
            );
        }
    }

    cprintf("DTB init completed\n");
}

uint64_t
get_memory_base(void) {
    return memory_base;
}

uint64_t
get_memory_size(void) {
    return memory_size;
}

size_t
get_memory_region_count(void) {
    return memory_region_count;
}

int
get_memory_region(size_t index,
                  uint64_t *base,
                  uint64_t *size) {
    if (index >= memory_region_count ||
        base == NULL ||
        size == NULL) {
        return -1;
    }

    *base = memory_regions[index].base;
    *size = memory_regions[index].size;
    return 0;
}

size_t
get_reserved_region_count(void) {
    return reserved_region_count;
}

int
get_reserved_region(size_t index,
                    uint64_t *base,
                    uint64_t *size) {
    if (index >= reserved_region_count ||
        base == NULL ||
        size == NULL) {
        return -1;
    }

    *base = reserved_regions[index].base;
    *size = reserved_regions[index].size;
    return 0;
}
