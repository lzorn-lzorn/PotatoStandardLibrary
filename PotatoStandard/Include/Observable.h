#pragma once

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
#include <atomic>

namespace ptd
{

namespace details
{

template <bool Enable>
struct SMF_Copy_Constructible
{
	constexpr SMF_Copy_Constructible() = default;
	constexpr SMF_Copy_Constructible(const SMF_Copy_Constructible&) = default;
	constexpr SMF_Copy_Constructible(SMF_Copy_Constructible&&) = default;
	constexpr SMF_Copy_Constructible& operator=(const SMF_Copy_Constructible&) = default;
	constexpr SMF_Copy_Constructible& operator=(SMF_Copy_Constructible&&) = default;
};

template <>
struct SMF_Copy_Constructible<false>
{
	constexpr SMF_Copy_Constructible() = default;
	SMF_Copy_Constructible(const SMF_Copy_Constructible&) = delete;
	constexpr SMF_Copy_Constructible(SMF_Copy_Constructible&&) = default;
	constexpr SMF_Copy_Constructible& operator=(const SMF_Copy_Constructible&) = default;
	constexpr SMF_Copy_Constructible& operator=(SMF_Copy_Constructible&&) = default;
};

template <bool Enable>
struct SMF_Copy_Assignable
{
	constexpr SMF_Copy_Assignable() = default;
	constexpr SMF_Copy_Assignable(const SMF_Copy_Assignable&) = default;
	constexpr SMF_Copy_Assignable(SMF_Copy_Assignable&&) = default;
	constexpr SMF_Copy_Assignable& operator=(const SMF_Copy_Assignable&) = default;
	constexpr SMF_Copy_Assignable& operator=(SMF_Copy_Assignable&&) = default;
};

template <>
struct SMF_Copy_Assignable<false>
{
	constexpr SMF_Copy_Assignable() = default;
	constexpr SMF_Copy_Assignable(const SMF_Copy_Assignable&) = default;
	constexpr SMF_Copy_Assignable(SMF_Copy_Assignable&&) = default;
	SMF_Copy_Assignable& operator=(const SMF_Copy_Assignable&) = delete;
	constexpr SMF_Copy_Assignable& operator=(SMF_Copy_Assignable&&) = default;
};

template <bool Enable>
struct SMF_Move_Constructible
{
	constexpr SMF_Move_Constructible() = default;
	constexpr SMF_Move_Constructible(const SMF_Move_Constructible&) = default;
	constexpr SMF_Move_Constructible(SMF_Move_Constructible&&) = default;
	constexpr SMF_Move_Constructible& operator=(const SMF_Move_Constructible&) = default;
	constexpr SMF_Move_Constructible& operator=(SMF_Move_Constructible&&) = default;
};

template <>
struct SMF_Move_Constructible<false>
{
	constexpr SMF_Move_Constructible() = default;
	constexpr SMF_Move_Constructible(const SMF_Move_Constructible&) = default;
	SMF_Move_Constructible(SMF_Move_Constructible&&) = delete;
	constexpr SMF_Move_Constructible& operator=(const SMF_Move_Constructible&) = default;
	constexpr SMF_Move_Constructible& operator=(SMF_Move_Constructible&&) = default;
};

template <bool Enable>
struct SMF_Move_Assignable
{
	constexpr SMF_Move_Assignable() = default;
	constexpr SMF_Move_Assignable(const SMF_Move_Assignable&) = default;
	constexpr SMF_Move_Assignable(SMF_Move_Assignable&&) = default;
	constexpr SMF_Move_Assignable& operator=(const SMF_Move_Assignable&) = default;
	constexpr SMF_Move_Assignable& operator=(SMF_Move_Assignable&&) = default;
};

template <>
struct SMF_Move_Assignable<false>
{
	constexpr SMF_Move_Assignable() = default;
	constexpr SMF_Move_Assignable(const SMF_Move_Assignable&) = default;
	constexpr SMF_Move_Assignable(SMF_Move_Assignable&&) = default;
	constexpr SMF_Move_Assignable& operator=(const SMF_Move_Assignable&) = default;
	SMF_Move_Assignable& operator=(SMF_Move_Assignable&&) = delete;
};

template <typename Ty>
struct SMF_Controll
	: SMF_Copy_Constructible<std::copy_constructible<Ty>>
	, SMF_Copy_Assignable<std::copy_constructible<Ty> && std::is_copy_assignable_v<Ty>>
	, SMF_Move_Constructible<std::move_constructible<Ty>>
	, SMF_Move_Assignable<std::move_constructible<Ty> && std::is_move_assignable_v<Ty>>
{
	constexpr SMF_Controll() = default;
	constexpr SMF_Controll(const SMF_Controll&) = default;
	constexpr SMF_Controll(SMF_Controll&&) = default;
	constexpr SMF_Controll& operator=(const SMF_Controll&) = default;
	constexpr SMF_Controll& operator=(SMF_Controll&&) = default;
};

template <typename Ty>
class Value_Storage
{
public:
	constexpr Value_Storage()
		requires std::default_initializable<Ty>
		: m_value()
	{
	}

