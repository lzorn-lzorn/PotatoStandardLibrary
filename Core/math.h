
#pragma once

#include <cstddef>
#include <cmath>
#include <array>
#include <limits>
#include <numbers>
#include <concepts>
#include <algorithm>
#include <execution>
#include <numeric>
#include <iostream>
#include <sstream>
#include <random>
#include <string>
#include <string_view>
#include <iomanip>
#include <type_traits>
#include <charconv>

#if defined(__SSE4_1__)
#include <smmintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif


namespace core::math
{

template <typename Number>
concept arithmetic = std::integral<Number> || std::floating_point<Number>;



[[maybe_unused]] static constexpr float epsilon     = std::numeric_limits<float>::epsilon();
[[maybe_unused]] static constexpr float tiny        = 1e-5f;
[[maybe_unused]] static constexpr float infinity    = std::numeric_limits<float>::infinity();
[[maybe_unused]] static constexpr float nan         = std::numeric_limits<float>::quiet_NaN();
[[maybe_unused]] static constexpr float pi          = std::numbers::pi_v<float>;
[[maybe_unused]] static constexpr float sqrt_pi     = 1.77245385090551602729816748f;  // sqrt(pi)
[[maybe_unused]] static constexpr float inv_sqrt_pi = std::numbers::inv_sqrtpi_v<float>;
[[maybe_unused]] static constexpr float inv_pi      = std::numbers::inv_pi_v<float>;
[[maybe_unused]] static constexpr float sqrt2       = std::numbers::sqrt2_v<float>;  // sqrt(2)
[[maybe_unused]] static constexpr float inv_sqrt2   = 1 / sqrt2;  
[[maybe_unused]] static constexpr float sqrt3       = std::numbers::sqrt3_v<float>;  // sqrt(3)
[[maybe_unused]] static constexpr float inv_sqrt3   = 1 / sqrt3;  
[[maybe_unused]] static constexpr float deg2rad     = pi / 180.0f;
[[maybe_unused]] static constexpr float rad2deg     = 180.0f / pi;

}


/**
 * @brief vec start ============================================================
 */
namespace core::math
{
template <arithmetic Ty, std::size_t Dimensions>
struct vec;

template <typename Vec>
struct vector_traits;

template <arithmetic Ty, std::size_t N>
struct vector_traits<vec<Ty, N>>
{
	using value_type = Ty;
	static constexpr std::size_t dimensions = N;
};

namespace details
{

/**
 * @brief 使用 CRTP 实现的通用向量类模板基类
 * @note 如果需要特化 例如 整型 的 Ty, 则在 对应函数中, 增加  if constexpr(std::is_integral_v<Ty>) 之类的分支处理
 * @note 如果是新增函数, 可以专门使用
 *  template <typename U = Ty>
 *       requires std::is_integral_v<U>
 *  [[nodiscard]] std::size_t NewFunctionForIntegral() const noexcept { ... }
 */
template <class Derived>
struct TVector 
{
	using traits		  = vector_traits<Derived>;
    using value_type      = typename traits::value_type;
    using reference       = value_type&;
    using const_reference = const value_type&;

    static constexpr std::size_t dimensions = traits::dimensions;

    std::array<value_type, dimensions> coordinates{};

private:
    static consteval std::array<std::size_t, dimensions> MakeIndices() noexcept
    {
        std::array<std::size_t, dimensions> values{};
        for (std::size_t index = 0; index < dimensions; ++index) {
            values[index] = index;
        }
        return values;
    }

    template <typename Func>
    static constexpr void ForEachIndex(Func&& func)
    {
        if (std::is_constant_evaluated()) {
            for (std::size_t index = 0; index < dimensions; ++index) {
                func(index);
            }
            return;
        }

        std::for_each(std::execution::par_unseq, indices.begin(), indices.end(),
                      [&func](std::size_t index) {
                          func(index);
                      });
    }

    template <typename Result, typename Reduce, typename Transform>
    static constexpr Result TransformReduceIndices(Result init, Reduce reduce, Transform&& transform)
    {
        if (std::is_constant_evaluated()) {
            for (std::size_t index = 0; index < dimensions; ++index) {
                init = reduce(init, transform(index));
            }
            return init;
        }

        return std::transform_reduce(std::execution::par_unseq,
                                     indices.begin(), indices.end(),
                                     init,
                                     reduce,
                                     [&transform](std::size_t index) {
                                         return transform(index);
                                     });
    }

    inline static constexpr auto indices = MakeIndices();

public:

    constexpr Derived&       derived()       noexcept { return static_cast<Derived&>(*this); }
    constexpr const Derived& derived() const noexcept { return static_cast<const Derived&>(*this); }

    constexpr TVector() = default;
    explicit constexpr TVector(value_type value) 
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] = value;
        });
    }

    template <typename U>
        requires std::convertible_to<U, value_type>
    constexpr TVector(const std::array<U, dimensions>& values)
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] = static_cast<value_type>(values[index]);
        });
    }

    template <typename U>
        requires std::convertible_to<U, value_type>
    constexpr TVector(const U (&values)[dimensions])
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] = static_cast<value_type>(values[index]);
        });
    }

    template <typename... Args>
        requires (sizeof...(Args) == dimensions) &&
                 (std::conjunction_v<std::is_convertible<Args, value_type>...>)
    constexpr TVector(Args&&... args) 
    {
        std::size_t i = 0;
        ((coordinates[i++] = static_cast<value_type>(std::forward<Args>(args))), ...);
    }

    constexpr const_reference operator[](std::size_t index) const noexcept { return coordinates[index]; }
    constexpr reference operator[](std::size_t index) noexcept { return coordinates[index]; }

    /*
     * @note: 
     * 以下函数是有二义性问题的: 反向重写候选(reversed rewritten candidate)
     * C++20 允许编译器在找不到精确匹配的 operator== 时, 尝试将 a == b 重写为 b == a(即参数顺序反转). 
     * 定义了一个成员 operator==(const Derived& other) 后, 编译器会同时生成一个对应的非成员候选 operator==(const Derived&, const Derived&), 这个候选被视为"反向调用". 
     
     constexpr bool operator==(const Derived& other) const noexcept 
     { 
        return coordinates == other.coordinates; 
     }
    
     * 此时当 operator== 的两个参数类型完全相同 (都是 Derived const&) 时, 
     *  1. 常规调用候选: 成员函数 operator==(const Derived& other), 第一个参数是隐式的 this(类型 const Derived&), 第二个参数是 other
     *  2. 反向调用候选: 编译器生成的非成员函数 operator==(const Derived&, const Derived&), 两个参数都是 const Derived&
     * 因此, 当执行 v1 == v2 时, 编译器发现两个完全等价的候选函数, 无法选择, 从而报歧义错误
     */
    friend constexpr bool operator==(const Derived& lhs, const Derived& rhs) noexcept 
    { 
        return lhs.coordinates == rhs.coordinates; 
    }

    constexpr Derived& operator+=(const Derived& other) noexcept 
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] += other.coordinates[index];
        });
        return derived();
    }

    constexpr Derived& operator-=(const Derived& other) noexcept 
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] -= other.coordinates[index];
        });
        return derived();
    }

    constexpr Derived& operator*=(const Derived& other) noexcept 
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] *= other.coordinates[index];
        });
        return derived();
    }

    constexpr Derived& operator/=(const Derived& other) noexcept 
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] /= other.coordinates[index];
        });
        return derived();
    }

    constexpr Derived& operator+=(value_type other) noexcept 
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] += other;
        });
        return derived();
    }

    constexpr Derived& operator-=(value_type other) noexcept 
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] -= other;
        });
        return derived();
    }

    constexpr Derived& operator*=(value_type other) noexcept 
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] *= other;
        });
        return derived();
    }

    constexpr Derived& operator/=(value_type other) noexcept 
    {
        ForEachIndex([&](std::size_t index) {
            coordinates[index] /= other;
        });
        return derived();
    }

    constexpr Derived operator-() const noexcept 
    {
        Derived result;
        ForEachIndex([&](std::size_t index) {
            result.coordinates[index] = -coordinates[index];
        });
        return result;
    }

    friend constexpr Derived operator+(Derived lhs, const Derived& rhs) noexcept { return lhs += rhs, lhs; }
    friend constexpr Derived operator-(Derived lhs, const Derived& rhs) noexcept { return lhs -= rhs, lhs; }
    friend constexpr Derived operator*(Derived lhs, const Derived& rhs) noexcept { return lhs *= rhs, lhs; }
    friend constexpr Derived operator/(Derived lhs, const Derived& rhs) noexcept { return lhs /= rhs, lhs; }
    friend constexpr Derived operator+(Derived lhs, value_type rhs) noexcept { return lhs += rhs, lhs; }
    friend constexpr Derived operator+(value_type lhs, Derived rhs) noexcept { return rhs += lhs, rhs; }
    friend constexpr Derived operator*(Derived lhs, value_type rhs) noexcept { return lhs *= rhs, lhs; }
    friend constexpr Derived operator*(value_type lhs, Derived rhs) noexcept { return rhs *= lhs, rhs; }
    friend constexpr Derived operator-(Derived lhs, value_type rhs) noexcept { return lhs -= rhs, lhs; }
    friend constexpr Derived operator/(Derived lhs, value_type rhs) noexcept { return lhs /= rhs, lhs; }
    friend constexpr Derived operator-(value_type lhs, Derived rhs) noexcept 
    {
        ForEachIndex([&](std::size_t index) {
            rhs.coordinates[index] = lhs - rhs.coordinates[index];
        });
        return rhs;
    }

    [[nodiscard]] constexpr float square() const noexcept 
    {
        return TransformReduceIndices(0.0f, std::plus<>{}, [&](std::size_t index) {
            return static_cast<float>(coordinates[index] * coordinates[index]);
        });
    }

    [[nodiscard]] constexpr float dot(const Derived& other) const noexcept 
    {
        return TransformReduceIndices(0.0f, std::plus<>{}, [&](std::size_t index) {
            return static_cast<float>(coordinates[index] * other.coordinates[index]);
        });
    }

    [[nodiscard]] float length() const noexcept {
        return static_cast<float>(std::sqrt(static_cast<long double>(square())));
    }
    
    [[nodiscard]] Derived normalized() const
        requires std::is_floating_point_v<value_type>
    {
        value_type len = length();
        if (len == value_type(0)) {
            return Derived{};
        }
        return static_cast<const Derived&>(*this) / len;
    }

    constexpr Derived& normalize() 
		requires std::is_floating_point_v<value_type> 
	{
        value_type len = length();
        if (len != value_type(0)) {
            *this /= len;
        }
        return derived();
    }

	static constexpr Derived normalize(const Derived& v, const Derived& fallback = Derived{})
		requires std::is_floating_point_v<typename traits::value_type>
	{
		auto len = v.length();
		if (len == value_type(0)){
			return fallback;
		}
		return v / static_cast<value_type>(len);
	}

	static constexpr Derived lerp(const Derived& a, const Derived& b, value_type t) noexcept {
		return a + (b - a) * t;
	}

	static constexpr Derived clamp_len(const Derived& v, value_type max_len)
		requires std::is_floating_point_v<value_type>
	{
		auto lenSq = v.square();
		if (lenSq > max_len * max_len)
			return v * (max_len / std::sqrt(lenSq));
		return v;
	}

	constexpr auto as_tuple() const noexcept
	{
		return std::apply([](auto&&... args) {
			return std::make_tuple(args...);
		}, coordinates);
	}
};


} // namespace core::math::details end

