# Lab2 AI 辅助记录

## 成员1 AI 交流记录

# Lab2 个人 AI Prompt 使用记录

**负责内容：** 物理内存初始化 `page_init()` 与 First-Fit 理解及验证

**记录说明：** 本文根据实际开发过程中与 AI 的交流进行整理。下列 Prompt 为事后归纳，非原始聊天逐字记录；测试结果均来自实际运行。

---

## 一、物理内存初始化相关源码分析

### [PROMPT]

请结合当前 uCore Lab2 项目，帮助我分析物理内存初始化的整体流程，重点解释 `pmm.c`、`pmm.h`、`default_pmm.c` 和 `dtb.c` 之间的关系。

我负责完成 `page_init()`，希望先理解现有项目的接口和初始化顺序，再开始编写代码。请一次指导一小步，并根据实际代码进行分析。

### [RELY]

参考项目中已有的源文件及接口，包括：

- `kern/mm/pmm.c`
- `kern/mm/pmm.h`
- `kern/mm/default_pmm.c`
- DTB 相关初始化代码
- 物理内存布局与地址转换相关宏定义

### [GUARANTEE]

分析应基于当前项目的真实代码，不随意假设不存在的接口，不修改其他成员负责的代码，也不破坏原有 First-Fit 分配器的实现。

### [SPECIFICATION]

重点分析以下内容：

1. DTB 如何提供物理内存起始地址和内存大小。
2. `pmm_init()` 与 `page_init()` 的调用关系。
3. `struct Page` 和 `pages` 数组的作用。
4. `end`、`npage`、`nbase`、`va_pa_offset` 的含义。
5. `init_memmap()` 如何接收并管理空闲物理页。

**实际工作记录：** 阅读并检查相关源文件，确认了初始化流程、物理内存范围获取接口以及 `page_init()` 需要完成的任务。

---

## 二、page_init() 函数实现

### [PROMPT]

请根据当前 Lab2 源码，逐步指导我完成 `kern/mm/pmm.c` 中的 `page_init()` 函数。

需要正确获取物理内存范围，建立 `Page` 描述符数组，保留内核及管理数据占用的区域，并将剩余空闲页交给当前物理内存分配器管理。

希望每次只完成一小步，检查无误后再继续。

### [RELY]

使用项目中已有的接口和数据结构，包括：

- `get_memory_base()`
- `get_memory_size()`
- `PHYSICAL_MEMORY_OFFSET`
- `PGSIZE`
- `ROUNDUP()`
- `PADDR()`
- `pa2page()`
- `init_memmap()`

### [GUARANTEE]

1. 不改变 `pmm_manager` 的原有接口。
2. 不修改 First-Fit 已提供的测试函数。
3. 内核及页描述符数组占用的物理页不能被错误加入空闲页集合。
4. 保留老师要求的 `lab2` 标记并填写对应学号。
5. 代码能够与现有 uCore 内存管理模块兼容。

### [SPECIFICATION]

按照以下顺序完成实现：

1. 获取并检查物理内存地址和容量。
2. 初始化虚拟地址与物理地址的偏移量。
3. 计算物理页号范围。
4. 确定 `pages` 数组位置。
5. 初始化物理页描述符并标记保留页。
6. 确定空闲物理页起始地址和数量。
7. 调用 `init_memmap()` 注册空闲页。
8. 编译运行并检查内存初始化结果。

**实际工作记录：** 已完成 `page_init()` 实现，相关代码通过 Git Pull Request #6 合并到小组 `lab2` 分支。

---

## 三、First-Fit 运行验证与问题排查

### [PROMPT]

请帮助我验证刚刚完成的 `page_init()` 是否正常工作，并检查默认 First-Fit 物理页分配器是否能够正确运行。

如果内核无法正常启动，希望根据实际终端输出逐步定位问题，区分启动加载、地址映射和物理内存初始化自身的问题。

### [RELY]

主要依据：

- `make qemu` 的编译及运行输出。
- QEMU 和 OpenSBI 启动信息。
- `default_pmm_manager` 的初始化与检查结果。
- 必要时使用 GDB 观察异常寄存器及指令执行位置。

### [GUARANTEE]

1. 不将启动阶段的错误直接归因于 `page_init()`。
2. 以真实运行输出为依据判断测试结果。
3. 不将未执行的自动评分测试记为通过。
4. 不随意修改 First-Fit 的既有测试逻辑。

### [SPECIFICATION]

完成以下检查：

1. 确认内核能够获得 DTB 物理内存信息。
2. 确认物理内存起始地址和容量。
3. 检查空闲物理页数量。
4. 检查 First-Fit 分配器的内置测试输出。
5. 对启动过程中遇到的问题进行定位，记录实际观察结果。

**实际测试结果：**

- 物理内存起始地址：`0x80000000`
- 物理内存大小：128 MB
- 空闲物理页数量：31929
- 内存管理器：`default_pmm_manager`
- 测试结果：`check_alloc_page() succeeded!`

**实际问题记录：** 调试过程中发现过 OpenSBI 内核加载入口异常，以及启用 Sv39 分页后的指令页错误。通过检查启动参数、GDB 异常寄存器和启动页表，完成了本地环境下的启动问题排查。该问题发生在 `page_init()` 执行之前。

---

## 四、AI 辅助使用总结

本次实验中，AI 主要用于辅助阅读已有源码、梳理函数调用关系、分析物理内存初始化流程，以及根据真实运行信息定位问题。

具体代码需要结合项目实际接口进行检查，功能是否正确则以编译结果、QEMU 实际输出和内置测试为依据。

