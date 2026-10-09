# 成员3实验 Prompt 记录

## Prompt 1：分析 Lab1 整体构建与启动流程

[PROMPT]

任务：基于当前 Lab1 工程中的真实代码，分析 Makefile、tools/kernel.ld、
kern/init/entry.S 和 kern/init/init.c，梳理最小内核从编译、链接到
QEMU + OpenSBI 启动的完整流程。

操作要求：以项目中的真实文件内容为准，不修改内核核心代码。
需要结合实际编译和运行结果进行验证，不凭空假设工程中的宏、
地址或函数。

输出要求：重点解释 Makefile 构建流程、kernel.ld 内存布局、
ELF/BIN 的关系、OpenSBI 的作用，以及 entry.S 到 kern_init 的
控制流。

[RELY]

当前工程中可以依赖：

- Makefile
- tools/kernel.ld
- kern/init/entry.S
- kern/init/init.c
- libs/sbi.c
- kern/libs/stdio.c

关键工程信息：

- RISC-V 交叉编译工具链前缀为 riscv64-unknown-elf-
- kernel.ld 中 BASE_ADDRESS = 0x80200000
- kernel.ld 中 ENTRY(kern_entry)
- QEMU 使用 -bios default
- QEMU loader 将 ucore.img 加载到 0x80200000
- entry.S 中通过 la sp, bootstacktop 建立启动栈
- entry.S 通过 tail kern_init 进入 C 语言内核入口

[GUARANTEE]

必须完成以下分析：

1. Makefile 中从源文件到 bin/kernel 的构建过程
2. bin/kernel 从 ELF 转换为 bin/ucore.img 的过程
3. kernel.ld 中入口点和各 section 的作用
4. entry.S 中内核栈初始化和 kern_init 跳转过程
5. OpenSBI 与 0x80000000、0x80200000 的关系
6. 使用实际工具验证 ELF 入口地址和关键符号地址
7. 使用 make qemu 验证最终内核能够启动

[SPECIFICATION]

## 构建流程

Pre-Condition
- 当前位于 Lab1 的 code 目录
- RISC-V 交叉编译工具链已经正确安装

Post-Condition
- 能解释 C/汇编源代码到目标文件、ELF、BIN 的转换过程
- 能指出 kernel.ld 在链接过程中的作用

## 内核入口

Pre-Condition
- kernel.ld、entry.S 和 init.c 均为当前工程中的真实文件

Post-Condition
- 明确内核入口为 kern_entry
- 明确最终 ELF 入口地址为 0x80200000
- 能说明 la sp, bootstacktop 和 tail kern_init 的作用

## 启动验证

Pre-Condition
- bin/kernel 和 bin/ucore.img 可以正常生成
- QEMU 与 OpenSBI 环境可用

Post-Condition
- make qemu 可以正常启动
- OpenSBI 输出 Next Address 为 0x80200000
- 内核成功输出 "(THU.CST) os is loading ..."
