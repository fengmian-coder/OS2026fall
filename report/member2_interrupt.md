# 操作系统实验三：中断与异常处理（成员二实验报告）

> 课程：操作系统（riscv64-ucore，2026 Fall）
> 实验：Lab3 断，都可以断
> 个人任务：`interrupt_handler(struct trapframe *tf)` 的实现与持续时钟中断验证
> 项目仓库：`fengmian-coder/OS2026fall`
> 验收分支：公共 `lab3`（本地独立工作区检出后测试）

## 一、实验目的与整体思路

本次实验是在前两次实验已实现的内核启动、物理内存管理基础上，增加 RISC-V 的 Trap 处理能力。Trap 包括异步的中断（Interrupt）和同步的异常（Exception），例如时钟事件属于中断，`ebreak` 与非法指令属于异常。处理器发生 Trap 后，需要保存现场、找到对应的处理函数，再恢复现场返回。由此，实验主要包括三个环节：汇编入口保存和恢复寄存器；C 语言分发并处理不同 Trap；通过断点、非法指令和持续时钟中断验证整个处理链路。

整体处理过程是：Trap 发生 → `__alltraps` 保存现场 → `trap(tf)` → `trap_dispatch(tf)` → `interrupt_handler()` 或 `exception_handler()` → `__trapret` 恢复现场并执行 `sret`。我负责其中的中断处理部分，重点保证时钟中断不仅能触发一次，而且能持续重新安排下一次事件。

## 二、任务分工与需求分析（Spec）

小组将 Lab3 分为三个部分：成员一负责 `trapentry.S` 中的恢复现场与汇编入口；成员二负责 `trap.c` 中的 `interrupt_handler()`；成员三负责 `exception_handler()` 和团队集成。我的代码修改集中在 `code/kern/trap/trap.c`，没有改动 `trap_dispatch()` 的既有分流方式。

针对本次提供的实际源码与自动测试，个人任务的具体约束如下：

1. 从 `tf->cause` 去除中断标志位，得到中断原因编号，并根据编号分发处理。
2. 对 supervisor timer 中断，调用 `clock_set_next_event()`，更新 `ticks`，并在 `ticks` 为 `TICK_NUM`（100）的整数倍时调用 `print_ticks()`。
3. 对 supervisor software 中断，输出 `Supervisor software interrupt`。
4. 对未知中断，输出包含原因编号的信息，以覆盖自动测试中的 `Unknown interrupt: cause 15`。
5. 保留 `trap_dispatch()`、其他成员的处理函数、已有数据结构与自动测试的评分条件；通过集成测试确认 `ticks=200` 与 Test 3 的通过结果。

## 三、核心代码实现及理解

### 3.1 中断原因的获取与分类

`trap_dispatch()` 根据 `tf->cause` 的最高位区分中断和异常。进入 `interrupt_handler()` 后，必须从 `scause` 中去掉最高位的中断标志，再读取剩余原因编号；否则直接拿完整值与 `IRQ_S_TIMER` 比较，无法正确识别时钟中断。我使用了以下写法：

```c
uintptr_t cause = tf->cause & (((uintptr_t)-1) >> 1);
```

这段表达式生成一个最高位为 0、其余位为 1 的掩码，使代码能够适应 `uintptr_t` 的机器字长。随后通过 `switch (cause)` 处理不同中断。其中本项目使用 `IRQ_S_TIMER = 5`、`IRQ_S_SOFT = 1`。

### 3.2 时钟中断处理

核心实现如下（节选自个人负责的处理逻辑）：

```c
case IRQ_S_TIMER:
    clock_set_next_event();
    ticks++;
    if (ticks % TICK_NUM == 0) {
        print_ticks();
    }
    break;
```

`clock_set_next_event()` 由 `clock.c` 提供，最终调用 SBI 定时器接口安排下一次时钟事件。它必须在每次时钟中断发生后再次执行，否则无法保证中断持续触发。`ticks` 是 `clock.c` 中定义的 `volatile size_t` 计数变量，用于记录累计收到的时钟中断次数。每满 100 次调用一次 `print_ticks()`，函数会输出固定文本 `100 ticks`，所以多次看到 `100 ticks` 并不表示计数器始终为 100，而是表示又累计经过了 100 次中断。

最终集成的公共分支还包含达到设定输出次数后通过 SBI 关机的逻辑。这属于团队集成版的附加结束行为；我个人实现与本次 Test 3 的核心验证点是重设定时器、累计 `ticks` 和周期输出。

### 3.3 软件中断和未知中断

