# Lab4 成员 A：AI 辅助开发与 Prompt 交互记录

> **课程**：操作系统实验（2026 秋）  
> **实验**：Lab4 进程管理  
> **成员**：张馨月  
> **学号**：2414029  
> **负责模块**：虚拟内存页操作  
> **代码分支**：`lab4-A-vmm`  
> **集成分支**：`lab4`  
> **记录日期**：截至 2026 年 11 月 22 日

## 一、AI 辅助开发的目标

本次实验要求使用 AI 辅助完成部分操作系统代码的设计与实现。

本人负责 Lab4 功能模块一，主要涉及以下函数：

```c
int page_insert(pde_t *pgdir,
                struct Page *page,
                uintptr_t la,
                uint32_t perm);

void page_remove(pde_t *pgdir, uintptr_t la);
```

此外，还需要检查 `page_remove_pte()`、`get_pte()`、`get_page()`、`tlb_invalidate()` 等辅助接口，以及此前 Lab2/Lab3 代码的兼容性。

在与 AI 交互时，主要提出以下要求：

1. 严格依据老师提供的 Lab4 指导书和源码。
2. 只修改成员 A 负责的代码和必要的兼容文件。
3. 保留 Lab4 原始框架，不随意重写整个工程。
4. 优先保证实现的正确性，而不是只让代码通过编译。
5. 每次出现错误，都根据真实终端输出逐步排查。
6. 不代替成员 B、C 实现进程创建或调度代码。
7. 所有修改均通过实际编译、QEMU 运行和测试进行验证。

**记录说明：** 下文中的 Prompt 模板是在本次真实交互基础上整理的可复用版本，并非全部都是当时逐字发送的原始消息。能够核对的用户原话会单独注明，避免将事后整理内容冒充原始聊天记录。

## 二、第一阶段：理解 Lab4 框架与明确分工

### 2.1 任务目标

首先需要了解 Lab4 相比 Lab3 新增了什么内容，以及本人需要完成哪些函数。

在分析老师的实验指导书后，将 Lab4 分为三个功能模块：

- 成员 A：虚拟内存页映射与底层兼容。
- 成员 B：内核线程创建与初始化。
- 成员 C：进程调度与上下文切换。

本人主要负责 `code/kern/mm/pmm.c` 中的 `LAB4 function1` 部分。

### 2.2 SPEC

要求 AI 检查实验框架中已有的内存管理接口，明确需要实现的代码和此前实验需要迁移的内容。

不能直接使用 Lab3 的整个 `pmm.c` 覆盖 Lab4 文件，因为 Lab4 新增了页表检查和其他内存管理功能。

### 2.3 Prompt 设计（整理版）

```text
请根据老师提供的 Lab4 指导书、实验源码和 ABC 分工规范，
检查当前操作系统实验四的代码结构。

我负责成员 A，主要实现 page_insert 和 page_remove。

请先检查：
1. pmm.c 中哪些函数已经由老师提供；
2. 哪些部分需要从 Lab2/Lab3 迁移；
3. get_pte、get_page、tlb_invalidate 等接口如何使用；
4. 本人代码与 B、C 成员的依赖关系。

不要重写整个 pmm.c，也不要修改 B、C 负责的模块。

请按照 VS Code + WSL Ubuntu 的环境逐步指导我，
每次给出明确的操作和检查方法。
```

### 2.4 AI 辅助结果

通过阅读源码，明确了本人的主要任务：

- 实现 `page_insert()`。
- 实现 `page_remove()`。
- 补充必要的 `page_remove_pte()`。
- 维护页引用计数和 TLB。
- 迁移此前完成的物理内存初始化。
- 检查 `check_pgdir()` 和 `check_boot_pgdir()`。

## 三、第二阶段：实现虚拟内存页操作

### 3.1 任务目标

正确建立和删除虚拟地址映射。

需要重点处理多个虚拟地址共享同一物理页时的引用计数变化。

### 3.2 SPEC

`page_insert()` 应当满足：

- 映射建立成功返回 0。
- 页表申请失败返回 `-E_NO_MEM`。
- 原地址无映射时正常创建。
- 原地址映射相同物理页时不重复增加引用计数。
- 原地址映射其他物理页时正确删除旧映射。
- 完成映射更新后刷新 TLB。

`page_remove()` 应当满足：

- 删除存在的有效映射。
- 减少对应物理页引用计数。
- 引用计数归零时释放物理页。
- 对未映射地址安全返回。
- 不因为删除操作而新建页表。

### 3.3 Prompt 设计（整理版）

