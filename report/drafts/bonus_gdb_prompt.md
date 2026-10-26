# Lab2 加分题 B：GDB/MMU 智能调试工具 Prompt 记录

## 1. 任务背景

本次加分题的目标是在 Lab2 必做的 GDB/MMU/Sv39 调试基础上，开发一个能够自动分析 Sv39 页表的辅助调试工具。

工具使用 GDB 原生命令脚本实现，最终注册自定义命令：

```gdb
sv39walk <virtual_address>
```

能够自动读取 `satp`、计算 VPN 索引、解析页表项、识别页大小并计算物理地址。

本文件按照课程推荐的四段式 SPEC 结构，整理 AI 辅助开发过程中涉及的关键任务和问题。以下 Prompt 是对实际交互意图的归纳整理，并非逐字聊天记录。

---

## 2. Prompt 1：确定加分题 B 的实现方案

### [PROMPT]

阅读 Lab2 加分题 B 的要求，结合已经完成的 GDB/MMU/Sv39 调试工作，设计一个具有教学演示价值的智能调试工具。

希望在已有环境上继续开发，尽量复用前期调试成果，不重新修改内核代码。

### [RELY]

- 项目：OS2026fall Lab2。
- 已完成 `satp`、Sv39 根页表与虚拟地址转换调试。
- 已通过 QEMU MMU 日志观察 `riscv_cpu_tlb_fill` 记录。
- 实验要求通过 `make qemu` 启动 QEMU。
- 加分题 B 要求形成更完整的地址查询、页表查询或单步调试演示工具。

### [GUARANTEE]

- 不影响已经完成的 Lab2 基础功能。
- 不侵入 `pmm.c`、Best-Fit、Buddy 等内核模块。
- 保留现有 `Makefile` 和 `entry.S` 启动修复。
- 工具应具有独立性和可复用性。
- 不将普通 GDB 调试直接当作已经完成的加分成果。

### [SPECIFICATION]

实现一个 Sv39 自动地址转换工具，支持输入虚拟地址，输出分页模式、页表索引、页表项和物理地址，并形成可复现的操作演示。

**阶段结果：** 确定开发 `sv39walk` 自定义 GDB 命令，采用独立脚本实现。

---

## 3. Prompt 2：设计 Python-GDB 自动页表分析脚本

### [PROMPT]

编写一个 GDB Python 脚本，通过自定义 `sv39walk` 命令，自动完成 RISC-V Sv39 页表查询。

要求解释核心实现逻辑，并提供能够在现有 uCore 调试环境中运行的代码。

### [RELY]

- `satp = 0x8000000000080205`。
- 根页表物理地址为 `0x80205000`。
- `PTE[2] = PTE[511] = 0x200000cf`。
- 内核高虚拟地址 `0xffffffffc02000d6` 对应物理地址 `0x802000d6`。
- 当前环境已经具备 RISC-V GDB 和 QEMU。

### [GUARANTEE]

- 只读取调试信息，不修改页表。
- 检查 Sv39 分页模式和虚拟地址合法性。
- 对无效页表项提供错误提示。
- 输出计算过程，方便教学演示。

### [SPECIFICATION]

创建：

```text
tools/sv39/sv39_walk.py
```

使用 GDB Python API 实现：

- 读取 `satp`。
- 计算 VPN[2:0]。
- 查询 L2、L1、L0 页表。
- 识别不同级别的叶子页表项。
- 计算最终物理地址。

**阶段结果：** 完成 Python 脚本初版和语法检查，但加载时发现当前 RISC-V GDB 不支持 Python scripting，因此该版本没有完成实际运行验证。

---

## 4. Prompt 3：解决 GDB Python 不支持的问题

### [PROMPT]

在 GDB 中执行 Python 脚本时出现：

```text
Python scripting is not supported in this copy of GDB.
```

希望在不重新安装 GDB、不更换工具链的前提下，继续实现自动化地址转换功能。

能否使用 GDB 自身支持的命令编写脚本？

### [RELY]

- 当前 RISC-V GDB 16.3.90 可以正常连接 QEMU。
- 已经成功使用 `break`、`continue`、`si`、`info registers` 和 `x/gx` 调试内核。
- 只有 Python 脚本功能不可用。
- 课程已有的 `make qemu` 和 `make gdb` 工作流程需要保留。

### [GUARANTEE]

- 不重新安装工具链。
- 不改变 QEMU 和内核启动方式。
- 不修改内核源代码。
- 优先复用 GDB 已有功能。

### [SPECIFICATION]

采用 GDB 原生脚本实现自定义命令，使用：

```text
define
set
if
while
printf
document
```

完成与 Python 方案相近的基础页表分析功能。

**阶段结果：** 确定使用 `.gdb` 脚本，创建 `tools/sv39/sv39_walk.gdb`。

---

## 5. Prompt 4：恢复 make qemu 与 GDB 调试连接

### [PROMPT]

之前已经通过修改 `Makefile` 和 `entry.S` 实现 `make qemu` 正常启动，但现在使用 GDB 时出现：

