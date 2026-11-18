# Lab4 进程管理实验——成员 B：内核线程创建与初始化

> 负责模块：功能模块二（进程创建及初始化）
> 学号：2412074
> 个人分支：`lab4-B-process`
> 代码提交：`65e36ad`
> Pull Request：[#23](https://github.com/fengmian-coder/OS2026fall/pull/23)
> 记录状态：B 模块源码已编写并通过单文件编译；A/B/C 集成后的运行测试尚待完成。

## 一、实验目标与分工

本次实验以已有的物理内存管理、页表和中断异常处理功能为基础，进一步加入内核线程管理，使系统能够建立多个执行实体，并通过调度器切换执行。本实验的功能划分为页操作、线程创建与初始化、调度与上下文切换三个模块。我负责第二个模块，在 `code/kern/process/proc.c` 的 `//LAB4 function2 2412074` 标记下实现六个函数：`alloc_proc()`、`proc_init()`、`kernel_thread()`、`do_fork()`、`copy_thread()`、`copy_mm()`。

本模块主要解决“线程如何被创建并放入可运行状态”的问题，不负责 C 模块的 `cpu_idle()`、`schedule()`、`proc_run()`、`switch_to()`。本阶段的内核线程共享内核地址空间，不实现完整的用户地址空间复制、按需分页和页面置换。

## 二、整体实现思路

Lab4 的初始化与首次运行主线可以概括为：

```text
kern_init → pmm_init → pic_init/idt_init → vmm_init → proc_init
                                       ↓
                  alloc_proc → kernel_thread → do_fork
                                       ↓
                    分配 PCB / 内核栈 / PID / 链表登记
                                       ↓
                       copy_mm + copy_thread
                                       ↓
                   initproc 进入 PROC_RUNNABLE 状态
                                       ↓
        cpu_idle → schedule → proc_run → switch_to
                                       ↓
    forkret → forkrets → __trapret → kernel_thread_entry → init_main
```

上述链路中，创建 PCB、组织初始 TrapFrame/Context 和登记线程是我负责的部分；调度与底层上下文切换由成员 C 实现。只有全部模块完成集成，才能证明新线程已经真正执行。

## 三、核心函数设计与实现

### 3.1 `alloc_proc()`：初始化进程控制块

首先通过 `kmalloc(sizeof(struct proc_struct))` 申请 PCB 空间。分配失败返回 `NULL`；分配成功后先使用 `memset` 清零，再显式设置进程初始状态 `PROC_UNINIT`、PID 初始值 `-1`、运行次数、内核栈、调度标记、父进程、地址空间、TrapFrame、页表地址等字段，并分别初始化 `list_link`、`hash_link` 两个链表节点。

先清零再补充初始化，可以避免后续读取未初始化字段；链表节点也需要处于安全状态，便于进程登记和资源管理。

### 3.2 `proc_init()`：初始化 idleproc 与 initproc

该函数首先初始化全局进程链表和 PID 哈希桶；随后使用 `alloc_proc()` 创建代表当前内核执行流的 `idleproc`。`idleproc` 的 PID 为 0、状态为 `PROC_RUNNABLE`、内核栈使用已有的 `bootstack`，并设置 `need_resched = 1`，使它能在进入 `cpu_idle()` 后触发调度。

接着设置 `current = idleproc`、`nr_process = 1`，再通过 `kernel_thread(init_main, "Hello world!!", 0)` 创建 `initproc`。创建后调用 `find_proc(pid)` 定位线程，设置名称 `init`，并断言初始系统共有两个线程。这里的 `idleproc` 不是通过普通 fork 产生的线程，而是对原有内核启动现场的封装。

### 3.3 `kernel_thread()`：组织新线程入口

使用临时 `struct trapframe` 记录首次执行现场，令 `gpr.s0` 指向入口函数 `fn`、`gpr.s1` 保存参数 `arg`、`epc` 指向 `kernel_thread_entry`；同时设置 `sstatus` 中的 SPP/SPIE 等位以满足内核态返回的约定，然后调用 `do_fork(clone_flags | CLONE_VM, 0, &tf)`。

对应的汇编入口通过 `move a0, s1` 传递第一个参数，并使用 `jalr s0` 跳转到线程函数。这解释了为什么线程入口与参数需要预先放在 `s0`、`s1` 中，而不是直接调用 `fn(arg)`。

### 3.4 `copy_mm()`：当前实验的地址空间约定

本实验只要求创建内核线程，故当前实现断言 `current->mm == NULL`，并将新线程的 `mm` 置为 `NULL`、`pgdir` 设为 `boot_pgdir_pa`，表示使用内核页表。函数保留 `clone_flags` 参数和返回值接口，但未实现用户地址空间的复制或共享引用计数。这是 Lab4 范围内的简化，不应描述为已完成用户进程的完整 fork。

### 3.5 `copy_thread()`：设置第一次执行现场

在新线程已分配的内核栈高地址一侧放置 TrapFrame，并复制调用者传入的寄存器现场；将子线程首次返回值寄存器 `a0` 设为 0。当前内核线程调用 `do_fork()` 时传入的栈参数为 0，因此将 TrapFrame 中需要恢复的 `sp` 设置为新线程可用的内核栈顶部（传入非零 `esp` 时仍使用指定值）。

随后将 `context.ra` 指向 `forkret`，将 `context.sp` 指向所布置的 TrapFrame。这样，新线程首次被 `switch_to()` 切换时，会进入 `forkret → forkrets → __trapret`，依照 TrapFrame 恢复 `sstatus`、`sepc` 和通用寄存器，最后到达 `kernel_thread_entry`。

### 3.6 `do_fork()`：组织线程生命周期与失败清理

`do_fork()` 先检查最大进程数量，再按顺序执行 PCB 分配、内核栈分配、`copy_mm`、`copy_thread`。随后在中断保护区域内分配 PID、登记 PID 哈希表与 `proc_list`、递增 `nr_process`，并调用 `wakeup_proc()` 将新线程置为 `PROC_RUNNABLE`。最后恢复原有中断状态并返回新 PID。

失败路径采用标签逐级释放已经获得的资源：栈申请失败时释放 PCB；地址空间处理失败时先通过 `put_kstack()` 归还内核栈，再通过 `kfree()` 释放 PCB。这样能避免把创建到一半的线程登记到全局结构中。当前 `copy_mm()` 在本实验实现中不会失败，但保留了对应错误清理路径供后续扩展。

## 四、SPEC 与 Prompt 设计

本模块对六个函数的共同 SPEC 是：只修改 `function2` 区域，不覆盖成员 C 的 `function3`；依照工程现有的 `proc_struct`、TrapFrame、Context 与辅助接口实现；处理申请失败和资源回滚；保持 PID、链表和进程状态一致；不擅自扩大到用户进程完整实现。六个函数分别对应“PCB 空白初始化”“idle/init 建立”“入口现场封装”“地址空间占位”“首次执行现场”“资源组合与登记”六项要求。

针对六个函数，实际开发时遵循的 SPEC 和 AI 提供的实现指令可整理为：

| 函数 | SPEC（验收约束） | 实际 AI 交互中的实现要求摘要 |
| --- | --- | --- |
| `alloc_proc` | 成功返回初始化完整的 PCB，失败返回 NULL | 使用 `kmalloc`、`memset`，初始化状态和链表节点，仅插入 function2 |
| `proc_init` | 建立 PID=0 的 idle 和 PID=1 的 init | 初始化全局链表/哈希桶、复用 bootstack、调用 kernel_thread 建 init |
| `kernel_thread` | 提供首次入口、参数与内核返回现场 | 按 entry.S 的 s0/s1 约定准备 TrapFrame，调用 do_fork |
| `copy_mm` | 在 Lab4 保留共享内核地址空间约定 | 不实现用户空间复制，沿用 boot_pgdir_pa 接口 |
| `copy_thread` | 在新内核栈正确放置 TrapFrame 和 Context | 设置 forkret 入口、上下文栈指针并核对 RESTORE_ALL |
| `do_fork` | 完成资源申请、PID/链表登记及失败回滚 | 按分配顺序构建线程，使用中断临界区保护登记和唤醒 |

AI 辅助过程中，我首先提供实验指导、ABC 分工文件与源码骨架，随后在 WSL 中通过 `grep`、`sed` 检查接口和数据结构，再按函数逐个加入实现。对于每个功能，我要求保持对应的 Lab4 标记、检查 `git diff --check`、在编译后根据真实日志继续排查。详细的实际交互记录、命令以及修改顺序见 `memberB_prompt.md`；该文件不把事后整理的说明冒充原始提问。

## 五、发现的问题与调试过程

### 5.1 首次编译时链接缺少 `cpu_idle`

执行 `make -C code -j1` 时，`kern/process/proc.c` 成功编译为目标文件，但最终 `ld bin/kernel` 失败，报 `undefined reference to 'cpu_idle'`（链接错误，退出码 2）。`cpu_idle()` 由成员 C 负责，当前不应为了消除链接错误而自行写一个虚假的调度函数。此阶段能够确认的是 B 模块通过编译，不能宣称整个内核已成功构建。

### 5.2 `do_fork()` 状态更新的临界区

初版实现先恢复中断，后调用 `wakeup_proc(proc)`。在复核 PID 登记、链表挂接和线程就绪状态的一致性时，将 `wakeup_proc()` 移动到中断保护区域内，使新线程的登记与就绪状态更新在同一临界区完成。

### 5.3 新线程栈指针为零的风险

初版 `copy_thread()` 直接将 `esp` 写入 TrapFrame 中的 `sp` 字段，而 `kernel_thread()` 传入的 `stack` 为 0。检查 `trapentry.S` 中 `RESTORE_ALL` 的最后一条恢复操作 `LOAD x2, 2*REGBYTES(sp)` 后，确认首次返回可能将 `sp` 恢复为 0。于是调整为：`esp != 0` 时使用 `esp`，否则使用新线程的内核栈顶部。汇编静态复核已完成，实际线程切换仍待集成测试。

### 5.4 Git 格式检查发现行尾空格

修改栈指针表达式后，`git diff --check` 报 `trailing whitespace`。使用 `sed -i 's/[[:blank:]]\+$//' code/kern/process/proc.c` 删除行尾空格，再次检查后无格式错误。

## 六、已完成的检查和待补实验结果

| 项目 | 实际状态与证据 |
| --- | --- |
| 六个核心函数 | 已写入 `proc.c` 的 function2 区域 |
| 修改范围 | 只修改 `code/kern/process/proc.c`，function3 保留 |
| `proc.c` C 编译 | 已见 `+ cc kern/process/proc.c`，通过 |
| 整体链接 | 未通过，缺少成员 C 的 `cpu_idle()` |
| `git diff --check` | 最终检查无输出，格式通过 |
| Git 提交 | `65e36ad`，已推送 `lab4-B-process` |
| Pull Request | #23 已创建，GitHub 当时显示无合并冲突 |
| `[Test Process Create] PASS` | **尚未取得，必须在集成后运行** |
| `make grade` / QEMU | **尚未取得完整成功输出** |

在老师提供的 `tools/test_lab4.c` 中，`test_lab4_process_create()` 会检查 `current == idleproc`、idle 的 PID/状态/内核栈/字段初值以及 init 的 PID、状态、父进程、mm 和 Context。最终集成后应保存 `make -C code`、`make -C code grade` 和 QEMU 输出，并将真实结果更新进本节。若评分结果与预期不一致，应记录实际错误和修复，不能把预期日志写作已完成记录。

## 七、对应的操作系统知识点

- **PCB 与线程生命周期**：PCB 是线程状态、调度信息和执行现场的管理实体；本实验通过 `alloc_proc`、`do_fork`、`wakeup_proc` 体现从未初始化到可运行的过程。
- **进程与线程**：两者都可以有自己的执行现场，但本实验的内核线程共享内核地址空间，不具备完整用户进程的独立用户空间。
- **内核栈与 TrapFrame**：每个新线程有独立内核栈，TrapFrame 为第一次异常返回式的执行恢复准备寄存器现场；这与 `context` 中的 `ra/sp/s0…s11` 不同，后者由上下文切换函数保存与恢复。
- **PID、链表和哈希表**：PID 提供身份标识，`proc_list` 支持调度遍历，哈希表支持按 PID 快速查找。
- **临界区与原子性**：进程登记、状态更新需要避免中途被中断破坏一致性。本实验使用 `local_intr_save/restore` 保护关键操作；该机制不等同于多核环境下完整的互斥锁。
- **`sret` 与寄存器恢复**：新线程的首次执行通过 `forkret/forkrets/__trapret` 进入约定入口，与普通 C 函数直接调用有所不同。

## 八、本实验尚未对应的操作系统内容

本实验未实现真正的用户态进程装载、系统调用的完整生命周期、用户空间复制与写时复制（COW）、缺页按需分配、页面置换、用户进程退出和父子回收、完整的抢占式多核调度等内容。这些机制对实际操作系统很重要，但不能因为 Lab4 已有某些结构体或占位接口，就认为相应功能已经完成。

## 九、下一步集成验证

待成员 C 的 `cpu_idle/schedule/proc_run/switch_to` 和其他依赖合并后，在最新 `lab4` 集成分支上执行：

```bash
make -C code clean
make -C code -j1
make -C code grade
make -C code qemu
```

留存构建日志、`[Test Process Create] PASS` 等评分证据以及 `initproc` 的真实运行输出。若出现 `do_exit` 的 `panic`，要结合输出时序判断是线程创建、调度还是当前实验未实现的退出逻辑导致，不能仅凭一个 panic 推断整个 B 模块失败。
