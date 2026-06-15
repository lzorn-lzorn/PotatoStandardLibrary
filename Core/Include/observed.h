
#pragma once

#include <functional>
#include <type_traits>
#include <utility>
#include <atomic>
#include <concepts>
namespace core::details
{
// 通用 SMF 标签基类：仅存编译期布尔常量，不干涉任何特殊成员
template <bool CopyCtor, bool CopyAssign, bool MoveCtor, bool MoveAssign>
struct SMF_Control_Tag
{
    static constexpr bool enable_copy_ctor    = CopyCtor;
    static constexpr bool enable_copy_assign  = CopyAssign;
    static constexpr bool enable_move_ctor    = MoveCtor;
    static constexpr bool enable_move_assign  = MoveAssign;

    // 全部使用平凡默认成员，不删除、不自定义，避免干扰类型特征
    SMF_Control_Tag() = default;
    ~SMF_Control_Tag() = default;
    SMF_Control_Tag(const SMF_Control_Tag&) = default;
    SMF_Control_Tag(SMF_Control_Tag&&) = default;
    SMF_Control_Tag& operator=(const SMF_Control_Tag&) = default;
    SMF_Control_Tag& operator=(SMF_Control_Tag&&) = default;
};

// 自动根据 Ty 推导四组开关：Ty 的属性决定 SMF 状态
template <typename Ty>
using SMF_AutoControl = SMF_Control_Tag<
    std::copy_constructible<Ty>,
    std::is_copy_assignable_v<Ty>,
    std::move_constructible<Ty>,
    std::is_move_assignable_v<Ty>
>;

// 值存储层：独立，仅负责持有 Ty
template <typename Ty>
class Value_Storage
{
public:
    // 默认构造：仅 Ty 可默认构造时可用
    constexpr Value_Storage()
        requires std::default_initializable<Ty>
        : m_value()
    {}

    // 原位构造
    template <typename... Args>
        requires std::constructible_from<Ty, Args...>
    constexpr explicit Value_Storage(std::in_place_t, Args&&... args)
        : m_value(std::forward<Args>(args)...)
    {}

    // 平凡特殊成员
    constexpr ~Value_Storage() = default;
    constexpr Value_Storage(const Value_Storage&) = default;
    constexpr Value_Storage(Value_Storage&&) = default;
    constexpr Value_Storage& operator=(const Value_Storage&) = default;
    constexpr Value_Storage& operator=(Value_Storage&&) = default;

    [[nodiscard]] constexpr Ty& Value() noexcept { return m_value; }
    [[nodiscard]] constexpr const Ty& Value() const noexcept { return m_value; }
    [[nodiscard]] constexpr bool has_value() const noexcept { return true; }

protected:
    constexpr Ty& Assign(const Ty& val)
    {
        m_value = val;
        return m_value;
    }
    constexpr Ty& Assign(Ty&& val)
	{
		if constexpr (std::is_move_assignable_v<Ty>)
			m_value = std::move(val);
		else
			m_value = val;   // 回退到拷贝赋值
		return m_value;
	}

private:
    Ty m_value;
};

template <typename Fn>
class Observer_List
{
private:
    struct Node
    {
        template <typename F>
        explicit Node(F&& f) : func(std::forward<F>(f)) {}
        Node() = default;

        Fn func;
        std::atomic<Node*> next{nullptr};
    };

    static_assert(std::default_initializable<Fn>,
        "Observer_List: Fn must be default constructible");

public:
    Observer_List()
    {
        Node* dummy = new Node{};
        m_head.store(dummy, std::memory_order_relaxed);
        m_tail.store(dummy, std::memory_order_relaxed);
    }

    ~Observer_List()
    {
        Clear();
        delete m_head.load(std::memory_order_relaxed);
    }

