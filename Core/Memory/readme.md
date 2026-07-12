# Memory 模块设计说明

## 1. 目标

Memory 模块面向游戏引擎运行时，目标是：

- 小对象高并发分配低延迟
- 线程缓存快路径常量时间
- 中央池可控竞争与批处理
- 页级虚拟内存统一封装
- Debug/Release 双模式行为清晰
- 观测能力与分配核心解耦
- 可直接适配 STL 与 PMR

## 2. 命名约定

本目录代码使用统一命名规则：

- 类型名：大驼峰（UpperCamelCase）
- 常量：大驼峰（UpperCamelCase）
- 类型成员对象：大驼峰（UpperCamelCase）
- 类成员函数：动词开头小驼峰（lowerCamelCase）

## 3. 分层架构

### 3.1 视图

```text
User API (AllocatorFacade / EngineAllocator / EngineMemoryResource)
    -> MemoryAllocatorEngine (pipeline orchestration)
        -> ThreadCache (TLS, lock-free fast path)
            -> CentralPool (size-class shared pool)
                -> PageAllocator (span provider)
                    -> VirtualMemoryManager (OS VM wrapper)
```

### 3.2 组件职责

- `VirtualMemoryManager`
  - 封装 reserve / commit / decommit / protect / release
  - 保证 OS 相关调用集中管理
- `PageAllocator`
  - 管理 `VirtualRegion` 与 `PageSpan`
  - 提供按页连续 span 的获取与回收
- `CentralPool`
  - 按 SizeClass 维护共享 Span 链表
  - 为 ThreadCache 提供批量 fetch/return
- `ThreadCache`
  - 每线程私有 freelist
  - 命中时不触发共享竞争
- `MemoryAllocatorEngine`
  - 串联分配管线阶段
  - 处理小对象路径与大对象 dedicated 路径
  - 基于 `AllocationLifetime` 区分 transient / persistent 行为
- 观测模块（独立）
  - `MemoryLeakDetectorObserver`
  - `MemoryTracker`（统一统计/问题检测/JSON 报告）
  - 目录：`Core/Memory/Observability/`（提供 `memory_observability.h` 统一入口）

## 4. 分配管线

一次分配请求按固定阶段执行：

1. `prepareContext`
   - 填充线程 ID、时间戳、分配 ID、Frame/Resource 元数据
2. `validateRequest`
   - 检查 size 与 alignment 合法性
3. `buildLayout`
   - 计算 Header/Guard/UserOffset/TotalSize
   - 判定 small class 或 dedicated region 路径
4. `allocateRawBlock`
   - small: `ThreadCache -> CentralPool`
   - large: `VirtualMemoryManager reserve + commit`
5. `initializeLayout`
   - Debug Header
   - Front/Back Guard 填充
   - 用户区填充（零初始化或分配填充值）
6. `emitAllocationEvent`
   - 通过 `MemoryEventBus` 通知观测器

释放路径执行对称阶段：

- 定位 dedicated 或 small block
- guard 校验
- poison fill
- 小对象进入 deferred free quarantine（Debug）后再回收到 ThreadCache/CentralPool
- 大对象按 lifetime 进入 dedicated cache（transient）或直接 release（default/persistent）
- 发送 deallocation 事件

## 5. 关键数据结构

### 5.1 SizeClass

- 采用固定尺寸表（8B 到 256KB）
- 每个 class 有固定 `BlockSize / BlocksPerSpan / PagesPerSpan`

### 5.2 Span

- 一个 Span 仅服务一个 SizeClass
- 内部 block 通过 intrusive 单链表组织
- `FreeBlocks` 驱动状态机：Full / Partial / Empty

### 5.3 ThreadCache::FreeList

- `Head + Count + MaxCount`
- 超阈值触发批量回收到 CentralPool

## 6. 并发模型

- ThreadCache：线程私有，无共享锁
- CentralPool：按 size class + shard 分桶，热点路径使用自旋锁
- PageAllocator：按线程哈希分片（Shard），每个分片独立维护 Region/FreeSpanCache 与互斥锁
- Span 页映射：写路径使用互斥锁 + 快照发布；读路径通过 RCU 风格快照无锁查询
- EventBus：快照发布模型，读路径无写锁放大

