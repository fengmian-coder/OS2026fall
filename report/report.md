# 操作系统实验报告

## 实验基本信息

|项目|内容|
|-|-|
|**实验名称**|Lab 1：RISC-V 最小内核启动与调试|
|**小组成员**|2414029-张馨月、2412074-贾璐菡、2411785-李文慧|
|**完成日期**|2026-10-09|

### 小组分工

|成员|负责的练习/模块|
|-|-|
|2414029-张馨月|练习1：`entry.S` 启动入口分析；`la sp, bootstacktop` 与 `tail kern\\\_init`；GDB 单步验证；启动参数问题定位与修复|
|2412074-贾璐菡|练习2：使用 GDB 跟踪 `0x1000 → 0x80000000 → 0x80200000` 的启动流程|
|2411785-李文慧|Makefile 构建流程、`kernel.ld`、ELF/BIN、QEMU/OpenSBI/uCore 关系分析；整体报告整合与最终运行验证|

实验报告由三名成员分别完成对应草稿与实验验证，最后由李文慧统一整理、校对和合并为正式报告。

\---

## 一、实验目的

本实验围绕 RISC-V 平台下最小 uCore 内核的构建、启动和调试展开，主要目标如下：

1. 理解 RISC-V 系统从 CPU 复位、进入 OpenSBI，再进入 uCore 内核的完整启动流程。
2. 理解 `entry.S` 中 `la sp, bootstacktop` 与 `tail kern\\\_init` 的作用，掌握内核启动栈和 C 语言入口之间的关系。
3. 理解 Makefile、链接脚本 `kernel.ld`、ELF 内核文件与原始二进制镜像之间的关系。
4. 掌握使用 QEMU 和 GDB 对系统启动过程进行单步跟踪、断点验证和寄存器观察的方法。
5. 通过实际构建、运行和调试结果验证 AI 辅助分析的正确性，避免只依赖文字解释而缺少实验依据。

\---

## 二、实验环境

本实验在 Windows 的 WSL2 Ubuntu 环境中完成，目标体系结构为 RISC-V 64 位，使用交叉编译工具链、QEMU 与 GDB 完成构建和调试。

主要工具包括：

* `riscv64-unknown-elf-gcc`
* `riscv64-unknown-elf-ld`
* `riscv64-unknown-elf-objcopy`
* `riscv64-unknown-elf-objdump`
* `riscv64-unknown-elf-gdb` / `gdb-multiarch`
* QEMU `virt` 机器
* OpenSBI
* Git / GitHub
* ChatGPT（用于实验规划、原理分析、命令指导、错误排查与报告整理）

AI 使用情况如下：

|成员|AI 编程工具|底层模型|备注|
|-|-|-|-|
|2414029-张馨月|Claudecode|DeepSeek-flash|用于练习1原理分析、反汇编/GDB验证、问题复核|
|2412074-贾璐菡|ChatGPT|GPT-6|用于 GDB 启动跟踪、环境与 Git 问题排查|
|2411785-李文慧|ChatGPT|GPT-5.6 Sol|用于整体构建与启动流程分析、验证和报告整合|

\---

## 三、实验整体逻辑分析

### 3.1 本实验的逻辑主线

Lab 1 的核心不是实现复杂的操作系统功能，而是理解“一个最小内核如何被构建出来，并最终获得 CPU 控制权开始运行”。

从构建角度看，实验流程为：

```text
C / 汇编源代码
        ↓
riscv64-unknown-elf-gcc
        ↓
目标文件 .o
        ↓
riscv64-unknown-elf-ld + tools/kernel.ld
        ↓
bin/kernel（ELF）
        ↓
objcopy -O binary
        ↓
bin/ucore.img（原始二进制镜像）
```

从启动角度看，实验主线为：

```text
CPU 复位入口
0x1000
   ↓
OpenSBI
0x80000000
   ↓
uCore 内核入口
0x80200000
   ↓
kern\\\_entry
   ↓
设置内核栈
   ↓
kern\\\_init
   ↓
输出 "(THU.CST) os is loading ..."
```

因此，本实验把“构建出的内核文件”和“CPU 实际执行到内核入口”两个过程连接起来。