```c
case IRQ_S_SOFT:
    cprintf("Supervisor software interrupt\n");
    break;
default:
    cprintf("Unknown interrupt: cause %d\n", (int)cause);
    break;
```

这两个分支分别处理 supervisor software 中断和本实验尚未单独实现的其他中断，保证出现相应事件时能够给出可识别的诊断信息。这里的兜底输出以本次实际测试脚本要求为准，未擅自增加与本模块无关的处理流程。

## 四、开发、调试与问题解决

### 4.1 单文件编译和团队代码依赖

最初在老师提供的起始代码中实现了 `interrupt_handler()`。第一次执行 `make` 时，编译到 `trap_dispatch()` 提示 `exception_handler` 尚未声明：这是成员三的函数尚未补全导致的。为单独检查自己的函数，我在本地临时加入前向声明，完成 `trap.c` 的编译检查后又将其移除，避免将临时措施提交到公共分支。

后续编译还在 `best_fit_pmm.c` 中出现 `free_list`、`nr_free` 未定义，原因是 Lab3 起始包需要补充上一实验的物理内存管理实现。组长的 `test/lab3-integration` 分支集成了 `best_fit_pmm.c` 和 `pmm.c` 的必要实现，此后工程可以完整编译。

### 4.2 uCore 启动失败与分页映射

集成版本使用 `make qemu` 时只显示 OpenSBI，没有出现 uCore 初始化信息。进一步排查发现，旧的 QEMU 镜像加载方式会使 OpenSBI 的 `Domain0 Next Address` 为 `0x0`。改为使用 `-kernel bin/ucore.img` 后，入口地址为 `0x80200000`，但内核仍未正常输出初始化信息。

使用 QEMU 异常日志 `-d int` 定位到 `exec_page_fault`，异常地址为 `0x80200038`。与反汇编比较后发现，这正是写入 `satp` 启用分页后的下一条指令附近。查看启动页表发现原先只有内核高虚拟地址映射，缺少启用分页时继续执行低地址指令所需的临时恒等映射。团队诊断时在 `entry.S` 的启动页表中补充 `0x80000000 → 0x80000000` 的 1 GiB 映射，并保留高地址映射。重新编译后，内核能够输出 DTB、内存分配初始化和持续时钟中断信息。

这部分属于小组集成排错，不是我负责的中断处理函数本身。

### 4.3 自动评分环境与断点指令长度

第一次运行原始 `make grade` 时三组均得 0 分，日志中三个测试的 OpenSBI 下一阶段入口地址均为 `0x0`。在独立诊断副本中，仅将评分脚本的 QEMU 加载参数改为 `-kernel bin/ucore.img` 后，测试能够正常进入内核，结果提升至 65/100：Test 2 为 25/25，Test 3 为 40/40，而 Test 1 的寄存器上下文测试失败。

通过反汇编发现，测试中的 `ebreak` 实际编码为 2 字节压缩指令，下一条指令地址比断点位置高 2 字节，而原异常处理代码统一令 `tf->epc += 4`。该行为会使异常返回位置错误。诊断副本将断点指令的返回地址改为根据指令低两位判断指令长度后，Test 1 通过。该修复由团队集成到公共版本。

上述调试体现了将启动、内存、Trap 入口、异常处理和测试脚本分开定位的重要性：单文件编译成功不代表内核能够启动，内核能够持续打印时钟输出也不等于所有自动测试已通过。

## 五、测试过程与结果

使用 WSL Ubuntu、RISC-V 交叉编译工具链及 QEMU，在从公共 `origin/lab3` 检出的独立工作区 `OS2026fall-lab3-public-check` 中执行：

```bash
make -C code clean
make -C code
make -C code grade
```

本地终端记录显示，编译完成并生成 `bin/kernel`、`bin/ucore.img`，三组自动测试全部通过：

| 测试项目 | 结果 | 说明 |
|---|---:|---|
| Test 1：Trap 入口、寄存器恢复与断点返回 | PASS（35/35） | 上下文恢复和断点返回通过 |
| Test 2：非法指令 EPC 推进与返回 | PASS（25/25） | 异常处理及相关兜底输出通过 |
| Test 3：持续 supervisor timer 中断 | **PASS（40/40）** | 能持续重新设置时钟事件并达到 `ticks=200` |
| **总分** | **100/100** | 公共 `lab3` 分支在本地测试通过 |

核心输出包括：

```text
++ setup timer interrupts
100 ticks
100 ticks
...
re-armed supervisor timer and repeated trap return: PASS (40/40)
Total Score: 100/100
```

**建议插图位置**（应使用实际保存的截图，未截图前不要当作已经提交）：

