# Lab2 加分题 B：GDB/MMU 智能调试演示工具

## 1. 实验目的

本加分题在 Lab2 必做部分的 GDB/MMU/Sv39 调试基础上，进一步实现一个可重复使用的地址转换调试工具。

在基础实验中，我们通过 GDB 手动读取 `satp` 寄存器、定位 Sv39 根页表、计算 VPN 索引、读取页表项，并根据页表映射关系计算物理地址。虽然能够完成地址转换分析，但需要输入多条命令，操作比较繁琐，也不容易展示完整的页表查询过程。

因此，本次加分实验希望将这些操作整合为一条 GDB 自定义命令，实现以下目标：

1. 自动获取当前 CPU 的分页配置。
2. 自动计算虚拟地址对应的 VPN 索引。
3. 自动定位、读取和解析 Sv39 页表项。
4. 支持逐级查询 L2、L1、L0 页表。
5. 识别不同层级的叶子页表项并计算物理地址。
6. 对无效页表项、非规范虚拟地址等情况提供错误提示。
7. 将调试过程整理为可复现的演示材料，帮助理解 MMU、TLB 和 Sv39 地址转换机制。

本工具主要用于教学演示和辅助调试，不修改内核实际地址转换逻辑。

## 2. 实验环境

| 项目 | 实验配置 |
|---|---|
| 操作系统 | Ubuntu 22.04.5 LTS（WSL2） |
| 目标架构 | RISC-V 64 |
| 模拟器 | QEMU 7.0.0 |
| 调试工具 | RISC-V GDB 16.3.90 |
| 编译工具 | RISC-V GCC 15.1.0 |
| 实验内核 | uCore |
| 脚本语言 | GDB 原生命令脚本 |
| 开发分支 | `zxy-lab2-challenge-b` |

本实验沿用 Lab2 基础实验中已经修复的 QEMU 启动环境。

由于当前使用的 RISC-V GDB 不支持 Python 脚本功能，最终采用 GDB 自带的 `define`、`if`、`while`、`set` 和 `printf` 等命令实现自动化调试，不依赖额外的 Python-GDB 环境。

## 3. 工具设计

### 3.1 基本思路

本工具定义了一个新的 GDB 命令：

```gdb
sv39walk <virtual_address>
```

用户只需要输入一个虚拟地址，工具就会自动读取当前分页配置，并尝试完成 Sv39 页表查询。

例如：

```gdb
sv39walk 0xffffffffc02000d6
```

工具的基本执行流程如下：

```text
输入虚拟地址
      |
      v
读取 satp 寄存器
      |
      v
检查是否启用 Sv39
      |
      v
检查虚拟地址是否合法
      |
      v
计算 VPN[2]、VPN[1]、VPN[0]
      |
      v
获取根页表物理地址
      |
      v
从 L2 页表开始查询
      |
      v
读取 PTE，检查有效位和权限位
      |
      +---- 无效 PTE ------> 输出错误信息
      |
      +---- 非叶子 PTE ----> 进入下一级页表
      |
      +---- 叶子 PTE -----> 检查大页对齐
                                  |
                                  v
                         计算最终物理地址
                                  |
                                  v
                             输出结果
```

### 3.2 Sv39 地址结构

Sv39 使用 39 位有效虚拟地址，虚拟地址由三个 VPN 索引和页内偏移组成：

| 字段 | 位范围 | 位数 |
|---|---|---|
| VPN[2] | 38～30 | 9 |
| VPN[1] | 29～21 | 9 |
| VPN[0] | 20～12 | 9 |
| Offset | 11～0 | 12 |

在 RV64 系统中，Sv39 虚拟地址还需要满足符号扩展规则：第 63～39 位必须与第 38 位一致。

工具首先检查这一规则，避免将非规范地址当作正常地址查询。

VPN 的计算方式为：

```text
VPN[2] = (VA >> 30) & 0x1ff
VPN[1] = (VA >> 21) & 0x1ff
VPN[0] = (VA >> 12) & 0x1ff

Offset = VA & 0xfff
```

### 3.3 satp 寄存器解析

RISC-V RV64 的 `satp` 寄存器主要包含：