template <arithmetic Ty, std::size_t Dimensions>
struct vec : details::TVector<vec<Ty, Dimensions>> {
    using base            = details::TVector<vec<Ty, Dimensions>>;
	using value_type      = Ty;
	using reference       = typename base::reference;
    using const_reference = typename base::const_reference;
    using base::base;

	constexpr static std::size_t dimensions = Dimensions;

	constexpr reference x() noexcept { return this->coordinates[0]; }
	[[nodiscard]] constexpr const_reference x() const noexcept { return this->coordinates[0]; }

    static constexpr vec fill(Ty value) noexcept { return vec{value}; }
    static constexpr vec zero() noexcept { return vec{Ty{0}}; }
    static constexpr vec one() noexcept { return vec{Ty{1}}; }
    static constexpr vec min() noexcept { return vec{std::numeric_limits<Ty>::lowest()}; }
    static constexpr vec max() noexcept { return vec{std::numeric_limits<Ty>::max()}; }

    static constexpr vec axis(std::size_t axis_value, Ty magnitude = Ty{1}) noexcept
    {
        vec result{};
        if (axis_value < dimensions) {
            result[axis_value] = magnitude;
        }
        return result;
    }
};

template <arithmetic First, arithmetic... Rest>
vec(First, Rest...) -> vec<std::common_type_t<First, Rest...>, 1 + sizeof...(Rest)>;

template <arithmetic Ty, std::size_t N>
vec(const std::array<Ty, N>&) -> vec<Ty, N>;

template <arithmetic Ty, std::size_t N>
vec(const Ty (&)[N]) -> vec<Ty, N>;

template <arithmetic Ty>
struct vec<Ty, 2> : details::TVector<vec<Ty, 2>> 
{
    using base = details::TVector<vec<Ty, 2>>;
    using value_type      = Ty;
    using reference       = typename base::reference;
    using const_reference = typename base::const_reference;

    using base::base;

	constexpr static std::size_t dimensions = 2;

    constexpr reference x() noexcept { return this->coordinates[0]; }
    constexpr reference y() noexcept { return this->coordinates[1]; }
    [[nodiscard]] constexpr const_reference x() const noexcept { return this->coordinates[0]; }
    [[nodiscard]] constexpr const_reference y() const noexcept { return this->coordinates[1]; }

    static constexpr vec fill(Ty value) noexcept { return vec{value, value}; }
    static constexpr vec zero() noexcept { return fill(Ty{0}); }
    static constexpr vec one()  noexcept { return fill(Ty{1}); }
    static constexpr vec min() noexcept { return fill(std::numeric_limits<Ty>::lowest()); }
    static constexpr vec max() noexcept { return fill(std::numeric_limits<Ty>::max()); }
    static constexpr vec axis(std::size_t axis_value, Ty magnitude = Ty{1}) noexcept
    {
        vec result{};
        if (axis_value < dimensions	) {
            result[axis_value] = magnitude;
        }
        return result;
    }
    static constexpr vec x_axis() noexcept { return vec{Ty{1}, Ty{0}}; }
    static constexpr vec y_axis() noexcept { return vec{Ty{0}, Ty{1}}; }

	[[nodiscard]] static constexpr Ty cross(const vec& a, const vec& b) noexcept {
        return a.x() * b.y() - a.y() * b.x();
    }
};

template <arithmetic Ty>
struct vec<Ty, 3> : details::TVector<vec<Ty, 3>> {
    using base = details::TVector<vec<Ty, 3>>;
    using value_type      = Ty;
    using reference       = typename base::reference;
    using const_reference = typename base::const_reference;

    using base::base;

	constexpr static std::size_t dimensions = 3;

    constexpr reference x() noexcept { return this->coordinates[0]; }
    constexpr reference y() noexcept { return this->coordinates[1]; }
    constexpr reference z() noexcept { return this->coordinates[2]; }
    [[nodiscard]] constexpr const_reference x() const noexcept { return this->coordinates[0]; }
    [[nodiscard]] constexpr const_reference y() const noexcept { return this->coordinates[1]; }
    [[nodiscard]] constexpr const_reference z() const noexcept { return this->coordinates[2]; }

    static constexpr vec fill(Ty value) noexcept { return vec{value, value, value}; }
    static constexpr vec zero() noexcept { return fill(Ty{0}); }
    static constexpr vec one() noexcept { return fill(Ty{1}); }
    static constexpr vec min() noexcept { return fill(std::numeric_limits<Ty>::lowest()); }
    static constexpr vec max() noexcept { return fill(std::numeric_limits<Ty>::max()); }
    static constexpr vec axis(std::size_t axis_value, Ty magnitude = Ty{1}) noexcept
    {
        vec result{};
        if (axis_value < dimensions) {
            result[axis_value] = magnitude;
        }
        return result;
    }
    static constexpr vec x_axis() noexcept { return vec{Ty{1}, Ty{0}, Ty{0}}; }
    static constexpr vec y_axis() noexcept { return vec{Ty{0}, Ty{1}, Ty{0}}; }
    static constexpr vec z_axis() noexcept { return vec{Ty{0}, Ty{0}, Ty{1}}; }
    static constexpr vec up() noexcept { return vec{Ty{0}, Ty{0}, Ty{1}}; }
    static constexpr vec down() noexcept { return vec{Ty{0}, Ty{0}, Ty{-1}}; }
    static constexpr vec forward() noexcept { return vec{Ty{1}, Ty{0}, Ty{0}}; }
    static constexpr vec backward() noexcept { return vec{Ty{-1}, Ty{0}, Ty{0}}; }
    static constexpr vec left() noexcept { return vec{Ty{0}, Ty{1}, Ty{0}}; }
    static constexpr vec right() noexcept { return vec{Ty{0}, Ty{-1}, Ty{0}}; }

