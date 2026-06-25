#pragma once

#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>
#include <span>
#include <iterator>
#include <optional>
#include <memory>
#include <stdexcept>
#include <array>
// #include <thread>
// #include <iostream>

#include "common.h"

namespace core
{
enum class ring_buffer_policy
{
	SingleThread,
	SPSC,
	MPSC,
	MPMC,
};

template <typename T, typename Allocator, ring_buffer_policy Policy>
class ring_buffer;


template <typename T, typename Allocator>
class ring_buffer<T, Allocator, ring_buffer_policy::SingleThread>
{
	static constexpr bool is_power_of_2(size_t n)
	{
        return n != 0 && (n & (n - 1)) == 0;
    }

public:
	using value_type = T;
	using allocator_type = Allocator;
	using allocator_traits = std::allocator_traits<Allocator>;
	using size_type = std::size_t;
	using difference_type = allocator_traits::difference_type;
	using pointer = typename allocator_traits::pointer;
	using const_pointer = typename allocator_traits::const_pointer;
	using reference = value_type&;
	using const_reference = const value_type&;

private:
	template <bool IsConst>
	class iterator_impl {
		friend class ring_buffer;
		using pointer = std::conditional_t<IsConst, const ring_buffer*, ring_buffer*>;
		using reference = std::conditional_t<IsConst, const T&, T&>;
		using buffer_type = std::conditional_t<IsConst, const ring_buffer, ring_buffer>;
		using value_type = std::conditional_t<IsConst, const T, T>;

		buffer_type* m_Parent;
		size_type m_Offset;  // 从 head 开始的偏移

		explicit iterator_impl(buffer_type* p, size_type off) 
			: m_Parent(p)
			, m_Offset(off)
		{
			
		}

	public:
		using iterator_category = std::random_access_iterator_tag;
		using iterator_concept = std::random_access_iterator_tag;

		iterator_impl() = default;
		iterator_impl(const iterator_impl<false>& other)
			requires (!IsConst)
			: m_Parent(other.m_Parent), m_Offset(other.m_Offset)
		{
			
		}
		
		value_type& operator*() const noexcept
		{
			const size_type idx = (m_Parent->m_Head + m_Offset) & m_Parent->m_Mask;
			return m_Parent->m_Buffer[idx];
		}

		iterator_impl& operator++() { ++m_Offset; return *this; }
		iterator_impl operator++(int) { auto tmp = *this; ++m_Offset; return tmp; }
		iterator_impl& operator--() { --m_Offset; return *this; }
		iterator_impl operator--(int) { auto tmp = *this; --m_Offset; return tmp; }

		iterator_impl& operator+=(difference_type n) { m_Offset += n; return *this; }
		iterator_impl& operator-=(difference_type n) { m_Offset -= n; return *this; }
		friend iterator_impl operator+(iterator_impl it, difference_type n) { it += n; return it; }
		friend iterator_impl operator-(iterator_impl it, difference_type n) { it -= n; return it; }
		friend difference_type operator-(const iterator_impl& a, const iterator_impl& b)
		{
			return static_cast<difference_type>(a.m_Offset) - b.m_Offset;
		}

		value_type* operator->() const { return &**this; }
		value_type& operator[](difference_type n) const { return *(*this + n); }

		std::strong_ordering operator<=>(const iterator_impl&) const = default;
		bool operator==(const iterator_impl&) const noexcept = default;
	};

public:
	using iterator       = iterator_impl<false>;
	using const_iterator = iterator_impl<true>;
	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

public:

    explicit ring_buffer(size_type capacity, const Allocator& alloc = Allocator{})
        : m_Capacity(capacity), m_Mask(capacity - 1), m_Allocator(alloc)
    {
        // 要求容量 > 0 且为 2 的幂（可选，也可用 % 代替）
        if (m_Capacity == 0 || !is_power_of_2(m_Capacity))
        {
        	throw std::invalid_argument("Capacity must be a power of 2 and > 0");
        }

        m_Buffer = allocator_traits::allocate(m_Allocator, m_Capacity);
    }

    ~ring_buffer()
	{
        clear();  // 析构所有存活的元素
        allocator_traits::deallocate(m_Allocator, m_Buffer, m_Capacity);
    }

    // 禁止拷贝（原子成员不可拷贝）
    ring_buffer(const ring_buffer&) = delete;
    ring_buffer& operator=(const ring_buffer&) = delete;

    // 允许移动（但移动后源对象原子仍有效，需谨慎）
    ring_buffer(ring_buffer&& other) noexcept
        : m_Capacity(other.m_Capacity)
		, m_Head(other.m_Head)
		, m_Tail(other.m_Tail)
		, m_Mask(other.m_Mask)
		, m_Buffer(other.m_Buffer)
		, m_Allocator(std::move(other.m_Allocator))
    {
        other.m_Capacity = 0;
        other.m_Buffer = nullptr;
    	other.m_Head = 0;
    	other.m_Tail = 0;
    	other.m_Mask = 0;
    }