- MODE：分页模式。
- ASID：地址空间标识符。
- PPN：根页表物理页号。

在 Sv39 模式下：

```text
MODE = 8
```

根页表物理地址由以下公式计算：

```text
Root PA = satp.PPN << 12
```

本实验实际读取到：

```text
satp = 0x8000000000080205
```

解析得到：

```text
MODE = 8
PPN  = 0x80205

Root PA = 0x80205000
```

### 3.4 页表逐级查询设计

Sv39 最多使用三级页表：

- L2：根页表。
- L1：中间层页表。
- L0：最后一级页表。

每个页表项占 8 字节，每级通过对应 VPN 索引定位页表项。

页表项地址计算公式为：

```text
PTE Address = Table PA + VPN[level] × 8
```

读取 PTE 后，检查以下标志位：

| 标志位 | 作用 |
|---|---|
| V | 页表项是否有效 |
| R | 是否可读 |
| W | 是否可写 |
| X | 是否可执行 |

工具主要按照以下规则判断：

1. 如果 `V=0`，判定为无效页表项。
2. 如果 `W=1` 且 `R=0`，判定为非法权限组合。
3. 如果 `R=1` 或 `X=1`，将当前页表项视为叶子页表项。
4. 否则尝试根据 PTE 中的 PPN 进入下一级页表。

不同层级的叶子页表项对应不同映射大小：

| 叶子页表层级 | 映射页大小 |
|---|---|
| L2 | 1 GiB |
| L1 | 2 MiB |
| L0 | 4 KiB |

如果是 L2 或 L1 大页，还需要检查物理页号是否符合大页对齐要求。

本工具已经编写 L2、L1、L0 逐级遍历逻辑，但当前实际运行的启动页表使用 L2 的 1 GiB 大页映射，因此本次实测主要验证 L2 路径。

## 4. 工具实现

### 4.1 文件结构

本加分项的主要文件为：

```text
OS2026fall/
├── tools/
│   └── sv39/
│       ├── sv39_walk.gdb
│       └── sv39_walk.py
└── report/
    ├── drafts/
    │   └── bonus_gdb_agent.md
    └── images/
        ├── bonus_gdb_high.png
        ├── bonus_gdb_low.png
        ├── bonus_gdb_invalid.png
        └── bonus_gdb_noncanonical.png
```

其中：

- `sv39_walk.gdb`：本次实际运行并通过测试的 GDB 原生命令脚本。
- `sv39_walk.py`：前期尝试的 Python-GDB 版本，因当前 GDB 不支持 Python 而未完成运行验证。
- `bonus_gdb_agent.md`：本次加分题实验报告。
- `bonus_gdb_*.png`：工具实际执行截图。

### 4.2 核心功能实现

GDB 脚本首先注册自定义命令：

```gdb
define sv39walk
```

使用 `$arg0` 接收输入地址：

```gdb
set $va = (unsigned long long)$arg0
```

读取当前 `satp`：

```gdb
set $satp_value = (unsigned long long)$satp
set $mode = ($satp_value >> 60) & 0xf
```

获取根页表地址：

```gdb
set $root = ($satp_value & 0xfffffffffff) << 12
```

随后计算 VPN 索引：

```gdb
set $vpn2 = ($va >> 30) & 0x1ff
set $vpn1 = ($va >> 21) & 0x1ff
set $vpn0 = ($va >> 12) & 0x1ff
```

逐级查询时，通过页表基址和 VPN 计算 PTE 地址：

```gdb
set $pte_addr = $table + $vpn * 8
set $pte = *(unsigned long long *)$pte_addr
```

再解析 PTE 标志位：

```gdb
set $v = $pte & 1
set $r = ($pte >> 1) & 1
set $w = ($pte >> 2) & 1
set $x = ($pte >> 3) & 1
```

当检测到叶子页表项时，根据当前层级确定页大小，并计算物理地址：

```gdb
set $page_bits = 12 + 9 * $level
set $page_mask = (1ULL << $page_bits) - 1
set $pa = (($ppn << 12) & ~$page_mask) | ($va & $page_mask)
```