### 3.2 Makefile 与内核构建流程

由于实验主机是 x86\_64，而目标系统是 RISC-V 64 位，因此使用 `riscv64-unknown-elf-` 工具链进行交叉编译。

Makefile 中主要经历：

1. 使用 GCC 编译 C 与汇编源文件，生成 `.o` 目标文件。
2. 使用 `riscv64-unknown-elf-ld`，并通过 `-T tools/kernel.ld` 指定链接脚本。
3. 链接生成 `bin/kernel`。
4. 使用 `objcopy -O binary` 将 ELF 转换为 `bin/ucore.img`。

实际使用 `file`、`readelf`、`nm` 等工具验证后，可以确认：

* `bin/kernel` 是 RISC-V 64 位 ELF 可执行文件；
* `bin/ucore.img` 是去除 ELF 元数据后的原始二进制镜像；
* ELF 入口地址为 `0x80200000`；
* `kern\\\_entry` 符号位于 `0x80200000`。

### 3.3 `kernel.ld` 与内核内存布局

`tools/kernel.ld` 指定了：

```text
OUTPUT\\\_ARCH(riscv)
ENTRY(kern\\\_entry)
BASE\\\_ADDRESS = 0x80200000
```

其中：

* `OUTPUT\\\_ARCH(riscv)` 指定目标体系结构；
* `ENTRY(kern\\\_entry)` 指定 ELF 的入口符号；
* `BASE\\\_ADDRESS = 0x80200000` 指定内核链接基址。

链接脚本还安排了 `.text`、`.rodata`、`.data`、`.sdata` 和 `.bss` 等节，使内核在链接阶段就形成确定的地址布局。

实际验证得到：

```text
kern\\\_entry    = 0x80200000
kern\\\_init     = 0x8020000a
bootstack     = 0x80201000
bootstacktop  = 0x80203000
```

其中 `bootstacktop - bootstack = 0x2000 = 8192` 字节，即启动栈大小为 8 KiB。

### 3.4 QEMU、OpenSBI 与 uCore 的关系

老师提供的原始 Lab 1 Makefile 使用：

```text
-device loader,file=$(UCOREIMG),addr=0x80200000
```

其设计意图是由 QEMU 将内核镜像放到 `0x80200000`，OpenSBI 完成底层初始化后再将控制权交给下一阶段。

在本组当前实验环境中，张馨月通过 QEMU 与 GDB 对照调试发现，原来的 `-device loader` 启动方式没有正确进入 `0x80200000` 的内核入口。随后将 `qemu` 和 `debug` 目标修改为：

```text
-kernel $(UCOREIMG)
```

修改后 OpenSBI 正确报告：

```text
Domain0 Next Address : 0x80200000
Domain0 Next Mode    : S-mode
```

并成功进入 uCore。

需要区分三者职责：

* QEMU：模拟 RISC-V 硬件环境并准备内核镜像；
* OpenSBI：运行在 M 模式，完成底层初始化，并向 S 模式操作系统提供 SBI 服务；
* uCore：从 `0x80200000` 的 `kern\\\_entry` 开始执行内核代码。

\---

## 四、实验内容与实现

### 4.1 练习1：理解内核启动中的程序入口操作

**负责人：** 2414029-张馨月

题目要求分析 `kern/init/entry.S` 中：

```asm
kern\\\_entry:
    la sp, bootstacktop
    tail kern\\\_init
```

#### 4.1.1 `la sp, bootstacktop`

`la` 是 load address 伪指令，该指令将 `bootstacktop` 的地址装入栈指针寄存器 `sp`。

其作用可以表示为：

```text
sp = \\\&bootstacktop
```

这样做的目的是在进入 C 语言函数 `kern\\\_init()` 前建立一个合法的内核栈。

本实验中：

```text
bootstack     = 0x80201000
bootstacktop  = 0x80203000
```

二者相差：

```text
0x80203000 - 0x80201000 = 0x2000 = 8192 字节
```

因此启动栈大小为 8 KiB。

RISC-V 栈从高地址向低地址增长，因此将 `sp` 初始化为 `bootstacktop`，表示栈初始为空，并可向低地址方向使用。

