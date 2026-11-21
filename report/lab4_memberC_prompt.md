# Lab4 成员 C：AI 辅助 Prompt 记录与可复用提示词（待微调）

> **实验**：操作系统实验四 Lab4 进程管理
> **成员**：C，李文慧，2411785
> **个人分支**：`lab4-lwh`；主要提交：`2112ba6`、`22eccaf`
> **合并 PR**：[PR #24](https://github.com/fengmian-coder/OS2026fall/pull/24)；公共分支 `lab4` 合并提交：`a3bcaa9`
> **原始交流依据**：`ChatGPT-oslwh-lab4-20261121-2317.pdf` 及随后与 AI 讨论的修复、Git 合并记录。
> **真实性说明**：本文的“用户原话摘录”来自实际交流；其余 Prompt 属于**事后整理的可复用版本**，不是伪造的逐字历史聊天。实际开发过程中 AI 曾给出不完善的代码建议，最终以源码和运行结果为准。

## 一、我使用 AI 的目标与边界

本次我只负责功能模块三，即：

- `cpu_idle()`：`code/kern/process/proc.c`
- `schedule()`：`code/kern/schedule/sched.c`
- `proc_run()`：`code/kern/process/proc.c`
- `switch_to()`：`code/kern/process/switch.S`

交互之前我提供了 Lab4 实验指导书、原始代码和分工说明，并说明小组已完成 A（页操作）和 B（进程创建），不能把别的成员代码覆盖掉。后续实验进入集成阶段时，我继续用 AI 辅助排查 Makefile、测试脚本、QEMU 和 GitHub PR，但这些属于联合集成修复，不改变原先的模块分工。

## 二、交互记录摘要（可与原始 PDF 对照）

| 阶段 | 用户实际问题或提问意图 | AI 提供的帮助 | 我如何验证、修正 |
|---|---|---|---|
| 分工与启动 | 原话包括：“这次的实验4我负责分工是成员C……接下来做我的分工部分” | 明确四个函数、确认 A/B 已有提交及所依赖的头文件 | `git fetch`、`git log`、检查 function3 标记和 `proc.h` |
| 线程 Context | 要求核对 Context 字段、`pgdir` 与调度接口 | 解释 `ra/sp/s0~s11` 的 RV64 保存顺序及 satp 的处理 | 比较 `proc.h` 与 `switch.S` 逐项偏移 |
| `switch_to` | 逐步实现汇编保存/恢复上下文 | 给出 14 条 `sd`、14 条 `ld` 和 `ret` | `cat -n`、`git diff --check`；修正 CRLF |
| `proc_run` | 如何在切换时处理中断、页表和 TLB | 给出更新 `current`、写 `satp`、刷新 TLB、调用 `switch_to` 的方案 | 阅读 B 的 `pgdir` 设置，后来整合为“不同页表时才切换” |
| `schedule` | 选择可运行线程、如何处理空链表和循环边界 | 提出遍历 `proc_list`，找 `PROC_RUNNABLE`，必要时回退 idle | 初版存在边界与大括号/重复代码问题，按真实编译信息修正 |
| `cpu_idle` | 连接空闲循环与调度器 | 实现 `need_resched` 触发 `schedule` | 检查 `kern_init` 已调用 `cpu_idle` |
| 整体编译 | `make -C code` 输出错误 | 区分源码编译、链接、Makefile 构建目标问题 | 修复 `sched.c` 重复行，完整生成 `bin/kernel` 与 `bin/ucore.img` |
| QEMU 启动 | OpenSBI 后不输出 / 反复输出异常信息 | 对照加载参数、DTB、入口、日志 | 统一 `-kernel $(UCOREIMG)`，能执行到 `initproc` |
| Lab4 自动测试 | `make grade` 仍残留 Lab3 注入逻辑 | 调整 `inject.py/grade.sh/Makefile` 测试入口 | `lab4 injection complete`，PMM/Create/Schedule 均 OK |
| 兼容宏 | `SSTATUS_SUM` undeclared | 检查 `libs/riscv.h` 中 `SSTATUS_PUM` | 再次编译、链接成功 |
| Panic / `K>` | QEMU 被 `K>` 刷屏 | 定位 `proc.c` 的 `do_exit` 占位 Panic 和 `kmonitor` 循环 | 调用 SBI 关机用于结束当前演示，不误报退出机制已实现 |
| GitHub 合并 | 个人分支提交后如何合并公共分支 | 检查 PR #24 的 2 个提交、13 个文件和 `switch.S` | `lab4-lwh → lab4`；再次清理编译与评分，30/30 |

实际交流中有一条原话是：**“下面是 schedule.c 完整代码，你给我完整可替换的版本”**。这次要求产生的替换稿保留 `wakeup_proc()` 和 function3，解决了文件末尾重复的中断恢复代码。记录中其他条目则采用意图摘要，避免误当逐字 Prompt。

## 三、关键 SPEC 与整理后的 Prompt（可复用版）

### Prompt 1：分工核对与整体分析

**SPEC**：仅分析 Lab4 功能模块三，确认 A/B 已合并的代码和 function3 的位置；输出四个函数的依赖与完成顺序，不允许直接覆盖成员 B 的 function2。

```text
请阅读我提供的《riscv64-ucore Lab4 指导书》、ABC 分工说明以及当前仓库中 proc.h、proc.c、sched.c、switch.S 的源码。
我负责成员 C（进程调度与上下文切换），只实现 cpu_idle、schedule、proc_run、switch_to 四个函数。
成员 A 已完成页表操作，成员 B 已完成 alloc_proc、proc_init、kernel_thread、do_fork、copy_thread、copy_mm。
请先说明这四个函数在启动链路中的作用、使用的数据结构和需要检查的接口；不要修改 A/B 的代码。最后给出逐步验证命令。
```

### Prompt 2：`switch_to()` 汇编

**SPEC**：按 `struct context` 字段顺序，使用 RV64 `sd`/`ld` 保存恢复 14 个寄存器，避免破坏 a0/a1 的上下文指针，在末尾 `ret`。

```text
下面是当前 proc.h 中的 struct context 和 switch.S 骨架。
请严格按照 ra、sp、s0~s11 的字段顺序，计算 RV64 结构体字段偏移并实现 switch_to(struct context *from, struct context *to)。
a0 指向旧上下文，a1 指向新上下文。请只保存 ABI 要求跨函数调用保留的寄存器，解释每条偏移为何是 8 字节递增，保留 LAB4 function3 学号标记。
实现后请给出静态核对方法以及 git diff --check、编译命令。
```

**验收结果**：字段偏移为 `0,8,16,...,104`；`struct context` 共 112 字节。编辑时遇到 CRLF，改用 LF 后检查通过。

### Prompt 3：`proc_run()` 线程切换

**SPEC**：目标等于当前进程时不重复切换；在本 CPU 的中断保护区更新 `current`；检查 `pgdir` 的物理地址约定，按需切换 satp、失效 TLB；调用 `switch_to`。

```text
我在 Lab4 成员 C 中实现 proc_run。请先根据实际 proc.c、proc.h、pmm.h 和 riscv.h 判断 proc->pgdir 保存的是物理地址还是虚拟地址。
要求：仅在 proc != current 时执行；先保存旧 current，关闭并记住原中断状态；更新 current；只有新旧页表不同时才切换 satp 并执行 sfence.vma；随后 switch_to(&prev->context, &proc->context)，最后恢复中断状态。
请解释为什么这个顺序必要，以及 Lab4 内核线程共享页表时的简化与局限，不要直接更改 B 的 do_fork/copy_mm。
```

**验收结果**：最终代码采用 `prev->pgdir != proc->pgdir` 条件判断；编译通过，并参与集成评分。

### Prompt 4：`schedule()` 进程选择

**SPEC**：中断临界区内清除标志；从 `proc_list` 环形遍历，跳过链表头，只选 `PROC_RUNNABLE`；找不到则退回 idle；确保一圈内退出，不重复执行中断恢复。

```text
当前工程有 proc_list、idleproc、current、list_next、le2proc 和 wakeup_proc。
请帮我实现 schedule：从 current 的下一节点开始按链表顺序寻找 PROC_RUNNABLE；current 是 idleproc 时从链表头开始；遍历时跳过头节点；最多检查一整圈；没有合适进程时选择 idleproc。
调度过程要保存并关闭中断，清除 need_resched，更新 next->runs，在 next != current 时调用 proc_run(next)，最后正确恢复中断。
请分析空链表、只有一个普通线程、多个就绪线程、全为不可运行线程的边界情况。保留原 wakeup_proc，不要重复闭合大括号。
```

**AI 输出中的问题及改进**：初期给出的遍历草稿存在边界不够清楚的问题，实际文件还出现过函数后重复的 `local_intr_restore`，导致编译报错。后续根据编译日志、链表接口和完整 `sched.c` 重写，最终评分通过。

### Prompt 5：`cpu_idle()`

**SPEC**：空闲执行流仅根据 `need_resched` 触发调度，不直接修改 PCB 或恢复寄存器。

```text
请基于 kern_init() 对 cpu_idle() 的调用位置以及 idleproc->need_resched 的初始化值，实现 Lab4 中的 cpu_idle。
要求在无限循环中检测 current->need_resched，为真时调用 schedule；不要在这里重新实现线程选择、上下文保存，也不要加入未经要求的关机逻辑。
给出它怎样促成首次从 idleproc 切换到 initproc 的解释。
```

### Prompt 6：编译失败时要求 AI 分析日志

**SPEC**：先分类错误，指出文件和行号；先小改动，不要重构整个项目；任何修改必须通过 `git diff --check` 与 `make -C code`。

```text
我在 WSL 的 OS2026fall/lab4-lwh 执行 make -C code，下面是完整错误日志。
请先判断这是 C 语法、汇编、链接还是 Makefile 问题，再定位到具体文件和报错行。
优先提出最小修复，特别检查 sched.c 是否存在函数大括号不匹配或重复 local_intr_restore。
不要把旧 Lab3 评分问题和成员 C 的进程调度逻辑混为一谈。修复后告诉我如何重新编译验证。
```

### Prompt 7：Makefile、自动评分与 QEMU 联合排查

**SPEC**：区分“编译成功但运行失败”“启动失败”“评分脚本错误”“实验范围之外的 Panic”；保留备份并优先对照项目代码和已验证的参考版本。

```text
我们 Lab4 的普通 make 编译可以完成，但 make qemu 和 make grade 曾出现不同问题。
请对照这份工程的 Makefile、tools/inject.py、tools/grade.sh、kern/trap/trap.c、kern/debug/kmonitor.c，分别解释每种故障。
重点检查：QEMU 是否用正确内核加载参数；评分脚本是否仍是 Lab3；SSTATUS_SUM 是否与本项目 riscv.h 兼容；init_main 返回后 do_exit 的 panic 是否只是占位。
请先给出定位命令，再给最小修复方案，不要宣称已实现真正的进程退出。每次修复后都要求重新 make 和 grade 验证。
```

### Prompt 8：GitHub 分支合并与复核

**SPEC**：只合并到公共 `lab4`，不用 `main`；检查提交历史、PR 的 13 个文件，避免误传备份；合并后在公共分支重新跑测试。

```text
我的 lab4-lwh 已经 push 到 GitHub，现在准备合并到公共 lab4 分支。
请指导我创建 base=lab4、compare=lab4-lwh 的 Pull Request，检查是否包含无关的旧实验文件、是否误提交备份、switch.S 的寄存器偏移是否与 proc.h 一致。
没有冲突后指导 merge，并在本地 git fetch + pull --ff-only；最后在公共 lab4 分支 make clean、make、make grade。不要强推或覆盖别人的分支。
```

## 四、实际出现过的关键报错与验证命令

### 4.1 编辑与格式

```bash
cd ~/OS2026fall
git diff --check
cat -n code/kern/process/switch.S
nl -ba code/kern/process/proc.h | sed -n '14,38p'
```

出现过 `^M`/`trailing whitespace`、文件末尾多余空行等问题。处理原则是先修格式，再确认没有修改代码逻辑。

### 4.2 编译与调度

```bash
make -C code clean
make -C code
```

过程中曾遇到 `sched.c` 函数结束后重复的 `local_intr_restore()`、评分相关构建变量不一致等问题。修复后能看到 `+ ld bin/kernel` 和 `objcopy ... bin/ucore.img`。

### 4.3 QEMU 与自动评分

```bash
make -C code qemu
make -C code grade
```

`make qemu` 的末尾会显示框架中的 `do_exit()` 占位 Panic；通过 SBI 关机使模拟器退出。**不要把它解释成完整的进程退出实现。**

最终 `make grade` 输出：

```text
lab4 injection complete
  -check PMM:                                OK
  -check Process Create:                     OK
  -check Schedule:                           OK
Total Score: 30/30
```

### 4.4 Git 合并与公共分支复验

```bash
git push origin lab4-lwh
git fetch origin
git switch lab4
git pull --ff-only origin lab4
git rev-parse HEAD
git rev-parse origin/lab4
make -C code clean
make -C code
make -C code grade
```

最后两条 `git rev-parse` 对应的哈希一致：`a3bcaa9741eaff8656c6e4020740752dde1e8474`，公共分支重新评分 `30/30`。

## 五、AI 辅助的局限与自己做的判断

1. **不能把 AI 给出的代码当成已通过**：早期 `schedule` 的边界和大括号处理就需要结合真实日志重写。
2. **文件版本必须匹配**：参考代码中的 `SSTATUS_SUM` 在当前工程头文件中没有定义，最后改用已定义的 `SSTATUS_PUM`。
3. **不能只看编译成功**：普通 `make` 成功后，仍可能在 QEMU 参数、初始化顺序、退出占位或评分注入中出错。
4. **不能混淆团队分工**：A 的页映射、B 的创建初始化和 C 的调度是不同模块；后续排查共享文件不等于把所有模块都算作 C 实现。
5. **保留证据**：关键修改前备份，保留 `git diff`、QEMU 输出、`make grade` 和 GitHub PR，报告中的“通过”均对应实际输出。

## 六、可关联的原始证据与项目资料

- 原始交流导出：`ChatGPT-oslwh-lab4-20261121-2317.pdf`（可单独保存或作为附件）
- 课程指导书：`riscv64-ucore-lab4-guide(1)(1).pdf`
- 小组仓库：<https://github.com/fengmian-coder/OS2026fall>
- 成员 C 合并记录：<https://github.com/fengmian-coder/OS2026fall/pull/24>
- 已有成员 B 资料（仓库 `report/`）：`memberB_process.md`、`memberB_prompt.md`
- 本次集成报告：`lab4_report.md` 及 `images/lab4_*.png`
