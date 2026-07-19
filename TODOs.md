
# 1. try-catch 封装
在实际构建引擎时, C++语言自带的异常处理机制是可以通过编译器命令而关闭的, 所以在引擎层面需
要自己重新封装关于异常的部分.
通过 `__cpp_exceptions` 等方式判断此时引擎是否启用了异常. 在 `exception_handler.h` 已
有一定程度的封装. 但是尚不完整, 我们需要实现的特性为:

不论异常功能是否开启 `try-catch` 是否可以正常工作但是对于错误依旧需要记录日志, 与 `logger.h` 
中的日志不同, 其依赖于操作系统的本身的机制, 在应用层面以可靠和速度为准.
如果异常关闭的情况 `TRY`, `CATCH`, `THROW` 都需要发生相应的变化


# 2. SmallVector


# 3. SparseArray


# 4. SlotArray


# 5. Event System


# 6. Memory System
## Frame Allocator
游戏引擎中大量对象只会存活短短几帧的时间, 对于每帧的内存分配内存分配器要进行一定程度的优化
和统计

## Pool Allocator
在游戏引擎中有一些相对特殊的大小的内存会被经常申请, 此时内存分配器应该提供特殊路径优化