需要注意：真正为栈预留空间的是：

```asm
.space KSTACKSIZE
```

而 `la sp, bootstacktop` 只是让 `sp` 指向已经预留好的栈顶。

#### 4.1.2 `tail kern\\\_init`

`tail kern\\\_init` 将控制流直接转移到 C 语言实现的 `kern\\\_init()`。

其语义相当于不保存返回地址的跳转，目标寄存器为 `x0`，因此不会像普通函数调用那样要求从 `kern\\\_init()` 返回到 `entry.S`。

这里使用 `tail` 是合理的，因为 `kern\\\_init()` 被设计为内核启动后的主入口，函数末尾进入无限循环，运行过程中不会返回。

#### 4.1.3 反汇编与 GDB 验证

反汇编结果显示：

```text
0000000080200000 <kern\\\_entry>:
    80200000: 00003117   auipc sp,0x3
    80200004: 00010113   addi  sp,sp,0
    80200008: a009       c.j   8020000a <kern\\\_init>
```

说明：

* `la sp, bootstacktop` 最终形成设置 `sp` 的真实机器指令；
* `tail kern\\\_init` 经链接器处理后最终跳转到 `0x8020000a` 的 `kern\\\_init`。

动态调试也验证了该过程。

!\[GDB 在 kern\_entry 断点处停下](./images/exercise1\_gdb\_entry.png)

执行设置栈指针的指令后：

```text
sp = 0x80203000
```

与 `bootstacktop` 完全一致。

!\[单步后 sp = 0x80203000](./images/exercise1\_gdb\_stack.png)

继续单步后：

```text
pc = 0x8020000a
```

进入 `kern\\\_init()`。

!\[单步跳转到 kern\_init](./images/exercise1\_gdb\_jump.png)

### 4.2 启动问题的发现、定位与修复

\*\*负责人：\*\*2414029-张馨月

在最初实验中，使用：

```text
-device loader,file=$(UCOREIMG),addr=0x80200000
```

启动 QEMU 时，OpenSBI 能正常显示，但内核没有输出：

```text
(THU.CST) os is loading ...
```

在 `make debug` 下设置 `0x80200000` 断点也无法命中。

为排除内核本身的问题，张馨月保持同一份内核镜像和工具链不变，只修改 QEMU 启动选项进行对照实验。将启动参数改为：

```text
-kernel $(UCOREIMG)
```

后，OpenSBI 报告：

```text
Domain0 Next Address : 0x80200000
```

并成功进入 uCore。

因此本组最终在 `qemu` 和 `debug` 两个目标中采用 `-kernel $(UCOREIMG)`。

这一过程形成了完整的问题处理闭环：

```text
发现现象
   ↓
GDB 验证没有进入内核
   ↓
对照启动参数
   ↓
修改为 -kernel
   ↓
QEMU 与 GDB 再次验证
   ↓
问题解决
```

### 4.3 练习2：使用 GDB 验证 RISC-V 启动流程

**负责人：** 2412074-贾璐菡

本练习使用 QEMU 和 GDB 跟踪：

```text
0x1000 → 0x80000000 → 0x80200000
```

#### 4.3.1 CPU 复位入口 `0x1000`

连接 GDB 后执行：

```gdb
info registers pc
x/10i 0x1000
```

观察到：

```text
pc = 0x1000
```

以及复位代码中的关键指令：

```asm
0x1000: auipc t0,0x0
0x1004: addi  a2,t0,40
0x1008: csrr  a0,mhartid
0x100c: ld    a1,32(t0)
0x1010: ld    t0,24(t0)
0x1014: jr    t0
```

其中 `jr t0` 将控制权跳转到下一阶段入口。

!\[CPU 复位入口及启动指令](./images/gdb\_reset\_1000.png)

#### 4.3.2 OpenSBI 入口 `0x80000000`

执行：

```gdb
si 6
```

GDB 显示：

```text
0x0000000080000000 in ?? ()
```

说明复位代码执行完间接跳转后，PC 已经进入 `0x80000000` 的 OpenSBI 固件入口。