通过本次实验，进一步理解了 DTB 内存探测、物理页描述符初始化、保留页管理、First-Fit 页分配器以及内核启动地址映射之间的关系。

---

## 成员2基础实验 AI 交流记录

# OS2026fall Lab2 — AI Prompt 使用记录

## 一、基本信息

- 课程：操作系统
- 实验：Lab2 物理内存管理
- 负责内容：Best-Fit 物理内存分配算法、GDB 调试、MMU/TLB 与 Sv39 地址转换分析
- 开发环境：WSL2 Ubuntu、RISC-V 工具链、QEMU、GDB
- 工作分支：`zxy-lab2`
- AI 辅助工具：ChatGPT

## 二、AI 辅助使用说明

本次实验主要使用 AI 辅助理解实验要求、分析算法实现、排查编译与运行问题，以及解释 GDB 调试结果。

实际代码修改、终端命令执行、QEMU 测试、GDB 调试和截图保存均由本人完成。对于 AI 提供的解释，通过实际运行输出进行验证。

以下记录按照实验开展顺序整理，Prompt 为实际交流内容的概括性记录，并非完整逐字转录。

---

## 三、Best-Fit 算法实现

### Prompt 1：明确实验分工与任务范围

**问题描述：**

根据 Lab2 三人协作分工，明确本人需要完成的实验任务，区分必做内容与拓展内容，并确定 Git 分支及文件修改范围。

**AI 辅助内容：**

AI 帮助梳理了本人负责的内容，包括：

1. 实现 Best-Fit 内存分配算法。
2. 使用 GDB 调试内核地址转换过程。
3. 分析 Sv39 页表与 TLB 的作用。
4. 编写个人实验报告草稿和保存调试截图。

同时建议在个人分支 `zxy-lab2` 上开发，不直接修改其他组员负责的公共文件。

**实际处理结果：**

使用个人分支完成实验，并将 Best-Fit 实现与调试报告分别保存，避免影响小组其他成员的开发工作。

### Prompt 2：理解 Best-Fit 分配算法

**问题描述：**

解释 Best-Fit 的工作原理，以及它与 First-Fit 的区别。说明如何从空闲链表中选择合适的连续物理页。

**AI 辅助内容：**

AI 解释了 Best-Fit 的核心逻辑：

- 遍历整个空闲块链表。
- 选择所有满足申请大小的空闲块中最小的一块。
- 如果空闲块大于申请大小，需要分割剩余空间。
- 分配完成后更新空闲页计数。

**实际处理结果：**

根据实验框架中的数据结构，在 `kern/mm/best_fit_pmm.c` 中实现 Best-Fit 分配逻辑。

### Prompt 3：实现物理页释放与空闲块合并

**问题描述：**

如何实现 `best_fit_free_pages()`，保证释放后的物理页能够正确加入空闲链表，并与相邻空闲块合并？

**AI 辅助内容：**

AI 帮助分析了以下处理步骤：

1. 恢复被释放物理页的空闲状态。
2. 按照物理地址顺序插入空闲链表。
3. 判断是否与前一个空闲块连续。
4. 判断是否与后一个空闲块连续。
5. 合并连续空闲块并更新相关信息。

**实际处理结果：**

完成 `best_fit_free_pages()`，同时实现初始化函数和空闲页数量查询函数。

### Prompt 4：排查 Best-Fit 编译问题

**问题描述：**

Best-Fit 代码编译过程中出现错误，需要判断是算法问题、函数声明问题还是内存页状态宏使用错误。

**AI 辅助内容：**

AI 帮助检查了相关代码，定位到内存页状态宏名称的拼写问题，并解释了 `PageReserved` 的用途。

**实际处理结果：**

将错误的 `PageReserver` 修改为 `PageReserved`，随后 Best-Fit 模块编译通过。

---

## 四、Best-Fit 运行测试

### Prompt 5：排查 QEMU 内核无法正常启动的问题

**问题描述：**

执行课程提供的 QEMU 启动命令后，程序停留在 OpenSBI 阶段，没有进入 uCore 内核，如何排查？

**AI 辅助内容：**

AI 协助分析 OpenSBI 输出，发现原启动方式未正确指定下一阶段内核入口地址。

建议使用：

```bash
qemu-system-riscv64 \
    -machine virt \
    -nographic \
    -bios default \
    -kernel bin/ucore.img
```

**实际处理结果：**

修改启动命令后，OpenSBI 显示：

```text
Domain0 Next Address : 0x0000000080200000
```

随后成功进入 uCore 内核，输出内存初始化信息。

### Prompt 6：在不修改共享文件的情况下测试 Best-Fit

**问题描述：**

小组其他成员正在修改 `pmm.c`，如何单独启用 Best-Fit 并进行运行测试，同时避免影响正式仓库？

**AI 辅助内容：**

AI 建议复制一份 `code` 目录到临时目录：

```bash
mkdir -p /tmp/oslab2-bestfit-test
cp -a code/. /tmp/oslab2-bestfit-test/
```

然后仅在临时副本中将内存管理器设置为：

```c
pmm_manager = &best_fit_pmm_manager;
```

重新编译并启动 QEMU。

**实际处理结果：**

成功启用 Best-Fit，内核输出：

```text
memory management: best_fit_pmm_manager
Free physical pages: 31929
check_alloc_page() succeeded!
```

说明 Best-Fit 已被实际启用，并通过课程框架提供的运行检查。

正式仓库中的共享 `pmm.c` 未因此受到修改。

---

## 五、GDB 与 Sv39 分页调试

