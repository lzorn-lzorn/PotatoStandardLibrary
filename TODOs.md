# TODOs

本文档记录 PotatoStandardLibrary 自身的待办事项。清单以 `Core/`、`Test/` 和
`PerfBench/` 为范围；`benchmark/` 是随仓库引入的第三方 Google Benchmark 源码，
其中的 TODO/FIXME 不纳入本项目清单。

## 状态与优先级

- `[ ]` 未开始或尚未完成
- `[-]` 已有部分实现，仍需补齐
- `[x]` 已完成，保留在这里用于说明当前基线
- **P0**：影响基础设施正确性或公共 API，优先处理
- **P1**：核心功能，完成后才能供引擎稳定使用
- **P2**：优化、扩展或工程化完善

## 当前进度总览

| 模块 | 状态 | 优先级 | 当前情况 |
| --- | --- | --- | --- |
| 异常/错误处理封装 | `[-]` | P0 | `Core/exception_handler.h` 已提供宏和作用域处理器，但异常开启/关闭两种编译模式尚未统一验证 |
| SmallVector | `[ ]` | P1 | 尚未实现 |
| SparseArray | `[ ]` | P1 | 尚未实现 |
| SlotArray | `[ ]` | P1 | 尚未实现 |
| 通用 Event System | `[ ]` | P1 | 尚未实现；内存模块已有专用 `MemoryEventBus` |
| Frame Allocator | `[-]` | P1 | 分配器已有 `Transient` lifetime、帧号和延迟释放，但还没有独立的帧线性分配器 API |
| Pool Allocator | `[-]` | P1 | 已有 size class、ThreadCache、CentralPool 和 PageAllocator，仍需补齐对象池 API、验证和调优 |
| 数学模块随机颜色 | `[ ]` | P2 | `linear_color3d::make_random_color()` 仍直接依赖 `<random>` |
| 三角形几何类型 | `[ ]` | P1 | 当前 `triangle` 是二维三角形；需要明确二维/三维 API，不能仅靠注释约定 |

---

# 1. 异常与错误处理封装（P0）

目标是在编译器关闭 C++ 异常时，库仍能提供统一、可诊断的错误处理路径；开启异常时，
则保留标准异常语义，并在边界处记录可靠日志。相关代码位于
`Core/exception_handler.h`，普通应用日志位于 `Core/logger.h`。

## 1.1 当前实现与风险

- 已有 `TRY`、`CATCH`、`CATCH_WITH`、`TRY_CATCH`、`THROW` 宏。
- 已有 `scoped_exception_handler`，可在作用域内替换处理器。
- 已使用 `std::source_location` 记录文件、行号和函数名。
- 异常关闭时，`TRY/CATCH` 为空操作，`THROW` 直接终止；这还不能满足“记录错误后再按策略处理”的要求。
- `CATCH` 系列宏对 `std::exception`、`std::exception_ptr` 和未知异常的参数约定不一致，且需要统一命名空间与调用方式。
- 全局 `std::function` 处理器不是线程局部状态；并发修改或作用域覆盖时需要明确同步语义。

## 1.2 待办

- [ ] **P0** 统一异常处理器接口：明确标准异常、未知异常和 `std::exception_ptr` 的表示方式，避免宏展开后参数类型不匹配。
- [ ] **P0** 统一宏的命名空间和展开规则，确保 `TRY_CATCH` 与 `TRY ... CATCH` 两种写法在 MSVC、Clang、GCC 上行为一致。
- [ ] **P0** 设计异常关闭时的错误策略：至少支持可靠错误输出、`abort`/`terminate` 策略，以及可选的 `Result`/错误码路径；不能让错误被静默吞掉。
- [ ] **P0** 将处理器状态改为线程安全的设计，优先考虑线程局部处理器和显式全局 sink；补充嵌套作用域、并发线程和异常处理器恢复测试。
- [ ] **P1** 增加低分配、低延迟的诊断 sink。异常路径不能依赖普通异步 logger 才能留下关键信息；至少应保留 stderr/操作系统调试输出后端。
- [ ] **P1** 为启用异常和禁用异常分别建立构建测试，覆盖标准异常、未知异常、嵌套处理器、源位置和 `THROW` 行为。
- [ ] **P1** 在公共文档中写清楚：哪些 API 会抛异常、哪些 API 在禁用异常时终止，以及调用方如何选择错误码/结果值接口。