	[[nodiscard]] static constexpr vec cross(const vec& a, const vec& b) noexcept {
        return vec{
            a.y() * b.z() - a.z() * b.y(),
            a.z() * b.x() - a.x() * b.z(),
            a.x() * b.y() - a.y() * b.x()
        };
    }
};

template <arithmetic Ty>
struct vec<Ty, 4> : details::TVector<vec<Ty, 4>> {
    using base = details::TVector<vec<Ty, 4>>;
    using value_type      = Ty;
    using reference       = typename base::reference;
    using const_reference = typename base::const_reference;

    using base::base;

	constexpr static std::size_t dimensions = 4;

    constexpr reference x() noexcept { return this->coordinates[0]; }
    constexpr reference y() noexcept { return this->coordinates[1]; }
    constexpr reference z() noexcept { return this->coordinates[2]; }
    constexpr reference w() noexcept { return this->coordinates[3]; }
    [[nodiscard]] constexpr const_reference x() const noexcept { return this->coordinates[0]; }
    [[nodiscard]] constexpr const_reference y() const noexcept { return this->coordinates[1]; }
    [[nodiscard]] constexpr const_reference z() const noexcept { return this->coordinates[2]; }
    [[nodiscard]] constexpr const_reference w() const noexcept { return this->coordinates[3]; }

    static constexpr vec fill(Ty value) noexcept { return vec{value, value, value, value}; }
    static constexpr vec zero() noexcept { return fill(Ty{0}); }
    static constexpr vec one() noexcept { return fill(Ty{1}); }
    static constexpr vec min() noexcept { return fill(std::numeric_limits<Ty>::lowest()); }
    static constexpr vec max() noexcept { return fill(std::numeric_limits<Ty>::max()); }
    static constexpr vec axis(std::size_t axis_value, Ty magnitude = Ty{1}) noexcept
    {
        vec result{};
        if (axis_value < dimensions) {
            result[axis_value] = magnitude;
        }
        return result;
    }
    static constexpr vec x_axis() noexcept { return vec{Ty{1}, Ty{0}, Ty{0}, Ty{0}}; }
    static constexpr vec y_axis() noexcept { return vec{Ty{0}, Ty{1}, Ty{0}, Ty{0}}; }
    static constexpr vec z_axis() noexcept { return vec{Ty{0}, Ty{0}, Ty{1}, Ty{0}}; }
    static constexpr vec w_axis() noexcept { return vec{Ty{0}, Ty{0}, Ty{0}, Ty{1}}; }
};


template <arithmetic Ty> using vec1d = vec<Ty, 1>;
template <arithmetic Ty> using vec2d = vec<Ty, 2>;
template <arithmetic Ty> using vec3d = vec<Ty, 3>;
template <arithmetic Ty> using vec4d = vec<Ty, 4>;

using vec1f = vec<float, 1>;
using vec2f = vec<float, 2>;
using vec3f = vec<float, 3>;
using vec4f = vec<float, 4>;

using vec1i = vec<int32_t, 1>;
using vec2i = vec<int32_t, 2>;
using vec3i = vec<int32_t, 3>;
using vec4i = vec<int32_t, 4>;

template <arithmetic Ty, std::size_t N>
inline vec<Ty, N> operator*(const Ty s, const vec<Ty, N>& v) noexcept 
{
    vec<Ty, N> result(v);
    result *= s;
    return result;
}

template <arithmetic Ty, std::size_t N>
inline vec<Ty, N> operator/(const vec<Ty, N>& v, const Ty s) noexcept 
{
    vec<Ty, N> result(v);
    result /= s;
    return result;
}

} // namespace core::math vec end ==============================================

namespace core::math
{

struct color3d;
struct color4d;
class linear_color3d;
class linear_color4d;

class linear_color3d 
{
public:
    using vector_type = vec3f;
    using value_type = float;

    // ---------- 构造与转换 ----------
    constexpr linear_color3d() noexcept : m_data{0.0f, 0.0f, 0.0f} {}
    constexpr linear_color3d(float r, float g, float b) noexcept : m_data{r, g, b} {}
    explicit constexpr linear_color3d(float gray) noexcept : m_data{gray, gray, gray} {}
    explicit constexpr linear_color3d(const vector_type& v) noexcept : m_data(v) {}
    explicit constexpr linear_color3d(const linear_color4d& c) noexcept;

    [[nodiscard]] constexpr linear_color4d with_alpha(float alpha = 1.0f) const noexcept;
    // 允许从其他算术类型的 TVector 转换
    template <typename U>
        requires std::is_arithmetic_v<U>
    explicit constexpr linear_color3d(const vec3d<U>& v) noexcept 
        : m_data{static_cast<float>(v.x()), 
                static_cast<float>(v.y()), 
                static_cast<float>(v.z())} {}

    // ---------- 分量访问 ----------
    [[nodiscard]] constexpr float r() const noexcept { return m_data.x(); }
    [[nodiscard]] constexpr float g() const noexcept { return m_data.y(); }
    [[nodiscard]] constexpr float b() const noexcept { return m_data.z(); }
    constexpr void set_r(float r) noexcept { m_data.x() = r; }
    constexpr void set_g(float g) noexcept { m_data.y() = g; }
    constexpr void set_b(float b) noexcept { m_data.z() = b; }

    [[nodiscard]] constexpr float  operator[](std::size_t i) const noexcept { return m_data[i]; }
    constexpr float& operator[](std::size_t i) noexcept { return m_data[i]; }

    // 获取底层向量（用于需要向量操作的特殊场景）
    [[nodiscard]] constexpr const vector_type& as_vector() const noexcept { return m_data; }

    // ---------- 算术运算符（转发给 Vector） ----------
    constexpr linear_color3d& operator+=(const linear_color3d& rhs) noexcept 
	{
        m_data += rhs.m_data;
        return *this;
    }
    constexpr linear_color3d& operator-=(const linear_color3d& rhs) noexcept 
	{
        m_data -= rhs.m_data;
        return *this;
    }
    constexpr linear_color3d& operator*=(const linear_color3d& rhs) noexcept 
	{
        m_data *= rhs.m_data;
        return *this;
    }
    constexpr linear_color3d& operator/=(const linear_color3d& rhs) noexcept 
	{
        m_data /= rhs.m_data;
        return *this;
    }
    constexpr linear_color3d& operator*=(float s) noexcept 
	{
        m_data *= s;
        return *this;
    }
    constexpr linear_color3d& operator/=(float s) noexcept 
	{
        m_data /= s;
        return *this;
    }

    constexpr linear_color3d operator-() const noexcept 
	{
        return linear_color3d(-m_data);
    }

    // 二元运算符（使用 friend 以便隐式转换标量）
    friend constexpr linear_color3d operator+(linear_color3d lhs, const linear_color3d& rhs) noexcept 
	{
        return lhs += rhs;
    }
    friend constexpr linear_color3d operator-(linear_color3d lhs, const linear_color3d& rhs) noexcept 
	{
        return lhs -= rhs;
    }
    friend constexpr linear_color3d operator*(linear_color3d lhs, const linear_color3d& rhs) noexcept 
	{
        return lhs *= rhs;
    }
    friend constexpr linear_color3d operator/(linear_color3d lhs, const linear_color3d& rhs) noexcept 
	{
        return lhs /= rhs;
    }
    friend constexpr linear_color3d operator*(linear_color3d lhs, float rhs) noexcept 
	{
        return lhs *= rhs;
    }
    friend constexpr linear_color3d operator*(float lhs, linear_color3d rhs) noexcept 
	{
        return rhs *= lhs;
    }
    friend constexpr linear_color3d operator/(linear_color3d lhs, float rhs) noexcept 
	{
        return lhs /= rhs;
    }

    friend constexpr bool operator==(const linear_color3d& lhs, const linear_color3d& rhs) noexcept 
	{
        return lhs.m_data == rhs.m_data;
    }

    // ---------- 颜色专属功能 ----------

	/**
     * @brief 从 RGBE 格式解码（Radiance HDR 格式）
     * @param rgbe 4 字节 RGBE 数据（R, g, b, Exponent）
     * @return 线性 HDR 颜色
     */
    static constexpr linear_color3d from_rgbe(const uint8_t rgbe[4]) noexcept 
	{
        if (rgbe[3] == 0) {
            return linear_color3d::zero();
        }
        const float scale = std::ldexp(1.0f, static_cast<int>(rgbe[3]) - 128); // 2^(E-128)
        return linear_color3d(static_cast<float>(rgbe[0]) * scale,
                       static_cast<float>(rgbe[1]) * scale,
                       static_cast<float>(rgbe[2]) * scale) / 255.0f;
    }