### Prompt 7：使用 GDB 连接 QEMU

**问题描述：**

如何使用 GDB 调试 RISC-V 内核，并在内核入口设置断点？

**AI 辅助内容：**

AI 建议使用以下参数启动 QEMU：

```bash
qemu-system-riscv64 \
    -machine virt \
    -nographic \
    -bios default \
    -kernel bin/ucore.img \
    -s -S
```

然后在另一个终端执行：

```bash
make gdb
```

在 GDB 中设置物理入口断点：

```gdb
break *0x80200000
continue
```

**实际处理结果：**

成功命中 `0x80200000`，观察到：

```text
pc   = 0x80200000
satp = 0x0
```

确认内核入口处尚未开启分页。

### Prompt 8：定位 Sv39 分页开启指令

**问题描述：**

如何通过 GDB 找到内核开启 Sv39 分页的指令，并观察 `satp` 寄存器的变化？

**AI 辅助内容：**

AI 建议使用：

```gdb
x/35i $pc
```

查看内核入口附近的反汇编指令，定位到：

```asm
0x80200034: csrw satp,t0
0x80200038: sfence.vma
```

并通过断点和单步执行观察寄存器。

**实际处理结果：**

在写入 `satp` 前观察到：

```text
pc   = 0x80200034
t0   = 0x8000000000080205
satp = 0x0
```

单步执行后：

```text
pc   = 0x80200038
satp = 0x8000000000080205
```

确认 `satp` 的 MODE 字段变为 8，CPU 进入 Sv39 分页模式。

### Prompt 9：分析 `sfence.vma` 与 TLB

**问题描述：**

`sfence.vma` 的作用是什么？它与 TLB 有什么关系？是否能够通过 GDB 直接观察 TLB 命中情况？

**AI 辅助内容：**

AI 解释了 `sfence.vma` 的作用，以及 TLB 缓存虚拟地址转换结果的基本原理。

同时说明，当前 GDB 调试过程不能直接证明 TLB 命中或未命中，因此应区分实际观察结果与理论分析。

**实际处理结果：**

单步执行 `sfence.vma` 后观察到：

```text
pc   = 0x8020003c
satp = 0x8000000000080205
```

确认指令执行完成，`satp` 保持不变。

实验报告中仅分析 TLB 的工作机制，没有编造 TLB 命中率或内部缓存数据。

---

## 六、Sv39 页表与地址转换

### Prompt 10：查看根页表内容

**问题描述：**

如何根据 `satp` 找到 Sv39 根页表，并使用 GDB 查看页表项？

**AI 辅助内容：**

AI 解释了 `satp` 中 PPN 字段与根页表物理地址之间的关系。

根据：

```text
satp = 0x8000000000080205
```

得到根页表物理地址：

```text
0x80205000
```

建议使用：

```gdb
x/4gx 0x80205000
x/gx 0x80205ff8
```

**实际处理结果：**

观察到：

```text
PTE[2]   = 0x200000cf
PTE[511] = 0x200000cf
```

两个页表项映射到同一个物理内存区域。

### Prompt 11：理解 Sv39 三级页表与大页映射

**问题描述：**

为什么 Sv39 被称为三级页表，但当前实验中只读取根页表就能确定物理地址？

**AI 辅助内容：**

AI 解释了 Sv39 的 VPN[2]、VPN[1]、VPN[0] 三级索引结构，以及根页表叶子项形成 1 GiB 大页映射的机制。

**实际处理结果：**

结合页表项的有效位和权限位，确认当前启动页表使用的是根页表级别的 1 GiB 大页映射。

因此，当前地址转换不需要实际遍历全部三级页表。

### Prompt 12：验证虚拟地址到物理地址的转换

**问题描述：**

如何根据虚拟地址计算 Sv39 根页表索引，并验证内核高虚拟地址对应的物理地址？

**AI 辅助内容：**

AI 提供了 GDB 表达式：

```gdb
p/x (0xffffffffc02000d6 >> 30) & 0x1ff
p/x (0x802000d6 >> 30) & 0x1ff
```

以及物理地址计算表达式：

```gdb
p/x 0x80000000 + (0xffffffffc02000d6 & 0x3fffffff)
```

**实际处理结果：**

GDB 输出：

```text
高虚拟地址 VPN[2] = 0x1ff
低虚拟地址 VPN[2] = 0x2
物理地址 = 0x802000d6
```

计算结果与页表映射关系一致。

### Prompt 13：验证内核在高虚拟地址执行

**问题描述：**

如何证明 CPU 不只是能够读取高虚拟地址，而是已经真正跳转到高虚拟地址执行？

**AI 辅助内容：**

AI 建议在 `kern_init()` 的高虚拟地址入口设置断点：

```gdb
break *0xffffffffc02000d6
continue
```

然后查看 PC 和 `satp`：

```gdb
info registers pc
info registers satp
x/4i $pc
```

**实际处理结果：**

成功命中 `kern_init()`：

```text
pc   = 0xffffffffc02000d6
satp = 0x8000000000080205
```

证明 CPU 已经在 Sv39 模式下进入内核高虚拟地址执行。

---

## 七、实验报告与 Git 协作

### Prompt 14：整理实验报告

**问题描述：**

根据实际编译、QEMU 运行及 GDB 调试结果，整理 Best-Fit 和 MMU/Sv39 实验报告，要求保留真实命令、输出及必要解释。

**AI 辅助内容：**

AI 协助将实验过程整理为两份 Markdown 草稿：

```text
report/drafts/best_fit.md
report/drafts/gdb_mmu.md
```