## 1.3 验收标准

- 异常开启和关闭时，公共头文件均可编译。
- 同一错误在两种模式下都能留下包含错误类别和 `source_location` 的诊断信息。
- 处理器在嵌套作用域结束后恢复；不同线程之间不会互相覆盖处理器。
- 测试不依赖输出文本的偶然格式，且能验证未知异常不会被错误地当作标准异常处理。

---

# 2. SmallVector（P1）

新增建议路径：`Core/Containers/small_vector.h`。目标是为小规模、短生命周期数组提供
内联存储，减少堆分配；接口应尽量接近 `std::vector`，但不承诺地址在扩容后稳定。

- [ ] 设计模板参数和公共 API：`T`、内联容量、可选 allocator；提供 `size`、`capacity`、`reserve`、`resize`、`clear`、`push_back`、`emplace_back`、`pop_back`、下标访问和迭代器。
- [ ] 实现未初始化内联存储，正确支持非默认构造、不可复制类型、只移动类型以及有状态 allocator。
- [ ] 实现内联存储与堆存储之间的迁移、扩容和收缩；处理自引用插入和异常安全。
- [ ] 明确拷贝/移动构造和赋值的条件，避免因为 `T` 的特殊成员函数而产生错误的可用性声明。
- [ ] 提供边界断言或异常策略，并在禁用异常构建下保持明确行为。
- [ ] 增加基础操作、扩容、移动类型、异常回滚、对齐和大对象测试。
- [ ] 增加与 `std::vector` 的功能对比 benchmark，至少覆盖无扩容、频繁扩容和超过内联容量三种场景。

验收标准：内联容量范围内的常见操作不产生堆分配；所有元素生命周期和 allocator 操作
通过 sanitizer/压力测试；迭代器和连续内存语义有明确文档。

---

# 3. SparseArray（P1）

建议用于“稀疏整数索引 + 紧凑存储”的场景。它与 `SlotArray` 的区别是：SparseArray 主要
解决索引空间稀疏和遍历密集元素，不自动承诺外部句柄的代际安全。

- [ ] 确定索引类型、无效索引表示和最大索引范围。
- [ ] 设计 sparse index 到 dense index 的映射，以及 dense 元素和反向索引的存储布局。
- [ ] 提供 `insert`、`emplace`、`find`、`contains`、`erase`、`clear`、`size`、`empty` 和密集迭代器。
- [ ] 明确删除后的顺序语义：允许 swap-and-pop 以保持 O(1)，或提供保持顺序的慢路径；两者不能隐式混用。
- [ ] 支持非默认构造、只移动类型和自定义 allocator，并保证失败时不会留下半插入状态。
- [ ] 增加空洞比例、重复删除、极大索引、迭代期间修改和异常回滚测试。
- [ ] 对比 `std::vector`、`std::unordered_map` 和后续 `SlotArray` 的内存占用与遍历性能。

验收标准：插入、查找和删除的复杂度及迭代器失效规则写入文档；密集遍历不扫描整个
稀疏索引空间；删除后所有反向映射保持一致。

---

# 4. SlotArray（P1）

SlotArray 面向需要稳定句柄的引擎对象。句柄应包含槽位索引和 generation，删除后旧句柄
必须失效，避免索引复用造成 use-after-free 式逻辑错误。