```text
现在需要实现 Lab4 function1 中的 page_insert 和 page_remove。

请严格使用项目已有的函数和数据结构：

get_pte()
pte2page()
page_ref_inc()
page_ref_dec()
free_page()
pte_create()
tlb_invalidate()

实现时需要分析以下情况：

1. 虚拟地址首次映射物理页；
2. 同一个虚拟地址重新映射到相同物理页；
3. 同一个虚拟地址重新映射到不同物理页；
4. 删除不存在的映射；
5. 中间页表申请失败。

请给出正确的 C 语言实现，并解释为什么要增加或减少引用计数。

不要修改老师已经提供的其他模块。
```

### 3.4 AI 辅助结果

最终在 `pmm.c` 中完成三个相关函数：

```text
page_remove_pte()
page_insert()
page_remove()
```

实现后对以下内容进行了检查：

- 页表项有效位 `PTE_V`。
- 新旧物理页是否相同。
- 页引用计数是否平衡。
- 删除映射是否会重复释放。
- TLB 刷新是否在页表修改后执行。

## 四、第三阶段：解决代码兼容与编译错误

### 4.1 错误一：kdebug.h 找不到

实际编译时出现：

```text
fatal error: kdebug.h: No such file or directory
```

**问题分析**

Makefile 中的内核头文件搜索路径不完整。

**解决方法**

检查 `KINCLUDE`，补充：

```makefile
kern/debug/
```

重新编译后，程序可以继续处理后续文件。

### 4.2 错误二：uint_t 未定义

编译过程中出现：

```text
unknown type name 'uint_t'
```

**问题分析**

Lab4 使用的部分类型定义与当前工程旧版头文件不一致。

**解决方法**

对比老师原始 Lab4 的 `libs/defs.h` 与当前文件，使用符合 Lab4 接口要求的类型定义。

同时核对 `pmm.h`、`mmu.h` 等文件，确保内存管理接口之间兼容。

### 4.3 错误三：kmalloc.c 参数类型不匹配

实际错误：

```text
passing argument 1 of 'kva2page'
makes pointer from integer without a cast
```

进一步检查发现：

```c
static inline struct Page *
kva2page(void *kva)
```

该函数接收 `void *` 类型。

但原调用代码：

```c
free_pages(kva2page(kva), 1 << order);
```

中的 `kva` 为 `unsigned long`。

**解决方法**

修改为：

```c
free_pages(kva2page((void *)kva), 1 << order);
```

随后执行：

```bash
make -C code clean
make -C code
```

类型检查问题得到解决。

### 4.4 Prompt 设计（整理版）

```text
下面是我在 WSL 中编译 Lab4 时得到的错误日志。

请先判断错误属于：
1. 头文件搜索路径；
2. 类型定义；
3. C 语言编译；
4. 重复符号；
5. 链接阶段缺少函数。

请先定位具体文件与错误原因。

如果涉及老师的原始文件，
先比较老师版本与当前版本，
不要直接大范围覆盖。

每次只修复当前问题，并告诉我如何重新编译验证。
```

### 4.5 错误四：readline 重复定义

后续链接时出现：

```text
multiple definition of 'readline'
```

检查发现：

```text
code/libs/readline.c
code/kern/libs/readline.c
```

两个文件同时参与编译，造成重复定义。

**解决方法**

删除旧版重复的 `libs/readline.c`，保留 Lab4 框架中的 `kern/libs/readline.c`。

### 4.6 错误五：proc_init 和 cpu_idle 未定义

后续链接出现：

```text
undefined reference to 'proc_init'
undefined reference to 'cpu_idle'
```

通过搜索源码发现：

```c
void proc_init(void);
void cpu_idle(void);
```

已经有函数声明，但是当时还没有相应实现。

经核对分工：

- `proc_init()` 由 B 成员负责。
- `cpu_idle()` 由 C 成员负责。

因此没有擅自在 A 模块加入临时空实现，而是等待其他成员完成代码后再进行整体链接和运行。

## 五、第四阶段：排查 QEMU 启动异常

### 5.1 实际观察到的现象

小组联调时，曾出现 QEMU 只能输出 OpenSBI 信息，随后无法正常进入内核的现象。

GDB 调试中还曾观察到 `kern_init()` 断点再次命中。

当时的调用回溯涉及：

```text
kern_init()
check_boot_pgdir()
pmm_init()
```

这些现象表明，内核启动和页表初始化过程需要进一步排查。

### 5.2 排查重点

主要检查：

- `kernel.ld` 中的内核链接基地址。
- QEMU 加载内核镜像使用的参数。
- `entry.S` 中的初始 Sv39 页表。
- 内核虚拟地址与物理地址的转换关系。
- `check_boot_pgdir()` 对低地址的映射测试。
- `pmm_init()` 与 `idt_init()` 的初始化顺序。