并根据调试步骤建议截图名称及相对路径。

**实际处理结果：**

完成两份报告草稿，并保存 Best-Fit 运行截图及 GDB 调试截图。

### Prompt 15：检查 Git 提交范围

**问题描述：**

如何提交个人实验报告和截图，同时避免提交编译产物或覆盖其他组员的文件？

**AI 辅助内容：**

AI 建议先检查：

```bash
git branch --show-current
git status --short
```

并使用明确的文件路径暂存报告与截图，避免使用 `git add .` 将 `code/bin/`、`code/obj/` 等编译产物加入提交。

**实际处理结果：**

在 `zxy-lab2` 分支整理个人实验成果，按文件范围提交代码、报告与截图。

---

## 八、AI 使用总结

本次实验中，AI 主要起到了辅助解释、调试指导和文档整理的作用。

在 Best-Fit 实现过程中，AI 帮助梳理了空闲块选择、分割和合并的逻辑；在运行测试过程中，协助分析了 QEMU 启动参数问题，并提出使用临时副本测试的方法，避免影响小组共享代码。

在 GDB 调试过程中，AI 逐步指导了断点设置、寄存器观察、页表项读取和地址转换计算。相关结论均结合实际 GDB 输出进行检查。

本次使用 AI 的主要收获是能够将源码中的内存管理和地址转换机制，与实际内核运行过程对应起来。同时也认识到，AI 给出的分析需要通过编译、运行、断言检查和调试输出进行验证，不能直接将理论推测当作实验结果。

---

## 成员2智能流程演示 AI 交流记录

# Lab2 加分题 B：GDB/MMU 智能调试工具 Prompt 记录

## 1. 任务背景

本次加分题的目标是在 Lab2 必做的 GDB/MMU/Sv39 调试基础上，开发一个能够自动分析 Sv39 页表的辅助调试工具。

工具使用 GDB 原生命令脚本实现，最终注册自定义命令：

```gdb
sv39walk <virtual_address>
```

能够自动读取 `satp`、计算 VPN 索引、解析页表项、识别页大小并计算物理地址。

本文件按照课程推荐的四段式 SPEC 结构，整理 AI 辅助开发过程中涉及的关键任务和问题。以下 Prompt 是对实际交互意图的归纳整理，并非逐字聊天记录。

---

## 2. Prompt 1：确定加分题 B 的实现方案

### [PROMPT]

阅读 Lab2 加分题 B 的要求，结合已经完成的 GDB/MMU/Sv39 调试工作，设计一个具有教学演示价值的智能调试工具。

希望在已有环境上继续开发，尽量复用前期调试成果，不重新修改内核代码。

### [RELY]

- 项目：OS2026fall Lab2。
- 已完成 `satp`、Sv39 根页表与虚拟地址转换调试。
- 已通过 QEMU MMU 日志观察 `riscv_cpu_tlb_fill` 记录。
- 实验要求通过 `make qemu` 启动 QEMU。
- 加分题 B 要求形成更完整的地址查询、页表查询或单步调试演示工具。

### [GUARANTEE]

- 不影响已经完成的 Lab2 基础功能。
- 不侵入 `pmm.c`、Best-Fit、Buddy 等内核模块。
- 保留现有 `Makefile` 和 `entry.S` 启动修复。
- 工具应具有独立性和可复用性。
- 不将普通 GDB 调试直接当作已经完成的加分成果。

### [SPECIFICATION]

实现一个 Sv39 自动地址转换工具，支持输入虚拟地址，输出分页模式、页表索引、页表项和物理地址，并形成可复现的操作演示。

**阶段结果：** 确定开发 `sv39walk` 自定义 GDB 命令，采用独立脚本实现。

---

## 3. Prompt 2：设计 Python-GDB 自动页表分析脚本

### [PROMPT]

编写一个 GDB Python 脚本，通过自定义 `sv39walk` 命令，自动完成 RISC-V Sv39 页表查询。

要求解释核心实现逻辑，并提供能够在现有 uCore 调试环境中运行的代码。

### [RELY]

- `satp = 0x8000000000080205`。
- 根页表物理地址为 `0x80205000`。
- `PTE[2] = PTE[511] = 0x200000cf`。
- 内核高虚拟地址 `0xffffffffc02000d6` 对应物理地址 `0x802000d6`。
- 当前环境已经具备 RISC-V GDB 和 QEMU。

### [GUARANTEE]

- 只读取调试信息，不修改页表。
- 检查 Sv39 分页模式和虚拟地址合法性。
- 对无效页表项提供错误提示。
- 输出计算过程，方便教学演示。

### [SPECIFICATION]

创建：

```text
tools/sv39/sv39_walk.py
```

使用 GDB Python API 实现：

- 读取 `satp`。
- 计算 VPN[2:0]。
- 查询 L2、L1、L0 页表。
- 识别不同级别的叶子页表项。
- 计算最终物理地址。

**阶段结果：** 完成 Python 脚本初版和语法检查，但加载时发现当前 RISC-V GDB 不支持 Python scripting，因此该版本没有完成实际运行验证。

---

## 4. Prompt 3：解决 GDB Python 不支持的问题

### [PROMPT]

在 GDB 中执行 Python 脚本时出现：

```text
Python scripting is not supported in this copy of GDB.
```

希望在不重新安装 GDB、不更换工具链的前提下，继续实现自动化地址转换功能。

能否使用 GDB 自身支持的命令编写脚本？

### [RELY]

