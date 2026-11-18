# Lab4 成员 B：AI 辅助交互与 Prompt 记录

> 学号：2412074
> 分支：`lab4-B-process`
> 对应代码提交：`65e36ad`
> 说明：本文件根据本次实际聊天中的用户要求、助手给出的命令和用户回传的终端结果整理。下文“交互摘要”是事后归纳，**不是伪造的逐字原始聊天记录**；如课程要求完整原始 Prompt，可另附聊天导出或对应原始截图。

## 一、最初任务与输入材料

实际交互开始时，我提供了 Lab4 指导书 PDF、实验代码压缩包、组长的 ABC 分工与 Git 协作规范，并明确说明自己负责 B 部分，要求阅读并总结任务。随后我提出“高质量完成这份作业”的目标，按终端反馈一步一步实施。

AI 根据文件帮助定位六个函数和主要依赖：`alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm`；只允许修改 `code/kern/process/proc.c` 的 function2 区域；C 负责 function3 与调度代码。

## 二、开发环境与仓库操作记录

### 2.1 进入 WSL 并检查旧仓库

我在 VSCode/Windows 终端输入 `wsl` 后进入 Ubuntu，并执行：

```bash
cd ~/oslab
ls
cd ~/oslab/OS2026fall
git status -sb
git remote -v
git branch -a
```

发现旧仓库位于 `jlh-lab2-qemu-fix`，`code/Makefile` 有未提交修改，还有 `code/bin/`、`code/obj/` 未跟踪文件。AI 建议不要覆盖这些旧工作，先通过 `git fetch origin` 确认 `origin/lab4` 已存在。

### 2.2 创建独立 worktree

实际执行：

```bash
git worktree add -b lab4-B-process ~/oslab/OS2026fall-lab4-B origin/lab4
cd ~/oslab/OS2026fall-lab4-B
git status -sb
grep -n "LAB4 function2\|LAB4 function3" code/kern/process/proc.c
```

终端显示基于 PR #22 的合并提交 `c2772a4`，工作区干净；两个占位标记分别在第 221、224 行。

## 三、源码阅读与实现所用的交互步骤

### 3.1 检查 PCB、辅助函数和首次执行接口

AI 指导使用 `sed`/`grep`/`cat` 阅读 `proc.h`、`proc.c`、`entry.S`、`trap.h`、`sched.c`、`memlayout.h`、`pmm.h`、`trapentry.S`。实际检查确认：

- `struct context` 包含 `ra`、`sp`、`s0` 到 `s11`；`proc_struct` 包含 state、pid、kstack、mm、context、tf、pgdir 等字段。
- `setup_kstack/put_kstack`、`get_pid`、`hash_proc`、`find_proc`、`forkret` 已提供。
- `KSTACKPAGE = 2`，`KSTACKSIZE = KSTACKPAGE * PGSIZE`。
- `CLONE_VM` 定义在 `pmm.h`，`boot_pgdir_pa` 已声明。
- `kernel_thread_entry` 从 `s1` 取参数、通过 `s0` 调用入口函数。

### 3.2 按函数逐步生成代码

AI 每次给出一个 Python here-document 脚本，以 `//LAB4 function3 YOUR CODE` 为插入点，仅在它之前添加 B 的函数，防止整文件覆盖 C 的工作。用户实际逐段执行，终端依次返回：

| 顺序 | 真实终端结果或检验 | 作用 |
| --- | --- | --- |
| 1 | `alloc_proc() 已写入` | 分配并清零 PCB、初始化链表节点 |
| 2 | `proc_init() 已写入` | 初始化 `proc_list/hash_list`、建立 idle/init |
| 3 | `kernel_thread() 已写入` | 构建临时 TrapFrame，调用 `do_fork` |
| 4 | `copy_mm() 已写入` | 使用内核页表的简化地址空间处理 |
| 5 | `copy_thread() 已写入` | 布置线程内核栈中的 TrapFrame/Context |
| 6 | `do_fork() 已写入` | 分配内核栈、登记 PID 和链表、处理失败清理 |