### 5.3 Prompt 设计（整理版）

```text
目前 Lab4 的 QEMU 运行出现异常。

OpenSBI 能正常启动，但 uCore 有时无法继续输出。

GDB 调试中发现 kern_init 再次命中，
调用回溯涉及 check_boot_pgdir 和 pmm_init。

请检查：

1. kernel.ld 的链接地址；
2. QEMU 内核镜像加载地址；
3. entry.S 中的 Sv39 初始映射；
4. 页表访问是否正确；
5. check_boot_pgdir 对低地址的访问；
6. stvec 是否已经初始化。

请区分已经确定的代码问题与尚需 GDB 验证的推测，
不要在没有 scause、sepc、stval 的情况下直接认定异常原因。
```

### 5.4 阶段性处理

对照代码后发现，内核启动依赖正确的镜像装载方式。

在团队后续集成过程中，QEMU 的启动参数调整为：

```makefile
-kernel $(UCOREIMG)
```

随后团队成功运行内核，得到内存初始化以及 `initproc` 的正常输出。

这一阶段的启动参数修复属于小组联合排查，不全部归为本人 A 模块独立实现。

## 六、第五阶段：修复 check_boot_pgdir() 高地址检查

### 6.1 实际交互背景

在团队已经完成 Lab4 集成和评分后，我再次要求 AI 检查 GitHub 公共仓库。

用户原话：

> 查看目前仓库，应该都解决了

在复核最新 `lab4` 代码时，发现 `check_boot_pgdir()` 仍然存在高地址检查方面的潜在问题。

随后提出：

> 行，那咱们来修改下，先给我把最新代码拉到本地的指令，然后我们再修改

完成最新代码同步后，针对该函数进行单独修复。

### 6.2 发现的问题

原有代码使用：

```c
int i;
```

保存循环地址。

但 RV64 内核使用 64 位高虚拟地址，32 位 `int` 无法可靠表示。

此外，启动页表中的映射使用 Sv39 1 GiB 叶子项，而 `get_pte()` 的原实现按照普通三级页表进行查找。

因此，该测试存在检查范围与页表层级不匹配的问题。

### 6.3 SPEC

修复后的检查需要：

1. 使用 `uintptr_t` 保存地址。
2. 从 `DRAM_BASE` 开始检查物理内存映射。
3. 根据高虚拟地址取得 Sv39 根页表项。
4. 确认其为当前启动预期的有效 1 GiB 叶子项。
5. 验证虚拟地址转换得到的物理地址正确。
6. 保留原有低地址 `0x100` 测试。
7. 修改范围仅限 A 负责的页表检查代码。

### 6.4 Prompt 设计（整理版）

```text
现在检查 Lab4 的 check_boot_pgdir()。

原函数使用 int i 来保存地址，
但当前系统为 RISC-V 64 位，
KERNBASE 位于高地址内核空间。

同时 entry.S 使用 Sv39 1GiB 大页映射，
而 get_pte 主要按普通三级页表进行查找。

请分析：
1. 原有循环是否存在地址截断问题；
2. get_pte 是否能正确处理当前 1GiB 叶子项；
3. 应当如何验证高虚拟地址到物理地址的映射。

请提供最小修改方案。

要求使用 uintptr_t，
检查 PTE_V、PTE_R、PTE_W、PTE_X，
并通过物理基址与页内偏移验证地址转换结果。

后续低地址 0x100 的测试保持不变。
```

### 6.5 实际修改

将原来的高地址检查调整为针对 Sv39 1 GiB 叶子映射的验证。

核心代码为：