	/**
     * @brief 将当前 HDR 颜色编码为 RGBE 格式
     * @param out_rgbe 输出 4 字节数组
     */
    void to_rgbe(uint8_t out_rgbe[4]) const noexcept 
	{
        float v = std::max({r(), g(), b()});
        if (v < 1e-32f) {
            out_rgbe[0] = out_rgbe[1] = out_rgbe[2] = out_rgbe[3] = 0;
            return;
        }
        int e;
        float scale = std::frexp(v, &e) * 256.0f / v;
        out_rgbe[0] = static_cast<uint8_t>(std::clamp(r() * scale, 0.0f, 255.0f));
        out_rgbe[1] = static_cast<uint8_t>(std::clamp(g() * scale, 0.0f, 255.0f));
        out_rgbe[2] = static_cast<uint8_t>(std::clamp(b() * scale, 0.0f, 255.0f));
        out_rgbe[3] = static_cast<uint8_t>(e + 128);
    }

	/**
     * @brief 从十六进制字符串构造（支持 #RGB, #RRGGBB, RRGGBB, RGB）
     * @param hex 字符串，大小写不敏感
     * @return 线性颜色（注意：输入被解释为 sRGB，返回线性值）
     */
    static linear_color3d from_hex(std::string_view hex) 
	{
        std::string_view trimmed = hex;
        if (trimmed.starts_with('#'))
            trimmed.remove_prefix(1);
        
        uint32_t int_value = 0;
        if (trimmed.size() == 3) 
		{
            // RGB -> 每个字符重复一次
            auto from_hex_char = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'a' && c <= 'F') return c - 'a' + 10;
                return 0;
            };
            uint8_t r = from_hex_char(trimmed[0]);
            uint8_t g = from_hex_char(trimmed[1]);
            uint8_t b = from_hex_char(trimmed[2]);
            int_value = (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
        } else if (trimmed.size() == 6) 
		{
            auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), int_value, 16);
            if (result.ec != std::errc{}) 
			{
                int_value = 0;
            }
        } else 
		{
            // 非法长度，返回黑色
            return linear_color3d::black();
        }

        uint8_t r = (int_value >> 16) & 0xFF;
        uint8_t g = (int_value >> 8) & 0xFF;
        uint8_t b = int_value & 0xFF;
        
        // 输入为 sRGB 编码，需转为线性
        return from_byte_srgb(r, g, b);
    }

	/**
     * @brief 从 8-bit sRGB 分量构造线性颜色
     */
    static linear_color3d from_byte_srgb(uint8_t r, uint8_t g, uint8_t b) 
	{
        linear_color3d srgb(r / 255.0f, g / 255.0f, b / 255.0f);
        return srgb.to_linear();
    }
    /**
     * @brief 计算相对亮度（Rec.709 系数）
     */
    [[nodiscard]] constexpr float luminance() const noexcept 
	{
        return 0.212671f * r() + 0.715160f * g() + 0.072169f * b();
    }

    /**
     * @brief 判断是否为黑色（考虑浮点误差）
     */
    [[nodiscard]] bool is_black(float eps = 1e-6f) const noexcept 
	{
        return m_data.square() <= eps * eps;
    }

    /**
     * @brief 逐分量钳位
     */
    [[nodiscard]] linear_color3d clamp(float minVal = 0.0f, float maxVal = INFINITY) const noexcept 
	{
        return linear_color3d(std::clamp(r(), minVal, maxVal),
                       std::clamp(g(), minVal, maxVal),
                       std::clamp(b(), minVal, maxVal));
    }

    /**
     * @brief 简单伽马校正（幂函数）
     */
    [[nodiscard]] linear_color3d gamma_correct(float gamma = 2.2f) const noexcept 
	{
        float inv = 1.0f / gamma;
        return linear_color3d(std::pow(r(), inv),
                       std::pow(g(), inv),
                       std::pow(b(), inv));
    }

    /**
     * @brief 线性 RGB → sRGB 转换（带线性段的标准公式）
     */
    [[nodiscard]] linear_color3d to_srgb() const noexcept 
	{
        auto linear2srgb = [](float x) -> float 
		{
            if (x <= 0.0031308f)
                return 12.92f * x;
            else
                return 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
        };
        return linear_color3d(linear2srgb(r()), linear2srgb(g()), linear2srgb(b()));
    }

    /**
     * @brief sRGB → 线性 RGB 转换
     */
    [[nodiscard]] linear_color3d to_linear() const noexcept 
	{
        auto srgb2linear = [](float x) -> float 
		{
            if (x <= 0.04045f)
                return x / 12.92f;
            else
                return std::pow((x + 0.055f) / 1.055f, 2.4f);
        };
        return linear_color3d(srgb2linear(r()), srgb2linear(g()), srgb2linear(b()));
    }

    /**
     * @brief aces 电影色调映射（Narkowicz 拟合）
     */
    [[nodiscard]] linear_color3d aces() const noexcept 
	{
        auto aces = [](float x) 
		{
            const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
            return (x * (a * x + b)) / (x * (c * x + d) + e);
        };
        return linear_color3d(aces(r()), aces(g()), aces(b())).clamp();
    }

	[[nodiscard]] linear_color3d agx() const noexcept
	{
		auto agx = [](float x) 
		{
			const float a = 0.22f, b = 0.30f, c = 0.10f, d = 0.20f, e = 0.01f;
			return (x * (a * x + b)) / (x * (c * x + d) + e);
		};
		return linear_color3d(agx(r()), agx(g()), agx(b())).clamp();
	}

	[[nodiscard]] linear_color3d filmic() const noexcept {
		auto filmic = [](float x) 
		{
			const float a = 0.22f; // Shoulder strength
			const float b = 0.30f; // Linear strength
			const float C = 0.10f; // Linear angle
			const float D = 0.20f; // Toe strength
			const float E = 0.01f; // Toe numerator
			const float F = 0.30f; // Toe denominator
			return ((x * (a * x + C * b) + D * E) / (x * (a * x + b) + D * F)) - E / F;
		};
		linear_color3d mapped(filmic(r()), filmic(g()), filmic(b()));
		// 归一化除以白点（通常为 filmic(11.2) 的值）
		float whiteScale = 1.0f / filmic(11.2f);
		return mapped * whiteScale;
	}

    /**
     * @brief Reinhard 色调映射
     */
    [[nodiscard]] linear_color3d Reinhard() const noexcept 
	{
        return linear_color3d(r() / (1.0f + r()),
                       g() / (1.0f + g()),
                       b() / (1.0f + b()));
    }

    /**
     * @brief 调整饱和度
     * @param factor 0.0 = 灰度，1.0 = 原图，>1.0 = 增强
     */
    [[nodiscard]] linear_color3d Saturation(float factor) const noexcept 
	{
        float lum = luminance();
        return linear_color3d(std::lerp(lum, r(), factor),
                       std::lerp(lum, g(), factor),
                       std::lerp(lum, b(), factor));
    }

	/**
     * @brief 生成一个随机但视觉舒适的线性颜色
	 * @todo: 接入随机库 Random.h
     */
    static linear_color3d make_random_color() 
	{
        static thread_local std::mt19937 gen(std::random_device{}());
        static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        // 在 HSV 空间生成，保证饱和度与亮度适中
        float h = dist(gen);
        float s = 0.5f + 0.5f * dist(gen); // 0.5 ~ 1.0
        float v = 0.6f + 0.4f * dist(gen); // 0.6 ~ 1.0
        return from_hsv(h * 360.0f, s, v);
    }

	/**
     * @brief 根据标量值在红-绿之间插值（0=红, 1=绿）
     */
    static linear_color3d make_red2green_color_from_scalar(float scalar) {
        scalar = std::clamp(scalar, 0.0f, 1.0f);
        return linear_color3d(1.0f - scalar, scalar, 0.0f);
    }

	/**
     * @brief 从黑体辐射色温（开尔文）转换为 RGB 色度
     * @param temp 色温（K），典型范围 1000 ~ 40000
     * @return 线性 RGB 颜色（未经亮度归一化）
     * @note 使用 Tanner Helland 的经典算法
     */
    static linear_color3d make_from_color_temperature(float temp) 
	{
        temp = std::clamp(temp, 1000.0f, 40000.0f) / 100.0f;
        
        float r, g, b;
        
        // 红色分量
        if (temp <= 66.0f) 
		{
            r = 1.0f;
        } else 
		{
            r = 1.29293618606f * std::pow(temp - 60.0f, -0.1332047592f);
            r = std::clamp(r, 0.0f, 1.0f);
        }
        
        // 绿色分量
        if (temp <= 66.0f) 
		{
            g = 0.39008157876f * std::log(temp) - 0.63184144378f;
            g = std::clamp(g, 0.0f, 1.0f);
        } else 
		{
            g = 1.1298908609f * std::pow(temp - 60.0f, -0.0755148492f);
            g = std::clamp(g, 0.0f, 1.0f);
        }
        
        // 蓝色分量
        if (temp >= 66.0f) 
		{
            b = 1.0f;
        } else if (temp <= 19.0f) 
		{
            b = 0.0f;
        } else 
		{
            b = 0.54320678911f * std::log(temp - 10.0f) - 1.19625408914f;
            b = std::clamp(b, 0.0f, 1.0f);
        }
        
        return linear_color3d(r, g, b);
    }

	
    /**
     * @brief 预乘 Alpha（用于合成）
     */
    [[nodiscard]] linear_color3d premultiply(float alpha) const noexcept 
	{
        return *this * alpha;
    }

    // ---------- 常用静态工厂 ----------
    static constexpr linear_color3d zero() noexcept { return linear_color3d(0.0f); }
    static constexpr linear_color3d one() noexcept { return linear_color3d(1.0f); }
    static constexpr linear_color3d black() noexcept { return zero(); }
    static constexpr linear_color3d White() noexcept { return one(); }
    static constexpr linear_color3d Red() noexcept { return linear_color3d(1.0f, 0.0f, 0.0f); }
    static constexpr linear_color3d Green() noexcept { return linear_color3d(0.0f, 1.0f, 0.0f); }
    static constexpr linear_color3d Blue() noexcept { return linear_color3d(0.0f, 0.0f, 1.0f); }
    static constexpr linear_color3d Yellow() noexcept { return linear_color3d(1.0f, 1.0f, 0.0f); }
    static constexpr linear_color3d Cyan() noexcept { return linear_color3d(0.0f, 1.0f, 1.0f); }
    static constexpr linear_color3d Magenta() noexcept { return linear_color3d(1.0f, 0.0f, 1.0f); }


	 /**
     * @brief 从 HSV 构造线性 RGB
     * @param h 色相 [0, 360)
     * @param s 饱和度 [0, 1]
     * @param v 明度 [0, 1]
     */
    static linear_color3d from_hsv(float h, float s, float v) {
        h = std::fmod(h, 360.0f);
        if (h < 0) h += 360.0f;
        h /= 60.0f;
        int i = static_cast<int>(h);
        float f = h - i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - s * f);
        float t = v * (1.0f - s * (1.0f - f));
        
        switch (i) {
            case 0:  return linear_color3d(v, t, p);
            case 1:  return linear_color3d(q, v, p);
            case 2:  return linear_color3d(p, v, t);
            case 3:  return linear_color3d(p, q, v);
            case 4:  return linear_color3d(t, p, v);
            default: return linear_color3d(v, p, q);
        }
    }

    /**
     * @brief 转换为 HSV 表示
     * @param h 输出色相 [0, 360)
     * @param s 输出饱和度 [0, 1]
     * @param v 输出明度 [0, 1]
     */
    void ToHSV(float& h, float& s, float& v) const 
	{
        float min_val = std::min({r(), g(), b()});
        float max_val = std::max({r(), g(), b()});
        float delta = max_val - min_val;
        
        v = max_val;
        if (max_val == 0.0f) 
		{
            s = 0.0f;
            h = 0.0f;
            return;
        }
        s = delta / max_val;
        
        if (delta == 0.0f) 
		{
            h = 0.0f;
        } else if (max_val == r()) 
		{
            h = 60.0f * std::fmod((g() - b()) / delta, 6.0f);
        } else if (max_val == g()) 
		{
            h = 60.0f * ((b() - r()) / delta + 2.0f);
        } else 
		{
            h = 60.0f * ((r() - g()) / delta + 4.0f);
        }
        if (h < 0.0f) h += 360.0f;
    }
	/**
     * @brief 格式化为 "(r=..., g=..., b=...)" 字符串
     * @param precision 小数位数（默认 3）
     */
    [[nodiscard]] std::string to_string(int precision = 3) const 
	{
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision);
        oss << "(r=" << r() << ", g=" << g() << ", b=" << b() << ")";
        return oss.str();
    }

    // ---------- 辅助：从线性值生成 8-bit sRGB ----------
    [[nodiscard]] linear_color3d quantize() const 
	{
        linear_color3d srgb = to_srgb();
        auto to_bytes = [](float x) -> uint8_t {
            return static_cast<uint8_t>(std::clamp(std::round(x * 255.0f), 0.0f, 255.0f));
        };
        return from_byte_srgb(to_bytes(srgb.r()), to_bytes(srgb.g()), to_bytes(srgb.b()));
    }