!\[进入 OpenSBI 固件入口](./images/gdb\_opensbi.png)

#### 4.3.3 uCore 内核入口 `0x80200000`

在 GDB 中设置：

```gdb
break \\\*0x80200000
continue
```

断点成功命中：

```text
Breakpoint 1, kern\\\_entry () at kern/init/entry.S:7
```

继续查看 PC：

```text
pc = 0x80200000 <kern\\\_entry>
```

由此确认 OpenSBI 已将执行控制权交给 uCore 内核。

!\[到达 uCore 内核入口](./images/gdb\_kernel\_80200000.png)

最终验证得到：

|启动阶段|PC 地址|验证方法|结果|
|-|-:|-|-|
|CPU 复位代码|`0x1000`|`info registers pc`、`x/10i`|成功|
|OpenSBI 固件入口|`0x80000000`|`si 6`|成功|
|uCore 内核入口|`0x80200000`|`break`、`continue`|成功|

### 4.4 整体构建与启动验证

**负责人：** 2411785-李文慧

李文慧从 Makefile、`kernel.ld`、ELF/BIN 和运行结果几个角度对整体流程进行交叉验证。

首先执行：

```bash
make clean
make
```

成功生成：

```text
bin/kernel
bin/ucore.img
```

使用 `file`、`readelf` 与 `nm` 检查后，确认：

```text
bin/kernel：RISC-V 64 位 ELF
ELF Entry：0x80200000
kern\\\_entry：0x80200000
kern\\\_init：0x8020000a
bootstack：0x80201000
bootstacktop：0x80203000
```

然后执行：

```bash
make qemu
```

OpenSBI 输出中可以观察到：

```text
Firmware Base        : 0x80000000
Domain0 Next Address : 0x80200000
Domain0 Next Mode    : S-mode
```

随后内核输出：

```text
(THU.CST) os is loading ...
```

说明从构建、链接、镜像准备、OpenSBI 初始化到进入 `kern\\\_init` 的完整流程已经成功。

\---

## 五、测试与验证

### 5.1 最终 QEMU 启动

执行：

```bash
make qemu
```

OpenSBI 正常启动，并显示下一阶段地址为 `0x80200000`。

!\[OpenSBI 启动及 Next Address](./images/exercise1\_qemu\_start.png)

随后 uCore 输出：

```text
(THU.CST) os is loading ...
```

!\[uCore 内核成功启动](./images/exercise1\_qemu\_success.png)

### 5.2 最终运行验证

李文慧在合并最新 `lab1` 后重新执行构建和启动，确认最终版本仍可正常运行。

!\[李文慧 make qemu 运行验证](./images/make\_qemu.png)

### 5.3 GDB 启动路径验证

GDB 验证了三个关键地址：

```text
0x1000
   ↓
0x80000000
   ↓
0x80200000
```

分别对应 CPU 复位入口、OpenSBI 固件入口和 uCore 内核入口。

同时，练习1通过 GDB 单步确认：

```text
kern\\\_entry
   ↓
sp = bootstacktop = 0x80203000
   ↓
kern\\\_init = 0x8020000a
```

因此静态分析、链接结果、QEMU 输出和 GDB 动态调试结果相互一致。

### 5.4 关于 `make grade`

当前课程提供的 Lab 1 源码中，Makefile 虽然保留了 `grade` 目标，但实际工程中未提供 `tools/grade.sh`。因此本组没有伪造或自行补写测试脚本来制造“全部通过”的结果，而是使用 `make qemu`、ELF/符号检查以及 GDB 启动跟踪作为本实验的实际验证依据。

\---

## 六、实验总结与收获

### 6.1 对操作系统的理解

本实验中最重要的知识点包括：

1. **系统启动的分层交接**

   CPU 并不是上电后直接执行 C 语言内核代码，而是经历：

```text
   复位代码 → 固件 OpenSBI → uCore 内核
   ```

每一阶段只负责自己的初始化任务，并把执行权交给下一阶段。

2. **特权级与 SBI**

   OpenSBI 运行在 Machine Mode，uCore 运行在 Supervisor Mode。S 模式内核可以通过 SBI 接口请求 M 模式固件提供底层服务。