```c
uintptr_t pa;

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

该修改并没有重新设计整个页表管理系统，而是修正了当前启动大页映射的检查方式。

### 6.6 修复后验证

修改后要求重新执行：

```bash
make -C code clean
make -C code
make -C code qemu
make -C code grade
```

用户实际反馈：

> 都过了

说明本次修复后的编译、QEMU 和评分回归测试均已通过。

其中自动评分保持为：

```text
Total Score: 30/30
```

这里记录的是本人实际测试反馈，并非 AI 在本地直接运行获得的结果。

## 七、AI 辅助开发中的主要经验

### 7.1 不应直接相信第一次生成的代码

AI 生成的代码需要结合当前项目使用的接口、类型和页表结构进行检查。

例如 `check_boot_pgdir()` 的问题，如果只看函数能够编译和输出成功信息，就容易忽略其循环是否真正覆盖了预期地址范围。

### 7.2 不能混淆编译、链接和运行错误

本次出现过不同阶段的错误：

| 阶段 | 典型错误 |
|---|---|
| 预处理与编译 | 找不到头文件 |
| 编译 | 类型定义缺失、参数类型不匹配 |
| 链接 | 重复定义、函数未实现 |
| 运行 | QEMU 启动异常、页表访问风险 |
| 功能验证 | 页映射和引用计数检查 |

不同类型的问题应采取不同的排查方法，不能将所有错误都归因于同一个函数。

### 7.3 应当遵守小组分工

当链接出现 `proc_init()` 和 `cpu_idle()` 未定义时，没有擅自实现其他成员的函数。

这样可以避免多个成员同时修改相同区域，造成代码冲突或逻辑不一致。

### 7.4 测试通过不代表不存在其他边界问题

本次最终自动评分为 30/30，但评分脚本只能覆盖特定测试场景。

例如，不同类型的大页映射、异常处理、页面置换和用户进程内存管理，仍然可能需要在后续实验中实现或完善。

## 八、实际使用的验证命令

### 8.1 检查代码和分支

```bash
git branch --show-current
git status -sb
git diff --check
git diff -- code/kern/mm/pmm.c
```

### 8.2 获取最新公共代码

```bash
git fetch origin
git switch lab4
git pull --ff-only origin lab4
```

### 8.3 检查页表相关函数

```bash
grep -n "page_insert" code/kern/mm/pmm.c
grep -n "page_remove" code/kern/mm/pmm.c
grep -n "check_boot_pgdir" code/kern/mm/pmm.c
```

### 8.4 编译和功能测试

```bash
make -C code clean
make -C code
make -C code qemu
make -C code grade
```

### 8.5 Git 提交和推送

```bash
git add code/kern/mm/pmm.c
git diff --cached --check
git commit -m "fix(lab4): correct Sv39 boot page table validation"
git push origin lab4
```

以上提交命令属于实际开发过程中采用的操作方案。是否已完成对应远程提交，应通过仓库提交记录核对，不能只根据命令本身认定推送成功。

## 九、AI 与本人在实验中的具体分工

| 工作内容 | AI 的主要作用 | 本人完成的工作 |
|---|---|---|
| 阅读实验指导书 | 分析任务要求和相关接口 | 确认个人分工 |
| 页映射代码 | 提出算法和代码实现方案 | 检查并应用于实验工程 |
| 编译错误 | 解释报错原因并提出修改方案 | 在 WSL 中执行修复和重新编译 |
| 页表调试 | 分析 Sv39、PTE 和地址转换 | 根据实际源码和运行结果判断 |
| QEMU 验证 | 提供调试命令和排查思路 | 运行 QEMU，反馈实际输出 |
| Git 协作 | 提供分支管理和提交命令 | 拉取、检查、提交和推送代码 |
| 实验报告 | 协助整理实现原理与调试记录 | 核对真实过程和测试结果 |

本次实验中，AI 主要承担分析、解释和提供初步实现方案的作用。

最终代码是否正确，需要通过本人在实际实验环境中的编译、运行和测试来判断。

## 十、总结

本次使用 AI 辅助完成了 Lab4 成员 A 的虚拟内存页操作以及必要的底层兼容工作。

主要完成内容包括：

1. 阅读和分析 Lab4 内存管理框架。
2. 实现 `page_insert()`、`page_remove()` 等页映射操作。
3. 维护物理页引用计数和 TLB 状态。
4. 迁移 Lab3 的物理内存初始化代码。
5. 解决头文件、类型定义和链接兼容问题。
6. 配合小组排查 QEMU 启动异常。
7. 修复 `check_boot_pgdir()` 中与 RV64 地址及 Sv39 大页相关的检查问题。
8. 完成修复后的编译、QEMU 和自动评分回归测试。

最终，小组集成后的 Lab4 通过全部三个自动评分项目，总分为 **30/30**，本次 A 部分页表检查修复后也重新通过测试。

通过这一过程，我认识到 AI 可以提高阅读源码和定位问题的效率，但实验中的关键判断仍需要结合真实代码、编译日志、运行结果以及操作系统原理来完成。

---

**附注：原始交互记录与整理版 Prompt 的区别**

本文档记录了真实开发过程中的关键问题、错误日志和交互摘要。

第三、四、五、六部分的 Prompt 设计为根据实际任务整理的可复用提示词，不应视为每次交互中逐字发送过的完整原始消息。

如果老师要求完整原始 AI 交流记录，需要单独导出真实聊天内容，不能用本文中的整理版替代。