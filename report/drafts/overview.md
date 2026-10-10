# Lab1 整体流程与启动过程

## 1. 实验整体流程

本实验主要完成最小可执行内核的构建与启动，并理解 RISC-V
系统从 QEMU 启动到执行 uCore 内核的完整过程。

整体流程如下：

源代码（C / 汇编）
→ 交叉编译生成目标文件
→ 使用 kernel.ld 链接生成 ELF 内核
→ 使用 objcopy 生成二进制内核镜像 ucore.img
→ QEMU 启动 RISC-V virt 虚拟机
→ OpenSBI 完成底层初始化
→ 将控制权转交给 0x80200000
→ 执行 kern_entry
→ 建立内核栈
→ 进入 kern_init
→ 输出内核启动信息。

## 2. Makefile 与编译流程

实验使用 riscv64-unknown-elf 工具链进行交叉编译。

Makefile 中主要使用：

- riscv64-unknown-elf-gcc：编译 C 和汇编源文件；
- riscv64-unknown-elf-ld：链接目标文件；
- riscv64-unknown-elf-objcopy：将 ELF 文件转换为二进制镜像；
- riscv64-unknown-elf-objdump：生成反汇编和符号信息；
- riscv64-unknown-elf-gdb：用于 RISC-V 内核调试。

链接阶段使用：

-T tools/kernel.ld

指定 uCore 自己的链接脚本。

链接完成后生成 bin/kernel，随后通过：

objcopy -O binary

生成 bin/ucore.img。

实际使用 file 命令检查发现：

- bin/kernel 为 RISC-V 64 位 ELF 可执行文件；
- bin/ucore.img 为原始二进制数据。

## 3. kernel.ld 与内核内存布局

tools/kernel.ld 中定义：

OUTPUT_ARCH(riscv)
ENTRY(kern_entry)

BASE_ADDRESS = 0x80200000;

其中：

- OUTPUT_ARCH(riscv) 表示目标架构为 RISC-V；
- ENTRY(kern_entry) 指定内核入口符号为 kern_entry；
- BASE_ADDRESS = 0x80200000 指定内核链接基址。

链接脚本随后依次安排 .text、.rodata、.data、.sdata 和
.bss 等段。

其中 .text 主要存放程序代码，.rodata 存放只读数据，
.data 存放已经初始化的可读写数据，.bss 存放需要初始化
为零的数据。

使用 readelf 实际查看 bin/kernel 得到：

Entry point address: 0x80200000

使用 nm 查看符号表得到：

kern_entry    = 0x80200000
kern_init     = 0x8020000a
bootstack     = 0x80201000
bootstacktop  = 0x80203000
edata         = 0x80203008
end           = 0x80203008

因此可以确认 kernel.ld 中设置的内核基地址和最终 ELF
中的实际入口地址一致。

## 4. entry.S 与 kern_init

OpenSBI 将控制权交给内核后，CPU 首先执行
kern/init/entry.S 中的 kern_entry。

其中：

la sp, bootstacktop

将内核启动栈的栈顶地址装入 sp 寄存器，为之后执行
C 语言函数建立正常的栈环境。

entry.S 中通过：

.space KSTACKSIZE

为启动栈预留空间，bootstacktop 表示该栈的顶部位置。

随后：

tail kern_init

将执行流程转移到 C 语言实现的 kern_init 函数，并且
不再需要返回 entry.S。

kern_init 首先使用链接脚本提供的 edata 和 end 符号
完成需要清零区域的初始化，然后调用 cprintf 输出内核
启动信息，最后进入无限循环。

## 5. OpenSBI 与 QEMU 启动过程

Makefile 中 make qemu 最终使用 QEMU virt 机器，并通过：

-bios default

加载 QEMU 自带的 OpenSBI。

在本实验中，内核镜像并不是由 OpenSBI 从磁盘读取并加载的。
Makefile 使用 QEMU 参数：

-device loader,file=$(UCOREIMG),addr=0x80200000

由 QEMU loader 在启动时直接将 ucore.img 放入物理地址
0x80200000。

OpenSBI 的主要作用是完成 M 模式下的基础硬件和运行环境初始化，
并在初始化完成后把下一阶段执行地址设置为 0x80200000，
切换到 S-mode 后将执行控制权交给 uCore。

因此需要区分：

QEMU loader：负责把 ucore.img 放到 0x80200000；

OpenSBI：负责基础初始化，并最终把执行控制权交给 0x80200000。

实际运行 make qemu 后，OpenSBI 输出中可以观察到：

Firmware Base = 0x80000000

Domain0 Next Address = 0x80200000

Domain0 Next Mode = S-mode

说明 OpenSBI 自身运行在固件阶段，并在初始化结束后准备
将执行环境切换到 S 模式，同时把下一阶段入口设置为
0x80200000。

之后终端成功输出：

(THU.CST) os is loading ...

说明 CPU 已经成功进入 uCore 内核并执行到 kern_init。

因此本实验中的主要启动链可以总结为：

QEMU 启动
→ OpenSBI
→ 0x80200000
→ kern_entry
→ 设置内核栈
→ kern_init
→ cprintf 输出。

## 6. ELF 与 BIN 的区别

bin/kernel 是 ELF 格式文件。

ELF 中除了程序代码和数据，还保存了程序头、段信息、
符号和调试信息，因此适合链接、分析和 GDB 调试。

bin/ucore.img 是由 objcopy 从 ELF 转换得到的原始二进制
镜像，更适合由 QEMU loader 直接放入指定物理内存地址。

因此实验中的关系为：

源代码
→ 目标文件
→ ELF（bin/kernel）
→ BIN（bin/ucore.img）
→ QEMU 加载运行。

## 7. 最终运行验证

执行：

make qemu

后，OpenSBI 正常启动，并且可以看到：

Domain0 Next Address = 0x80200000
Domain0 Next Mode = S-mode

之后 uCore 成功输出：

(THU.CST) os is loading ...

说明 Lab1 最小内核启动流程可以正常运行。