- `report/images/timer_ticks_200.png`：Test 3 的 PASS（40/40）与 `ticks=200` 证据。
- `report/images/lab3_grade_100.png`：公共 `lab3` 分支运行 `make -C code grade` 的 100/100 终端截图。
- 如组内统一整合，还可加入 uCore 启动、内存初始化与断点测试截图。

需要说明的是，以上成绩来自小组公共 `lab3` 版本在本地已调整 QEMU 加载参数后的环境；正式课程平台是否接受同样的启动设置，需要以助教要求为准。

## 六、AI 协助开发：Spec、Prompt 与人工核对

本次使用 AI 的主要方式不是一次性生成整个 Lab3，而是先明确自己负责的模块边界，再根据源码、编译器信息和自动测试反馈逐步迭代。

**Spec 的核心约束**：只实现 `interrupt_handler(tf)`；不修改分流接口；严格区分时钟、软件与未知中断；周期性重设定时器并更新计数；输出与测试脚本一致；不替其他成员实现异常处理或汇编恢复。

**实际交互过程摘要**（以下是根据操作记录整理的提示词需求，不是逐字原始 Prompt）：

1. 根据分工与起始源码，请 AI 说明成员二应修改哪些文件及函数，明确时钟中断、软件中断、未知中断的实现要求。
2. 提供 `trap.c` 内容，要求在 `YOUR CODE` 位置补充 `interrupt_handler()`，保留已有框架。
3. 提供编译错误与 `clock.c`、`riscv.h` 的常量定义，排查函数重复定义、`exception_handler()` 缺失和 Lab2 代码依赖问题。
4. 提供 `readelf`、`objdump` 和 QEMU 异常日志，分析分页启动问题；在独立目录中验证修复，而不是直接覆盖公共分支。
5. 提供 `make grade` 的实际输出，针对启动参数和压缩 `ebreak` 指令长度定位问题，再重新验收。

人工核对重点包括：检查补丁只新增所负责函数；用 `git diff --check` 排除格式问题；通过单独工作区保留原代码；对照 `tools/grade.sh` 的输出要求；所有重要判断均尽量由编译、运行和反汇编结果支持。相关逐字对话可根据需要附在 `report/prompt.md` 中，不宜把这里的摘要冒充完整原始提示词。

## 七、与操作系统原理的联系及未覆盖内容

本实验与理论课程联系最紧密的知识点有以下几个。

- **中断与异常的区别**：中断通常是异步硬件事件，异常与当前执行的指令同步发生；在 RISC-V 中它们统一通过 Trap 机制进入内核，但处理方式和返回地址推进规则并不相同。
- **CSR 与特权级控制**：`stvec` 指向 Trap 入口，`scause` 表明原因，`sepc` 保存异常返回位置，`sstatus` 与 `sie` 控制相关执行状态和中断使能。
- **上下文保存与恢复**：汇编层保存通用寄存器和关键 CSR，C 层处理具体原因，最后通过 `sret` 恢复执行流；两层必须严格使用一致的 TrapFrame 布局。
- **时钟中断的意义**：定时器为内核提供周期性执行机会，是以后实现时间片管理和抢占式调度的基础；本次只完成周期 Trap 处理，还没有实现完整的进程调度。
- **临界区与中断屏蔽**：对共享内核状态的修改，需要根据场景使用中断开关保护；但“关闭本地中断”并不等于通用的多核互斥机制。

本次实验尚未覆盖完整的用户态到内核态系统调用路径、进程调度、真实设备 I/O 中断处理及多核并发同步等内容。这些知识与 Trap 机制密切相关，但不应当作本次已经实现的功能。此外，组长明确说明今年的旧版 Challenge 不要求完成，因此没有实现 `lab3_switch_test()` 等遗留挑战功能。

## 八、实验总结

我负责的 `interrupt_handler()` 实现了 supervisor timer、supervisor software 和未知中断的基本处理。其中最关键的不是单纯将 `ticks` 加一，而是每次中断后都调用 `clock_set_next_event()`，让下一次中断能够继续到来。最终公共 `lab3` 分支在本地完成全新编译和 `make grade`，Test 3 获得 40/40，三组测试合计 100/100。

从开发过程来看，调试的主要困难并不总出现在刚写的函数中：起始代码的 Lab2 依赖、QEMU 加载参数、分页切换地址，以及异常指令的实际长度，都会影响最终表现。这次实验让我更清楚地理解了 Trap 从硬件事件、汇编现场到 C 函数处理再到返回的完整过程，也体会到将问题分层排查、保持个人分支与集成版本隔离的重要性。