private:
    vector_type m_data;
};

// ---------- 与 TVector 的互操作（可选） ----------
inline vec3d<float> to_vector(const linear_color3d& c) noexcept {
    return c.as_vector();
}

inline linear_color3d from_vector(const vec3d<float>& v) noexcept {
    return linear_color3d(v);
}

/**
 * @brief 四维颜色（线性空间，HDR 支持），用于合成、纹理、材质属性等
 * @note 包含 Alpha 通道，语义上为 (r, g, b, a)
 * @note 底层基于 vec4d<float>，复用向量运算，屏蔽几何接口
 */
class linear_color4d {
public:
    using vector_type = vec4d<float>;
    using value_type = float;

    // ---------- 构造 ----------
    constexpr linear_color4d() noexcept : m_data{0.0f, 0.0f, 0.0f, 1.0f} {}
    constexpr linear_color4d(float r, float g, float b, float a = 1.0f) noexcept : m_data{r, g, b, a} {}
    explicit constexpr linear_color4d(float gray, float a = 1.0f) noexcept : m_data{gray, gray, gray, a} {}
    explicit constexpr linear_color4d(const vector_type& v) noexcept : m_data(v) {}

    // 从三维颜色构造（Alpha 默认为 1）
    explicit constexpr linear_color4d(const linear_color3d& c, float a = 1.0f) noexcept 
        : m_data{c.r(), c.g(), c.b(), a} {}

