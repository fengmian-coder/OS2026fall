# 硬件内存范围 Challenge

## 1. 问题背景

原 Lab2 的物理内存初始化主要针对 QEMU virt 平台，系统通过 `get_memory_base()` 和 `get_memory_size()` 获取一段连续物理内存，并默认其起始地址为 `DRAM_BASE`。

真实硬件的物理地址空间可能包含多个不连续 RAM 区域、固件保留区域以及 reserved-memory。因此本 Challenge 对 DTB 解析和 `page_init()` 进行了扩展，使系统能够依据设备树提供的物理内存布局初始化物理内存管理器。

## 2. DTB 多内存区域解析

原实现找到第一个 `memory` 节点的 `reg` 属性后立即返回，只能获得一组 `base + size`。

修改后支持：
- 枚举多个 `memory` 节点；
- 解析一个 `reg` 属性中的多组 `<base, size>`；
- 读取根节点的 `#address-cells` 和 `#size-cells`；
- 解析 FDT memory reservation block；
- 解析 `/reserved-memory` 子节点；
- 保存多个 RAM region 和 reserved region。

当前 QEMU 环境识别结果：

```text
Physical Memory Regions from DTB: 1
  Region 0: 0x0000000080000000 - 0x0000000087ffffff (128 MB)
Reserved Memory Regions from DTB: 1
  Reserved 0: 0x0000000080000000 - 0x000000008003ffff
```

## 3. PMM 按 DTB 内存范围初始化

新的 `page_init()` 会：
1. 获取 DTB 中所有 RAM region；
2. 建立 `Page` 描述符数组，并默认标记为 Reserved；
3. 逐个处理 RAM region；
4. 排除内核、Page 描述符数组和 reserved region；
5. 将剩余连续页对齐区间分别通过 `init_memmap()` 加入 PMM。

这样即使 RAM 中存在地址空洞或保留区，对应页面也不会被错误分配。

## 4. 实验结果

```text
PMM physical memory regions from DTB: 1
Page metadata: 0x0000000080207000 - 0x0000000080346fff
RAM region 0 candidate: 0x0000000080347000 - 0x0000000087ffffff
  Usable RAM: 0x0000000080347000 - 0x0000000087ffffff (31929 pages)
Free physical pages from DTB regions: 31929
check_alloc_page() succeeded!
```

原 Lab2 测试仍通过：

```text
-check page init: OK
-check best fit: OK
Total Score: 20/20
```

`make gdb` 仍可正常连接：

```text
Remote debugging using localhost:1234
0x0000000000001000 in ?? ()
```

## 5. 当前实现边界

当前实验内核仍使用固定的 `DRAM_BASE`、`PHYSICAL_MEMORY_END` 和高地址直接映射窗口。因此本实现可以发现窗口外 RAM，但只将当前内核支持物理地址窗口内的 RAM 加入 PMM。若要支持完全任意硬件布局，还需要继续修改内核直接映射、`nbase` 和页表建立方式。