最后输出页大小和物理地址。

工具还包含错误处理逻辑，例如：

```text
ERROR: Invalid PTE
ERROR: Misaligned superpage
ERROR: Non-canonical Sv39 virtual address
```

需要说明的是，这些错误提示主要用于教学演示。工具当前只实现了基础页表合法性检查，尚未完整模拟 RISC-V 所有访问权限、A/D 位及异常检查规则。

## 5. 实验操作过程

### 5.1 启动 QEMU 调试环境

本实验保留课程要求的 `make qemu` 启动方式。

在第一个 WSL 终端执行：

```bash
cd ~/os-lab/OS2026fall/code
make qemu QEMUOPTS="-s -S"
```

其中：

- `-s`：启动 QEMU 的 GDB 调试服务，默认使用 TCP 端口 1234。
- `-S`：使 CPU 在启动时暂停，等待 GDB 连接。

普通内核运行仍然可以使用：

```bash
make qemu
```

### 5.2 启动 GDB

在第二个 WSL 终端执行：

```bash
cd ~/os-lab/OS2026fall/code
make gdb
```

连接成功后，设置内核入口断点：

```gdb
set pagination off
break *0x80200000
continue
```

实际观察结果：

```text
pc   = 0x80200000
satp = 0x0
```

说明 CPU 已经到达内核物理入口，此时尚未启用分页。

### 5.3 观察 Sv39 分页开启

设置断点：

```gdb
break *0x80200034
continue
```

实际停在：

```text
0x80200034
```

查看指令：

```gdb
x/i $pc
```

得到：

```asm
0x80200034: csrw satp,t0
```

执行前：

```text
t0   = 0x8000000000080205
satp = 0x0
```

单步执行：

```gdb
si
info registers satp
```

实际得到：

```text
satp = 0x8000000000080205
```

说明 Sv39 分页已开启。

继续执行 `sfence.vma`，随后检查根页表：

```gdb
x/gx 0x80205010
x/gx 0x80205ff8
```

实际输出：

```text
0x80205010: 0x00000000200000cf
0x80205ff8: 0x00000000200000cf
```

分别对应：

```text
PTE[2]   = 0x200000cf
PTE[511] = 0x200000cf
```

### 5.4 加载自定义调试工具

在 GDB 中运行到 `kern_init()`：

```gdb
break *0xffffffffc02000d6
continue
```

随后加载脚本：

```gdb
source /home/fengmian/os-lab/OS2026fall/tools/sv39/sv39_walk.gdb
```

加载成功后，即可使用自定义命令：

```gdb
sv39walk <virtual_address>
```

整个过程中不需要重新编译内核，也不需要额外安装支持 Python 的 GDB。

## 6. 测试设计与实际结果

本次共完成四组测试，分别覆盖高地址映射、低地址映射、无效页表项和非规范虚拟地址。

### 6.1 测试一：内核高虚拟地址转换

执行：

```gdb
sv39walk 0xffffffffc02000d6
```

实际输出：

```text
===== Sv39 Page Table Debugger =====
Virtual address: 0xffffffffc02000d6
SATP: 0x8000000000080205
Mode: Sv39
Root page table: 0x0000000080205000
VPN[2]: 511
VPN[1]: 1
VPN[0]: 0
Page offset: 0x0d6

[L2] Table PA: 0x0000000080205000
[L2] VPN index: 511
[L2] PTE address: 0x0000000080205ff8
[L2] PTE value: 0x00000000200000cf
Flags: V=1 R=1 W=1 X=1
Page size: 1 GiB
Physical address: 0x00000000802000d6
Translation successful!
====================================
```

该测试表明工具能够自动找到高地址对应的根页表项，并成功计算物理地址：

```text
VA = 0xffffffffc02000d6
PA = 0x802000d6
```

这一结果与之前 GDB 手动计算以及 QEMU MMU 日志中的地址转换结果一致。

![高虚拟地址转换测试](../images/bonus_gdb_high.png)

### 6.2 测试二：低地址恒等映射

执行：

```gdb
sv39walk 0x802000d6
```

实际输出中的关键部分：

