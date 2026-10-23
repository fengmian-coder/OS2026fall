# Lab2 成员3 AI Prompt 使用记录

## Prompt 1：实现 Buddy System 物理页分配器

[PROMPT]

任务：基于当前 OS2026fall Lab2 工程中的真实物理内存管理框架，
实现一个符合 `struct pmm_manager` 接口的 Buddy System 物理页分配器。

本任务只负责新增 Buddy 模块，不修改成员1已经完成的 `page_init()`、
`entry.S` 启动修复，不修改成员2已经完成的 Best-Fit 实现，也暂不修改
`pmm.c` 中当前使用的 `pmm_manager`。Buddy 模块完成并独立检查后，
再由集成人统一完成管理器切换。

Buddy System 应按照 2 的幂大小管理空闲物理页块：
分配时将请求页数向上取整到能够容纳请求的最小 2 的幂块；
若对应阶没有空闲块，则从更高阶空闲块逐级拆分。
释放时寻找同阶 buddy，只有 buddy 同阶、空闲且属于合法可管理范围时
才能合并，并允许继续向更高阶递归/迭代合并。

[RELY]

只能依赖当前 Lab2 工程中的真实结构和接口：

1. `code/kern/mm/pmm.h`
   - `struct pmm_manager` 提供以下接口：
     - `init`
     - `init_memmap`
     - `alloc_pages`
     - `free_pages`
     - `nr_free_pages`
     - `check`

2. `code/kern/mm/memlayout.h`
   - `struct Page` 已包含：
     - `ref`
     - `flags`
     - `property`
     - `page_link`
   - `PG_reserved` 表示保留页；
   - `PG_property` 表示空闲块头页；
   - `property` 可以用于记录 Buddy 空闲块的实际页数；
   - `page_link` 可以作为空闲链表节点。
   - 不要修改 `struct Page`。

3. `code/libs/list.h`
   - 已提供双向链表：
     - `list_init`
     - `list_add`
     - `list_add_before`
     - `list_add_after`
     - `list_del`
     - `list_empty`
     - `list_next`
     - `list_prev`
   - 不要修改公共链表实现。

4. `code/kern/mm/default_pmm.c`
   - 作为 `pmm_manager` 模块结构、Page 状态初始化和空闲页计数的参考；
   - 不复制 First-Fit 的“找到第一个满足块”的策略。

5. `code/kern/mm/best_fit_pmm.c`
   - 仅作为另一种 `pmm_manager` 实现形式参考；
   - 不修改其中现有实现和测试。

6. 当前成员1已经完成：
   - `page_init()`
   - 物理内存范围识别；
   - `pages` 描述符数组；
   - `npage`
   - `va_pa_offset`
   - `init_memmap(pa2page(free_start_pa), nfree)`
   - `entry.S` 启动页表修复。

因此 Buddy 的 `init_memmap(base, n)` 必须能够接收由现有
`page_init()` 传入的一段连续空闲 Page 范围，不能假设 `n`
本身一定是 2 的幂。

[GUARANTEE]

必须新增：

- `code/kern/mm/buddy_pmm.c`
- `code/kern/mm/buddy_pmm.h`

必须提供以下 Buddy 接口：

- `buddy_init`
- `buddy_init_memmap`
- `buddy_alloc_pages`
- `buddy_free_pages`
- `buddy_nr_free_pages`
- `buddy_check`
- `buddy_pmm_manager`

实现必须满足：

1. 使用多个 order 的空闲链表管理 1、2、4、8、16……页块；
2. 不修改 `struct Page`，复用 `property`、`PG_property` 和 `page_link`；
3. `buddy_init_memmap(base, n)` 必须正确处理任意连续页数，
   将其划分为地址合法且对齐的 2 的幂空闲块；
4. 初始化可管理页时正确清除 Reserved 状态、引用计数和旧属性；
5. 分配 `n` 页时，选择最小的 `2^order >= n`；
6. 当前 order 无块时，从更高 order 查找并逐级拆分；
7. 拆分后未使用的一半重新加入低一阶空闲链表；
8. 释放时只能与真正的 buddy 合并；
9. buddy 必须：
   - 位于当前可管理范围；
   - 当前为空闲块头；
   - 与当前块大小/order 相同；
10. 合并后形成高一阶块，并继续尝试向上合并；
11. `buddy_nr_free_pages()` 返回的是空闲“页数”，不是块数；
12. `nr_free` 或等价计数在分配、释放、拆分和合并过程中必须保持一致；
13. 不得修改：
   - `page_init()`
   - `entry.S`
   - First-Fit
   - Best-Fit
   - 公共 `struct Page`
   - 公共链表实现；
14. 暂时不要在本 Prompt 中修改 `pmm.c` 切换当前分配器；
15. 不要宣称测试通过，必须等实际编译和运行后再判断。

[SPECIFICATION]

### buddy_init

Pre-Condition:
- Buddy 模块尚未初始化。

Post-Condition:
- 所有 order 的空闲链表均为空；
- 空闲页总计数为 0。

### buddy_init_memmap(base, n)

Pre-Condition:
- `base != NULL`
- `n > 0`
- `[base, base+n)` 是 `page_init()` 交给 PMM 的连续可用物理页范围。

Post-Condition:
- 所有页的旧管理状态被正确初始化；
- 整段内存被划分为若干合法且满足地址对齐要求的 2 的幂块；
- 每个空闲块只有头 Page 设置 `PG_property`；
- 头 Page 的 `property` 记录该块包含的实际页数；
- 每个块加入对应 order 的空闲链表；
- 总空闲页数等于 n。

### buddy_alloc_pages(n)

Pre-Condition:
- `n > 0`
- Buddy 已初始化。