    Observer_List(const Observer_List& other)
    {
        Node* dummy = new Node{};
        m_head.store(dummy, std::memory_order_relaxed);
        m_tail.store(dummy, std::memory_order_relaxed);

        Node* cur = other.m_head.load(std::memory_order_acquire)->next.load(std::memory_order_acquire);
        while (cur)
        {
            PushBack(cur->func);
            cur = cur->next.load(std::memory_order_acquire);
        }
    }

    Observer_List(Observer_List&& other) noexcept
        : m_head(other.m_head.load(std::memory_order_relaxed))
        , m_tail(other.m_tail.load(std::memory_order_relaxed))
    {
        Node* dummy = new Node{};
        other.m_head.store(dummy, std::memory_order_relaxed);
        other.m_tail.store(dummy, std::memory_order_relaxed);
    }

    Observer_List& operator=(const Observer_List& other)
    {
        if (this == &other) return *this;
        Observer_List temp(other);
        Swap(temp);
        return *this;
    }

    Observer_List& operator=(Observer_List&& other) noexcept
    {
        if (this == &other) return *this;
        Clear();
        delete m_head.load(std::memory_order_relaxed);

        m_head.store(other.m_head.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_tail.store(other.m_tail.load(std::memory_order_relaxed), std::memory_order_relaxed);

        Node* dummy = new Node{};
        other.m_head.store(dummy, std::memory_order_relaxed);
        other.m_tail.store(dummy, std::memory_order_relaxed);
        return *this;
    }

    void PushBack(const Fn& f)
    {
        AppendNode(new Node(f));
    }
    void PushBack(Fn&& f)
    {
        AppendNode(new Node(std::move(f)));
    }

    template <typename Callback>
    void ForEach(Callback&& cb) const
    {
        Node* cur = m_head.load(std::memory_order_acquire)->next.load(std::memory_order_acquire);
        while (cur)
        {
            std::forward<Callback>(cb)(cur->func);
            cur = cur->next.load(std::memory_order_acquire);
        }
    }

    void Clear() noexcept
    {
        Node* dummy = m_head.load(std::memory_order_relaxed);
        Node* cur = dummy->next.load(std::memory_order_relaxed);
        while (cur)
        {
            Node* next = cur->next.load(std::memory_order_relaxed);
            delete cur;
            cur = next;
        }
        dummy->next.store(nullptr, std::memory_order_relaxed);
        m_tail.store(dummy, std::memory_order_relaxed);
    }

    void Swap(Observer_List& other) noexcept
	{
		// 不能使用 std::swap(m_head, other.m_head) —— std::atomic 不可移动
		Node* my_head = m_head.load(std::memory_order_relaxed);
		Node* my_tail = m_tail.load(std::memory_order_relaxed);

		m_head.store(other.m_head.load(std::memory_order_relaxed), std::memory_order_relaxed);
		m_tail.store(other.m_tail.load(std::memory_order_relaxed), std::memory_order_relaxed);

		other.m_head.store(my_head, std::memory_order_relaxed);
		other.m_tail.store(my_tail, std::memory_order_relaxed);
	}

private:
    void AppendNode(Node* node) noexcept
    {
        node->next = nullptr;
        while (true)
        {
            Node* tail = m_tail.load(std::memory_order_acquire);
            Node* next = tail->next.load(std::memory_order_acquire);
            if (tail == m_tail.load(std::memory_order_acquire))
            {
                if (!next)
                {
                    if (tail->next.compare_exchange_weak(next, node, std::memory_order_release))
                    {
                        m_tail.compare_exchange_strong(tail, node, std::memory_order_release);
                        return;
                    }
                }
                else
                {
                    m_tail.compare_exchange_weak(tail, next, std::memory_order_release);
                }
            }
        }
    }

    std::atomic<Node*> m_head{nullptr};
    std::atomic<Node*> m_tail{nullptr};
};;

} // namespace core::details
namespace core
{
template <typename Ty>
class observed
    : private details::Value_Storage<Ty>
    , private details::SMF_AutoControl<Ty>
{
public:
    // 1. 类型别名（前置定义，解决依赖报错）
    using value_type        = std::remove_cv_t<Ty>;
    using smf_ctrl          = details::SMF_AutoControl<Ty>;
    using observer_type     = std::function<void(const value_type&)>;
    using observer_list     = details::Observer_List<observer_type>;
    using storage_type      = details::Value_Storage<Ty>;

    // ===================== 构造函数 =====================
	constexpr observed() requires std::default_initializable<Ty> : storage_type() {}

	// 拷贝构造
	constexpr observed(const observed& other)
		requires smf_ctrl::enable_copy_ctor
		: storage_type(other), m_observers() {}

	observed(const observed&) requires (!smf_ctrl::enable_copy_ctor) = delete;

	// 移动构造
	constexpr observed(observed&& other) noexcept
		requires smf_ctrl::enable_move_ctor
		: storage_type(std::move(other)), m_observers() {}

	observed(observed&&) requires (!smf_ctrl::enable_move_ctor) = delete;

	// 拷贝赋值
	constexpr observed& operator=(const observed& other)
		requires (smf_ctrl::enable_copy_ctor && smf_ctrl::enable_copy_assign)
	{
		if (this != &other) {
			this->Assign(other.Value());
			notify();
		}
		return *this;
	}

	observed& operator=(const observed&) 
		requires (!(smf_ctrl::enable_copy_ctor && smf_ctrl::enable_copy_assign)) = delete;

	// 移动赋值
	constexpr observed& operator=(observed&& other) noexcept
		requires (smf_ctrl::enable_move_ctor && smf_ctrl::enable_move_assign)
	{
		if (this != &other) {
			this->Assign(std::move(other.Value()));
			notify();
		}
		return *this;
	}

	observed& operator=(observed&&) 
		requires (!(smf_ctrl::enable_move_ctor && smf_ctrl::enable_move_assign)) = delete;

    // 直接用 Ty 构造
    observed(const Ty& val)
        : storage_type(std::in_place, val)
    {}
    observed(Ty&& val)
        : storage_type(std::in_place, std::move(val))
    {}

    // 原位构造
    template <typename... Args>
        requires std::constructible_from<Ty, Args...>
    observed(std::in_place_t, Args&&... args)
        : storage_type(std::in_place, std::forward<Args>(args)...)
    {}

    // ===================== 赋值重载 =====================
    observed& operator=(const Ty& val)
    {
        set_value(val);
        return *this;
    }
    observed& operator=(Ty&& val)
    {
        set_value(std::move(val));
        return *this;
    }

    // ===================== 订阅接口（修复签名） =====================
    observed& subscribe(const observer_type& obs)
    {
        m_observers.PushBack(obs);
        return *this;
    }
    observed& subscribe(observer_type&& obs)
    {
        m_observers.PushBack(std::move(obs));
        return *this;
    }

    // ===================== 值修改 & 通知 =====================
    observed& set_value(const Ty& val)
    {
        this->Assign(val);
        notify();
        return *this;
    }
    observed& set_value(Ty&& val)
    {
        this->Assign(std::move(val));
        notify();
        return *this;
    }

  	template <typename Fn, typename... Args>
		requires std::invocable<Fn, Ty&, Args...>
    observed& modify(Fn&& fn, Args&&... args)
        noexcept(std::is_nothrow_invocable_v<Fn, Ty&, Args...>)
    {
        std::invoke(std::forward<Fn>(fn), this->Value(), std::forward<Args>(args)...);
        notify();
        return *this;
    }

    // ===================== 取值接口 =====================
    [[nodiscard]] const Ty& value() const noexcept
    {
        return this->Value();
    }
    [[nodiscard]] bool has_value() const noexcept
    {
        return storage_type::has_value();
    }

private:
    // 通知所有观察者
    void notify() const
    {
        m_observers.ForEach([this](const observer_type& obs)
        {
            obs(this->value());
        });
    }

    observer_list m_observers;
};

} // namespace core