- [ ] 设计 `SlotHandle`：索引、generation、无效值和可选容器/类型标识。
- [ ] 实现 `emplace`、`get`、`try_get`、`contains`、`erase`、`clear` 和遍历接口。
- [ ] 使用 free list 复用槽位；每次删除和复用都更新 generation，并处理 generation 溢出策略。
- [ ] 明确扩容时对象地址是否稳定。若不能稳定，应让句柄只依赖槽位而不是对象地址，并补充文档。
- [ ] 支持只移动类型、非默认构造类型和自定义 allocator；正确处理构造失败和析构顺序。
- [ ] 决定是否提供并发读/写版本；未实现同步前，公共文档必须明确容器不是线程安全的。
- [ ] 增加旧句柄失效、槽位复用、generation 回绕、移动容器和批量清理测试。

验收标准：任何已删除对象的旧句柄都不能访问新对象；`erase` 后资源只析构一次；句柄
检查为 O(1)。

---

# 5. Event System（P1）

`Core/Memory/memory_common.h` 中的 `MemoryEventBus` 是内存分配器专用事件总线，不等同于
通用事件系统。通用系统需要服务于输入、窗口、资源和引擎生命周期等事件，同时避免把内存
模块的耦合扩散到其他模块。

- [ ] 确定事件模型：类型安全的静态事件、运行时类型事件，或二者组合；明确同步派发和异步派发边界。
- [ ] 设计订阅句柄/连接对象，保证订阅者销毁后不会收到回调，并支持显式取消订阅。
- [ ] 规定回调期间新增、删除订阅者以及递归派发的语义；回调顺序是否稳定需要写入文档。
- [ ] 设计线程模型：单线程事件循环、多生产者队列，或按事件类型指定线程；不能只依赖调用方自律。
- [ ] 控制热路径分配，优先支持预分配、批量派发和可配置队列容量；队列满时提供明确的丢弃/阻塞/报错策略。
- [ ] 增加生命周期、递归派发、并发生产、取消订阅和异常回调测试。
- [ ] 为 MemoryEventBus 补充独立的线程安全和观察者生命周期文档，必要时复用通用订阅句柄实现。

验收标准：订阅者生命周期可证明安全；同步和异步模式的线程约束、队列满行为及异常策略
均有测试和文档；不会因无订阅者而产生不必要的热路径开销。

---

# 6. Memory System

内存模块已经具备较完整的分层实现，主要代码位于 `Core/Memory/`：

- `ThreadCache`：线程本地 freelist 和批量回收。
- `CentralPool`：size class、span、分片和批量 fetch/return。
- `virtual_memory_backend.h` / `PageAllocator`：页级 reserve、commit、decommit 和 release。
- `memory_facade.h`：small/dedicated 分配路径、`Transient`/`Persistent` lifetime、quarantine、prewarm 和统计接口。
- `Observability/`：泄漏检测、统计、MemoryTracker、JSON 报告和 new/delete hook。

因此，本节记录的是后续补齐项，而不是重新实现已有组件。

## 6.1 Frame Allocator（[-]，P1）

当前的 `AllocationLifetime::Transient`、`setCurrentFrame()` 和延迟释放机制可以表达临时分配，
但还没有显式的 frame arena/linear allocator。`setCurrentFrame()` 目前也不等价于“重置本帧
所有分配”。

- [ ] 设计独立 `FrameAllocator`/`FrameArena` API：`beginFrame`、按对齐分配、溢出页处理和 `endFrame/reset`。
- [ ] 明确单线程、线程分片或多线程线性分配策略；禁止跨线程释放或规定跨线程归还方式。
- [ ] 支持固定预算、页扩容、显式 trim 和统计：本帧使用量、峰值、溢出次数、回收耗时。
- [ ] 确定对象析构策略：对 trivially destructible 类型可批量回收，对需要析构的对象提供注册或 typed API。
- [ ] 将 frame 生命周期与现有 `CurrentFrame`、`Transient` 和 quarantine 规则区分清楚，避免重复释放。
- [ ] 增加多帧复用、对齐、预算耗尽、线程边界和 shutdown 测试，并加入 PerfBench 对比通用分配器。

验收标准：reset 后整个 frame arena 可一次性回收；分配失败和预算超限行为明确；不会把
仍被持有的跨帧对象静默回收。

## 6.2 Pool Allocator（[-]，P1）