```text
Virtual address: 0x00000000802000d6
VPN[2]: 2
Root page table: 0x0000000080205000

[L2] PTE address: 0x0000000080205010
[L2] PTE value: 0x00000000200000cf
Flags: V=1 R=1 W=1 X=1

Page size: 1 GiB
Physical address: 0x00000000802000d6
Translation successful!
```

可以看到：

```text
VA = 0x802000d6
PA = 0x802000d6
```

低地址与高地址分别通过 `PTE[2]` 和 `PTE[511]`，最终映射到同一物理地址。

该测试说明工具能够根据不同 VPN 索引正确定位不同根页表项，而不是将物理地址直接写死。

![低地址恒等映射测试](../images/bonus_gdb_low.png)

### 6.3 测试三：无效页表项

执行：

```gdb
sv39walk 0x40000000
```

实际输出：

```text
===== Sv39 Page Table Debugger =====
Virtual address: 0x0000000040000000
SATP: 0x8000000000080205
Mode: Sv39
Root page table: 0x0000000080205000
VPN[2]: 1
VPN[1]: 0
VPN[0]: 0
Page offset: 0x000

[L2] Table PA: 0x0000000080205000
[L2] VPN index: 1
[L2] PTE address: 0x0000000080205008
[L2] PTE value: 0x0000000000000000
Flags: V=0 R=0 W=0 X=0
ERROR: Invalid PTE
====================================
```

该地址对应根页表的 `PTE[1]`。

由于读取到的页表项为 `0x0`，有效位 `V=0`，因此工具正确报告：

```text
ERROR: Invalid PTE
```

本测试验证了工具能够识别当前页表中不存在有效映射的地址，而不是无条件输出物理地址。

![无效页表项测试](../images/bonus_gdb_invalid.png)

### 6.4 测试四：非规范 Sv39 虚拟地址

执行：

```gdb
sv39walk 0x0000008000000000
```

实际输出：

```text
===== Sv39 Page Table Debugger =====
Virtual address: 0x0000008000000000
SATP: 0x8000000000080205
ERROR: Non-canonical Sv39 virtual address
====================================
```

根据 Sv39 的地址规范，虚拟地址高位必须满足符号扩展要求。

本次输入的地址不符合该规则，因此工具没有继续查询页表，而是直接输出：

```text
ERROR: Non-canonical Sv39 virtual address
```

说明工具能够在查询前进行虚拟地址合法性检查。

![非规范虚拟地址测试](../images/bonus_gdb_noncanonical.png)

### 6.5 测试结果汇总

| 测试编号 | 测试内容 | 预期结果 | 实际结果 |
|---|---|---|---|
| 1 | 高虚拟地址转换 | 输出 `0x802000d6` | 通过 |
| 2 | 低地址恒等映射 | 输出 `0x802000d6` | 通过 |
| 3 | 无效页表项 | 报告 `Invalid PTE` | 通过 |
| 4 | 非规范虚拟地址 | 报告 `Non-canonical` | 通过 |

四组测试均得到预期输出。

需要说明的是，这四组测试全部基于现有内核的 1 GiB 大页启动映射，尚未完成针对真实 L1、L0 页表映射的独立验证。

## 7. TLB、MMU 与 Sv39 地址转换机制分析

### 7.1 TLB 的作用

TLB（Translation Lookaside Buffer）是用于缓存地址转换结果的结构。

CPU 访问虚拟地址时，可以先检查 TLB 中是否存在可用的转换信息。

如果 TLB 命中，就能够使用已有转换结果，减少页表查询开销。

如果 TLB 未命中，则需要通过地址转换机制查询页表，找到对应的物理地址及权限信息。

Sv39 页表查询可能涉及三级页表，但如果遇到 1 GiB 或 2 MiB 大页叶子项，可以提前结束查询。

### 7.2 本工具与 TLB miss 的关系

本次 `sv39walk` 并不是直接控制真实 CPU 的 TLB，也不负责统计 TLB 命中次数。

它的主要功能是：

**模拟和展示发生 TLB miss 后，如何根据当前 `satp` 和 Sv39 页表计算地址转换结果。**

例如，对于：

```text
0xffffffffc02000d6
```