- 当前 RISC-V GDB 16.3.90 可以正常连接 QEMU。
- 已经成功使用 `break`、`continue`、`si`、`info registers` 和 `x/gx` 调试内核。
- 只有 Python 脚本功能不可用。
- 课程已有的 `make qemu` 和 `make gdb` 工作流程需要保留。

### [GUARANTEE]

- 不重新安装工具链。
- 不改变 QEMU 和内核启动方式。
- 不修改内核源代码。
- 优先复用 GDB 已有功能。

### [SPECIFICATION]

采用 GDB 原生脚本实现自定义命令，使用：

```text
define
set
if
while
printf
document
```

完成与 Python 方案相近的基础页表分析功能。

**阶段结果：** 确定使用 `.gdb` 脚本，创建 `tools/sv39/sv39_walk.gdb`。

---

## 5. Prompt 4：恢复 make qemu 与 GDB 调试连接

### [PROMPT]

之前已经通过修改 `Makefile` 和 `entry.S` 实现 `make qemu` 正常启动，但现在使用 GDB 时出现：

```text
The program is not being run.
could not connect: Connection timed out.
```

后来启动 QEMU 调试模式又出现：

```text
-s: Failed to find an available port: Address already in use
```

希望保留课程要求的 `make qemu` 启动方式，排查调试连接问题。

### [RELY]

- 当前 `make qemu` 能正常启动 uCore。
- Makefile 已支持 `QEMUOPTS` 参数。
- QEMU 的 GDB 调试端口为 1234。
- `entry.S` 已配置低地址恒等映射和内核高地址映射。

### [GUARANTEE]

- 不重新安装 QEMU。
- 不重新修改已验证的页表初始化代码。
- 不影响普通 `make qemu` 启动。
- 定位问题后只进行必要处理。

### [SPECIFICATION]

使用：

```bash
sudo ss -ltnp | grep ':1234'
```

检查端口占用，确认旧 QEMU 进程后将其结束。

使用：

```bash
make qemu QEMUOPTS="-s -S"
```

启动调试模式，再通过：

```bash
make gdb
```

连接调试器。

**阶段结果：** 发现旧 QEMU 进程占用端口 1234，结束该进程后成功恢复连接。

---

## 6. Prompt 5：实现第一版 GDB 原生命令

### [PROMPT]

使用 GDB 原生脚本编写 `sv39walk` 命令。

先实现能够读取 `satp`、计算 VPN[2:0]、确定根页表地址，并自动读取对应的根页表项。

要求不依赖 Python。

### [RELY]

- GDB 已经成功连接 QEMU。
- CPU 已运行到 `kern_init()`。
- 当前 `satp = 0x8000000000080205`。
- 根页表物理地址为 `0x80205000`。
- 根页表第 511 项位于 `0x80205ff8`。

### [GUARANTEE]

- 不改变当前 CPU 寄存器及页表内容。
- 只通过 GDB 读取已有状态。
- 能够在现有 GDB 环境直接加载。
- 输出内容便于人工核对。

### [SPECIFICATION]

创建：

```text
tools/sv39/sv39_walk.gdb
```

实现：

```gdb
sv39walk 0xffffffffc02000d6
```

自动输出：

- 虚拟地址；
- `satp`；
- Sv39 根页表地址；
- VPN[2]、VPN[1]、VPN[0]；
- 根页表项地址；
- 页表项内容。

**阶段结果：** 脚本成功加载，自动读取到：

```text
VPN[2]: 511
Root PTE address: 0x0000000080205ff8
PTE value: 0x00000000200000cf
```

---

## 7. Prompt 6：增加页表项解析与物理地址计算

### [PROMPT]

目前 `sv39walk` 已经可以自动读取根页表项，但没有计算最终物理地址。

希望继续解析 PTE 标志位，判断页表项是否有效、是否为叶子页表项，以及是否满足大页对齐要求，并自动计算物理地址。

### [RELY]

- 当前 `PTE[511] = 0x200000cf`。
- 当前映射是 Sv39 根页表上的 1 GiB 大页。
- 已知参考结果：

```text
VA = 0xffffffffc02000d6
PA = 0x802000d6
```

### [GUARANTEE]

- 保留原有输出信息。
- 不直接写死物理地址。
- 根据读取的 PTE 和输入 VA 完成计算。
- 无效页表项不输出成功结果。

### [SPECIFICATION]

新增：

- V、R、W、X 标志位解析。
- 叶子页表项判断。
- 1 GiB 大页对齐检查。
- 物理地址计算。
- 成功或错误提示。

**阶段结果：** 实际运行得到：

```text
PTE value: 0x00000000200000cf
Page size: 1 GiB
Physical address: 0x00000000802000d6
Translation successful!
```

---

## 8. Prompt 7：完善 Sv39 三级页表查询

### [PROMPT]

第一版 `sv39walk` 已经成功完成 1 GiB 大页转换，希望进一步支持完整的 L2、L1、L0 页表查询逻辑。

要求在遇到非叶子页表项时自动进入下一级页表，并根据最终叶子项所在层级计算物理地址。

### [RELY]

- 现有 GDB 原生命令脚本。
- Sv39 的三级页表结构。
- 每个页表项占 8 字节。
- L2、L1、L0 叶子页表项分别对应 1 GiB、2 MiB、4 KiB 映射。

### [GUARANTEE]

- 保留之前已经验证通过的 1 GiB 大页转换功能。
- 检查无效 PTE 和非法权限组合。
- 对大页进行对齐检查。
- 对非规范虚拟地址给出错误提示。
- 不把尚未实际测试的功能写为已经通过。

### [SPECIFICATION]

通过循环实现：