```text
The program is not being run.
could not connect: Connection timed out.
```

后来启动 QEMU 调试模式又出现：

```text
-s: Failed to find an available port: Address already in use
```

希望保留课程要求的 `make qemu` 启动方式，排查调试连接问题。

### [RELY]

- 当前 `make qemu` 能正常启动 uCore。
- Makefile 已支持 `QEMUOPTS` 参数。
- QEMU 的 GDB 调试端口为 1234。
- `entry.S` 已配置低地址恒等映射和内核高地址映射。

### [GUARANTEE]

- 不重新安装 QEMU。
- 不重新修改已验证的页表初始化代码。
- 不影响普通 `make qemu` 启动。
- 定位问题后只进行必要处理。

### [SPECIFICATION]

使用：

```bash
sudo ss -ltnp | grep ':1234'
```

检查端口占用，确认旧 QEMU 进程后将其结束。

使用：

```bash
make qemu QEMUOPTS="-s -S"
```

启动调试模式，再通过：

```bash
make gdb
```

连接调试器。

**阶段结果：** 发现旧 QEMU 进程占用端口 1234，结束该进程后成功恢复连接。

---

## 6. Prompt 5：实现第一版 GDB 原生命令

### [PROMPT]

使用 GDB 原生脚本编写 `sv39walk` 命令。

先实现能够读取 `satp`、计算 VPN[2:0]、确定根页表地址，并自动读取对应的根页表项。

要求不依赖 Python。

### [RELY]

- GDB 已经成功连接 QEMU。
- CPU 已运行到 `kern_init()`。
- 当前 `satp = 0x8000000000080205`。
- 根页表物理地址为 `0x80205000`。
- 根页表第 511 项位于 `0x80205ff8`。

### [GUARANTEE]

- 不改变当前 CPU 寄存器及页表内容。
- 只通过 GDB 读取已有状态。
- 能够在现有 GDB 环境直接加载。
- 输出内容便于人工核对。

### [SPECIFICATION]

创建：

```text
tools/sv39/sv39_walk.gdb
```

实现：

```gdb
sv39walk 0xffffffffc02000d6
```

自动输出：

- 虚拟地址；
- `satp`；
- Sv39 根页表地址；
- VPN[2]、VPN[1]、VPN[0]；
- 根页表项地址；
- 页表项内容。

**阶段结果：** 脚本成功加载，自动读取到：

```text
VPN[2]: 511
Root PTE address: 0x0000000080205ff8
PTE value: 0x00000000200000cf
```

---

## 7. Prompt 6：增加页表项解析与物理地址计算

### [PROMPT]

目前 `sv39walk` 已经可以自动读取根页表项，但没有计算最终物理地址。

希望继续解析 PTE 标志位，判断页表项是否有效、是否为叶子页表项，以及是否满足大页对齐要求，并自动计算物理地址。

### [RELY]

- 当前 `PTE[511] = 0x200000cf`。
- 当前映射是 Sv39 根页表上的 1 GiB 大页。
- 已知参考结果：

```text
VA = 0xffffffffc02000d6
PA = 0x802000d6
```

### [GUARANTEE]

- 保留原有输出信息。
- 不直接写死物理地址。
- 根据读取的 PTE 和输入 VA 完成计算。
- 无效页表项不输出成功结果。

### [SPECIFICATION]

新增：

- V、R、W、X 标志位解析。
- 叶子页表项判断。
- 1 GiB 大页对齐检查。
- 物理地址计算。
- 成功或错误提示。

**阶段结果：** 实际运行得到：

```text
PTE value: 0x00000000200000cf
Page size: 1 GiB
Physical address: 0x00000000802000d6
Translation successful!
```

---

## 8. Prompt 7：完善 Sv39 三级页表查询

### [PROMPT]

第一版 `sv39walk` 已经成功完成 1 GiB 大页转换，希望进一步支持完整的 L2、L1、L0 页表查询逻辑。

要求在遇到非叶子页表项时自动进入下一级页表，并根据最终叶子项所在层级计算物理地址。

### [RELY]

- 现有 GDB 原生命令脚本。
- Sv39 的三级页表结构。
- 每个页表项占 8 字节。
- L2、L1、L0 叶子页表项分别对应 1 GiB、2 MiB、4 KiB 映射。

### [GUARANTEE]

- 保留之前已经验证通过的 1 GiB 大页转换功能。
- 检查无效 PTE 和非法权限组合。
- 对大页进行对齐检查。
- 对非规范虚拟地址给出错误提示。
- 不把尚未实际测试的功能写为已经通过。

### [SPECIFICATION]

通过循环实现：

```text
L2 → L1 → L0
```

如果遇到叶子页表项，则结束遍历并计算物理地址；否则进入下一级页表。

同时增加规范虚拟地址检查以及异常输出。

**阶段结果：** 完成新版原生 GDB 脚本，L2 级别的 1 GiB 映射运行验证通过。L1、L0 的 2 MiB 和 4 KiB 路径已编写，但尚未进行真实映射测试。