3. **链接脚本与内存布局**

   `kernel.ld` 决定内核的入口地址和各节在内存中的布局。源代码中的符号只有经过汇编和链接后，才会得到最终地址。

4. **内核栈与函数调用**

   在进入 C 代码前必须先建立可用栈。`la sp, bootstacktop` 完成栈指针初始化，而 `tail kern\\\_init` 把执行权交给 C 语言内核入口。

5. **交叉编译、ELF 与 BIN**

   `bin/kernel` 保留 ELF 头、段、符号及调试信息，适合链接分析和 GDB 调试；`bin/ucore.img` 是原始二进制镜像，适合由虚拟机准备到内存中运行。

6. **GDB 是启动问题的重要验证工具**

   仅看到 QEMU 没有输出时，很难判断问题来自内核、固件还是启动参数。通过观察 PC、断点和单步执行，可以直接确认 CPU 当前执行位置，从而缩小问题范围。

### 6.2 OS 原理中重要但本实验尚未涉及的内容

Lab 1 主要关注“内核如何开始运行”，还没有真正进入完整操作系统功能实现，因此以下重要内容尚未在本实验中体现：

* 虚拟内存和页表管理；
* 中断与异常处理的完整机制；
* 进程与线程调度；
* 系统调用；
* 用户态与内核态程序交互；
* 文件系统；
* 同步与并发控制；
* 设备驱动和 I/O 管理。

这些内容建立在“内核已经成功获得 CPU 控制权并完成基础运行环境初始化”的基础之上，因此 Lab 1 是后续实验的重要起点。

### 6.3 AI 协作开发的经验

本次 Lab 1 中 AI 主要承担实验规划、命令指导、技术解释、故障排查和报告整理工作，但实验结论最终均以真实代码、终端输出、QEMU 和 GDB 结果为依据。

实验过程中也出现过 AI 初始解释不够准确的情况，例如：

* 曾把 `call` 写返回地址寄存器误描述为自动写入栈；
* 对 `tail` 展开形式的描述过于简单，没有区分未链接目标文件与最终链接结果；
* 一度对 QEMU 启动失败原因做过缺乏证据的推测；
* 李文慧早期分析仍按照原始 `-device loader` 启动方式理解，随后根据张馨月实际调试和最终仓库状态进行修正。

这些问题说明，在操作系统实验中不能直接把 AI 的回答作为结论。更可靠的方法是：

```text
阅读真实源码
   ↓
提出问题并获得 AI 分析
   ↓
使用编译、readelf、nm、objdump、QEMU、GDB 验证
   ↓
发现矛盾
   ↓
修正 Prompt 或结论
   ↓
重新实验确认
```

通过这种方式，AI 更适合作为“分析与调试助手”，而不是代替实验过程本身。

\---

## 附：AI Prompt 与协作记录说明

三名成员均将各自实际使用的 AI Prompt 与协作过程单独整理并保存在仓库中：

```text
report/prompt-member1.md
report/prompt-member2.md
report/prompt-member3.md
```

其中，张馨月的 Prompt 记录保存在 `report/prompt-member1.md`，主要涵盖练习1的原理分析、反汇编与 GDB 验证、启动问题定位，以及对 AI 初始回答的复核和纠正过程。

贾璐菡的 Prompt 记录保存在 `report/prompt-member2.md`，主要涵盖实验任务规划、WSL/GitHub 环境与认证问题排查、QEMU/GDB 启动跟踪、截图整理、实验报告撰写和 Git 协作提交过程。

李文慧的 Prompt 记录保存在 `report/prompt-member3.md`，采用 `\[PROMPT]`、`\[RELY]`、`\[GUARANTEE]`、`\[SPECIFICATION]` 四部分结构约束 AI 的分析范围，并结合实际编译、QEMU 运行和仓库最终状态对整体构建与启动流程进行验证和修正。

三份 Prompt 记录均以各成员实际实验过程为依据，用于说明 AI 在实验中的辅助作用；最终技术结论仍以源码、编译结果、QEMU 输出和 GDB 调试结果为准。