```text
L2 → L1 → L0
```

如果遇到叶子页表项，则结束遍历并计算物理地址；否则进入下一级页表。

同时增加规范虚拟地址检查以及异常输出。

**阶段结果：** 完成新版原生 GDB 脚本，L2 级别的 1 GiB 映射运行验证通过。L1、L0 的 2 MiB 和 4 KiB 路径已编写，但尚未进行真实映射测试。

---

## 9. Prompt 8：设计正常地址转换测试

### [PROMPT]

针对 `sv39walk` 设计正常地址转换测试，验证工具能够正确处理高地址和低地址两种映射。

希望测试结果能够证明工具根据不同 VPN 索引查询不同页表项，而不是直接返回固定的物理地址。

### [RELY]

当前启动页表中：

```text
PTE[2]   = 0x200000cf
PTE[511] = 0x200000cf
```

这两个页表项映射到同一个物理内存区域。

### [GUARANTEE]

- 使用当前内核中真实存在的映射。
- 不修改页表制造测试结果。
- 记录工具的实际输出。
- 与 GDB 手动计算结果交叉核对。

### [SPECIFICATION]

测试：

```gdb
sv39walk 0xffffffffc02000d6
sv39walk 0x802000d6
```

检查不同根页表索引下的地址转换结果。

**阶段结果：** 两组测试均成功，分别通过 `PTE[511]` 和 `PTE[2]`，最终得到：

```text
Physical address: 0x00000000802000d6
Translation successful!
```

---

## 10. Prompt 9：设计异常地址测试

### [PROMPT]

为了验证 `sv39walk` 的可靠性，希望增加异常输入测试。

要求测试无效页表项以及不符合 Sv39 地址规范的虚拟地址，并检查工具是否能够正确报告错误。

### [RELY]

- 当前启动页表中的 `PTE[1]` 为无效项。
- Sv39 要求虚拟地址第 63～39 位与第 38 位一致。
- 脚本已实现 PTE 有效性和虚拟地址规范性检查。

### [GUARANTEE]

- 只执行读取和分析操作。
- 不让内核实际访问非法地址。
- 不修改当前页表。
- 使用真实 GDB 输出作为测试依据。

### [SPECIFICATION]

测试：

```gdb
sv39walk 0x40000000
sv39walk 0x0000008000000000
```

预期分别得到：

```text
ERROR: Invalid PTE
ERROR: Non-canonical Sv39 virtual address
```

**阶段结果：** 两组测试均通过，工具正确识别无效页表项和非规范虚拟地址。

---

## 11. Prompt 10：整理加分题演示和实验报告

### [PROMPT]

根据 Lab2 加分题 B 的要求，结合 `sv39walk` 的实际实现和测试结果，整理一份 Markdown 实验报告。

要求讲清楚工具原理、操作过程、实际测试、TLB miss 与 Sv39 页表查询的关系，并整理截图和 Prompt 记录。

### [RELY]

- 加分题 B 的三人协作与 Git 分支规范。
- `tools/sv39/sv39_walk.gdb` 的最终实现。
- 四组实际通过的测试。
- 此前完成的 GDB/MMU/Sv39 调试记录。
- QEMU `riscv_cpu_tlb_fill` 日志。

### [GUARANTEE]

- 只使用已经实际观察到的测试结果。
- 不虚构 L1、L0 映射测试结果。
- 不宣称测量过真实硬件 TLB 命中率。
- 不把尚未完成的课件库贡献写为已完成。
- 保持加分项与 Lab2 基础任务相互独立。

### [SPECIFICATION]

生成：

```text
report/drafts/bonus_gdb_agent.md
report/drafts/bonus_gdb_prompt.md
```

报告包含：

1. 实验目的和环境。
2. 工具设计和实现。
3. GDB/QEMU 操作流程。
4. 四组实际测试。
5. TLB、MMU、Sv39 地址转换分析。
6. 工具局限性。
7. 实验总结。

**阶段结果：** 已整理加分题报告及 Prompt 记录，待纳入仓库并完成最终提交检查。

---

## 12. AI 使用总结

本次加分题中，AI 主要用于以下方面：

1. **设计方案：** 将手动 GDB 调试整理为自动化页表查询工具。
2. **代码辅助：** 编写 GDB 脚本，完成 `satp` 解析、VPN 计算、PTE 查询以及物理地址计算。
3. **环境排查：** 分析 GDB Python 不支持、调试端口占用以及脚本加载错误等问题。
4. **测试设计：** 构造正常映射和异常地址测试用例。
5. **文档整理：** 规范化实验过程、测试记录和结果说明。

整个开发过程中，AI 提供实现建议和排查思路，最终以 QEMU 和 GDB 的实际运行结果作为判断依据。

已验证的核心结果包括：

| 测试内容 | 实际结果 |
|---|---|
| 高虚拟地址转换 | `0x802000d6` |
| 低地址恒等映射 | `0x802000d6` |
| 无效页表项检测 | `ERROR: Invalid PTE` |
| 非规范虚拟地址检测 | `ERROR: Non-canonical Sv39 virtual address` |

通过本次开发，将原本需要多次手动输入 GDB 命令的 Sv39 地址转换分析过程，整理成了能够重复使用的 `sv39walk` 调试命令。

本工具当前主要验证了 1 GiB 大页映射，其他页大小及更完整的访问权限检查仍有进一步完善空间。

---

## 成员3 AI 交流记录

# Lab2 成员3 AI Prompt 使用记录

## Prompt 1：实现 Buddy System 物理页分配器

[PROMPT]