	Value_Storage()
		requires (!std::default_initializable<Ty>) = delete;

	template <typename... Args>
		requires std::constructible_from<Ty, Args...>
	constexpr explicit Value_Storage(std::in_place_t, Args&&... args)
		: m_value(std::forward<Args>(args)...)
	{
	}

	constexpr ~Value_Storage() = default;
	constexpr Value_Storage(const Value_Storage&) = default;
	constexpr Value_Storage(Value_Storage&&) = default;
	constexpr Value_Storage& operator=(const Value_Storage&) = default;
	constexpr Value_Storage& operator=(Value_Storage&&) = default;

	[[nodiscard]] constexpr Ty& Value() noexcept
	{
		return m_value;
	}

	[[nodiscard]] constexpr const Ty& Value() const noexcept
	{
		return m_value;
	}

protected:
	constexpr Ty& Assign(const Ty& value)
	{
		m_value = value;
		return m_value;
	}

	constexpr Ty& Assign(Ty&& value)
	{
		m_value = std::move(value);
		return m_value;
	}

private:
	Ty m_value;
};

template <typename Fn>
class ObserverList
{
private:
    struct Node
    {
        template <typename Value>
        explicit Node(Value&& value)
            : observer(std::forward<Value>(value))
        {
        }

        // 专门用于 dummy 节点的构造函数
        Node() = default;

        Fn observer;
        std::atomic<Node*> next{nullptr};
    };

    // 静态断言保证 Fn 可默认构造，否则 dummy 节点无法创建
    static_assert(std::default_initializable<Fn>,
        "ObserverList requires default-constructible function type for internal dummy node");

public:
    ObserverList()
    {
        Node* dummy = new Node();
        dummy->next.store(nullptr, std::memory_order_relaxed);
        m_head.store(dummy, std::memory_order_relaxed);
        m_tail.store(dummy, std::memory_order_relaxed);
    }

    ~ObserverList()
    {
        Clear();
        // 清除 dummy 节点
        delete m_head.load(std::memory_order_relaxed);
    }

    ObserverList(const ObserverList& other)
    {
        // 先创建 dummy
        Node* dummy = new Node();
        dummy->next.store(nullptr, std::memory_order_relaxed);
        m_head.store(dummy, std::memory_order_relaxed);
        m_tail.store(dummy, std::memory_order_relaxed);

        // 遍历 other 的有效节点（跳过 dummy）
        Node* current = other.m_head.load(std::memory_order_acquire);
        if (current) current = current->next.load(std::memory_order_acquire); // 跳过 dummy

        for (; current != nullptr; current = current->next.load(std::memory_order_acquire))
        {
            PushBack(current->observer);
        }
    }

    ObserverList(ObserverList&& other) noexcept
        : m_head(other.m_head.load(std::memory_order_relaxed))
        , m_tail(other.m_tail.load(std::memory_order_relaxed))
    {
        // 让 other 回到合法空状态（需要重建 dummy）
        Node* dummy = new Node();
        dummy->next.store(nullptr, std::memory_order_relaxed);
        other.m_head.store(dummy, std::memory_order_relaxed);
        other.m_tail.store(dummy, std::memory_order_relaxed);
    }