---

## 9. Prompt 8：设计正常地址转换测试

### [PROMPT]

针对 `sv39walk` 设计正常地址转换测试，验证工具能够正确处理高地址和低地址两种映射。

希望测试结果能够证明工具根据不同 VPN 索引查询不同页表项，而不是直接返回固定的物理地址。

### [RELY]

当前启动页表中：

```text
PTE[2]   = 0x200000cf
PTE[511] = 0x200000cf
```

这两个页表项映射到同一个物理内存区域。

### [GUARANTEE]

- 使用当前内核中真实存在的映射。
- 不修改页表制造测试结果。
- 记录工具的实际输出。
- 与 GDB 手动计算结果交叉核对。

### [SPECIFICATION]

测试：

```gdb
sv39walk 0xffffffffc02000d6
sv39walk 0x802000d6
```

检查不同根页表索引下的地址转换结果。

**阶段结果：** 两组测试均成功，分别通过 `PTE[511]` 和 `PTE[2]`，最终得到：

```text
Physical address: 0x00000000802000d6
Translation successful!
```

---

## 10. Prompt 9：设计异常地址测试

### [PROMPT]

为了验证 `sv39walk` 的可靠性，希望增加异常输入测试。

要求测试无效页表项以及不符合 Sv39 地址规范的虚拟地址，并检查工具是否能够正确报告错误。

### [RELY]

- 当前启动页表中的 `PTE[1]` 为无效项。
- Sv39 要求虚拟地址第 63～39 位与第 38 位一致。
- 脚本已实现 PTE 有效性和虚拟地址规范性检查。

### [GUARANTEE]

- 只执行读取和分析操作。
- 不让内核实际访问非法地址。
- 不修改当前页表。
- 使用真实 GDB 输出作为测试依据。

### [SPECIFICATION]

测试：

```gdb
sv39walk 0x40000000
sv39walk 0x0000008000000000
```

预期分别得到：

```text
ERROR: Invalid PTE
ERROR: Non-canonical Sv39 virtual address
```

**阶段结果：** 两组测试均通过，工具正确识别无效页表项和非规范虚拟地址。

---

## 11. Prompt 10：整理加分题演示和实验报告

### [PROMPT]

根据 Lab2 加分题 B 的要求，结合 `sv39walk` 的实际实现和测试结果，整理一份 Markdown 实验报告。

要求讲清楚工具原理、操作过程、实际测试、TLB miss 与 Sv39 页表查询的关系，并整理截图和 Prompt 记录。

### [RELY]

- 加分题 B 的三人协作与 Git 分支规范。
- `tools/sv39/sv39_walk.gdb` 的最终实现。
- 四组实际通过的测试。
- 此前完成的 GDB/MMU/Sv39 调试记录。
- QEMU `riscv_cpu_tlb_fill` 日志。

### [GUARANTEE]

- 只使用已经实际观察到的测试结果。
- 不虚构 L1、L0 映射测试结果。
- 不宣称测量过真实硬件 TLB 命中率。
- 不把尚未完成的课件库贡献写为已完成。
- 保持加分项与 Lab2 基础任务相互独立。

### [SPECIFICATION]

生成：

```text
report/drafts/bonus_gdb_agent.md
report/drafts/bonus_gdb_prompt.md
```

报告包含：

1. 实验目的和环境。
2. 工具设计和实现。
3. GDB/QEMU 操作流程。
4. 四组实际测试。
5. TLB、MMU、Sv39 地址转换分析。
6. 工具局限性。
7. 实验总结。

**阶段结果：** 已整理加分题报告及 Prompt 记录，待纳入仓库并完成最终提交检查。

---

## 12. AI 使用总结

本次加分题中，AI 主要用于以下方面：

1. **设计方案：** 将手动 GDB 调试整理为自动化页表查询工具。
2. **代码辅助：** 编写 GDB 脚本，完成 `satp` 解析、VPN 计算、PTE 查询以及物理地址计算。
3. **环境排查：** 分析 GDB Python 不支持、调试端口占用以及脚本加载错误等问题。
4. **测试设计：** 构造正常映射和异常地址测试用例。
5. **文档整理：** 规范化实验过程、测试记录和结果说明。

整个开发过程中，AI 提供实现建议和排查思路，最终以 QEMU 和 GDB 的实际运行结果作为判断依据。

已验证的核心结果包括：

| 测试内容 | 实际结果 |
|---|---|
| 高虚拟地址转换 | `0x802000d6` |
| 低地址恒等映射 | `0x802000d6` |
| 无效页表项检测 | `ERROR: Invalid PTE` |
| 非规范虚拟地址检测 | `ERROR: Non-canonical Sv39 virtual address` |

通过本次开发，将原本需要多次手动输入 GDB 命令的 Sv39 地址转换分析过程，整理成了能够重复使用的 `sv39walk` 调试命令。

本工具当前主要验证了 1 GiB 大页映射，其他页大小及更完整的访问权限检查仍有进一步完善空间。