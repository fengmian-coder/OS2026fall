# Lab2：GDB 调试与 Sv39 地址转换机制分析

## 1. 实验目的

本部分实验使用 QEMU 和 GDB 调试 RISC-V 内核启动过程，重点观察 `satp` 寄存器的变化，理解 Sv39 分页机制、页表地址转换过程以及 TLB 的作用。

## 2. 实验环境

- 操作系统：Ubuntu（WSL2）
- 目标架构：RISC-V 64
- 模拟器：QEMU
- 调试工具：RISC-V GDB
- 实验内核：uCore

## 3. 实验过程

### 3.1 启动 QEMU 与 GDB

在 `code` 目录下启动 QEMU：

```bash
qemu-system-riscv64 \
    -machine virt \
    -nographic \
    -bios default \
    -kernel bin/ucore.img \
    -s -S
```

其中，`-s` 用于启动 GDB 调试服务，`-S` 使 CPU 在启动时暂停，等待调试器连接。

在另一个终端执行：

```bash
make gdb
```

连接成功后，设置内核物理入口断点：

```gdb
break *0x80200000
continue
info registers pc
info registers satp
```

得到：

```text
pc   = 0x80200000
satp = 0x0
```

此时内核已经到达物理入口地址，但还没有启用 Sv39 分页。

### 3.2 观察 Sv39 分页开启过程

使用 GDB 反汇编内核入口附近的指令：

```gdb
x/35i $pc
```

发现两条关键指令：

```asm
0x80200034: csrw satp,t0
0x80200038: sfence.vma
```

在写入 `satp` 之前设置断点：

```gdb
break *0x80200034
continue
```

检查寄存器：

```text
pc   = 0x80200034
t0   = 0x8000000000080205
satp = 0x0
```

执行单步调试：

```gdb
si
info registers pc
info registers satp
```

结果为：

```text
pc   = 0x80200038
satp = 0x8000000000080205
```

说明执行 `csrw satp,t0` 后，CPU 的地址转换模式已经由 Bare 切换为 Sv39。

对 `satp` 的分析如下：

- MODE = 8：使用 Sv39 分页模式。
- ASID = 0：地址空间标识符为 0。
- PPN = 0x80205：根页表的物理页号。

因此根页表物理地址为：

```text
0x80205 × 4096 = 0x80205000
```

接着执行 `sfence.vma`：

```gdb
si
info registers pc
info registers satp
```

得到：

```text
pc   = 0x8020003c
satp = 0x8000000000080205
```

说明 `sfence.vma` 已执行，`satp` 的值保持不变。该指令用于同步地址转换相关状态，使后续地址转换遵循相应的页表配置。

### 3.3 分析 Sv39 根页表

使用 GDB 查看根页表：

```gdb
x/4gx 0x80205000
```

结果：

```text
0x80205000:
0x0000000000000000  0x0000000000000000

0x80205010:
0x00000000200000cf  0x0000000000000000
```

因此：

```text
PTE[2] = 0x200000cf
```

继续查看根页表的第 511 项：

```gdb
x/gx 0x80205ff8
```

结果：

```text
PTE[511] = 0x200000cf
```

两个页表项具有相同的值，说明它们映射到同一个物理内存区域。

其中，`0x200000cf` 的低位标志包含：

- V = 1：有效
- R = 1：可读
- W = 1：可写
- X = 1：可执行
- A = 1：已访问
- D = 1：已修改

由于这两个页表项位于 Sv39 根页表中，并且设置了读写执行权限，因此它们是叶子页表项，形成 1 GiB 大页映射。

Sv39 通常采用三级页表结构，但允许在较高层级直接使用大页。本实验启动阶段的映射就在根页表层级结束，没有继续访问下两级页表。

### 3.4 验证虚拟地址与物理地址转换

内核的高虚拟地址为：

```text
0xffffffffc02000d6
```

在 GDB 中计算该地址的 VPN[2]：

```gdb
p/x (0xffffffffc02000d6 >> 30) & 0x1ff
```

结果：

```text
$1 = 0x1ff
```

即根页表索引为 511。

对低地址进行同样的计算：

```gdb
p/x (0x802000d6 >> 30) & 0x1ff
```

结果：

```text
$2 = 0x2
```

即根页表索引为 2。

由于两个根页表项均指向物理地址 `0x80000000` 开始的 1 GiB 区域，因此对于该高虚拟地址：

```text
物理地址 = 0x80000000 + (虚拟地址 & 0x3fffffff)
```

在 GDB 中执行：

```gdb
p/x 0x80000000 + (0xffffffffc02000d6 & 0x3fffffff)
```

结果：

```text
$3 = 0x802000d6
```

说明：

```text
高虚拟地址：0xffffffffc02000d6
对应物理地址：0x802000d6
```

这验证了当前页表的高地址映射关系。

### 3.5 验证内核高虚拟地址执行

在 `kern_init()` 入口设置断点：

```gdb
break *0xffffffffc02000d6
continue
```

成功命中断点：

```text
Breakpoint 3, kern_init () at kern/init/init.c:30
```

查看寄存器：

```text
pc   = 0xffffffffc02000d6
satp = 0x8000000000080205
```

进一步反汇编：

```gdb
x/4i $pc
```

得到：

```asm
0xffffffffc02000d6 <kern_init>:    auipc a0,0x6
0xffffffffc02000da <kern_init+4>:  addi  a0,a0,-190
0xffffffffc02000de <kern_init+8>:  auipc a2,0x6
0xffffffffc02000e2 <kern_init+12>: addi  a2,a2,-102
```

说明 CPU 已经成功到达内核高虚拟地址，且仍处于 Sv39 分页模式。

## 4. TLB 机制分析

TLB（Translation Lookaside Buffer）用于缓存虚拟地址到物理地址的转换信息。

当 CPU 访问虚拟地址时，如果 TLB 中已有对应的有效转换信息，就可以减少页表访问开销；否则可能需要通过硬件页表遍历获得映射结果。

本实验在内核入口观察到了：

```asm
csrw satp,t0
sfence.vma
```

其中，`csrw satp,t0` 配置分页模式和根页表位置；`sfence.vma` 用于同步地址转换相关状态，避免继续使用不符合当前页表配置的旧转换信息。

本次实验没有直接读取 TLB 内部条目，也没有测量 TLB 命中率。因此，实验结论主要基于对 `satp`、`sfence.vma`、页表项以及内核地址转换过程的观察。

## 5. 实验总结

通过本次调试，我理解了 RISC-V 内核开启 Sv39 分页的基本过程。内核首先准备根页表地址，将 Sv39 配置写入 `satp`，再执行 `sfence.vma` 同步地址转换状态，最后跳转到高虚拟地址继续执行。

实验中读取到的 `PTE[2]` 和 `PTE[511]` 均为 `0x200000cf`，验证了低地址与高地址映射到同一物理内存区域的关系。通过计算 VPN[2] 和物理地址，也进一步理解了 Sv39 的地址转换方法。

本实验使用的是根页表级别的 1 GiB 大页映射，因此虽然 Sv39 支持三级页表，实际启动映射并不需要完整遍历三级页表。

通过 GDB 逐条观察指令和寄存器变化，比单纯阅读源码更容易理解 MMU 的初始化过程及其与 TLB 的关系。