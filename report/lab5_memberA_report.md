# Lab5 A 部分实现与验证

## 实现

- `copy_range(to, from, start, end, share)` 遍历页对齐的用户区间 `[start, end)`。源页表不存在时跳到下一个叶级页表边界，源页表项无效时跳过该页。普通 fork 为每个有效源页分配新物理页并复制整页内容，使用源 PTE 的 `PTE_USER` 权限掩码（R/W/X/U/V）调用 `page_insert()`。子页映射的引用计数由 `page_insert()` 增加，父页的引用计数保持原值。现有接口中的 `share=1` 表示显式共享源物理页；普通 fork 始终使用 `share=0`。
- 数据页分配失败时返回 `-E_NO_MEM`。插入失败时释放当前尚未挂入页表的私有页并返回插入错误。此前已复制的前缀由 `unmap_range()` 和 `exit_range()` 回收，包括映射页及随之变空的页表。Lab4 的 `get_pte()` 负责回滚当前插入过程中分配失败的中间页表。
- `dup_mmap(to, from)` 逆序遍历源 VMA，复制各区间和 `vm_flags`，经 `insert_vma_struct()` 保持目标链表升序，再逐区间执行私有页复制。失败时先用 `exit_mmap()` 回收本轮所有目标区间的页映射，再释放目标 VMA、复位计数与缓存。调用者仍持有目标 `mm` 对象和根页目录，失败后可重试或释放二者。

## 实际验证

在仓库中运行以下命令，两个 RISC-V 目标对象均编译成功，`git diff --check` 通过。

```bash
make -C code obj/kern/mm/pmm.o obj/kern/mm/vmm.o
git diff --check
```

QEMU 专项验证使用 `/tmp/os2026-lab5-A.VE2v6Y/code` 中的源码副本。仅在副本中启用已有的 Lab4 OOM 注入开关、加入 VMA 分配失败钩子，并在 `vmm_init()` 的原有自检之后运行专项断言。测试镜像通过 `KSRCDIR` 选择当前可独立编译的内核模块。实际构建和成功启动命令如下。

```bash
make -C /tmp/os2026-lab5-A.VE2v6Y/code \
  KSRCDIR='kern/init kern/libs kern/debug kern/driver kern/trap kern/mm kern/sync kern/process kern/schedule'
timeout 20s qemu-system-riscv64 -machine virt -nographic -bios default \
  -kernel /tmp/os2026-lab5-A.VE2v6Y/code/bin/ucore.img
```

测试镜像构建完成。第一次用 `make qemu` 启动时没有沿用 `KSRCDIR`，Make 重新尝试构建尚未实现的系统调用模块并报错。随后直接启动已构建镜像，QEMU 正常退出，退出码为 0。

| 专项检查 | 实际结果 |
| --- | --- |
| Lab4 页表分配失败回滚及原有 VMA 自检 | 通过。OOM 前后空闲页数相同。 |
| 私有页复制 | 通过。验证完整页内容、修改子页后的父页隔离、R/W/X/U 权限、VMA 顺序和标志、稀疏区间及引用计数。 |
| 物理页分配失败 | 通过。在 8 个不同分配位置注入失败，验证错误码、目标页目录与 VMA 清空、源页引用计数和空闲页数，随后重试成功。 |
| VMA 分配失败 | 通过。第二个 VMA 分配失败后，第一个已复制 VMA 及其映射全部回收。 |
| 显式共享与最终清理 | 通过。验证插入失败时源引用计数不变、成功共享后增加一份引用、解除映射后还原，以及结束时空闲页数回到初始值。 |

测试断言源码和输出保存在同一临时目录的 `harness.c`、`build.log` 和 `qemu.log`。用户态普通 fork 的端到端测试和课程评分留待集成。COW 留待 Challenge 阶段。

### 2026-11-27 复测

重新编译两份对象，并从当前源码另建隔离镜像。旧专项测试的临时目录已不存在，以下复测使用新目录。QEMU 退出码为 0，Lab4 页表回滚和六处物理页分配失败注入均通过，同时验证了父子页隔离、权限、空洞及最终空闲页数。VMA 分配失败和显式共享仍以首轮专项测试为记录。

```bash
make -C code -B obj/kern/mm/pmm.o obj/kern/mm/vmm.o
make -C /tmp/lab5-a-recheck.vVXvua/code \
  KSRCDIR='kern/init kern/libs kern/debug kern/driver kern/trap kern/mm kern/sync kern/process kern/schedule'
timeout 20s qemu-system-riscv64 -machine virt -nographic -bios default \
  -kernel /tmp/lab5-a-recheck.vVXvua/code/bin/ucore.img
```

## 联调接口

- B 的 `copy_mm()` 创建尚未发布、引用计数为零的目标 `mm` 和独立根页目录。在源 `mm` 锁保护下调用 `dup_mmap()`，成功后再交给子进程。若失败，保留错误码并释放空目标根页目录和 `mm` 对象。共享整个地址空间的路径由 B 在 `copy_mm()` 中处理。
- C 通过系统调用分发把 fork 请求交给 B 的 `do_fork()`，并验证错误码和父子返回值经 trapframe 正确返回。普通 fork 的复制工作在内核中同步完成，写缺页入口在 COW 阶段再联调。