工具自动完成：

```text
读取 satp
    ↓
获取根页表 PA = 0x80205000
    ↓
计算 VPN[2] = 511
    ↓
读取 PTE[511] = 0x200000cf
    ↓
识别 1 GiB 叶子页表项
    ↓
计算物理地址 PA = 0x802000d6
```

这条过程能够直观展示页表查询和物理地址形成的逻辑。

### 7.3 与 QEMU MMU 日志交叉验证

在 Lab2 基础调试中，我们还使用了 QEMU 的 MMU 日志功能：

```bash
make qemu QEMUOPTS="-d mmu,int -D /tmp/lab2-mmu.log"
```

得到过以下实际记录：

```text
riscv_cpu_tlb_fill ad ffffffffc02000d6 rw 2 mmu_idx 1
riscv_cpu_tlb_fill address=ffffffffc02000d6 ret 0 physical 00000000802000d6 prot 7
riscv_cpu_tlb_fill PMP address=00000000802000d6 ret 0 prot 7 tlb_size 4096
```

日志说明 QEMU 在处理对应高虚拟地址的取指访问时，执行了 TLB 填充相关处理，并成功得到物理地址 `0x802000d6`。

该结果与本工具自动计算出的地址完全一致。

因此，本实验通过两种方法进行验证：

- GDB 原生脚本：读取页表并自动计算物理地址。
- QEMU MMU 日志：观察模拟器实际运行过程中的地址转换结果。

需要注意，这并不代表已经测量真实硬件的 TLB 命中率，也不代表可以读取 QEMU 内部所有 TLB 缓存项。

## 8. 工具特点与不足

### 8.1 主要特点

**（1）操作简单**

原本需要分别读取 `satp`、计算 VPN、定位 PTE 和计算 PA，现在使用一条 `sv39walk` 命令就可以完成。

**（2）不依赖 Python-GDB**

由于当前 RISC-V GDB 没有 Python 脚本支持，本工具最终采用原生 GDB 命令实现，能够直接适配现有调试环境。

**（3）不侵入内核**

工具只读取 GDB 当前寄存器和内存数据，不需要修改 `pmm.c`、`entry.S` 或内存分配算法，也不需要向内核加入额外调试逻辑。

**（4）有明确的错误提示**

对于无效 PTE、非规范地址以及部分非法页表结构，工具能够输出相应错误信息。

**（5）便于复现和教学演示**

其他使用同类环境的同学可以加载脚本，输入虚拟地址，查看对应的 Sv39 页表分析过程。

### 8.2 目前存在的不足

**（1）依赖 GDB 的内存读取能力**

工具通过 GDB 直接读取页表地址，假定当前调试环境允许访问这些物理地址。对于不同的调试环境，需要进一步考虑 GDB 地址空间和物理地址的访问方式。

**（2）L1、L0 路径尚未实测**

代码包含三级页表逐级遍历逻辑，但现有启动映射直接在根页表形成 1 GiB 大页，因此本次只完成了 L2 映射的运行验证。

**（3）没有完全实现 Sv39 的全部权限检查**

目前主要检查 V、R、W、X，以及大页对齐和规范地址规则。尚未完整处理 U、G、A、D 等标志对具体访问类型的影响。

**（4）没有直接检测硬件 TLB 命中或未命中**

工具展示的是页表查询过程，不能直接读取真实 CPU 的 TLB 状态，也不能据此统计 TLB 性能指标。

因此，本次实现定位为教学演示与辅助分析工具，而不是完整的 RISC-V MMU 模拟器。

## 9. AI 辅助开发与 Prompt 记录

本加分题使用 AI 辅助分析工具设计、排查环境问题和设计测试用例。

主要过程如下。

### 9.1 工具设计阶段

**[PROMPT]**

希望将 Lab2 中手动进行的 GDB/Sv39 地址转换分析，改造成一个能够自动查询虚拟地址、读取 `satp`、解析页表并计算物理地址的调试工具。

**[RELY]**

依赖已有的 uCore 内核、QEMU、RISC-V GDB 环境，以及此前已经验证过的 Sv39 启动页表和地址映射关系。