	/**
     * @brief 从十六进制字符串构造（支持 #RGBA, #RRGGBBAA, RRGGBBAA, RGBA）
     * @return 线性颜色（输入为 sRGB）
     */
    static linear_color4d from_hex(std::string_view hex) 
	{
        std::string_view trimmed = hex;
        if (trimmed.starts_with('#'))
            trimmed.remove_prefix(1);
        
        uint32_t int_value = 0;
        uint8_t a = 255;
        
        if (trimmed.size() == 4) 
		{
            // RGBA -> 每个字符重复
            auto fromHexChar = [](char c) -> uint8_t 
			{
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'a' && c <= 'F') return c - 'a' + 10;
                return 0;
            };
            uint8_t r = fromHexChar(trimmed[0]);
            uint8_t g = fromHexChar(trimmed[1]);
            uint8_t b = fromHexChar(trimmed[2]);
            a = fromHexChar(trimmed[3]);
            int_value = (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
        } 
		else if (trimmed.size() == 8) 
		{
            auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), int_value, 16);
            if (result.ec == std::errc{}) 
			{
                a = int_value & 0xFF;
                int_value >>= 8;
            } 
			else 
			{
                int_value = 0;
            }
        } 
		else if (trimmed.size() == 6) 
		{
            // 只有 RGB，Alpha 默认 255
            auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), int_value, 16);
            if (result.ec != std::errc{}) 
			{
				int_value = 0;
			}
        }
		else 
		{

		}
        uint8_t r = (int_value >> 16) & 0xFF;
        uint8_t g = (int_value >> 8) & 0xFF;
        uint8_t b = int_value & 0xFF;
        
        linear_color3d rgb = linear_color3d::from_byte_srgb(r, g, b);
        return linear_color4d(rgb, a / 255.0f);
    }
    // 允许从其他算术类型的 vec4d 转换
    template <typename U> requires std::is_arithmetic_v<U>
    explicit constexpr linear_color4d(const vec4d<U>& v) noexcept
        : m_data{static_cast<float>(v.x()), static_cast<float>(v.y()),
                static_cast<float>(v.z()), static_cast<float>(v.w())} {}

    // ---------- 分量访问 ----------
    [[nodiscard]] constexpr float r() const noexcept { return m_data.x(); }
    [[nodiscard]] constexpr float g() const noexcept { return m_data.y(); }
    [[nodiscard]] constexpr float b() const noexcept { return m_data.z(); }
    [[nodiscard]] constexpr float a() const noexcept { return m_data.w(); }

    constexpr void SetR(float r) noexcept { m_data.x() = r; }
    constexpr void SetG(float g) noexcept { m_data.y() = g; }
    constexpr void SetB(float b) noexcept { m_data.z() = b; }
    constexpr void SetA(float a) noexcept { m_data.w() = a; }

    [[nodiscard]] constexpr float  operator[](std::size_t i) const noexcept { return m_data[i]; }
    constexpr float& operator[](std::size_t i) noexcept { return m_data[i]; }

    // 提取 RGB 部分（显式丢弃 Alpha）
    [[nodiscard]] constexpr linear_color3d to_color3d() const noexcept 
	{
        return linear_color3d{r(), g(), b()};
    }

    // 获取底层向量（用于需要向量操作的特殊场景）
    [[nodiscard]] constexpr const vector_type& as_vector() const noexcept { return m_data; }

    // ---------- 算术运算符（转发给 Vector）----------
    constexpr linear_color4d& operator+=(const linear_color4d& rhs) noexcept 
	{
        m_data += rhs.m_data;
        return *this;
    }
    constexpr linear_color4d& operator-=(const linear_color4d& rhs) noexcept 
	{
        m_data -= rhs.m_data;
        return *this;
    }
    constexpr linear_color4d& operator*=(const linear_color4d& rhs) noexcept 
	{
        m_data *= rhs.m_data;
        return *this;
    }
    constexpr linear_color4d& operator/=(const linear_color4d& rhs) noexcept 
	{
        m_data /= rhs.m_data;
        return *this;
    }
    constexpr linear_color4d& operator*=(float s) noexcept 
	{
        m_data *= s;
        return *this;
    }
    constexpr linear_color4d& operator/=(float s) noexcept 
	{
        m_data /= s;
        return *this;
    }

    constexpr linear_color4d operator-() const noexcept 
	{
        return linear_color4d(-m_data);
    }

    friend constexpr linear_color4d operator+(linear_color4d lhs, const linear_color4d& rhs) noexcept 
	{
        return lhs += rhs;
    }
    friend constexpr linear_color4d operator-(linear_color4d lhs, const linear_color4d& rhs) noexcept 
	{
        return lhs -= rhs;
    }
    friend constexpr linear_color4d operator*(linear_color4d lhs, const linear_color4d& rhs) noexcept 
	{
        return lhs *= rhs;
    }
    friend constexpr linear_color4d operator/(linear_color4d lhs, const linear_color4d& rhs) noexcept 
	{
        return lhs /= rhs;
    }
    friend constexpr linear_color4d operator*(linear_color4d lhs, float rhs) noexcept 
	{
        return lhs *= rhs;
    }
    friend constexpr linear_color4d operator*(float lhs, linear_color4d rhs) noexcept 
	{
        return rhs *= lhs;
    }
    friend constexpr linear_color4d operator/(linear_color4d lhs, float rhs) noexcept 
	{
        return lhs /= rhs;
    }

    friend constexpr bool operator==(const linear_color4d& lhs, const linear_color4d& rhs) noexcept 
	{
        return lhs.m_data == rhs.m_data;
    }

    // ---------- 颜色专属功能（扩展至四通道）----------

    /**
     * @brief 计算 RGB 部分相对亮度（忽略 Alpha）
     */
    [[nodiscard]] constexpr float luminance() const noexcept 
	{
        return to_color3d().luminance();
    }

    /**
     * @brief 判断 RGB 是否为黑色（考虑浮点误差）
     */
    [[nodiscard]] constexpr bool is_black(float eps = 1e-6f) const noexcept 
	{
        return to_color3d().is_black(eps);
    }

    /**
     * @brief 逐分量钳位（包含 Alpha）
     */
    [[nodiscard]] constexpr linear_color4d clamp(float minVal = 0.0f, float maxVal = INFINITY) const noexcept 
	{
        return linear_color4d(std::clamp(r(), minVal, maxVal),
                       std::clamp(g(), minVal, maxVal),
                       std::clamp(b(), minVal, maxVal),
                       std::clamp(a(), minVal, maxVal));
    }

    /**
     * @brief 简单伽马校正（仅应用于 RGB，Alpha 保持不变）
     */
    [[nodiscard]] constexpr linear_color4d gamma_correct(float gamma = 2.2f) const noexcept 
	{
        linear_color3d rgb = to_color3d().gamma_correct(gamma);
        return linear_color4d(rgb, a());
    }

    /**
     * @brief RGB 部分线性 → sRGB 转换，Alpha 不变
     */
    [[nodiscard]] constexpr linear_color4d to_srgb() const noexcept 
	{
        linear_color3d rgb = to_color3d().to_srgb();
        return linear_color4d(rgb, a());
    }

    /**
     * @brief RGB 部分 sRGB → 线性转换，Alpha 不变
     */
    [[nodiscard]] linear_color4d to_linear() const noexcept 
	{
        linear_color3d rgb = to_color3d().to_linear();
        return linear_color4d(rgb, a());
    }

    /**
     * @brief 色调映射（应用于 RGB，Alpha 不变）
     */
    [[nodiscard]] linear_color4d aces() const noexcept 
	{
        linear_color3d rgb = to_color3d().aces();
        return linear_color4d(rgb, a());
    }

    [[nodiscard]] linear_color4d Reinhard() const noexcept 
	{
        linear_color3d rgb = to_color3d().Reinhard();
        return linear_color4d(rgb, a());
    }

    /**
     * @brief 调整 RGB 饱和度，Alpha 不变
     */
    [[nodiscard]] linear_color4d Saturation(float factor) const noexcept 
	{
        linear_color3d rgb = to_color3d().Saturation(factor);
        return linear_color4d(rgb, a());
    }

    /**
     * @brief 预乘 Alpha（将 RGB 乘以 A，Alpha 不变）
     * @note 常用于合成管线
     */
    [[nodiscard]] linear_color4d PremultipliedAlpha() const noexcept 
	{
        return linear_color4d(r() * a(), g() * a(), b() * a(), a());
    }

    /**
     * @brief 反预乘 Alpha（将 RGB 除以 A，Alpha 不变）
     * @note 要求 a > 0
     */
    [[nodiscard]] linear_color4d UnpremultipliedAlpha() const noexcept 
	{
        float inv_a = (a() != 0.0f) ? 1.0f / a() : 0.0f;
        return linear_color4d(r() * inv_a, g() * inv_a, b() * inv_a, a());
    }

    /**
     * @brief Alpha 混合（Over 操作）
     * @param bg 背景颜色（假定已预乘或未预乘？这里采用非预乘标准公式）
     * @note 公式：result = fg * fg.a + bg * (1 - fg.a)
     */
    [[nodiscard]] linear_color4d Over(const linear_color4d& bg) const noexcept 
	{
        float a_ = a();
        return linear_color4d(r() + bg.r() * (1.0f - a_),
                       g() + bg.g() * (1.0f - a_),
                       b() + bg.b() * (1.0f - a_),
                       a_ + bg.a() * (1.0f - a_));
    }

	/**
     * @brief 生成随机颜色（带透明度）
     * @param random_alpha 是否随机透明度（默认固定为 1.0）
     */
    static linear_color4d make_random_color(bool random_alpha = false) 
	{
        linear_color3d rgb = linear_color3d::make_random_color();
        float a = 1.0f;
        if (random_alpha) {
            static thread_local std::mt19937 gen(std::random_device{}());
            static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            a = dist(gen);
        }
        return linear_color4d(rgb, a);
    }

    /**
     * @brief 根据标量值在红-绿之间插值（0=红, 1=绿），带透明度
     */
    static linear_color4d make_red2green_color_from_scalar(float scalar, float alpha = 1.0f) 
	{
        linear_color3d rgb = linear_color3d::make_red2green_color_from_scalar(scalar);
        return linear_color4d(rgb, alpha);
    }

    /**
     * @brief 从色温构造颜色（带透明度）
     */
    static linear_color4d make_from_color_temperature(float temp, float alpha = 1.0f) 
	{
        linear_color3d rgb = linear_color3d::make_from_color_temperature(temp);
        return linear_color4d(rgb, alpha);
    }

    /**
     * @brief 格式化为 "(r=..., g=..., b=..., a=...)" 字符串
     */
    [[nodiscard]] std::string to_string(int precision = 3) const 
	{
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision);
        oss << "(r=" << r() << ", g=" << g() << ", b=" << b() << ", a=" << a() << ")";
        return oss.str();
    }

    /**
     * @brief 从 RGBE 构造（Alpha 固定为 1.0）
     */
    static linear_color4d from_rgbe(const uint8_t rgbe[4]) 
	{
        return linear_color4d(linear_color3d::from_rgbe(rgbe), 1.0f);
    }

    /**
     * @brief 编码为 RGBE（忽略 Alpha）
     */
    void to_rgbe(uint8_t outRgbe[4]) const 
	{
        to_color3d().to_rgbe(outRgbe);
    }
    // ---------- 静态工厂 ----------
    static constexpr linear_color4d zero() noexcept { return linear_color4d(0.0f, 0.0f, 0.0f, 0.0f); }
    static constexpr linear_color4d one() noexcept { return linear_color4d(1.0f, 1.0f, 1.0f, 1.0f); }
    static constexpr linear_color4d black() noexcept { return linear_color4d(0.0f, 0.0f, 0.0f, 1.0f); }
    static constexpr linear_color4d white() noexcept { return linear_color4d(1.0f, 1.0f, 1.0f, 1.0f); }
    static constexpr linear_color4d transparent() noexcept { return linear_color4d(0.0f, 0.0f, 0.0f, 0.0f); }
    static constexpr linear_color4d red(float a = 1.0f) noexcept { return linear_color4d(1.0f, 0.0f, 0.0f, a); }
    static constexpr linear_color4d green(float a = 1.0f) noexcept { return linear_color4d(0.0f, 1.0f, 0.0f, a); }
    static constexpr linear_color4d blue(float a = 1.0f) noexcept { return linear_color4d(0.0f, 0.0f, 1.0f, a); }
    static constexpr linear_color4d yellow(float a = 1.0f) noexcept { return linear_color4d(1.0f, 1.0f, 0.0f, a); }
    static constexpr linear_color4d cyan(float a = 1.0f) noexcept { return linear_color4d(0.0f, 1.0f, 1.0f, a); }
    static constexpr linear_color4d magenta(float a = 1.0f) noexcept { return linear_color4d(1.0f, 0.0f, 1.0f, a); }