任务：基于当前 OS2026fall Lab2 工程中的真实物理内存管理框架，
实现一个符合 `struct pmm_manager` 接口的 Buddy System 物理页分配器。

本任务只负责新增 Buddy 模块，不修改成员1已经完成的 `page_init()`、
`entry.S` 启动修复，不修改成员2已经完成的 Best-Fit 实现，也暂不修改
`pmm.c` 中当前使用的 `pmm_manager`。Buddy 模块完成并独立检查后，
再由集成人统一完成管理器切换。

Buddy System 应按照 2 的幂大小管理空闲物理页块：
分配时将请求页数向上取整到能够容纳请求的最小 2 的幂块；
若对应阶没有空闲块，则从更高阶空闲块逐级拆分。
释放时寻找同阶 buddy，只有 buddy 同阶、空闲且属于合法可管理范围时
才能合并，并允许继续向更高阶递归/迭代合并。

[RELY]

只能依赖当前 Lab2 工程中的真实结构和接口：

1. `code/kern/mm/pmm.h`
   - `struct pmm_manager` 提供以下接口：
     - `init`
     - `init_memmap`
     - `alloc_pages`
     - `free_pages`
     - `nr_free_pages`
     - `check`

2. `code/kern/mm/memlayout.h`
   - `struct Page` 已包含：
     - `ref`
     - `flags`
     - `property`
     - `page_link`
   - `PG_reserved` 表示保留页；
   - `PG_property` 表示空闲块头页；
   - `property` 可以用于记录 Buddy 空闲块的实际页数；
   - `page_link` 可以作为空闲链表节点。
   - 不要修改 `struct Page`。

3. `code/libs/list.h`
   - 已提供双向链表：
     - `list_init`
     - `list_add`
     - `list_add_before`
     - `list_add_after`
     - `list_del`
     - `list_empty`
     - `list_next`
     - `list_prev`
   - 不要修改公共链表实现。

4. `code/kern/mm/default_pmm.c`
   - 作为 `pmm_manager` 模块结构、Page 状态初始化和空闲页计数的参考；
   - 不复制 First-Fit 的“找到第一个满足块”的策略。

5. `code/kern/mm/best_fit_pmm.c`
   - 仅作为另一种 `pmm_manager` 实现形式参考；
   - 不修改其中现有实现和测试。

6. 当前成员1已经完成：
   - `page_init()`
   - 物理内存范围识别；
   - `pages` 描述符数组；
   - `npage`
   - `va_pa_offset`
   - `init_memmap(pa2page(free_start_pa), nfree)`
   - `entry.S` 启动页表修复。

因此 Buddy 的 `init_memmap(base, n)` 必须能够接收由现有
`page_init()` 传入的一段连续空闲 Page 范围，不能假设 `n`
本身一定是 2 的幂。

[GUARANTEE]

必须新增：

- `code/kern/mm/buddy_pmm.c`
- `code/kern/mm/buddy_pmm.h`

必须提供以下 Buddy 接口：

- `buddy_init`
- `buddy_init_memmap`
- `buddy_alloc_pages`
- `buddy_free_pages`
- `buddy_nr_free_pages`
- `buddy_check`
- `buddy_pmm_manager`

实现必须满足：

1. 使用多个 order 的空闲链表管理 1、2、4、8、16……页块；
2. 不修改 `struct Page`，复用 `property`、`PG_property` 和 `page_link`；
3. `buddy_init_memmap(base, n)` 必须正确处理任意连续页数，
   将其划分为地址合法且对齐的 2 的幂空闲块；
4. 初始化可管理页时正确清除 Reserved 状态、引用计数和旧属性；
5. 分配 `n` 页时，选择最小的 `2^order >= n`；
6. 当前 order 无块时，从更高 order 查找并逐级拆分；
7. 拆分后未使用的一半重新加入低一阶空闲链表；
8. 释放时只能与真正的 buddy 合并；
9. buddy 必须：
   - 位于当前可管理范围；
   - 当前为空闲块头；
   - 与当前块大小/order 相同；
10. 合并后形成高一阶块，并继续尝试向上合并；
11. `buddy_nr_free_pages()` 返回的是空闲“页数”，不是块数；
12. `nr_free` 或等价计数在分配、释放、拆分和合并过程中必须保持一致；
13. 不得修改：
   - `page_init()`
   - `entry.S`
   - First-Fit
   - Best-Fit
   - 公共 `struct Page`
   - 公共链表实现；
14. 暂时不要在本 Prompt 中修改 `pmm.c` 切换当前分配器；
15. 不要宣称测试通过，必须等实际编译和运行后再判断。

[SPECIFICATION]

### buddy_init

Pre-Condition:
- Buddy 模块尚未初始化。

Post-Condition:
- 所有 order 的空闲链表均为空；
- 空闲页总计数为 0。

### buddy_init_memmap(base, n)

Pre-Condition:
- `base != NULL`
- `n > 0`
- `[base, base+n)` 是 `page_init()` 交给 PMM 的连续可用物理页范围。

Post-Condition:
- 所有页的旧管理状态被正确初始化；
- 整段内存被划分为若干合法且满足地址对齐要求的 2 的幂块；
- 每个空闲块只有头 Page 设置 `PG_property`；
- 头 Page 的 `property` 记录该块包含的实际页数；
- 每个块加入对应 order 的空闲链表；
- 总空闲页数等于 n。

### buddy_alloc_pages(n)

Pre-Condition:
- `n > 0`
- Buddy 已初始化。

