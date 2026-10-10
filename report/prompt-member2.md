# Lab 1 练习 2：AI Prompt 使用记录

## 一、使用说明

本次实验使用 ChatGPT 辅助完成 RISC-V ucore Lab 1 练习 2。AI 主要用于调试环境配置、GDB 操作指导、启动指令解释、实验报告整理以及 Git 协作问题排查。

实验中的命令均由本人在 WSL Ubuntu 环境中实际执行，关键运行结果通过 QEMU 和 GDB 验证，并保存了相应的截图。

以下内容按照实验过程归纳主要 Prompt 及其用途，部分问题经过整理和合并。

## 二、主要 Prompt 记录

### 1. 实验任务规划

**Prompt：**

> 我们正在完成操作系统 Lab 1，三个人协作。我负责练习 2，需要使用 QEMU 和 GDB 跟踪 RISC-V 启动流程。请根据组长的 Git 协作规范，一步一步指导我完成实验、截图、报告和个人分支提交。

**AI 辅助内容：**

明确个人任务范围，确定需要验证的三个地址：`0x1000`、`0x80000000` 和 `0x80200000`，并规划调试、截图及提交步骤。

### 2. WSL 与 GitHub 网络问题

**Prompt：**

> 我在 WSL Ubuntu 中执行 git push 时连接 GitHub 失败，但 Windows 浏览器可以正常访问 GitHub。请帮我逐步排查网络和代理问题，尽量不影响已有仓库。

**AI 辅助内容：**

指导检查 Windows 代理、WSL 网络模式和 Git 代理配置。最终通过 WSL 镜像网络模式以及 Git 仓库本地代理，使 GitHub 连接恢复正常。

### 3. GitHub 身份认证问题

**Prompt：**

> 执行 git push 时提示 Password authentication is not supported for Git operations，我应该怎么办？如何安全地向小组仓库推送自己的分支？

**AI 辅助内容：**

解释 GitHub HTTPS 认证机制，指导使用 Personal Access Token 完成身份认证，并提醒避免泄露令牌。

### 4. QEMU 调试环境

**Prompt：**

> 小组仓库中的 Makefile 使用 `-kernel $(UCOREIMG)`，并提供 `make debug` 目标。我应该怎样确认 QEMU 已经启动，并使用 GDB 连接它？

**AI 辅助内容：**

指导使用 `make debug` 启动 QEMU，通过进程检查确认启动成功，再使用 `gdb-multiarch bin/kernel` 连接 `localhost:1234`。

### 5. CPU 复位入口分析

**Prompt：**

> GDB 显示 `pc = 0x1000`，执行 `x/10i 0x1000` 后出现 `auipc`、`addi`、`csrr`、`ld`、`jr` 等指令。请解释这些指令的作用，以及如何验证下一阶段的启动地址。

**AI 辅助内容：**

解释复位代码中的寄存器准备、启动参数传递和间接跳转，并指导通过 `si 6` 验证 CPU 进入 `0x80000000`。

### 6. OpenSBI 与内核入口验证

**Prompt：**

> 执行 `si 6` 后，GDB 显示 `0x0000000080000000 in ?? ()`。这说明了什么？我应该怎样继续验证内核入口 `0x80200000`？

**AI 辅助内容：**

分析从复位代码进入 OpenSBI 的过程，指导设置 `break *0x80200000` 断点，通过 `continue` 和 `info registers pc` 验证进入 `kern_entry`。

### 7. GDB 截图保存

**Prompt：**

> 我已经在 GDB 中验证了三个启动地址，应该截取哪些内容？如何在 Windows 上保存 Ubuntu 终端截图，并放进 WSL 的 Git 仓库？

**AI 辅助内容：**

指导保存三张 PNG 图片，并使用 Windows 文件资源管理器将截图放入 `report/images/`。

对应文件为：

- `gdb_reset_1000.png`
- `gdb_opensbi.png`
- `gdb_kernel_80200000.png`

### 8. 实验报告撰写

**Prompt：**

> 请根据我实际执行的 QEMU 和 GDB 命令、三个关键地址的调试结果及截图，帮我整理一份规范的 Lab 1 练习 2 Markdown 实验报告。

**AI 辅助内容：**

协助组织实验目的、环境、方法、启动流程分析、实验结果和总结，形成 `report/drafts/exercise2.md`。

### 9. Markdown 格式检查

**Prompt：**

> 我已经保存了 `exercise2.md`，但图片引用可能存在多余的反斜杠。请指导我检查并修复 Markdown 图片链接、标题和代码块格式。

**AI 辅助内容：**

指导使用终端命令检查 Markdown 内容，确认三张图片引用和主要标题、代码块标记正确。

### 10. Git 分支与提交

**Prompt：**

> 我的实验报告和截图已经整理好，如何只提交练习 2 的四个文件，而不把编译产生的 `code/bin/`、`code/obj/` 一起提交？

**AI 辅助内容：**

指导使用明确文件路径的 `git add`、`git commit` 和 `git push`，将实验成果提交并推送到个人工作分支 `jlh-lab1`。

### 11. 任务完成情况核对

**Prompt：**

> 在创建 Pull Request 之前，请你再次详细检查我是否完成了属于我的任务，有没有遗漏。

**AI 辅助内容：**

对照小组三人协作规范检查任务完成情况，确认核心 GDB 实验、截图和报告已经完成，并发现还需要整理个人 Prompt 记录和进行 PR 审核、合并。

## 三、AI 使用总结

本次实验中，AI 主要发挥操作指导、故障排查、技术解释与文字整理作用。

本人独立执行 QEMU、GDB 及 Git 命令，观察并验证了 RISC-V 从复位入口 `0x1000`，经过 OpenSBI `0x80000000`，最终到达 ucore 内核入口 `0x80200000` 的过程。

对于 AI 提供的命令和解释，结合实际终端输出进行检查和修正；实验结论以实际调试结果为依据，而非仅依赖 AI 生成内容。