private:
    vector_type m_data;
};

// ---------- 与 vec4d 的互操作 ----------
inline vec4d<float> to_vector(const linear_color4d& c) noexcept {

    return c.as_vector();
}

inline linear_color4d from_vector(const vec4d<float>& v) noexcept 
{
    return linear_color4d(v);
}

/**
 * @brief 8-bit 整型 RGB 颜色（隐式 sRGB 编码）
 * @note 内存布局与 uint8_t[3] 兼容，可直接写入图像缓冲区
 * @note 所有运算应通过 to_linear() 转换为 linear_color3d 进行
 */
struct color3d 
{
    uint8_t r, g, b;

    // ---------- 构造 ----------
    constexpr color3d() noexcept : r(0), g(0), b(0) {}
    constexpr color3d(uint8_t r, uint8_t g, uint8_t b) noexcept : r(r), g(g), b(b) {}

    // 从线性浮点颜色构造（自动执行 sRGB 编码与量化）
    explicit color3d(const linear_color3d& linear) noexcept 
	{
        linear_color3d srgb = linear.to_srgb();
        auto saturate = [](float x) -> uint8_t {
            // 注意：std::clamp 与 std::round 在 constexpr 中可用 (C++23 前部分编译器支持有限)
            // 为兼容性，使用简单条件判断，但此处不需要 constexpr，故直接使用 std::clamp
            return static_cast<uint8_t>(std::clamp(std::round(x * 255.0f), 0.0f, 255.0f));
        };
        r = saturate(srgb.r());
        g = saturate(srgb.g());
        b = saturate(srgb.b());
    }

    // 从十六进制字符串构造（支持 #RGB, #RRGGBB, RRGGBB, RGB）
    static color3d from_hex(std::string_view hex) 
	{
        // 复用 linear_color3d 的解析逻辑，再转回整型
        linear_color3d linear = linear_color3d::from_hex(hex);
        return color3d(linear);
    }

    // ---------- 转换 ----------
    [[nodiscard]] linear_color3d to_linear() const 
	{
        return linear_color3d::from_byte_srgb(r, g, b);
    }

    // ---------- 访问器 ----------
    const uint8_t* data() const noexcept { return &r; }
    uint8_t* data() noexcept { return &r; }

    // ---------- 字符串输出 ----------
    [[nodiscard]] std::string to_string() const 
	{
        std::ostringstream oss;
        oss << "(r=" << static_cast<int>(r)
            << ", g=" << static_cast<int>(g)
            << ", b=" << static_cast<int>(b) << ")";
        return oss.str();
    }

    // ---------- 静态工厂 ----------
    static constexpr color3d black() noexcept { return {0, 0, 0}; }
    static constexpr color3d white() noexcept { return {255, 255, 255}; }
    static constexpr color3d red() noexcept   { return {255, 0, 0}; }
    static constexpr color3d green() noexcept { return {0, 255, 0}; }
    static constexpr color3d blue() noexcept  { return {0, 0, 255}; }
    static constexpr color3d yellow() noexcept { return {255, 255, 0}; }
    static constexpr color3d cyan() noexcept   { return {0, 255, 255}; }
    static constexpr color3d magenta() noexcept{ return {255, 0, 255}; }

    // 随机颜色
    static color3d make_random_color() {
        linear_color3d lin = linear_color3d::make_random_color();
        return color3d(lin);
    }

    // 色温
    static color3d make_from_color_temperature(float temp) {
        linear_color3d lin = linear_color3d::make_from_color_temperature(temp);
        return color3d(lin);
    }

    // 红-绿插值
    static color3d make_red2green_color_from_scalar(float scalar) {
        linear_color3d lin = linear_color3d::make_red2green_color_from_scalar(scalar);
        return color3d(lin);
    }
};

/**
 * @brief 8-bit 整型 RGBA 颜色（隐式 sRGB 编码）
 * @note 内存布局与 uint8_t[4] 兼容，适用于纹理像素与帧缓存
 */
struct color4d 
{
    uint8_t r, g, b, a;

    // ---------- 构造 ----------
    constexpr color4d() noexcept : r(0), g(0), b(0), a(255) {}
    constexpr color4d(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept : r(r), g(g), b(b), a(a) {}

    // 从线性浮点颜色构造（自动 sRGB 编码与量化）
    explicit color4d(const linear_color4d& linear) noexcept 
	{
        linear_color4d srgb = linear.to_srgb();
        auto saturate = [](float x) -> uint8_t {
            return static_cast<uint8_t>(std::clamp(std::round(x * 255.0f), 0.0f, 255.0f));
        };
        r = saturate(srgb.r());
        g = saturate(srgb.g());
        b = saturate(srgb.b());
        a = saturate(srgb.a());
    }

    // 从 linear_color3d + Alpha 构造
    explicit color4d(const linear_color3d& rgb, uint8_t a = 255) noexcept
        : color4d(linear_color4d(rgb, a / 255.0f)) {}

    // 从十六进制字符串构造（支持 #RGBA, #RRGGBBAA, RRGGBBAA, RGBA）
    static color4d from_hex(std::string_view hex) 
	{
        linear_color4d linear = linear_color4d::from_hex(hex);
        return color4d(linear);
    }

    // ---------- 转换 ----------
    [[nodiscard]] linear_color4d to_linear() const 
	{
        linear_color3d rgb = linear_color3d::from_byte_srgb(r, g, b);
        return linear_color4d(rgb, a / 255.0f);
    }

    // 提取 RGB 部分
    [[nodiscard]] color3d to_color3d() const noexcept 
	{
        return {r, g, b};
    }

    // ---------- 访问器 ----------
    const uint8_t* data() const noexcept { return &r; }
    uint8_t* data() noexcept { return &r; }

    // 打包为 uint32_t (RGBA 顺序)
    [[nodiscard]] uint32_t to_packed_rgba() const noexcept 
	{
        return (static_cast<uint32_t>(r) << 24) |
               (static_cast<uint32_t>(g) << 16) |
               (static_cast<uint32_t>(b) << 8)  |
               static_cast<uint32_t>(a);
    }

    // ---------- 字符串输出 ----------
    [[nodiscard]] std::string to_string() const 
	{
        std::ostringstream oss;
        oss << "(r=" << static_cast<int>(r)
            << ", g=" << static_cast<int>(g)
            << ", b=" << static_cast<int>(b)
            << ", a=" << static_cast<int>(a) << ")";
        return oss.str();
    }

    // ---------- 静态工厂 ----------
    static constexpr color4d black() noexcept       { return {0, 0, 0, 255}; }
    static constexpr color4d white() noexcept       { return {255, 255, 255, 255}; }
    static constexpr color4d transparent() noexcept { return {0, 0, 0, 0}; }
    static constexpr color4d red(uint8_t a = 255) noexcept   { return {255, 0, 0, a}; }
    static constexpr color4d green(uint8_t a = 255) noexcept { return {0, 255, 0, a}; }
    static constexpr color4d blue(uint8_t a = 255) noexcept  { return {0, 0, 255, a}; }
    static constexpr color4d yellow(uint8_t a = 255) noexcept{ return {255, 255, 0, a}; }
    static constexpr color4d cyan(uint8_t a = 255) noexcept  { return {0, 255, 255, a}; }
    static constexpr color4d magenta(uint8_t a = 255) noexcept{ return {255, 0, 255, a}; }

    // 随机颜色
    static color4d make_random_color(bool randomAlpha = false) 
	{
        linear_color4d lin = linear_color4d::make_random_color(randomAlpha);
        return color4d(lin);
    }

    // 色温
    static color4d make_from_color_temperature(float temp, uint8_t alpha = 255) 
	{
        linear_color4d lin = linear_color4d::make_from_color_temperature(temp, alpha / 255.0f);
        return color4d(lin);
    }

    // 红-绿插值
    static color4d make_red2green_color_from_scalar(float scalar, uint8_t alpha = 255) 
	{
        linear_color4d lin = linear_color4d::make_red2green_color_from_scalar(scalar, alpha / 255.0f);
        return color4d(lin);
    }
};


inline constexpr linear_color3d::linear_color3d(const linear_color4d& c) noexcept
    : m_data{c.r(), c.g(), c.b()} {}

inline constexpr linear_color4d linear_color3d::with_alpha(float alpha) const noexcept {
    return linear_color4d{*this, alpha};
}

} // namespace core::color ================================================================================