### 6.1 PageAllocator Sharding

- `acquireSpan/releaseSpan` 不再竞争单一全局锁，而是先选择一个 shard。
- shard 内部独立维护：
  - `Regions`
  - `RegionIndex`
  - `FreeSpanCache`
  - `OversizedFreeSpanCache`
- 释放时通过 `OwnerRegionId` 回到原 shard，保证 span 生命周期一致性。

### 6.2 CentralPool PageToSpan 读无锁快照

- 注册/反注册 span 页映射时：
  - 在写锁下更新内部 `PageToSpan`
  - 同步构建并发布新的只读快照
- `returnBatch` 热路径：
  - 不再持有 `SpanMapMutex` 读锁
  - 直接从原子快照读取页到 span 的映射
- span 对象空闲池按 size class 分桶，避免跨 class 复用造成并发误判。

### 6.3 CentralPool Per-SizeClass Sharding

- 每个 size class 内部再划分多个 shard（固定数量），根据线程 ID 哈希定位 shard。
- `fetchBatch` 在当前线程 shard 内取/建 span，减少同一 size class 的锁碰撞。
- `returnBatch` 先按 span 分组，再回到各自 shard 合并 freelist，避免单锁串行化。

### 6.4 热路径分配优化

- `returnBatch` 使用固定大小分组数组（默认 8 组）代替每次 `std::vector` 动态分配。
- 当分组超过固定容量时，退化为单块即时归还，保持无堆分配热路径。

## 7. Debug 与 Release 策略

### 7.1 Debug 模式

- 启用 Header + FrontGuard + BackGuard
- 释放前校验 guard
- 释放后用户区 poison fill
- guard 破坏会发出 `GuardCorruption` 事件，分配器继续回收流程（不 silent fail）

### 7.2 Release 模式

- 默认关闭 debug guard（除非强制宏开启）
- 避免额外 header/guard 带来的内存开销

### 7.3 Use-After-Free 检测

- 当前实现包含 Debug 模式下的 UAF 检测窗口：
  - 小对象释放后进入 quarantine，而不是立即复用
  - 退出 quarantine 时校验用户区是否仍为 `FreedPattern`
  - 如果被修改，发出 `UseAfterFree` 事件
- 该机制是“窗口化检测”而非全时刻硬隔离：
  - 能覆盖释放后短时间写入场景
  - 不等价于 ASan 的全量 shadow-memory 检测

## 8. 观测与分析

- 统一统计：聚合计数、峰值字节、live blocks
- 问题检测：double free / invalid free / mismatch / guard / UAF / validation
- 报告输出：当前实现 JSON 后端（可扩展更多格式）

观测模块只依赖事件总线，不参与分配决策。

## 9. 与 STL / PMR 集成

- `EngineAllocator<T>`：用于 `std::vector/std::map/...`
- `EngineMemoryResource`：用于 `std::pmr` 容器
- 两者最终都回到 `AllocatorFacade`，保证同一行为与统计口径

## 10. 扩展建议

- 增加 size class 自适应统计调参
- 增加 NUMA 节点级 ThreadCache 策略
- 增加更细粒度的 central contention 指标
- 增加启动期/关卡切换期的批量预热 API

## 11. 生命周期与大对象优化

- `AllocationLifetime::Transient`
  - 小对象：优先 ThreadCache 快路径
  - 大对象：启用 dedicated region cache，减少 reserve/release 抖动
- `AllocationLifetime::Persistent`
  - 小对象：绕过 ThreadCache，直接走 CentralPool，减少 TLS cache 污染
  - 大对象：默认 dedicated 路径（可按策略直接 release）
- `AllocationLifetime::Default`
  - 保持当前通用策略（兼容旧调用点）

## 12. 启动期/关卡切换批量预热 API

- 提供 `AllocatorFacade::prewarm(const std::vector<MemoryPrewarmRequest>&)`
- 用于在加载阶段提前触发：
  - size class span 建立
  - ThreadCache/CentralPool 初次路径
  - 大对象路径页映射与缓存热身
