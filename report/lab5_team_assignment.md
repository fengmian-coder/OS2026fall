# OS2026fall Lab5 小组分工

本实验在 `lab5` 分支上完成用户程序启动、系统调用、进程复制和退出回收。基础代码已整合，四个必做模块待实现。

| 成员 | 负责文件 | 实现任务 | 验收重点 |
| --- | --- | --- | --- |
| A：物理内存、虚拟内存与地址空间复制 | `code/kern/mm/pmm.c`、`vmm.c` 及对应头文件 | 实现 `copy_range()`、`dup_mmap()`，复用现有页表映射和回收接口。保留 `page_insert()` 中的注释与 Lab4 的页表分配失败回滚。 | 普通 fork 后父子页内容一致且写入互不影响，映射权限保留。部分复制失败时回收已分配资源。 |
| B：用户进程、ELF 装载与生命周期 | `code/kern/process/proc.c`、`proc.h` | 扩展 `alloc_proc()`、`kernel_thread()`、`do_fork()`、`copy_mm()`。完成 `init_main()`、`user_main()`、`kernel_execve()`、`do_execve()`、`load_icode()` 以及 `do_wait()`、`do_exit()`、`do_yield()`、`do_kill()`。 | 用户 ELF、BSS 和用户栈正确装载。fork 时父进程得到子 PID，子进程得到 0。退出码、父进程唤醒、孤儿托管和资源回收正确。 |
| C：系统调用、陷阱与构建调试 | `code/kern/syscall/`、`code/kern/trap/`，以及 `code/Makefile` 和相关调试、评分工具 | 扩展 `exception_handler()`，实现 `syscall()` 及 `sys_exit`、`sys_fork`、`sys_wait`、`sys_exec`、`sys_yield`、`sys_kill`、`sys_getpid`、`sys_putc`、`sys_pgdir`。适配 `trapentry.S` 的 U/S 态栈切换和 `RESTORE_ALL`，完成双 GDB 调试记录。 | 验证 `ecall` 推进 `epc`、系统调用返回值、非法调用号处理，以及 `sret` 返回的特权级和栈。回归内核态陷阱和时钟中断。 |

## 接口交接与联调

1. **先核对接口。** A 和 B 确认 `copy_mm()` 调用 `dup_mmap()` 时的锁、引用计数和失败清理责任。B 和 C 对齐 trapframe、首次用户态入口及进程控制函数返回值。
2. **跑通首个用户程序。** B 和 C 联调 ELF 装载 → `sret` → 用户程序 → `ecall` → 退出与等待。先用 `hello` 和 `testbss` 核对启动、输出与 BSS。
3. **验证复制和进程控制。** A、B 联调普通 fork、父子写隔离及回收。B、C 联调 yield、kill、wait 和非法用户参数。C 汇总 `make`、QEMU 和 `make grade` 的实际结果。
4. **完成扩展与报告。** 普通 fork 验证后，A 主导 COW 页共享和复制，C 对接写缺页入口，B 核对进程复制和退出时的引用释放。C 完成 `ecall` 与 `sret` 的双 GDB 跟踪。各成员将各自的设计、验证结果和实际 AI 交互汇入 `report/report.md`、`report/prompt.md`，截图保存至 `report/images/`。

## 当前起点

基础整合阶段已生成 15 个用户 ELF。内核编译停在模板系统调用分发表，九个 `sys_*` 处理函数仍待实现。QEMU 和 Lab5 课程评分尚未运行。