**[GUARANTEE]**

不修改已有物理内存管理器，不影响 `make qemu` 正常启动，工具应保持独立、可复用，并能够输出真实调试结果。

**[SPECIFICATION]**

实现 `sv39walk <virtual_address>` 命令，自动显示分页模式、VPN 索引、PTE 信息、映射页大小以及对应物理地址。

### 9.2 环境适配阶段

**[PROMPT]**

当前 RISC-V GDB 可以正常连接 QEMU 和读取寄存器，但是执行 Python 脚本时提示不支持 Python scripting。希望在不重新安装工具链的前提下完成自动化调试。

**[RELY]**

依赖已经能够正常使用的 `make qemu`、`make gdb`，以及 GDB 原生命令脚本功能。

**[GUARANTEE]**

不重新配置课程环境，不修改内核启动代码，不破坏已经完成的 Lab2 基础实验。

**[SPECIFICATION]**

将原先的 Python-GDB 方案改为使用 GDB 的 `define`、`set`、`if`、`while`、`printf` 等原生命令实现。

### 9.3 功能完善阶段

**[PROMPT]**

现有 `sv39walk` 已经可以读取 `satp`、计算 VPN 并读取根页表项，希望进一步自动解析 PTE 标志位、识别叶子页表项、计算最终物理地址，并尝试支持 Sv39 三级页表。

**[RELY]**

依赖已验证的 `PTE[2]`、`PTE[511]` 和高虚拟地址映射数据，以及已有的 GDB 原生脚本。

**[GUARANTEE]**

不修改 uCore 页表内容，不将未经测试的功能描述为已经通过。

**[SPECIFICATION]**

增加 L2/L1/L0 查询逻辑、1 GiB/2 MiB/4 KiB 映射识别、物理地址计算及基本错误检查。

### 9.4 测试验证阶段

**[PROMPT]**

针对已经实现的 `sv39walk` 工具设计测试，验证高地址映射、低地址映射、无效页表项和非规范 Sv39 地址的处理结果。

**[RELY]**

依赖当前 QEMU 中实际存在的 Sv39 根页表，以及此前人工计算和 QEMU MMU 日志得到的参考结果。

**[GUARANTEE]**

所有测试结果以 GDB 实际输出为准，不使用预期值代替真实测试数据。

**[SPECIFICATION]**

使用四组地址：

```text
0xffffffffc02000d6
0x802000d6
0x40000000
0x0000008000000000
```

分别验证正常高地址转换、低地址恒等映射、无效 PTE 以及非规范地址检测，并保存对应输出截图。

以上为开发过程中关键 AI 交互意图的规范化整理，正式提交时应与实际 Prompt 使用记录合并保存。

## 10. 实验总结

本次加分题在 Lab2 已经完成的 GDB/MMU/Sv39 调试基础上，开发了一个使用 GDB 原生命令实现的自动化页表分析工具 `sv39walk`。

工具能够自动读取 `satp`、解析 Sv39 分页模式、计算 VPN 索引、查询页表项、识别叶子页表项并计算物理地址，同时能够针对无效 PTE 和非规范虚拟地址输出错误提示。

在实际测试中，完成了四组用例：

1. 内核高虚拟地址正确转换到物理地址 `0x802000d6`。
2. 内核低虚拟地址正确转换到相同的物理地址。
3. 对无效页表项正确输出错误信息。
4. 对非规范 Sv39 虚拟地址正确拒绝查询。

四组测试均获得预期结果。

通过本次实现，原本需要人工执行多条 GDB 命令的地址转换分析过程，被整合成一条可重复使用的 `sv39walk` 命令，提高了调试效率，也更加直观地展示了 Sv39 页表查询和物理地址计算过程。

此外，通过将脚本结果与之前的 QEMU `riscv_cpu_tlb_fill` 日志进行对照，进一步验证了工具在当前实验环境下的地址转换结果。

目前工具已经具备基本的教学演示价值，但 L1、L0 页表路径和更完整的访问权限检查还需要进一步测试和完善。后续可以继续扩展工具功能，并根据课程要求整理演示材料，尝试贡献至课件库。