当前 size-class/ThreadCache/CentralPool 路径已能完成原始内存分配，并已有 `FreeList` 和
运行时统计。后续工作集中在类型安全、边界验证和性能稳定性。

- [ ] 提供面向对象的 typed pool API，正确调用构造函数和析构函数，并支持批量创建/销毁。
- [ ] 完善 size class、alignment、超大对象和 allocator ownership 的公共文档。
- [ ] 对 CentralPool、PageAllocator、ThreadCache 的容量耗尽、跨线程归还、线程退出和 shutdown 顺序增加压力测试。
- [ ] 校验 dedicated cache、quarantine 和 lazy commit 在 Debug/Release 及不同配置组合下的行为。
- [ ] 建立稳定 benchmark 基线：单线程、并发小对象、大对象、不同生命周期和观察器开启/关闭。
- [ ] 根据 benchmark 结果再调整 shard 数、批量大小和缓存预算；不要在缺少数据时改变 tuning 常量。
- [ ] 增加失败注入测试，覆盖 OS 虚拟内存申请失败、span 池耗尽和观察器抛错等路径。

已完成的基线能力（无需重复立项）：

- `[x]` size class + span 的小对象分配路径。
- `[x]` ThreadCache/CentralPool/PageAllocator 分层和批量回收。
- `[x]` dedicated 大对象路径及 transient/persistent 回收策略。
- `[x]` Debug guard、poison、quarantine 和窗口化 UAF 检测。
- `[x]` `MemoryStatisticsObserver`、`MemoryLeakDetectorObserver`、`MemoryTracker` 和 JSON 报告。
- `[x]` STL allocator、PMR resource、prewarm、运行时统计和基础并发测试。

---

# 7. 数学模块待办

## 7.1 随机颜色接入随机库（P2）

`Core/math.h` 的 `linear_color3d::make_random_color()` 当前直接使用线程局部
`std::mt19937` 和 `std::random_device`。

- [ ] 新增或接入统一的 `Random` 模块，明确引擎级 seed、线程局部 RNG 和可复现模式。
- [ ] 让 `make_random_color()` 支持显式 RNG/seed 重载；默认重载不得让测试依赖系统随机设备。
- [ ] 固定 HSV 的色相、饱和度和亮度范围，补充范围、分布和确定性测试。
- [ ] 评估是否需要避免运行时初始化和锁竞争，并记录随机质量与性能取舍。

## 7.2 三角形二维/三维 API（P1）

`Core/math.h` 当前的 `geometry::triangle` 使用三个 `point2d`，并通过二维重心坐标计算
结果；注释提出“空间中应使用 `point3d`”，但直接把现有类型改成三维会破坏语义和调用方。

- [ ] 明确现有 `triangle` 是否固定表示二维三角形；若保留，改名为 `triangle2d` 或在文档中正式声明二维语义。
- [ ] 新增独立的 `triangle3d`，实现三维点的重心坐标、退化检测、contains 和属性插值。
- [ ] 规定退化三角形的返回值/错误策略，不能用看似有效的 `(1, 0, 0)` 静默掩盖数值错误。
- [ ] 增加共线、近退化、边界点、面外点、负坐标和三维插值测试。
- [ ] 完成后删除 `math.h` 中对应的 `TODO` 注释，避免二维和三维 API 继续混淆。

---

# 8. 完成前检查清单

每个新模块合并前至少应完成：

- [ ] 公共头文件可被独立包含，并通过 `Core/Core.h` 暴露（若属于公共模块）。
- [ ] CMake 构建、Debug/Release 构建和现有测试通过。
- [ ] 对象生命周期、异常/无异常构建行为和线程安全边界有测试或文档。
- [ ] 热路径没有未经说明的动态分配、锁竞争或日志输出。
- [ ] 新增 API 的复杂度、迭代器失效、句柄失效和 allocator ownership 规则已记录。
- [ ] 只搜索项目自有代码确认 TODO 已处理；不要把 `benchmark/` 第三方源码的 TODO 当成本项目完成条件。