Post-Condition:
- 若没有足够大小的块，返回 `NULL`；
- 否则返回能够容纳 n 页的最小 2 的幂块的起始 `Page *`；
- 如需从高阶块拆分，逐级二分直到目标 order；
- 未分配的一半重新进入对应低阶链表；
- 已分配块不再保持 `PG_property` 空闲块标记；
- 空闲页计数扣除实际分配的 2 的幂页数。

### buddy_free_pages(base, n)

Pre-Condition:
- `base` 是 Buddy 分配得到的合法块起点；
- `n > 0`；
- 根据 n 得到的目标 order 与分配时采用的 2 的幂块一致。

Post-Condition:
- 释放块恢复为空闲状态；
- 尝试寻找同阶 buddy；
- 只有 buddy 合法、空闲且同阶时才合并；
- 合并后继续向更高阶尝试；
- 最终块加入正确 order 的空闲链表；
- 空闲页计数增加实际释放的 2 的幂页数，合并本身不能重复增加页数。

### buddy_nr_free_pages

Post-Condition:
- 返回当前所有 order 中空闲页的总页数。

### buddy_check

本 Prompt 只要求保留可调用的 `buddy_check` 接口。
全面的 Buddy 测试方案由“Prompt 2：Buddy System 测试设计”
单独生成，避免把实现 Prompt 与测试 Prompt 混在一起。

输出要求：
- 给出 `buddy_pmm.h` 与 `buddy_pmm.c` 的完整实现；
- 保持代码风格与现有 `default_pmm.c` / `best_fit_pmm.c` 接近；
- 对拆分、buddy 地址计算、合并条件等关键逻辑添加简洁注释；
- 不修改本任务边界以外的文件。

---

## Prompt 2：Buddy System 测试设计

[PROMPT]

任务：基于 Prompt 1 已经实现的 Buddy System 物理页分配器，
设计并实现 `buddy_check()`，验证当前 Buddy 分配器的核心行为和
边界情况。

测试必须基于当前工程中的真实 Buddy 实现，不允许通过修改
Buddy 核心分配逻辑来迎合测试，也不能修改成员1的 `page_init()`、
First-Fit、成员2的 Best-Fit 或已有公共测试。

测试完成后，需要能够说明每一组测试具体验证了 Buddy 的什么性质。

[RELY]

当前 Buddy 模块已经实现：

- `buddy_init`
- `buddy_init_memmap`
- `buddy_alloc_pages`
- `buddy_free_pages`
- `buddy_nr_free_pages`
- `buddy_pmm_manager`

当前实现使用：

- 多个 order 的空闲链表；
- `order 0` 表示 1 页；
- `order 1` 表示 2 页；
- `order 2` 表示 4 页；
- 以此类推；
- `struct Page.property` 记录 Buddy 块实际包含的页数；
- `PG_property` 只表示当前 Page 是空闲块头；
- 非 2 的幂请求会向上取整：
  - 1 页 -> 1 页块
  - 3 页 -> 4 页块
  - 5 页 -> 8 页块；
- 分配时允许从高阶块逐级拆分；
- 释放时允许同阶 buddy 逐级合并；
- `buddy_nr_free_pages()` 记录的是总空闲页数，而不是空闲块数量。

[GUARANTEE]

`buddy_check()` 至少验证：

1. 初始空闲页数大于测试所需页数；
2. 超过 Buddy 最大可表示块大小的请求返回 `NULL`；
3. 申请 1 页能够正常返回；
4. 申请 3 页时实际得到 4 页 Buddy 块；
5. 申请 5 页时实际得到 8 页 Buddy 块；
6. 三次分配得到的内存块不重叠；
7. 分配后总空闲页数应准确减少：
   - 1 + 4 + 8 = 13 页；
8. 分别释放这些块后，总空闲页数准确恢复；
9. 释放顺序不能影响最终结果；
10. 测试前后每个 order 的空闲块数量恢复一致，用来验证
    拆分后能够正确进行伙伴合并；
11. 测试不得破坏原有物理内存管理状态；
12. 测试不能修改 First-Fit、Best-Fit 和 `page_init()`；
13. 不允许仅打印“成功”而没有断言；
14. 只有所有 assert 实际通过后，才能认为 Buddy 基础测试通过。

[SPECIFICATION]

### 测试前状态保存

Pre-Condition:
- Buddy 已执行 `buddy_init()`；
- `buddy_init_memmap()` 已经接管系统可用物理页。

要求：
- 保存测试前的总空闲页数；
- 保存所有 order 的空闲块数量。

### 非 2 的幂分配测试

执行：

- `alloc_pages(1)`
- `alloc_pages(3)`
- `alloc_pages(5)`

Post-Condition:

- 1 页申请实际块大小为 1；
- 3 页申请实际块大小为 4；
- 5 页申请实际块大小为 8；
- 三块不相互重叠；
- 总空闲页数减少 13。

### 超大请求测试

执行一个大于 `2^BUDDY_MAX_ORDER` 页的请求。

Post-Condition:

- 返回 `NULL`；
- 总空闲页数不发生改变。

### 释放和合并测试

将前面分配的块按照与分配不同的顺序释放。

Post-Condition:

- 总空闲页数恢复到测试前；
- 每个 order 的空闲块数量恢复到测试前；
- 说明拆分出来的伙伴块能够在释放后重新合并；
- 不产生重复计算空闲页的问题。

输出要求：

- 只修改 `buddy_pmm.c` 中 Buddy 测试相关内容；
- 不修改 Buddy 核心算法来适配测试；
- 测试以 assert 为主要验证手段；
- 保持测试代码简单、可解释，便于答辩。