namespace core::math
{
namespace details
{

template <typename Ty, size_t Row, size_t Col = Row>
struct TMatrix;

template <typename Ty, size_t Row, size_t Col>
struct TMatrix 
{
	using value_type = Ty;

	constexpr static size_t rows = Row;
	constexpr static size_t cols = Col;

	Ty m[Row][Col];
};

template <typename Ty, size_t N>
struct TMatrix <Ty, N, N>
{
	using value_type = Ty;

	constexpr static size_t rows = N;
	constexpr static size_t cols = N;

	Ty m[N][N];

	constexpr static TMatrix <Ty, N, N> identity() noexcept;
	constexpr static TMatrix <Ty, N, N> zero() noexcept;
	constexpr static TMatrix <Ty, N, N> one() noexcept;

	TMatrix <Ty, N, N>& operator+=(const TMatrix <Ty, N, N>& rhs) noexcept;
	TMatrix <Ty, N, N>& operator+=(Ty rhs) noexcept;
	TMatrix <Ty, N, N>& operator-=(const TMatrix <Ty, N, N>& rhs) noexcept;
	TMatrix <Ty, N, N>& operator-=(Ty rhs) noexcept;

	TMatrix <Ty, N, N>& multiply(const TMatrix <Ty, N, N>& rhs) noexcept;

	bool inverse() noexcept;
	double det() const noexcept; 
};

template <typename Ty, size_t N>
[[nodiscard]] constexpr static vec<Ty, N> operator*(const TMatrix <Ty, N, N>& mat, const vec<Ty, N>& vec) noexcept;
template <typename Ty, size_t N>
[[nodiscard]] constexpr static vec<Ty, N> operator*(const vec<Ty, N>& vec, const TMatrix <Ty, N, N>& mat) noexcept;

template <typename Ty, size_t N>
constexpr static TMatrix <Ty, N, N> perspective(Ty fov, Ty aspect, Ty near, Ty far) noexcept;

template <typename Ty, size_t N>
constexpr static TMatrix <Ty, N, N> translate(Ty x, Ty y, Ty z) noexcept;

template <typename Ty, size_t N>
constexpr static TMatrix <Ty, N, N> rotate(Ty angle, Ty x, Ty y, Ty z) noexcept;

template <typename Ty, size_t N>
constexpr static TMatrix <Ty, N, N> scale(Ty x, Ty y, Ty z) noexcept;

template <typename Ty, size_t N>
TMatrix <Ty, N, N> operator+(const TMatrix <Ty, N, N> lhs, const TMatrix <Ty, N, N>& rhs) noexcept;
template <typename Ty, size_t N>
TMatrix <Ty, N, N> operator+(Ty lhs, const TMatrix <Ty, N, N>& rhs) noexcept;
template <typename Ty, size_t N>
TMatrix <Ty, N, N> operator+(const TMatrix <Ty, N, N> lhs, Ty rhs) noexcept;

template <typename Ty, size_t N>
TMatrix <Ty, N, N> operator-(const TMatrix <Ty, N, N> lhs, const TMatrix <Ty, N, N>& rhs) noexcept;
template <typename Ty, size_t N>
TMatrix <Ty, N, N> operator-(Ty lhs, const TMatrix <Ty, N, N>& rhs) noexcept;
template <typename Ty, size_t N>
TMatrix <Ty, N, N> operator-(const TMatrix <Ty, N, N> lhs, Ty rhs) noexcept;


}

using mat2i = details::TMatrix<int32_t, 2>;
using mat2f = details::TMatrix<float, 2>;
using mat3i = details::TMatrix<int32_t, 3>;
using mat3f = details::TMatrix<float, 3>;
using mat4i = details::TMatrix<int32_t, 4>;
using mat4f = details::TMatrix<float, 4>;
}






namespace core::math
{
template<std::floating_point Ty>
constexpr bool approx_equal(Ty a, Ty b, Ty eps = Ty{0.0001}) noexcept
{
    const Ty diff = std::abs(a - b);
    const Ty scale = std::max(std::abs(a), std::abs(b));
    return diff <= eps * std::max(scale, Ty{1});
}

constexpr bool approx_equal(float a, float b, float eps = tiny) noexcept
{
    return approx_equal<float>(a, b, eps);
}
constexpr bool approx_equal(double a, double b, double eps = tiny) noexcept
{
    return approx_equal<double>(a, b, eps);
}
constexpr bool approx_equal(long double a, long double b, long double eps = tiny) noexcept
{
    return approx_equal<long double>(a, b, eps);
}

constexpr bool approx_zero(float a, float eps = tiny) noexcept
{
	return approx_equal<float>(a, 0.0f, eps);
}
constexpr bool approx_zero(double a, double eps = tiny) noexcept
{
	return approx_equal<double>(a, 0.0, eps);
}
constexpr bool approx_zero(long double a, long double eps = tiny) noexcept
{
	return approx_equal<long double>(a, 0.0L, eps);
}

using std::clamp;
using std::lerp;

template<typename Vec, std::size_t N>
	requires (Vec::dimensions >= 1) && std::is_arithmetic_v<typename Vec::value_type>
inline auto clamp(const vec<typename Vec::value_type, N>& value,
    const vec<typename Vec::value_type, N>& min,
    const vec<typename Vec::value_type, N>& max) -> vec<typename Vec::value_type, N>
{
    vec<typename Vec::value_type, N> result;
    for (std::size_t i = 0; i < N; ++i) 
	{
        result[i] = clamp(value[i], min[i], max[i]);
    }
    return result;
}
template<typename Vec, std::size_t N>
	requires (Vec::dimensions >= 1) && std::is_arithmetic_v<typename Vec::value_type>
inline auto lerp(const vec<typename Vec::value_type, N>& a, 
	const vec<typename Vec::value_type, N>& b, 
	typename Vec::value_type t) -> vec<typename Vec::value_type, N>
{
    vec<typename Vec::value_type, N> result;
    for (std::size_t i = 0; i < N; ++i) 
	{
        result[i] = lerp(a[i], b[i], t);
    }
    return result;
}

}





#if defined(__cpp_lib_isclose) && __cpp_lib_isclose >= 202207L

namespace core::math
{
    using std::isclose;
}

// C++23 之前没有 std::isclose
#else 

namespace core::math
{
    template<std::floating_point Ty>
    constexpr bool isclose(Ty x, Ty y, Ty rel_tol = Ty{1e-9}, Ty abs_tol = Ty{0}) noexcept
    {
        if (x == y)
            return true;

        if (std::isnan(x) || std::isnan(y))
            return false;

        if (std::isinf(x) || std::isinf(y))
            return false;

        const Ty diff = std::abs(x - y);
        const Ty max_mag = std::max(std::abs(x), std::abs(y));

        return diff <= rel_tol * max_mag || diff <= abs_tol;
    }

    // 字面量重载，方便直接传 float/double 数字
    constexpr bool isclose(float x, float y, float rel_tol = 1e-9f, float abs_tol = 0.f) noexcept
    {
        return isclose<float>(x, y, rel_tol, abs_tol);
    }
    constexpr bool isclose(double x, double y, double rel_tol = 1e-9, double abs_tol = 0.0) noexcept
    {
        return isclose<double>(x, y, rel_tol, abs_tol);
    }
    constexpr bool isclose(long double x, long double y, long double rel_tol = 1e-9L, long double abs_tol = 0.0L) noexcept
    {
        return isclose<long double>(x, y, rel_tol, abs_tol);
    }
}
#endif