每次通过 `grep -n` 确认函数位置与 function3 标记仍存在，并多次使用 `git diff --check`。有几次终端粘贴内容在显示时发生混入，因此随后又通过 `sed -n '220,410p'` 查看完整 B 代码，并用编译结果核对。

> 说明：这里列的是实际发生过的开发步骤与反馈，不应把表格标题当成逐字原始 Prompt。最初用户发送的原话和助手返回的具体脚本存在于聊天记录中。

## 四、真实错误、分析及修正

### 4.1 `cpu_idle` 链接错误

真实命令：

```bash
make -C code -j1 > /tmp/lab4_B_build.log 2>&1
```

结果为退出码 2，日志先出现 `+ cc kern/process/proc.c`，然后在 `ld bin/kernel` 阶段出现 `undefined reference to 'cpu_idle'`。AI 结合分工判断这个函数由成员 C 负责，故未在 B 模块添加临时空实现。结论仅为 B 模块可编译，完整内核尚不能链接或运行。

### 4.2 `do_fork` 唤醒操作移动至临界区

最初代码在 `local_intr_restore(intr_flag)` 之后调用 `wakeup_proc(proc)`。AI 复核链表登记与就绪状态一致性后，提供精确字符串替换脚本，将 `wakeup_proc(proc)` 移至 `local_intr_restore` 之前。终端返回 `do_fork() 临界区已修正`，并显示最终代码与 `git diff --check` 无报错。

### 4.3 `copy_thread` 的栈指针检查

实际通过 `sed -n '65,110p' code/kern/trap/trapentry.S` 看到了 `LOAD x2, 2*REGBYTES(sp)`，了解到异常返回会从 TrapFrame 恢复原始 `sp`。由于 `kernel_thread` 调用 `do_fork(..., 0, ...)`，初版 `proc->tf->gpr.sp = esp` 可能将线程栈恢复为 0。

AI 给出替换方案：

```c
proc->tf->gpr.sp = (esp != 0) ? esp :
    (proc->kstack + KSTACKSIZE);
```

终端返回 `copy_thread() 栈指针已修正`。之后再次对照 `RESTORE_ALL`、`forkrets` 和 `kernel_thread_entry` 的汇编代码进行了静态核对。这里还没有实际 QEMU 线程切换输出。

### 4.4 行尾空格修复

替换过程中 `git diff --check` 报：

```text
code/kern/process/proc.c:343: trailing whitespace.
```

实际执行 `sed -i 's/[[:blank:]]\+$//' code/kern/process/proc.c` 后，格式检查无报错。

## 五、提交与 PR 过程

完成六个函数后，实际执行 `git add`、`git commit`。初次提交为 `9f961aa`，随后根据要求将模块标记更新为 `//LAB4 function2 2412074` 并通过 `git commit --amend --no-edit` 得到最终代码提交 `65e36ad`。

用户随后创建新的 GitHub Token，通过 HTTPS 完成 `git push -u origin lab4-B-process`。GitHub 创建了远程个人分支，后续创建 PR #23：`feat(lab4): implement process creation and initialization`，目标是 `lab4`。截图显示当时 `No conflicts with base branch`。PR 描述注明 `proc.c` 已通过编译，完整测试等待成员 C 的 `cpu_idle()` 等实现。

Token 本身属于秘密凭据，**不记录、不上传、不粘贴到实验报告或 Git 仓库**。

## 六、AI 的帮助与本人仍需验证的内容

AI 在本次交流中主要帮助完成了：阅读实验材料并划分边界、定位源码结构和辅助函数、提出六个函数的初步实现方案、检查终端反馈、解释链接错误、识别首次恢复栈指针为零的风险、调整临界区和规范 Git 提交流程。

本人实际完成了：在 WSL 中执行命令并检查结果、提供真实的源码和编译日志、修正代码、确认修改范围、进行 Git 提交与推送、创建 PR。

尚未完成的验证包括：集成后 `make grade` 中的 `[Test Process Create] PASS`、`[Test Schedule] PASS`、内核线程真实运行与返回路径，以及与成员 C 的 `proc.c` 改动合并后的运行稳定性。后续如实际测试发现新 Bug，应将测试日志、修复提交和对应真实交流继续补入本文件，不得事后编造不存在的运行结果。
