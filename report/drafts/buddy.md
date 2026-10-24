# Buddy System 物理内存管理

## 1. 任务说明

本部分实现 Buddy System 物理内存分配器，并接入实验已有的 pmm_manager 框架。

主要新增：
- kern/mm/buddy_pmm.c
- kern/mm/buddy_pmm.h

正常运行时在 pmm.c 中选择 buddy_pmm_manager。

## 2. 基本原理

Buddy System 按 order 管理不同大小的空闲块：

- order 0：1 页
- order 1：2 页
- order 2：4 页
- order 3：8 页

使用多个空闲链表保存不同大小的块，并使用 Page.property 记录块大小。

## 3. 初始化

buddy_init_memmap() 接收 page_init() 找到的连续可用物理内存，并按照地址对齐情况将其划分成若干个 2 的幂大小的空闲块，加入对应 order 的链表。

## 4. 分配

buddy_alloc_pages(n) 会把请求页数向上取整到 2 的幂。

例如：
- 申请 1 页，实际使用 1 页块
- 申请 3 页，实际使用 4 页块
- 申请 5 页，实际使用 8 页块

如果当前 order 没有空闲块，就寻找更大的块并不断拆分。

## 5. 释放和合并

释放时根据块大小确定 order，并使用：

buddy_index = index ^ block_pages

计算伙伴块。

如果伙伴块为空闲状态且大小相同，就把两个块合并成更高一级的块，并继续尝试合并。

## 6. Buddy 测试

buddy_check() 中测试了：

1. 保存初始空闲页数和各 order 块数量；
2. 分别申请 1、3、5 页；
3. 验证实际块大小为 1、4、8 页；
4. 验证空闲页数减少 13 页；
5. 验证过大申请返回 NULL；
6. 按不同顺序释放；
7. 验证总空闲页数恢复；
8. 验证各 order 空闲块数量恢复。

执行：

make qemu

实际输出：

memory management: buddy_pmm_manager
Free physical pages: 31929
check_alloc_page() succeeded!

说明 Buddy 已成功运行并通过测试。

测试截图：
report/images/buddy_check.png

## 7. 环境适配

本组环境为 QEMU 7.0.0 和 OpenSBI v1.0。

原始启动方式使用 -device loader 时：

Domain0 Next Address : 0x0

因此将启动方式调整为：

-kernel $(UCOREIMG)

修改后：

Domain0 Next Address : 0x80200000

可以正常进入内核。

同时启动页表增加低地址恒等映射，使开启 Sv39 分页后当前低地址代码能够继续执行。

## 8. 最终验证

现场演示命令：

make qemu
make grade
make gdb

结果：

- make qemu：Buddy 正常运行，check_alloc_page() succeeded!
- make grade：Page Init 和 Best-Fit 均通过，Total Score: 20/20
- make gdb：自动启动 QEMU 并连接 GDB，停在 0x1000

最终 Buddy System 可以正常完成初始化、拆分、分配、释放和伙伴合并。