	ring_buffer& operator=(ring_buffer&& other) noexcept
    {
	    if (this != &other)
	    {
		    clear();
	    	allocator_traits::deallocate(m_Allocator, m_Buffer, m_Capacity);

	    	m_Allocator = std::move(other.m_Allocator);
	    	m_Head = other.m_Head;
	    	m_Tail = other.m_Tail;
	    	m_Capacity = other.m_Capacity;
	    	m_Mask = other.m_Mask;
	    	m_Buffer = other.m_Buffer;

	    	other.m_Head = 0;
	    	other.m_Tail = 0;
	    	other.m_Capacity = 0;
	    	other.m_Mask = 0;
	    	other.m_Buffer = nullptr;
	    }
    	return *this;
    }

    // 单线程访问（仅在无并发写入/读取时安全）
    // 只能在无并发修改时使用（如单线程消费后处理）
    reference front() noexcept { return m_Buffer[m_Head & m_Mask]; }
	const_reference front() const noexcept { return m_Buffer[m_Head & m_Mask]; }
	reference back() noexcept { return m_Buffer[(m_Tail - 1) & m_Mask]; }
	const_reference back() const noexcept { return m_Buffer[(m_Tail - 1) & m_Mask]; }

	void push_back(const value_type& value)
    {
	    if (full())
	    {
		    throw std::out_of_range("ring_buffer: buffer is full");
	    }

    	const auto idx = m_Tail & m_Mask;
    	allocator_traits::construct(m_Allocator, std::addressof(m_Buffer[idx]), value);
    	++m_Tail;
    }

	void push_back(value_type&& value)
    {
    	if (full())
    	{
    		throw std::out_of_range("ring_buffer: buffer is full");
    	}

    	const auto idx = m_Tail & m_Mask;
    	allocator_traits::construct(m_Allocator, std::addressof(m_Buffer[idx]), std::move(value));
    	++m_Tail;
    }

	template <typename... Args>
	T& emplace_back(Args&&... args)
    {
    	if (full())
    	{
    		throw std::out_of_range("ring_buffer: buffer is full");
    	}

    	const auto idx = m_Tail & m_Mask;
    	allocator_traits::construct(m_Allocator, std::addressof(m_Buffer[idx]), std::forward<Args>(args)...);
    	++m_Tail;
    	return m_Buffer[idx];
    }

    void pop_front() noexcept
	{
    	if (full())
    	{
    		throw std::out_of_range("ring_buffer: buffer is empty");
    	}

    	const auto idx = m_Head & m_Mask;
    	allocator_traits::destroy(m_Allocator, std::addressof(m_Buffer[idx]));
    	++m_Head;
    }

    void clear() noexcept
	{
        while (!empty())
        {
        	pop_front();
        }
    }

	[[nodiscard]] bool empty() const noexcept { return m_Head == m_Tail; }
	[[nodiscard]] bool full() const noexcept { return size() == m_Capacity; }
	[[nodiscard]] size_type size() const noexcept { return m_Tail - m_Head; }
	[[nodiscard]] size_type capacity() const noexcept { return m_Capacity; }
	[[nodiscard]] size_type max_size() const noexcept { return allocator_traits::max_size(m_Allocator); }