- 典型时机：
  - 引擎启动完成后
  - 关卡切换加载阶段
  - 大规模实体生成前

## 13. 面向 GC / ECS / 对象池的可扩展性

- GC：
  - 可将 ResourceId 对应 GC generation / arena id
  - 通过事件总线观察不同代的分配/回收行为
- ECS：
  - transient 适合 frame-local 组件暂存
  - persistent 适合长期 archetype/chunk 元数据
- 对象池：
  - 推荐对象池自身管理生命周期，底层仍可使用 transient/persistent hint
  - 可用 prewarm API 在场景切换前批量热身

## 14. 维护建议

- 修改布局相关逻辑时，优先保证 `buildLayout` 与 `destroyLayout` 对称
- 修改 CentralPool 结构时，先验证 span 状态转移与回收边界
- 修改 ThreadCache 策略时，保持 fast path 常量时间
- 任何新观测逻辑优先通过 EventBus 接入，避免耦合到分配核心

## 15. 编译期策略开关（Memory 子模块）

- 新增 `Core/Memory/CMakeLists.txt` 作为 Memory 子模块配置入口。
- `Core/CMakelists.txt` 会加载该模块并把开关转换成编译期宏。

当前支持：

- `POTATO_MEMORY_DEBUG_GUARDS_MODE`：`AUTO|ON|OFF`
- `POTATO_MEMORY_UAF_DETECTION_MODE`：`AUTO|ON|OFF`
- `POTATO_MEMORY_CAPTURE_STACK`：`ON|OFF`
- `POTATO_MEMORY_QUARANTINE_RELEASE_ONLY_ON_FLUSH`：`ON|OFF`
- `POTATO_MEMORY_LAZY_COMMIT`：`ON|OFF`

对应宏：

- `CORE_MEM_CFG_ENABLE_DEBUG_GUARDS`
- `CORE_MEM_CFG_ENABLE_UAF_DETECTION`
- `CORE_MEM_CFG_CAPTURE_STACK`
- `CORE_MEM_CFG_QUARANTINE_RELEASE_ONLY_ON_FLUSH`
- `CORE_MEM_CFG_LAZY_COMMIT`

在代码中统一映射为 `memory_common.h` 中的 `MemoryCompile*` 常量，分配器热点路径通过 `if constexpr` 进行编译期裁剪。

## 16. 测试与 Benchmark 构建（Debug / Release）

以下命令均在仓库根目录执行。

### 16.1 一次性配置

```powershell
cmake -S . -B build -DPOTATO_STANDARD_BUILD_TESTS=ON -DPOTATO_STANDARD_BUILD_BENCHMARKS=ON
```

### 16.2 构建并运行测试

Debug：

```powershell
cmake --build build --config Debug --target MemoryAllocatorTest_cpp
ctest --test-dir build -C Debug -R MemoryAllocatorTest_cpp --output-on-failure
```

Release：

```powershell
cmake --build build --config Release --target MemoryAllocatorTest_cpp
ctest --test-dir build -C Release -R MemoryAllocatorTest_cpp --output-on-failure
```

### 16.3 构建并运行 Benchmark

Debug：

```powershell
cmake --build build --config Debug --target MemoryAllocatorBenchmark
.\build\PerfBench\Debug\MemoryAllocatorBenchmark.exe
```

Release：

```powershell
cmake --build build --config Release --target MemoryAllocatorBenchmark
.\build\PerfBench\Release\MemoryAllocatorBenchmark.exe
```

### 16.4 常用 Benchmark 参数

仅跑多线程 64B 场景：

```powershell
.\build\PerfBench\Release\MemoryAllocatorBenchmark.exe --benchmark_filter="BM_TransientAllocateFreeThreaded64" --benchmark_min_time=1.0s
```

导出 JSON 结果：

```powershell
.\build\PerfBench\Release\MemoryAllocatorBenchmark.exe --benchmark_out=build\benchmark_result.json --benchmark_out_format=json
```
