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
	using reference = value_type&;
	using const_reference = const value_type&;

private:
	template <bool IsConst>
	class iterator_impl {
		friend class ring_buffer;
		using buffer_type = std::conditional_t<IsConst, const ring_buffer, ring_buffer>;
		using value_type = std::conditional_t<IsConst, const T, T>;

		buffer_type* parent_;
		size_type offset_;  // 从 head 开始的偏移

		iterator_impl(buffer_type* p, size_type off) : parent_(p), offset_(off) {}

	public:
		using iterator_category = std::random_access_iterator_tag;
		using difference_type = std::ptrdiff_t;

		value_type& operator*() const 
		{
			size_type idx = (parent_->m_Head.load(std::memory_order_relaxed) + offset_) & parent_->m_Mask;
			return parent_->m_Buffer[idx];
		}

		iterator_impl& operator++() { ++offset_; return *this; }
		iterator_impl operator++(int) { auto tmp = *this; ++offset_; return tmp; }
		iterator_impl& operator--() { --offset_; return *this; }
		iterator_impl operator--(int) { auto tmp = *this; --offset_; return tmp; }

		iterator_impl& operator+=(difference_type n) { offset_ += n; return *this; }
		iterator_impl& operator-=(difference_type n) { offset_ -= n; return *this; }
		friend iterator_impl operator+(iterator_impl it, difference_type n) { it += n; return it; }
		friend iterator_impl operator-(iterator_impl it, difference_type n) { it -= n; return it; }
		friend difference_type operator-(const iterator_impl& a, const iterator_impl& b) {
			return static_cast<difference_type>(a.offset_) - b.offset_;
		}

		value_type* operator->() const { return &**this; }
		value_type& operator[](difference_type n) const { return *(*this + n); }

		auto operator<=>(const iterator_impl&) const = default;
	};

public:
	using iterator       = iterator_impl<false>;
	using const_iterator = iterator_impl<true>;
	
public:
	public:
    explicit ring_buffer(size_type capacity, const Allocator& alloc = Allocator{})
        : m_Capacity(capacity), m_Mask(capacity - 1), m_Allocator(alloc)
    {
        // 要求容量 > 0 且为 2 的幂（可选，也可用 % 代替）
        if (m_Capacity == 0 || !is_power_of_2(m_Capacity))
            throw std::invalid_argument("Capacity must be a power of 2 and > 0");

        m_Buffer = allocator_traits::allocate(m_Allocator, m_Capacity);
    }

    ~ring_buffer() {
        clear();  // 析构所有存活的元素
        allocator_traits::deallocate(m_Allocator, m_Buffer, m_Capacity);
    }

    // 禁止拷贝（原子成员不可拷贝）
    ring_buffer(const ring_buffer&) = delete;
    ring_buffer& operator=(const ring_buffer&) = delete;

    // 允许移动（但移动后源对象原子仍有效，需谨慎）
    ring_buffer(ring_buffer&& other) noexcept
        : m_Capacity(other.m_Capacity), m_Mask(other.m_Mask),
          m_Buffer(other.m_Buffer), m_Allocator(std::move(other.m_Allocator))
    {
        // 移动原子：直接加载值，然后重设原对象
        m_Head.store(other.m_Head.load(std::memory_order_acquire), std::memory_order_relaxed);
        m_Tail.store(other.m_Tail.load(std::memory_order_acquire), std::memory_order_relaxed);
        other.m_Capacity = 0;
        other.m_Buffer = nullptr;
        // 源对象不应再使用，除非重新赋值
    }

    // 单线程访问（仅在无并发写入/读取时安全）
    // 只能在无并发修改时使用（如单线程消费后处理）
    T& front() {
        return m_Buffer[m_Head.load(std::memory_order_relaxed) & m_Mask];
    }

    void pop_front() {
        size_type h = m_Head.load(std::memory_order_relaxed);
        m_Buffer[h & m_Mask].~T();
        m_Head.store(h + 1, std::memory_order_relaxed);
    }

    T& back() {
        size_type t = m_Tail.load(std::memory_order_relaxed);
        return m_Buffer[(t - 1) & m_Mask];
    }

    void clear() {
        while (!empty())
            pop_front();
    }

    size_type size() const noexcept {
        // 快照，可能立即过时
        size_type h = m_Head.load(std::memory_order_acquire);
        size_type t = m_Tail.load(std::memory_order_acquire);
        return t - h;
    }

    bool empty() const noexcept { return size() == 0; }
    bool full() const noexcept {
        return (m_Tail.load(std::memory_order_acquire) -
                m_Head.load(std::memory_order_acquire)) == m_Capacity;
    }

    // 迭代器（非线程安全，遍历期间不能并发修改）
    iterator begin()        { return {this, 0}; }
	iterator end()          { return {this, size()}; }
	const_iterator begin() const { return {this, 0}; }
	const_iterator end()   const { return {this, size()}; }
	const_iterator cbegin() const { return begin(); }
	const_iterator cend()   const { return end(); }

    // C++20 视图：拆分为两个连续 span
    std::pair<std::span<T>, std::span<T>> linearize_views()
	{
		size_type h = m_Head.load(std::memory_order_acquire);
        size_type t = m_Tail.load(std::memory_order_acquire);
        size_type sz = t - h;
        if (sz == 0)
            return { {}, {} };
        size_type h_idx = h & m_Mask;
        size_type first_len = std::min(m_Capacity - h_idx, sz);
        return {
            std::span<T>(m_Buffer + h_idx, first_len),
            std::span<T>(m_Buffer, sz - first_len)
        };
	}
    std::pair<std::span<const T>, std::span<const T>> linearize_views() const
	{
		size_type h = m_Head.load(std::memory_order_acquire);
        size_type t = m_Tail.load(std::memory_order_acquire);
        size_type sz = t - h;
        if (sz == 0)
            return { {}, {} };
        size_type h_idx = h & m_Mask;
        size_type first_len = std::min(m_Capacity - h_idx, sz);
        return {
            std::span<const T>(m_Buffer + h_idx, first_len),
            std::span<const T>(m_Buffer, sz - first_len)
        };
	}


private:
	alignas(CacheLineSize) std::atomic<size_type> m_Head {0};
	alignas(CacheLineSize) std::atomic<size_type> m_Tail {0};

	size_type m_Capacity, m_Mask;
	value_type* m_Buffer;
	Allocator m_Allocator;

};

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
