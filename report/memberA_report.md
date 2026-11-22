# 操作系统实验四：虚拟内存页操作实验报告

> **课程**：操作系统实验（2026 秋）  
> **实验名称**：Lab4 进程管理  
> **成员**：张馨月  
> **学号**：2414029  
> **小组分工**：成员 A——虚拟内存页操作与底层兼容  
> **项目仓库**：[fengmian-coder/OS2026fall](https://github.com/fengmian-coder/OS2026fall)  
> **开发分支**：`lab4-A-vmm`  
> **最终集成分支**：`lab4`  
> **实验日期**：2026 年 11 月

---

## 一、实验目的与任务分析

本次实验在 Lab2 物理内存管理和 Lab3 中断异常处理的基础上，进一步实现虚拟内存页映射、内核线程创建和进程调度。

小组三人的任务分工如下：

| 成员 | 功能模块 | 主要任务 |
|---|---|---|
| A | 虚拟内存页操作 | 实现页映射与删除，维护引用计数，检查页表和底层兼容 |
| B | 进程创建与初始化 | 实现 PCB 初始化、内核线程创建和资源管理 |
| C | 进程调度 | 实现调度器、进程切换和汇编上下文恢复 |

本人负责功能模块一，主要实现以下两个函数：

```c
int page_insert(pde_t *pgdir, struct Page *page,
                uintptr_t la, uint32_t perm);

void page_remove(pde_t *pgdir, uintptr_t la);
```

这两个函数负责将物理页映射到指定虚拟地址，以及安全删除已有的映射。

本模块的核心目标是保证三个方面的一致性：

1. 页表项 PTE 正确记录虚拟地址到物理地址的映射。
2. 物理页引用计数正确反映当前映射数量。
3. 修改页表后及时刷新对应的 TLB 地址翻译缓存。

此外，由于 Lab4 依赖前两次实验，还需要保留此前已经实现的物理内存初始化功能，解决不同实验代码之间的接口兼容问题。

## 二、实验整体逻辑与实现顺序

### 2.1 实验整体调用流程

操作系统启动后，主要执行流程如下：

```text
OpenSBI
   |
   v
kern_entry
   |
   v
kern_init
   |
   +-- pmm_init()          物理内存和页表初始化
   |      |
   |      +-- init_pmm_manager()
   |      +-- page_init()
   |      +-- check_alloc_page()
   |      +-- check_pgdir()
   |      +-- check_boot_pgdir()
   |      +-- kmalloc_init()
   |
   +-- pic_init()
   +-- idt_init()
   +-- vmm_init()
   |
   +-- proc_init()         成员 B
   |
   +-- cpu_idle()          成员 C
           |
           v
        schedule()
           |
           v
        proc_run()
           |
           v
        switch_to()
           |
           v
        initproc 执行
```

我负责的虚拟内存页操作位于整个执行流程的前半部分。

只有先完成物理内存管理和页表操作，后续创建内核线程时才能安全分配内核栈、管理内存并进行地址访问。

### 2.2 本人实际实现顺序

本人的开发按照以下顺序进行：

1. 阅读老师的 Lab4 指导书和原始代码框架。
2. 检查 `pmm.c`、`pmm.h`、`mmu.h` 等相关文件。
3. 将此前 Lab3 已实现的 DTB 物理内存初始化迁移至 Lab4。
4. 实现 `page_insert()`、`page_remove()` 和内部辅助函数。
5. 修复头文件搜索路径、类型定义和函数参数兼容问题。
6. 排查编译、链接过程中出现的错误。
7. 等待 B、C 完成后进行整体集成测试。
8. 检查 Sv39 启动页表和 `check_boot_pgdir()` 的潜在问题。
9. 修复高地址检查逻辑，重新运行编译、QEMU 和自动评分。

## 三、核心函数设计与实现

### 3.1 page_insert()：建立页映射

**功能要求（SPEC）**

在给定页目录 `pgdir` 中，将线性地址 `la` 映射到物理页 `page`，同时根据 `perm` 设置相应权限。

函数应满足以下条件：

- 首次映射时，建立新的有效页表项。
- 如果原地址已经映射到同一个物理页，不重复增加引用计数。
- 如果原地址映射到其他物理页，正确撤销旧映射。
- 成功时返回 `0`。
- 页表分配失败时返回 `-E_NO_MEM`。
- 更新映射后刷新相关 TLB 项。

**核心代码**

```c
int page_insert(pde_t *pgdir, struct Page *page,
                uintptr_t la, uint32_t perm)
{
    pte_t *ptep = get_pte(pgdir, la, 1);

    if (ptep == NULL) {
        return -E_NO_MEM;
    }

    page_ref_inc(page);

    if (*ptep & PTE_V) {
        struct Page *old_page = pte2page(*ptep);

        if (old_page == page) {
            page_ref_dec(page);
        }
        else {
            page_remove_pte(pgdir, la, ptep);
        }
    }

    *ptep = pte_create(page2ppn(page), PTE_V | perm);

    tlb_invalidate(pgdir, la);

    return 0;
}
```

**实现分析**

首先通过 `get_pte()` 查找目标虚拟地址对应的页表项。如果所需的中间页表不存在，系统会尝试分配新的页表页。

如果申请失败，返回 `-E_NO_MEM`，避免继续访问无效的页表项。

获得 PTE 后，需要增加目标物理页的引用计数。

此时有两种特殊情况：

第一种是新旧映射指向同一个物理页。这种情况只需要更新权限，不应该增加实际引用数量，因此撤销刚才增加的一次引用计数。

第二种是新旧映射指向不同的物理页。需要先删除旧映射，再建立新映射，防止旧物理页的引用计数错误。

最后重新写入 PTE，并通过 `tlb_invalidate()` 刷新对应地址的 TLB。

### 3.2 page_remove_pte()：删除有效页表项

**功能要求（SPEC）**

删除指定页表项所对应的物理页映射，并维护物理页引用计数。

**核心代码**

```c
static void page_remove_pte(pde_t *pgdir,
                            uintptr_t la,
                            pte_t *ptep)
{
    if (*ptep & PTE_V) {
        struct Page *page = pte2page(*ptep);

        page_ref_dec(page);

        if (page_ref(page) == 0) {
            free_page(page);
        }

        *ptep = 0;

        tlb_invalidate(pgdir, la);
    }
}
```

**实现分析**

该函数先通过 `PTE_V` 判断页表项是否有效。

如果存在映射，就通过 `pte2page()` 找到对应的物理页，随后将引用计数减一。

只有引用计数下降到零时，才能通过 `free_page()` 归还该物理页。

最后清空 PTE，并刷新对应 TLB 项。

这里的关键是：不能仅因为删除了一个虚拟地址的映射，就直接释放物理页，因为同一物理页可能同时被多个虚拟地址引用。

### 3.3 page_remove()：撤销虚拟地址映射

**功能要求（SPEC）**

删除指定线性地址的映射。若该地址没有映射，应安全返回，不发生异常，也不申请新页表。

**核心代码**

```c
void page_remove(pde_t *pgdir, uintptr_t la)
{
    pte_t *ptep = get_pte(pgdir, la, 0);

    if (ptep != NULL) {
        page_remove_pte(pgdir, la, ptep);
    }
}
```

**实现分析**

`get_pte(pgdir, la, 0)` 中的第三个参数为 `0`，表示只查询现有页表，不创建新的页表。

若目标页表项不存在，直接返回。

若页表项存在，则交由 `page_remove_pte()` 处理。

这种设计可以保证对同一个未映射地址重复调用删除操作时不会产生额外的物理页释放。

### 3.4 相关辅助接口

| 接口 | 主要作用 |
|---|---|
| `get_pte()` | 定位或创建虚拟地址对应的页表项 |
| `get_page()` | 查找虚拟地址当前映射的物理页 |
| `pte2page()` | 将 PTE 转换为物理页描述结构 |
| `page_ref_inc()` | 增加物理页引用计数 |
| `page_ref_dec()` | 减少物理页引用计数 |
| `free_page()` | 释放不再被引用的物理页 |
| `tlb_invalidate()` | 使对应虚拟地址的 TLB 项失效 |

这些接口共同保证页表修改、物理页管理以及地址翻译状态之间的一致性。

## 四、Lab2/Lab3 代码迁移与兼容处理

由于老师提供的 Lab4 框架并不是我们 Lab3 完整代码的直接延续，因此不能简单覆盖整个工程。

本人主要处理了以下兼容工作。

### 4.1 恢复 DTB 物理内存初始化

保留 Lab4 原有的 `pmm_init()`、`get_pte()`、`get_page()` 等框架函数，将 Lab3 中已实现的 `page_init()` 按需迁入。

其主要过程为：

1. 使用 `get_memory_base()` 读取物理内存起始地址。
2. 使用 `get_memory_size()` 获取实际内存大小。
3. 计算物理页总数。
4. 为 `struct Page` 描述符数组预留空间。
5. 标记内核和描述符数组已占用的内存。
6. 将剩余物理页加入空闲内存管理结构。

这样使 Lab4 能继续使用此前完成的物理内存管理基础。

### 4.2 头文件和类型兼容

编译过程中出现过：

```text
fatal error: kdebug.h: No such file or directory
```

检查后发现 Makefile 的内核头文件搜索路径不完整。

通过补充 `kern/debug/`，解决了该问题。

随后又出现：

```text
unknown type name 'uint_t'
```

通过对比老师的 Lab4 版本与当前文件，调整 `libs/defs.h`，解决了相关类型定义问题。

### 4.3 kmalloc.c 参数类型问题

编译时出现：

```text
passing argument 1 of 'kva2page'
makes pointer from integer without a cast
```

原因是 `kva2page()` 接收 `void *`，而调用处传入的 `kva` 为 `unsigned long`。

修复前：

```c
free_pages(kva2page(kva), 1 << order);
```

修复后：

```c
free_pages(kva2page((void *)kva), 1 << order);
```

通过显式类型转换解决了编译器的参数类型检查问题。

### 4.4 链接阶段问题

后续链接时出现：

```text
multiple definition of 'readline'
undefined reference to 'proc_init'
undefined reference to 'cpu_idle'
```

其中，`readline` 重复定义来自 Lab3 遗留文件和 Lab4 新增文件同时参与编译，通过移除旧版重复文件解决。

而 `proc_init()` 和 `cpu_idle()` 分别属于 B、C 成员负责的函数。当时尚未完成，因此未在 A 模块添加临时空函数，而是保留接口等待后续集成。

这说明源文件编译通过和最终内核链接成功属于不同的验证阶段。

## 五、Sv39 启动页表异常与专项修复

### 5.1 实际运行中出现的现象

小组集成时，曾遇到 QEMU 启动后仅显示 OpenSBI 信息、未正常进入 uCore 的情况。

GDB 调试也曾观察到 `kern_init()` 断点再次命中，回溯涉及 `check_boot_pgdir()` 和 `pmm_init()`。

由于当时没有完整取得 `scause`、`sepc`、`stval` 等寄存器信息，因此不能直接把异常原因确定为某一处页表错误。

在后续检查中，重点分析了 Sv39 启动页表、虚拟地址宽度以及页表测试代码。

### 5.2 原 check_boot_pgdir() 存在的问题

原代码使用：

```c
int i;

for (i = ROUNDDOWN(KERNBASE, PGSIZE);
     i < npage * PGSIZE;
     i += PGSIZE)
{
    assert((ptep = get_pte(
        boot_pgdir_va,
        (uintptr_t)KADDR(i),
        0)) != NULL);

    assert(PTE_ADDR(*ptep) == i);
}
```

这里有两个需要解决的问题。

**问题一：地址类型不正确。**

本实验使用 RISC-V 64 位架构，而 `KERNBASE` 是高半区内核虚拟地址：

```text
0xFFFFFFFFC0200000
```

`int` 通常只有 32 位，不能正确保存这个地址。

因此原来的循环变量可能发生截断，使地址检查无法按预期进行。

**问题二：页表检查方式不适用于启动时的大页映射。**

`entry.S` 中的初始 Sv39 页表使用 1 GiB 叶子映射。

而现有 `get_pte()` 主要按照三级页表、4 KiB 叶子项的方式逐级查找，没有专门处理中间级已经是叶子项的情况。

如果直接把大页叶子项当成下一级页表继续访问，可能造成错误。

### 5.3 修复思路

本次修改采用 `uintptr_t` 类型保存物理地址，从 `DRAM_BASE` 开始遍历实际物理内存范围。

对于每一个物理页：

1. 将物理地址转换为对应的高虚拟地址。
2. 根据高虚拟地址取得 Sv39 根页表项。
3. 检查根页表项是否具有有效的大页叶子映射权限。
4. 根据叶子项中的物理基址和虚拟地址偏移计算实际物理地址。
5. 检查计算结果是否等于预期物理地址。

**修复后的核心检查代码：**

```c
size_t nr_free_store;
uintptr_t pa;

nr_free_store = nr_free_pages();

for (pa = DRAM_BASE;
     pa < npage * PGSIZE;
     pa += PGSIZE)
{
    uintptr_t va = (uintptr_t)KADDR(pa);
    pde_t pde = boot_pgdir_va[PDX1(va)];

    assert((pde & (PTE_V | PTE_R | PTE_W | PTE_X)) ==
           (PTE_V | PTE_R | PTE_W | PTE_X));

    assert(PTE_ADDR(pde) +
           (va & ((1UL << PDX1SHIFT) - 1)) == pa);
}
```

该代码针对当前启动阶段使用的 1 GiB Sv39 映射进行检查，并不代表已经支持任意形式的大页页表遍历。

同时保留原函数后续对低地址 `0x100` 的映射、别名访问和释放验证。

### 5.4 修复结果

完成修改后，在 WSL 中重新执行：

```bash
make -C code clean
make -C code
make -C code qemu
make -C code grade
```

本人实际运行后确认：

- 内核编译和链接成功。
- QEMU 能够正常执行实验启动流程。
- `check_boot_pgdir()` 检查通过。
- 自动评分仍然为 **30/30**。

因此，这次修复没有破坏已有的 Lab4 功能。

## 六、实验测试与结果

### 6.1 测试环境

| 项目 | 环境 |
|---|---|
| 开发系统 | WSL2 Ubuntu 22.04 |
| 开发工具 | VS Code |
| 目标架构 | RISC-V 64 位 |
| 编译工具 | riscv64-unknown-elf-gcc |
| 模拟器 | QEMU + OpenSBI |
| 项目分支 | lab4 |

### 6.2 编译测试

执行：

```bash
make -C code clean
make -C code
```

验证目标为成功生成：

```text
bin/kernel
bin/ucore.img
```

在三人代码集成完成后，整个工程已经能够成功编译和链接。

### 6.3 QEMU 运行测试

执行：

```bash
make -C code qemu
```

集成测试中的主要输出包括：

```text
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
kmalloc_init() succeeded!
check_vma_struct() succeeded!
check_vmm() succeeded.

this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
```

这些输出表明物理内存管理、页表测试和虚拟内存管理已经完成初始化，后续内核线程也能够进入指定执行流程。

### 6.4 自动评分

执行：

```bash
make -C code grade
```

小组集成后的自动评分结果为：

```text
-check PMM:             OK
-check Process Create:  OK
-check Schedule:        OK

Total Score: 30/30
```

其中 PMM 测试主要验证：

- 页映射建立。
- 旧映射替换。
- 同一物理页重新映射。
- 页引用计数更新。
- 页权限更新。
- 重复删除映射。

本人在最新 `check_boot_pgdir()` 修复后也重新执行测试，并确认全部通过。

需要说明的是，原小组总报告的截图对应此前的集成版本；本次新修复后的测试结果来自本人再次运行的反馈，不将旧截图冒充为新截图。

## 七、AI 辅助开发中的问题与处理

本次实验使用 AI 辅助分析老师的代码框架、生成页映射实现思路，并根据实际终端反馈逐步定位错误。

过程中遇到的主要问题如下：

| 问题 | 产生原因 | 处理方法 |
|---|---|---|
| 找不到 `kdebug.h` | 头文件搜索路径不完整 | 修改 Makefile |
| `uint_t` 未定义 | 新旧实验头文件类型差异 | 对齐 Lab4 的类型定义 |
| `kva2page` 类型错误 | 整数地址与指针类型不匹配 | 增加显式转换 |
| `readline` 重复定义 | 两份同名实现参与链接 | 保留 Lab4 对应文件 |
| `proc_init/cpu_idle` 未定义 | B/C 函数尚未实现 | 按分工等待集成 |
| QEMU 启动异常 | 启动方式与内核入口等因素需要联合排查 | 核对 QEMU 参数和入口映射，集成后恢复运行 |
| 页表高地址检查隐患 | 32 位变量处理 64 位地址，且未区分大页叶子项 | 调整 `check_boot_pgdir()` |

AI 在实现和排查过程中提供了思路，但最终必须根据实际项目接口、编译日志和运行结果判断正确性。

本次调试也说明，不能因为 AI 生成的代码在语法上没有错误，就直接认为功能正确。

## 八、实验涉及的重要操作系统知识点

| 知识点 | 实验对应内容 | 理解与区别 |
|---|---|---|
| 物理内存管理 | `page_init()`、`alloc_pages()` | 管理哪些物理内存可以分配 |
| 虚拟内存管理 | `get_pte()`、`page_insert()` | 管理虚拟地址与物理地址的映射关系 |
| 多级页表 | Sv39、PTE、页目录 | 将虚拟地址拆分为索引与页内偏移，逐级完成地址转换 |
| 大页映射 | 启动时的 1 GiB 叶子项 | 与普通 4 KiB 页映射相比，页表层级和偏移范围不同 |
| 物理页引用计数 | `page_ref_inc/dec()` | 保证共享物理页不会被过早释放 |
| TLB | `sfence.vma` | 保存近期地址转换结果；修改页表后要处理旧缓存 |
| 异常处理 | `stvec`、`scause`、`sepc` | 用于处理执行过程中出现的中断或异常 |
| 内核线程 | 后续 `proc_init()`、`schedule()` | 依赖已经建立的内核地址空间，不等于完整用户进程 |

其中，我认为最重要的区别是**物理内存分配与虚拟内存映射不是一回事**。

物理内存分配决定系统把哪一页实际内存交给使用者；虚拟内存映射决定程序使用的虚拟地址最终对应哪一页物理内存。两者需要配合，但承担的职责不同。

## 九、重要但本实验未实现的操作系统功能

虽然本次实验实现了基本的页表操作和内核线程调度，但仍有不少操作系统机制尚未实现，例如：

1. **按需分页**：当程序真正访问某个虚拟地址时，才分配对应物理页。
2. **页面置换**：物理内存不足时，通过 FIFO、LRU 等算法选择需要换出的页面。
3. **交换空间（Swap）**：将部分内存页面暂时保存到磁盘。
4. **写时复制（COW）**：多个进程先共享物理页，在发生写操作时再复制。
5. **完整用户进程地址空间**：实现用户态与内核态隔离以及不同进程的独立地址空间。
6. **完整进程退出与回收**：释放进程占用的资源并处理父子进程关系。

这些机制都与虚拟内存和进程管理密切相关，但不属于本次 Lab4 成员 A 的必做实现范围。

## 十、实验总结

通过本次 Lab4，我进一步理解了虚拟内存管理中页表、物理页引用计数和 TLB 之间的关系。

在实现 `page_insert()` 和 `page_remove()` 时，最需要注意的是重复映射和替换映射的情况。如果只是简单覆盖页表项，就可能导致引用计数不正确，甚至出现物理页被提前释放的问题。

在实际调试过程中，我也发现操作系统实验不仅需要实现指定函数，还需要解决不同实验阶段的代码兼容问题。头文件、编译参数、地址类型和页表结构都可能影响最终运行。

尤其是在修复 `check_boot_pgdir()` 时，我理解了 RISC-V 64 位高地址和 Sv39 大页映射之间的联系，认识到页表检查代码必须与实际使用的页表结构相匹配。

最终，成员 A 的核心页操作和相关兼容代码完成实现，并在三人集成后通过了编译、QEMU 运行及自动评分。针对启动页表检查的进一步修复也完成了回归测试，最终评分保持 **30/30**。

本次实验使我对操作系统中内存管理模块的工作过程有了更具体的认识，也提高了我根据真实编译和运行日志逐步定位问题的能力。
## 十一、扩展练习 Challenge

本节根据 Lab4 实验指导书中的扩展练习与 `get_pte()` 思考题整理，代码位置均相对于项目的 `code/` 目录。

### 11.1 `local_intr_save` 与 `local_intr_restore` 如何实现中断保护？

**答：** `local_intr_save(intr_flag)` 调用 `__intr_save()` 检查 `sstatus.SIE`。若当前 CPU 的 S 模式中断原本开启，则先关闭中断，并把原状态记为 `1`；若原本关闭，则记为 `0`。关键操作结束后，`local_intr_restore(intr_flag)` 通过 `__intr_restore()` 只在原来开启中断时重新打开中断，因此可以正确恢复进入临界区前的状态。

例如外层临界区关中断后，内层再次调用 `local_intr_save` 会记录 `0`；内层退出不会提前打开中断，外层退出才恢复原来的开启状态。这种方式保护**当前 CPU** 上的关键操作，不能替代多核系统中的互斥锁，也不会屏蔽非法指令、页故障等同步异常。

**代码位置：** `kern/sync/sync.h`：`__intr_save()`、`__intr_restore()`、`local_intr_save`、`local_intr_restore`；`kern/schedule/sched.c`：`schedule()`；`kern/process/proc.c`：`do_fork()`、`proc_run()`。

**答辩速记：** 先记录原来的中断状态，关中断执行关键操作，结束后恢复原状态。

### 11.2 Sv32、Sv39、Sv48 分页模式有什么区别？

**答：** 三者都使用多级页表进行虚拟地址到物理地址的转换，主要区别是有效虚拟地址位数和页表层数。本实验运行在 RV64 上，使用 **Sv39 三级页表**，普通页大小为 4 KiB。

| 分页模式 | 有效虚拟地址位数 | 页表层数 | 普通页地址划分 |
|---|---:|---:|---|
| Sv32 | 32 位 | 2 级 | 10 位索引 + 10 位索引 + 12 位偏移 |
| Sv39 | 39 位 | 3 级 | 9 + 9 + 9 位索引 + 12 位偏移 |
| Sv48 | 48 位 | 4 级 | 9 + 9 + 9 + 9 位索引 + 12 位偏移 |

Sv39 每张页表有 512 项，每项 8 字节，正好占一个 4 KiB 物理页。普通 4 KiB 映射需要查到最末级 PTE；如果上级页表项已经是合法叶子项，还可能形成 2 MiB 或 1 GiB 的大页映射。本项目启动映射中使用了 Sv39 的 1 GiB 叶子项。

**代码位置：** `kern/mm/mmu.h`：`PDX1`、`PDX0`、`PTX`、`PGSIZE` 和 PTE 标志位；`kern/init/entry.S`：启动页表和 `satp` 设置；`kern/mm/pmm.c`：`check_boot_pgdir()`。

**答辩速记：** Sv32 两级，Sv39 三级，Sv48 四级；本实验用 Sv39。

### 11.3 为什么 `get_pte()` 中有两段形式相似的代码？

**答：** `get_pte()` 要从 Sv39 根页表找到某个虚拟地址对应的末级页表项，因此需要先后经过两级中间页表。这两段代码分别按照 `PDX1(la)`、`PDX0(la)` 查找中间页表项；如果该项无效，并且 `create=1`，就申请一页内存作为下一级页表、清零并建立连接。两级处理步骤相同，因此代码形式相似。完成两级查找后，再通过 `PTX(la)` 返回末级 PTE 的地址。

**代码位置：** `kern/mm/pmm.c`：`get_pte(pde_t *pgdir, uintptr_t la, bool create)`。

**答辩速记：** Sv39 到末级 PTE 要经过两张中间页表，所以有两段相似的“检查—必要时分配—继续查找”代码。

### 11.4 `get_pte()` 将查询和页表分配合在一起好吗？有必要拆开吗？

**答：** 这种设计有优点：同一个函数通过 `create` 参数同时支持纯查询和按需建立中间页表，避免重复写多级页表遍历逻辑。`create=0` 时只查询；`create=1` 时允许申请缺少的中间页表。

缺点是创建过程中可能出现部分分配成功、后续分配失败的情况。如果没有回滚逻辑，就可能遗留无用的页表页。将查询与创建分开可以明确区分只读操作和资源分配操作，使错误处理更容易维护；但也会带来接口或实现上的重复。因此**不一定必须拆开，关键是创建失败时保持页表结构和物理页资源一致**。

针对本次发现的实际边界情况，我们修复了 `get_pte()` 在第二次中间页表申请失败时的回滚，并进行了下述故障注入专项测试。

**代码位置：** `kern/mm/pmm.c`：`get_pte()`、`page_insert()`。

**答辩速记：** 合并实现简洁，但分配失败必须回滚；拆分有利于区分查询和创建的副作用。

---

## 十二、`get_pte()` 分配失败回滚修复与实测验证

### 12.1 问题发现与修复目标

原先的 `get_pte()` 在 `create=1` 时可能需要连续分配两张中间页表。如果第一次申请成功、第二次申请失败，函数虽然返回 `NULL`，但第一次新建的页表连接和物理页可能仍然保留，形成资源泄漏。

本次修复针对该情况：记录**本次调用新分配的第一级中间页表页**；当后续页表页申请失败时，撤销对应的根页表项，并释放此次新分配的页表页。若第一级页表原本就存在，则不能误删原有页表。`page_insert()` 接收到 `get_pte()` 的失败结果后返回 `-E_NO_MEM`。

**代码位置：** `kern/mm/pmm.c`：`get_pte()`、`page_insert()`；物理页释放接口 `free_page()`。

### 12.2 专项测试设计

为实际验证回滚分支，采用**故障注入（Fault Injection）**。测试使用独立的临时根页表，通过临时测试开关人为使第一次中间页表申请成功、第二次申请返回 `NULL`，然后用断言检查：

1. 故障注入是否确实触发；
2. `page_insert()` 是否返回 `-E_NO_MEM`；
3. 本次新建的根页表项是否恢复为 `0`；
4. 目标数据页的引用计数是否仍为 `0`；
5. 操作前后空闲物理页数量是否一致。

**测试相关代码位置：** `kern/mm/pmm.c`：临时故障注入开关、`alloc_pages()` 中的注入判断、`check_pte_oom_rollback()` 和 `pmm_init()` 中的条件测试调用。正式版本关闭故障注入开关。

### 12.3 故障注入的实际运行结果

在 WSL 环境下执行以下命令，完成重新编译和 QEMU 运行：

```bash
make -C code clean
make -C code
make -C code qemu
```

QEMU 实际输出：

```text
[OOM] injected allocation failure
[Test PTE OOM] ret=-4, root=0x0, before=31728, after=31728
[Test PTE Rollback] PASS
```

**结果分析：**

- `[OOM] injected allocation failure` 表明测试指定的申请失败实际发生。
- `ret=-4` 对应本项目的 `-E_NO_MEM`，说明插入映射正确报告内存不足。
- `root=0x0` 表明新建的根页表连接被撤销。
- `before=31728, after=31728` 表明该测试路径的空闲物理页数量未减少；目标数据页引用计数保持为 `0` 的断言也已通过。
- `[Test PTE Rollback] PASS` 表明本次专项测试的全部断言执行通过。

**专项测试结论：** 在“第一次中间页表分配成功、第二次分配失败”的实际注入场景下，回滚操作通过测试，未发现遗留的页表连接或物理页数量损失。

### 12.4 关闭故障注入后的正式回归测试

完成专项测试后，关闭 `LAB4_TEST_OOM` 开关，重新执行：

```bash
make -C code clean
make -C code
make -C code qemu
make -C code grade
```

正式 QEMU 输出中未再出现 `[OOM] injected allocation failure`，且依次出现：

```text
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
kmalloc_init() succeeded!
check_vma_struct() succeeded!
check_vmm() succeeded.
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
```

正式自动评分的实际输出为：

```text
  -check PMM:                                OK
  -check Process Create:                     OK
  -check Schedule:                           OK
Total Score: 30/30
```

**回归测试结论：** 关闭故障注入后，内核能够完成编译、启动及既有初始化检查，Lab4 自动评分三项检查均为 `OK`，最终得分 **30/30**。因此，本次修改没有破坏上述已覆盖的物理内存页操作、内核线程创建和调度测试。

> 注：QEMU 末尾 `process exit!!.` 的 Panic 来自 Lab4 中尚未实现完整退出回收的 `do_exit()` 占位代码，并非此次回滚测试失败；其含义应与实际的退出资源回收功能区分。

### 12.5 本次修复总结

本次工作不仅修复了 `get_pte()` 在指定分配失败路径下的资源回滚问题，还通过**主动触发失败路径的专项测试**与**关闭故障注入后的正式回归测试**分别验证修复效果和原有功能。两轮测试的原始输出均已记录，可用于实验报告和答辩说明。
