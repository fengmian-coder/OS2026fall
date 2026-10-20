# Lab2：Best-Fit 物理内存分配算法实现与测试

## 1. 实验目的

本部分实验主要实现 Best-Fit（最佳适应）物理内存分配算法，理解操作系统如何管理空闲物理页，以及如何通过不同的分配策略减少内存碎片。

## 2. Best-Fit 算法原理

Best-Fit 的基本思想是：当系统需要分配连续的 `n` 个物理页时，遍历空闲块链表，寻找所有能够满足请求的空闲块，并选择其中**大小最小的合适空闲块**。

例如，空闲链表中存在大小分别为 3、8、5 页的空闲块，申请 4 页时，Best-Fit 会优先选择 5 页的空闲块，而不是 8 页的空闲块。

如果找到的空闲块大于请求大小，则将剩余部分重新放回空闲链表。

## 3. 算法实现

本次实验在 `kern/mm/best_fit_pmm.c` 中完成 Best-Fit 内存管理器的实现。

### 3.1 初始化空闲链表

在 `best_fit_init()` 中初始化空闲链表和空闲页计数器，为后续内存分配做好准备。

`best_fit_init_memmap()` 负责将指定范围内的物理页初始化为空闲页，并建立对应的空闲块信息。

### 3.2 分配物理页

在 `best_fit_alloc_pages(size_t n)` 中遍历空闲链表，检查每个空闲块的大小。

对于能够满足 `n` 页请求的空闲块，比较其大小，最终选择满足条件且大小最小的块。

如果该块大小恰好等于 `n`，则直接从空闲链表中移除；如果大于 `n`，则将剩余部分保留为空闲块。

完成分配后更新空闲页数量。

### 3.3 释放物理页

在 `best_fit_free_pages()` 中，将释放的连续物理页重新加入空闲链表。

为了便于合并相邻空闲块，释放时按照物理地址顺序插入，并检查是否能够与前后相邻的空闲块合并。

这样可以减少空闲内存被分割成多个小块的情况。

### 3.4 查询空闲页数量

`best_fit_nr_free_pages()` 返回当前空闲物理页的总数，用于内存管理状态检查。

## 4. 编译与测试

为了避免影响小组其他成员负责的 `pmm.c`，本次测试将 `code` 目录复制到临时目录，在副本中切换内存管理器：

```bash
mkdir -p /tmp/oslab2-bestfit-test
cp -a code/. /tmp/oslab2-bestfit-test/
cd /tmp/oslab2-bestfit-test
```

在临时副本的 `kern/mm/pmm.c` 中，将当前内存管理器设置为：

```c
pmm_manager = &best_fit_pmm_manager;
```

然后重新编译：

```bash
make
```

编译成功后，使用 QEMU 启动内核：

```bash
qemu-system-riscv64 \
    -machine virt \
    -nographic \
    -bios default \
    -kernel bin/ucore.img
```

## 5. 实验结果

QEMU 成功启动内核，输出：

```text
(THU.CST) os is loading ...

memory management: best_fit_pmm_manager
Free physical pages: 31929
Memory base: 0x0000000080000000
Memory size: 0x0000000008000000
check_alloc_page() succeeded!
```

其中：

- `memory management: best_fit_pmm_manager` 说明当前使用的是 Best-Fit 内存管理器。
- `Free physical pages: 31929` 表示初始化后可用的空闲物理页数量。
- `check_alloc_page() succeeded!` 说明框架中的物理页分配检查成功完成。

测试过程中未出现断言失败或内核异常，说明本次 Best-Fit 实现通过了课程框架提供的运行检查。

## 6. 实验总结

通过本次实验，我理解了 Best-Fit 算法的主要实现过程。与找到第一个合适空闲块就进行分配的 First-Fit 不同，Best-Fit 需要遍历空闲链表，选择最接近申请大小的空闲块。

实现过程中，除了正确选择空闲块，还需要处理分配后的剩余空间，以及释放时相邻空闲块的合并。如果没有正确维护空闲链表和空闲页数量，就可能出现内存管理错误。

最后，通过在临时副本中切换内存管理器并使用 QEMU 运行测试，验证了 Best-Fit 的基本功能，同时避免修改小组共享的内存管理器配置文件。