	// ==================== 迭代器接口 ====================
	[[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }
	[[nodiscard]] iterator end() noexcept { return iterator(this, size()); }

	[[nodiscard]] const_iterator begin() const noexcept { return const_iterator(this, 0); }
	[[nodiscard]] const_iterator end() const noexcept { return const_iterator(this, size()); }

	[[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
	[[nodiscard]] const_iterator cend() const noexcept { return end(); }

	[[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
	[[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

	[[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
	[[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

	[[nodiscard]] const_reverse_iterator crbegin() const noexcept { return rbegin(); }
	[[nodiscard]] const_reverse_iterator crend() const noexcept { return rend(); }

	/// 将环形数据转为两个连续 span（无拷贝，高效访问）
	[[nodiscard]] std::pair<std::span<T>, std::span<T>> linearize_views() noexcept
    {
    	const auto sz = size();
    	if (sz == 0) return {};

    	const auto h_idx = m_Head & m_Mask;
    	const auto first_len = std::min(m_Capacity - h_idx, sz);
    	return {
    		std::span<T>(m_Buffer + h_idx, first_len),
			std::span<T>(m_Buffer, sz - first_len)
		};
    }

	[[nodiscard]] std::pair<std::span<const T>, std::span<const T>> linearize_views() const noexcept
    {
    	const auto sz = size();
    	if (sz == 0) return {};

    	const auto h_idx = m_Head & m_Mask;
    	const auto first_len = std::min(m_Capacity - h_idx, sz);
    	return {
    		std::span<const T>(m_Buffer + h_idx, first_len),
			std::span<const T>(m_Buffer, sz - first_len)
		};
    }

	[[nodiscard]] allocator_type get_allocator() const noexcept { return m_Allocator; }
private:
	alignas(CacheLineSize) size_type m_Head {0};
	alignas(CacheLineSize) size_type m_Tail {0};

	size_type m_Capacity;
	size_type m_Mask;
	value_type* m_Buffer;
	Allocator m_Allocator;

};

/*
int test_signal_thread()
{
	// 创建容量为 8（2的幂）的环形缓冲区
	ring_buffer<int, std::allocator<int>, ring_buffer_policy::SingleThread> buf(8);

	// 插入元素
	for (int i = 0; i < 5; ++i) {
		buf.push_back(i);
	}

	// 原地构造
	buf.emplace_back(99);

	// 遍历输出
	std::cout << "遍历元素：";
	for (int val : buf) {
		std::cout << val << " ";
	}
	std::cout << "\n大小：" << buf.size() << "\n";

	// 弹出队首
	buf.pop_front();
	std::cout << "弹出后首元素：" << buf.front() << "\n";

	// C++20 span 视图访问
	auto [first, second] = buf.linearize_views();
	std::cout << "Span 访问：";
	for (int val : first) std::cout << val << " ";
	for (int val : second) std::cout << val << " ";
	std::cout << "\n";

	// 清空
	buf.clear();
	std::cout << "清空后是否为空：" << std::boolalpha << buf.empty() << "\n";

	return 0;
}
*/
template <typename T, typename Allocator>
class ring_buffer<T, Allocator, ring_buffer_policy::SPSC> 
{};

template <typename T, typename Allocator>
class ring_buffer<T, Allocator, ring_buffer_policy::MPSC>
{};

template <typename T, typename Allocator>
class ring_buffer<T, Allocator, ring_buffer_policy::MPMC>
{};

// SPSC
template<typename T>
concept TriviallyCopyableType = std::is_trivially_copyable_v<T>;

template <TriviallyCopyableType Ty, size_t Capacity>
class double_buffer
{
public:
    double_buffer() = default;
    double_buffer(const double_buffer&) = delete;
    double_buffer& operator=(const double_buffer&) = delete;
    double_buffer(double_buffer&& other) noexcept = delete;
    double_buffer& operator=(double_buffer&& other) noexcept = delete;
    ~double_buffer() = default;

    std::span<Ty, Capacity> get_write_buffer()
    {
        return std::span { m_buffers[write_idx] };
    }

    [[nodiscard]] bool is_committed() const noexcept
    {
        return m_swap_pending.load(std::memory_order_relaxed);
    }

    // 生产者填充缓冲区, 提交交换请求
    bool commit() noexcept
    {
        if (is_committed()) [[unlikely]]
        {
            return false;
        }
        const uint64_t new_id = m_frame_id.fetch_add(1, std::memory_order_relaxed) + 1;

        m_swap_pending.store(true, std::memory_order_release);
        m_last_write_frame_id = new_id;
        return true;
    }
    
    uint64_t current_write_frame_id() const noexcept
    {
        return m_last_write_frame_id;
    }

    void clear_write_buffer() noexcept
    {
        std::memset(m_buffers[write_idx].data(), 0, sizeof(m_buffers[write_idx]));
    }
    // ============== Consumer 消费者接口 =============================
    bool try_swap() noexcept
    {
        if (!m_swap_pending.load(std::memory_order_acquire))
        {
            return false;
        }
        
        write_idx = 1 - write_idx;
        m_last_read_frame_id = m_last_write_frame_id;
        m_swap_pending.store(false, std::memory_order_relaxed);
        return true;
    }
    
    std::span<const Ty, Capacity> get_read_buffer() const noexcept
    {
        return std::span { m_buffers[1-write_idx] };
    }

    uint64_t current_read_frame_id() const noexcept
    {
        return m_last_read_frame_id;
    }


    bool has_new_frame() const noexcept
    {
        const uint64_t latest = m_frame_id.load(std::memory_order_acquire);
        return latest > m_last_read_frame_id;
    }

    void clear_read_buffer() noexcept
    {
        std::memset(m_buffers[1-write_idx].data(), 0, sizeof(m_buffers[1-write_idx]));
    }

private:
    std::array<Ty, Capacity> m_buffers[2];
    size_t write_idx = 0;
    alignas(CacheLineSize) std::atomic<bool> m_swap_pending { false };
    alignas(CacheLineSize) std::atomic<uint64_t> m_frame_id {};

    uint64_t m_last_write_frame_id {0}, m_last_read_frame_id {0};
};
/*
void produce(double_buffer<float, 64>& buffer)
{
    float val = 0.0;
    for (size_t frame = 0; frame < 100; ++frame)
    {
        auto write_buffer = buffer.get_write_buffer();
        for (size_t index = 0; index < 64; ++index)
        {
            write_buffer[index] = val++;
        }
        buffer.commit();
        std::cout << "[Producer] 提交第 " << frame << " 帧\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void consume(double_buffer<float, 64>& buffer)
{
    size_t read_frames = 0;
    while (read_frames < 100)
    {
        if (buffer.try_swap())
        {
            auto read_span = buffer.get_read_buffer();
            float first = read_span[0];
            float last = read_span.back();
            std::cout << "[Consumer] 读取新帧, 首值=" << first << " 末值=" << last << "\n";
            read_frames++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

int main()
{
    double_buffer<float, 64> double_buffer_;
    std::jthread prod (produce, std::ref(double_buffer_));
    std::jthread cons (consume, std::ref(double_buffer_));
    return 0;
}
*/
}