    ObserverList& operator=(const ObserverList& other)
    {
        if (this == &other)
            return *this;
        ObserverList temp(other);
        Swap(temp);
        return *this;
    }

    ObserverList& operator=(ObserverList&& other) noexcept
    {
        if (this == &other)
            return *this;

        Clear();
        // 先删除当前 dummy
        Node* old_dummy = m_head.load(std::memory_order_relaxed);
        delete old_dummy;

        m_head.store(other.m_head.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_tail.store(other.m_tail.load(std::memory_order_relaxed), std::memory_order_relaxed);

        // 让 other 重建 dummy
        Node* new_dummy = new Node();
        new_dummy->next.store(nullptr, std::memory_order_relaxed);
        other.m_head.store(new_dummy, std::memory_order_relaxed);
        other.m_tail.store(new_dummy, std::memory_order_relaxed);
        return *this;
    }

    void PushBack(const Fn& observer)
    {
        AppendNode(new Node(observer));
    }

    void PushBack(Fn&& observer)
    {
        AppendNode(new Node(std::move(observer)));
    }

    template <typename Callback>
    void ForEach(Callback&& callback) const
    {
        // 跳过 dummy 节点
        Node* current = m_head.load(std::memory_order_acquire);
        if (current) current = current->next.load(std::memory_order_acquire);

        for (; current != nullptr; current = current->next.load(std::memory_order_acquire))
        {
            std::forward<Callback>(callback)(current->observer);
        }
    }

    void Clear() noexcept
    {
        // 保留 dummy，只清除数据节点
        Node* dummy = m_head.load(std::memory_order_relaxed);
        Node* current = dummy ? dummy->next.load(std::memory_order_relaxed) : nullptr;

        while (current != nullptr)
        {
            Node* next = current->next.load(std::memory_order_relaxed);
            delete current;
            current = next;
        }

        if (dummy)
        {
            dummy->next.store(nullptr, std::memory_order_relaxed);
            // 重置 tail 到 dummy
            m_tail.store(dummy, std::memory_order_relaxed);
        }
    }

    void Swap(ObserverList& other) noexcept
    {
        // 简单交换两个原子指针（注意它们都持有 dummy）
        Node* tmp_head = m_head.load(std::memory_order_relaxed);
        Node* tmp_tail = m_tail.load(std::memory_order_relaxed);
        m_head.store(other.m_head.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_tail.store(other.m_tail.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.m_head.store(tmp_head, std::memory_order_relaxed);
        other.m_tail.store(tmp_tail, std::memory_order_relaxed);
    }

private:
    void AppendNode(Node* node) noexcept
    {
        node->next.store(nullptr, std::memory_order_relaxed);

        while (true)
        {
            Node* tail = m_tail.load(std::memory_order_acquire);
            Node* next = tail->next.load(std::memory_order_acquire);

            if (tail == m_tail.load(std::memory_order_acquire))
            {
                if (next == nullptr)
                {
                    if (tail->next.compare_exchange_weak(
                            next, node,
                            std::memory_order_release,
                            std::memory_order_relaxed))
                    {
                        m_tail.compare_exchange_strong(tail, node,
                                                       std::memory_order_release,
                                                       std::memory_order_relaxed);
                        return;
                    }
                }
                else
                {
                    m_tail.compare_exchange_weak(tail, next,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed);
                }
            }
        }
    }

    std::atomic<Node*> m_head{nullptr};
    std::atomic<Node*> m_tail{nullptr};
};

}  // namespace details

template <typename Ty>
class Observed
	: private details::Value_Storage<Ty>
	, private details::SMF_Controll<Ty>
{
public:
	using oberver_type = std::function<void(const Ty&)>;
	using value_type = std::remove_cv_t<Ty>;
	using storage_type = details::Value_Storage<Ty>;
	using observer_storage_type = details::ObserverList<oberver_type>;

	Observed() = default;
	~Observed() = default;
	Observed(const Observed& other)
		requires std::copy_constructible<Ty>
		: storage_type(std::in_place, other.Value())
	{
	}

	Observed(const Observed& other)
		requires (!std::copy_constructible<Ty>) = delete;

	Observed(Observed&& other)
		noexcept(std::is_nothrow_move_constructible_v<Ty>)
		requires std::move_constructible<Ty>
		: storage_type(std::in_place, std::move(other.Value()))
	{
	}

	Observed(Observed&& other)
		requires (!std::move_constructible<Ty>) = delete;

	Observed(const Ty& initial_value) : storage_type(std::in_place, initial_value) 
	{
	}
	Observed(Ty&& initial_value) : storage_type(std::in_place, std::move(initial_value)) 
	{
	}
	template <typename... Args>
		requires std::constructible_from<Ty, Args...>
	Observed(std::in_place_t, Args&&... args)
		: storage_type(std::in_place, std::forward<Args>(args)...)
	{
	}

	Observed& operator=(const Observed& other)
		requires (std::copy_constructible<Ty> && std::is_copy_assignable_v<Ty>)
	{
		if (this == &other)
		{
			return *this;
		}

		this->Assign(other.Value());
		NotifyObservers();
		return *this;
	}

	Observed& operator=(const Observed& other)
		requires (!(std::copy_constructible<Ty> && std::is_copy_assignable_v<Ty>)) = delete;

	Observed& operator=(Observed&& other)
		noexcept(std::is_nothrow_move_assignable_v<Ty>)
		requires (std::move_constructible<Ty> && std::is_move_assignable_v<Ty>)
	{
		if (this == &other)
		{
			return *this;
		}

		this->Assign(std::move(other.Value()));
		NotifyObservers();
		return *this;
	}

	Observed& operator=(Observed&& other)
		requires (!(std::move_constructible<Ty> && std::is_move_assignable_v<Ty>)) = delete;

	Observed& operator=(const Ty& new_value)
	{
		return SetValue(new_value);
	}

	Observed& operator=(Ty&& new_value)
	{
		if constexpr (std::is_move_assignable_v<Ty>)
		{
			return SetValue(std::move(new_value));
		}
		else
		{
			return SetValue(new_value);
		}
	}

	Observed& Subscribe(const oberver_type& observer)
	{
		m_observers.PushBack(observer);
		return *this;
	}

	Observed& Subscribe(oberver_type&& observer)
	{
		m_observers.PushBack(std::move(observer));
		return *this;
	}

	Observed& SetValue(const Ty& new_value)
	{
		this->Assign(new_value);
		if (!std::is_constant_evaluated())
		{
			NotifyObservers();
		}
		return *this;
	}

	Observed& SetValue(Ty&& new_value)
	{
		this->Assign(std::move(new_value));
		if (!std::is_constant_evaluated())
		{
			NotifyObservers();
		}
		return *this;
	}

	template <typename Fn, typename... Args>
		requires std::invocable<Fn, Ty&, Args...>
	Observed& Modify(Fn&& modify, Args&&... args)
		noexcept(std::is_nothrow_invocable_v<Fn, Ty&, Args...>)
	{
		std::invoke(
			std::forward<Fn>(modify),
			this->Value(),
			std::forward<Args>(args)...
		);
		if (!std::is_constant_evaluated())
		{
			NotifyObservers();
		}
		return *this;
	}

	template <typename Fn, typename... Args>
		requires std::invocable<Fn, Ty&, Args...>
	static void Modify(Fn&& modify, Observed& observable, Args&&... args)
		noexcept(std::is_nothrow_invocable_v<Fn, Ty&, Args...>)
	{
		std::invoke(
			std::forward<Fn>(modify),
			observable.Value(),
			std::forward<Args>(args)...
		);
		if (!std::is_constant_evaluated())
		{
			observable.NotifyObservers();
		}
	}

	[[nodiscard]] const Ty& GetValue() const
	{
		return this->Value();
	}

	[[nodiscard]] bool HasValue() const noexcept
	{
		return storage_type::HasValue();
	}

private:
	void NotifyObservers()
	{
		m_observers.ForEach([this](const oberver_type& observer) {
			observer(this->Value());
		});
	}

	observer_storage_type m_observers;
};


}  // namespace ptd