Post-Condition:
- 若没有足够大小的块，返回 `NULL`；
- 否则返回能够容纳 n 页的最小 2 的幂块的起始 `Page *`；
- 如需从高阶块拆分，逐级二分直到目标 order；
- 未分配的一半重新进入对应低阶链表；
- 已分配块不再保持 `PG_property` 空闲块标记；
- 空闲页计数扣除实际分配的 2 的幂页数。

### buddy_free_pages(base, n)

Pre-Condition:
- `base` 是 Buddy 分配得到的合法块起点；
- `n > 0`；
- 根据 n 得到的目标 order 与分配时采用的 2 的幂块一致。

Post-Condition:
- 释放块恢复为空闲状态；
- 尝试寻找同阶 buddy；
- 只有 buddy 合法、空闲且同阶时才合并；
- 合并后继续向更高阶尝试；
- 最终块加入正确 order 的空闲链表；
- 空闲页计数增加实际释放的 2 的幂页数，合并本身不能重复增加页数。

### buddy_nr_free_pages

Post-Condition:
- 返回当前所有 order 中空闲页的总页数。

### buddy_check

本 Prompt 只要求保留可调用的 `buddy_check` 接口。
全面的 Buddy 测试方案由“Prompt 2：Buddy System 测试设计”
单独生成，避免把实现 Prompt 与测试 Prompt 混在一起。

输出要求：
- 给出 `buddy_pmm.h` 与 `buddy_pmm.c` 的完整实现；
- 保持代码风格与现有 `default_pmm.c` / `best_fit_pmm.c` 接近；
- 对拆分、buddy 地址计算、合并条件等关键逻辑添加简洁注释；
- 不修改本任务边界以外的文件。

---

## Prompt 2：Buddy System 测试设计

[PROMPT]

任务：基于 Prompt 1 已经实现的 Buddy System 物理页分配器，
设计并实现 `buddy_check()`，验证当前 Buddy 分配器的核心行为和
边界情况。

测试必须基于当前工程中的真实 Buddy 实现，不允许通过修改
Buddy 核心分配逻辑来迎合测试，也不能修改成员1的 `page_init()`、
First-Fit、成员2的 Best-Fit 或已有公共测试。

测试完成后，需要能够说明每一组测试具体验证了 Buddy 的什么性质。

[RELY]

当前 Buddy 模块已经实现：

- `buddy_init`
- `buddy_init_memmap`
- `buddy_alloc_pages`
- `buddy_free_pages`
- `buddy_nr_free_pages`
- `buddy_pmm_manager`

当前实现使用：

- 多个 order 的空闲链表；
- `order 0` 表示 1 页；
- `order 1` 表示 2 页；
- `order 2` 表示 4 页；
- 以此类推；
- `struct Page.property` 记录 Buddy 块实际包含的页数；
- `PG_property` 只表示当前 Page 是空闲块头；
- 非 2 的幂请求会向上取整：
  - 1 页 -> 1 页块
  - 3 页 -> 4 页块
  - 5 页 -> 8 页块；
- 分配时允许从高阶块逐级拆分；
- 释放时允许同阶 buddy 逐级合并；
- `buddy_nr_free_pages()` 记录的是总空闲页数，而不是空闲块数量。

[GUARANTEE]

`buddy_check()` 至少验证：

1. 初始空闲页数大于测试所需页数；
2. 超过 Buddy 最大可表示块大小的请求返回 `NULL`；
3. 申请 1 页能够正常返回；
4. 申请 3 页时实际得到 4 页 Buddy 块；
5. 申请 5 页时实际得到 8 页 Buddy 块；
6. 三次分配得到的内存块不重叠；
7. 分配后总空闲页数应准确减少：
   - 1 + 4 + 8 = 13 页；
8. 分别释放这些块后，总空闲页数准确恢复；
9. 释放顺序不能影响最终结果；
10. 测试前后每个 order 的空闲块数量恢复一致，用来验证
    拆分后能够正确进行伙伴合并；
11. 测试不得破坏原有物理内存管理状态；
12. 测试不能修改 First-Fit、Best-Fit 和 `page_init()`；
13. 不允许仅打印“成功”而没有断言；
14. 只有所有 assert 实际通过后，才能认为 Buddy 基础测试通过。

[SPECIFICATION]

### 测试前状态保存

Pre-Condition:
- Buddy 已执行 `buddy_init()`；
- `buddy_init_memmap()` 已经接管系统可用物理页。

要求：
- 保存测试前的总空闲页数；
- 保存所有 order 的空闲块数量。

### 非 2 的幂分配测试

执行：

- `alloc_pages(1)`
- `alloc_pages(3)`
- `alloc_pages(5)`

Post-Condition:

- 1 页申请实际块大小为 1；
- 3 页申请实际块大小为 4；
- 5 页申请实际块大小为 8；
- 三块不相互重叠；
- 总空闲页数减少 13。

### 超大请求测试

执行一个大于 `2^BUDDY_MAX_ORDER` 页的请求。

Post-Condition:

- 返回 `NULL`；
- 总空闲页数不发生改变。

### 释放和合并测试

将前面分配的块按照与分配不同的顺序释放。

Post-Condition:

- 总空闲页数恢复到测试前；
- 每个 order 的空闲块数量恢复到测试前；
- 说明拆分出来的伙伴块能够在释放后重新合并；
- 不产生重复计算空闲页的问题。

输出要求：

- 只修改 `buddy_pmm.c` 中 Buddy 测试相关内容；
- 不修改 Buddy 核心算法来适配测试；
- 测试以 assert 为主要验证手段；
- 保持测试代码简单、可解释，便于答辩。
