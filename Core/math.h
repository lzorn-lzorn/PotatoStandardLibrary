
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
#include <compare>
#include <string>
#include <string_view>
#include <execution>
#include <iomanip>
#include <type_traits>
#include <charconv>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

#include "platform.h"

#if defined(__MSVC__)
#include <corecrt_math.h>
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

    static constexpr float dot(const Derived& a, const Derived& b) noexcept {
        return a.dot(b);
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

inline bool is_parallel(const vec3f& a, const vec3f& b, float eps = tiny) noexcept 
{
    return approx_zero(vec3f::cross(a, b).length(), eps);
}

inline bool is_parallel(const vec2f& a, const vec2f& b, float eps = tiny) noexcept 
{
    return approx_zero(vec2f::cross(a, b), eps);
}

inline bool is_vertical(const vec3f& a, const vec3f& b, float eps = tiny) noexcept 
{
    return approx_zero(a.dot(b), eps);
}

inline bool is_vertical(const vec2f& a, const vec2f& b, float eps = tiny) noexcept 
{
    return approx_zero(a.dot(b), eps);
}

inline bool in_same_direction(const vec2f& a, const vec2f& b, float eps = tiny) noexcept 
{
    return a.dot(b) > 0 && is_parallel(a, b, eps);
}

inline bool in_same_direction(const vec3f& a, const vec3f& b, float eps = tiny) noexcept 
{
    return a.dot(b) > 0 && is_parallel(a, b, eps);
}

inline bool in_opposite_direction(const vec2f& a, const vec2f& b, float eps = tiny) noexcept 
{
    return a.dot(b) < 0 && is_parallel(a, b, eps);
}

inline bool in_opposite_direction(const vec3f& a, const vec3f& b, float eps = tiny) noexcept 
{
    return a.dot(b) < 0 && is_parallel(a, b, eps);
}

inline bool is_collinear(vec3f point, vec3f line_start, vec3f line_end, float eps = tiny) noexcept 
{
    return is_parallel(line_end - line_start, point - line_start, eps);
}

inline bool is_collinear(vec2f point, vec2f line_start, vec2f line_end, float eps = tiny) noexcept 
{
    return is_parallel(line_end - line_start, point - line_start, eps);
}

inline bool is_between(float point, float lower_bound, float upper_bound) noexcept 
{
    return point >= lower_bound && point <= upper_bound;
}

inline bool is_between(vec2f point, vec2f lower_bound, vec2f upper_bound) noexcept 
{
    return is_between(point.x(), lower_bound.x(), upper_bound.x()) && 
           is_between(point.y(), lower_bound.y(), upper_bound.y());
}

inline bool is_between(vec3f point, vec3f lower_bound, vec3f upper_bound) noexcept 
{
    return is_between(point.x(), lower_bound.x(), upper_bound.x()) && 
           is_between(point.y(), lower_bound.y(), upper_bound.y()) &&
           is_between(point.z(), lower_bound.z(), upper_bound.z());
}

inline float distance(vec2f p1, vec2f p2) noexcept 
{
    return (p1 - p2).length();
}

inline float distance(vec3f p1, vec3f p2) noexcept 
{
    return (p1 - p2).length();
}

inline float radians(vec2f p1, vec2f p2) noexcept
{
    float dot_product = p1.dot(p2);
    float lengths_product = p1.length() * p2.length();
    if (lengths_product == 0.0f) 
    {
        return 0.0f;
    }
    float cos_angle = std::clamp(dot_product / lengths_product, -1.0f, 1.0f);
    return std::acos(cos_angle);
}

inline float angle(vec2f p1, vec2f p2) noexcept
{
    return radians(p1, p2) * math::rad2deg;
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
    [[nodiscard]] linear_color3d saturation(float factor) const noexcept 
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
    [[nodiscard]] linear_color4d saturation(float factor) const noexcept 
	{
        linear_color3d rgb = to_color3d().saturation(factor);
        return linear_color4d(rgb, a());
    }

    /**
     * @brief 预乘 Alpha（将 RGB 乘以 A，Alpha 不变）
     * @note 常用于合成管线
     */
    [[nodiscard]] linear_color4d premultiplied_alpha() const noexcept 
	{
        return linear_color4d(r() * a(), g() * a(), b() * a(), a());
    }

    /**
     * @brief 反预乘 Alpha（将 RGB 除以 A，Alpha 不变）
     * @note 要求 a > 0
     */
    [[nodiscard]] linear_color4d unpremultiplied_alpha() const noexcept 
	{
        float inv_a = (a() != 0.0f) ? 1.0f / a() : 0.0f;
        return linear_color4d(r() * inv_a, g() * inv_a, b() * inv_a, a());
    }

    /**
     * @brief Alpha 混合（Over 操作）
     * @param bg 背景颜色（假定已预乘或未预乘？这里采用非预乘标准公式）
     * @note 公式：result = fg * fg.a + bg * (1 - fg.a)
     */
    [[nodiscard]] linear_color4d over(const linear_color4d& bg) const noexcept 
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
// forward declaration
template <typename Ty, size_t Row, size_t Col = Row>
struct TMatrix;

template <typename Ty, size_t Row, size_t Col>
struct TMatrix;

template <typename Ty, size_t N>
	requires std::is_arithmetic_v<Ty>
struct TMatrix <Ty, N, N>;
}

template <typename Ty, size_t N>
constexpr inline class details::TMatrix<Ty, N, N> scale(const Ty (&factors)[N]) noexcept;

template <typename Ty, size_t N>
constexpr inline details::TMatrix<Ty, N, N> scale(const std::array<Ty, N>& factors) noexcept;

template <typename Ty, size_t N>
constexpr inline details::TMatrix<Ty, N, N> scale(const vec<Ty, N>& factors) noexcept;

namespace details
{


/// 2D 旋转矩阵（Givens 旋转）
template <typename Ty, size_t N>
	requires std::is_arithmetic_v<Ty> && (N >= 2)
constexpr static TMatrix <Ty, N, N> Givens_Matrix(Ty radians, size_t i, size_t j) noexcept;

/// 获取平移矩阵
template <typename Ty, size_t N>
constexpr static details::TMatrix<Ty, N, N> Get_Translation_Matrix(const vec<Ty, N>& offset) noexcept;
template <typename Ty, size_t N>
constexpr static details::TMatrix<Ty, N, N> Get_Translation_Matrix(const Ty (&offset)[N]) noexcept;

/// 获取缩放矩阵
template <typename Ty, size_t N>
constexpr inline details::TMatrix<Ty, N, N> Get_Scaling_Matrix(const vec<Ty, N>& factors) noexcept;
template <typename Ty, size_t N>
constexpr inline details::TMatrix<Ty, N, N> Get_Scaling_Matrix(const Ty (&factors)[N]) noexcept;

/// 获取旋转矩阵
template <typename Ty>
constexpr static TMatrix<Ty, 4, 4> Get_Rotate4D_Matrix(Ty radians, Ty x, Ty y, Ty z, Ty w) noexcept;
template <typename Ty>
constexpr static TMatrix<Ty, 3, 3> Get_Rotate3D_Matrix(Ty radians, Ty x, Ty y, Ty z) noexcept;

template <typename Ty, size_t Rows, size_t Cols>
[[maybe_unused]] inline TMatrix <Ty, Cols, Rows> Transpose_Natively(TMatrix <Ty, Rows, Cols>& mat) noexcept;

template <typename Ty, size_t Rows, size_t Cols>
inline TMatrix<Ty, Rows, Cols> Random_Matrix(Ty left, Ty right) noexcept;

template <typename Ty, size_t Rows, size_t Cols>
[[maybe_unused]] inline TMatrix<Ty, Rows, Cols> Fill_Matrix_With(TMatrix<Ty, Rows, Cols>& mat, Ty value) noexcept;

template <typename Ty, size_t N>
[[nodiscard]] constexpr inline TMatrix <Ty, N, N> Multiply_Natively(const TMatrix <Ty, N, N>& mat1, const TMatrix <Ty, N, N>& mat2) noexcept;

template <typename Ty, size_t Cols, size_t Middle, size_t Rows>
[[nodiscard]] constexpr inline TMatrix <Ty, Cols, Rows> Multiply_Natively(const TMatrix <Ty, Cols, Middle>& mat1, const TMatrix <Ty, Middle, Rows>& mat2) noexcept;

template <typename Ty, size_t N>
[[nodiscard]] constexpr inline vec<Ty, N> Multiply_Natively(const details::TMatrix <Ty, N, N>& mat, const vec<Ty, N>& right) noexcept;

template <typename Ty, size_t Rows, size_t Cols>
[[nodiscard]] constexpr inline vec<Ty, Rows> Multiply_Natively(const vec<Ty, Rows>& left, const details::TMatrix <Ty, Rows, Cols>& mat) noexcept;

// 默认行主序
template <typename Ty, size_t Rows, size_t Cols>
struct TMatrix 
{
	using value_type = Ty;
    using self_type = TMatrix<Ty, Rows, Cols>;

	constexpr static size_t rows = Rows;
	constexpr static size_t cols = Cols;

	std::array<std::array<Ty, rows>, cols> m;

    constexpr static self_type zero() noexcept
	{
		return self_type{};
	}
	constexpr static self_type one() noexcept 
    {
		self_type m{};
		return  Fill_Matrix_With(m, 1);
	}

    static self_type random(Ty left, Ty right) noexcept
    {
        return Random_Matrix(left, right);
    }
    template <size_t NewRows, size_t NewCols>
        requires (NewCols == Cols)
    TMatrix<Ty, Rows, NewCols> multiply(const TMatrix<Ty, NewRows, NewCols> rhs)
    {
        return Multiply_Natively<Ty, Rows, NewCols, NewCols>(*this, rhs);
    }

    vec<Ty, Rows> as_vector() requires (Cols == 1) 
    {
        vec<Ty, Rows> result;
        for (size_t i = 0; i < Rows; ++i)
        {
            result[i] = m[i][0];
        }
        return result;
    }

    vec<Ty, Cols> as_vector() requires (Rows == 1) 
    {
        vec<Ty, Cols> result;
        for (size_t j = 0; j < Cols; ++j)
        {
            result[j] = m[0][j];
        }
        return result;
    }

    self_type inverse() noexcept = delete;
    double det() noexcept = delete;

	constexpr bool is_invertible() noexcept
    {
        return false;
    }
};

template <typename Ty, size_t N>
	requires std::is_arithmetic_v<Ty>
struct TMatrix <Ty, N, N>
{
	using value_type = Ty;
	using self_type = TMatrix<Ty, N, N>;
	constexpr static size_t rows = N;
	constexpr static size_t cols = N;

	alignas(16) std::array<std::array<Ty, N>, N> m {};

    /// 获取缩放矩阵
	constexpr static details::TMatrix<Ty, N, N> get_scaling_mat(const Ty (&factors)[N]) noexcept
	{
		return Get_Scaling_Matrix(factors);
	}

	constexpr static details::TMatrix<Ty, N, N> get_scaling_mat(const std::array<Ty, N>& factors) noexcept
	{
		return Get_Scaling_Matrix(factors);
	}

	constexpr static details::TMatrix<Ty, N, N> get_scaling_mat(const vec<Ty, N>& factors) noexcept
	{
		return Get_Scaling_Matrix(factors);
	}

    /// 获取2D旋转矩阵
	constexpr static TMatrix get_rotate2d_mat(size_t i, size_t j, Ty radians) noexcept
		requires std::is_floating_point_v<Ty>
	{
		return Givens_Matrix(radians, i, j);
	}

    /// 获取绕主轴的旋转矩阵
	constexpr static TMatrix get_rotate2d_x(Ty radians) noexcept { return Givens_Matrix(radians, 1, 2); }
	constexpr static TMatrix get_rotate2d_y(Ty radians) noexcept { return Givens_Matrix(radians, 2, 0); }
	constexpr static TMatrix get_rotate2d_z(Ty radians) noexcept { return Givens_Matrix(radians, 0, 1); }

	

    /// 对自身执行一次缩放变换
    self_type& scale(vec<Ty, N> factors) noexcept
	{
        multiply(details::Get_Scaling_Matrix(factors));
		return *this;
	}
	/// 对自身进行一次3D旋转变换
    self_type& rotate(Ty radians, const Ty (&axis)[3]) noexcept
	{
		static_assert(std::is_floating_point_v<Ty>, "rate radians only makes sense for floating point types");
        multiply(Get_Rotate3D_Matrix(radians, axis[0], axis[1], axis[2]));
		return *this;
	}
	self_type& rotate(Ty radians, const vec<Ty, 3> axis) noexcept
	{
		static_assert(std::is_floating_point_v<Ty>, "rate radians only makes sense for floating point types");
        multiply(Get_Rotate3D_Matrix(radians, axis.x(), axis.y(), axis.z()));
		return *this;
	}
	self_type& rotate(Ty radians, const Ty (&axis)[4]) noexcept
	{
		static_assert(std::is_floating_point_v<Ty>, "rate radians only makes sense for floating point types");
		multiply(Get_Rotate4D_Matrix(radians, axis[0], axis[1], axis[2], axis[3]));
        return *this;
	}
	self_type& rotate(Ty radians, const vec<Ty, 4> axis) noexcept
	{
		static_assert(std::is_floating_point_v<Ty>, "rate radians only makes sense for floating point types");
		multiply(Get_Rotate4D_Matrix(radians, axis.x(), axis.y(), axis.z(), axis.w()));
        return *this;
	}

	constexpr static self_type identity() noexcept
	{
		self_type result = zero();
		for (size_t i = 0; i < N; ++i) 
		{
			result.m[i][i] = static_cast<Ty>(1);
		}
		return result;
	}
	constexpr static self_type zero() noexcept
	{
		return self_type{};
	}
	constexpr static self_type one() noexcept 
    {
		self_type m{};
		return  Fill_Matrix_With(m, 1);
	}

    static self_type random(Ty left, Ty right) noexcept
    {
        return Random_Matrix<Ty, N, N>(left, right);
    }
    
	self_type& fill_with(Ty value) noexcept
	{
        Fill_Matrix_With(*this, value);
		return *this;
	}
	
	self_type& operator+=(const self_type& rhs) noexcept 
	{
		std::transform(&m[0][0], &m[0][0] + N*N, &rhs.m[0][0], &m[0][0], std::plus<>{});
		return *this;
	}
	self_type& operator+=(Ty rhs) noexcept 
	{
		std::for_each(&m[0][0], &m[0][0] + N*N, [rhs](Ty& v){ v += rhs; });
		return *this;
	}
	TMatrix <Ty, N, N>& operator-=(const TMatrix <Ty, N, N>& rhs) noexcept
	{
		std::transform(&m[0][0], &m[0][0] + N*N, &rhs.m[0][0], &m[0][0], std::minus<>{});
		return *this;
	}
	TMatrix <Ty, N, N>& operator-=(Ty rhs) noexcept
	{
		std::for_each(&m[0][0], &m[0][0] + N*N, [rhs](Ty& v){ v -= rhs; });
		return *this;
	}

	TMatrix <Ty, N, N>& multiply(const TMatrix <Ty, N, N>& rhs) noexcept
	{
		TMatrix <Ty, N, N> result = zero();
	#if defined(__SSE2__)
		if constexpr (std::is_same_v<Ty, float> && N == 4)
		{
			__m128 a0 = _mm_loadu_ps(&m[0][0]);
			__m128 a1 = _mm_loadu_ps(&m[1][0]);
			__m128 a2 = _mm_loadu_ps(&m[2][0]);
			__m128 a3 = _mm_loadu_ps(&m[3][0]);
			__m128 b0 = _mm_loadu_ps(&rhs.m[0][0]);
			__m128 b1 = _mm_loadu_ps(&rhs.m[1][0]);
			__m128 b2 = _mm_loadu_ps(&rhs.m[2][0]);
			__m128 b3 = _mm_loadu_ps(&rhs.m[3][0]);

			/// 矩阵A的第r行
			/// C[r] = ai[0] * B[0] + ai[1] * B[1] + ai[2] * B[2] + ai[3] * B[3]
			auto row = [&](__m128 ai, size_t r) {
				/**
				 * 0x00 = [00, 00, 00, 00] -> [a[0], a[0], a[0], a[0]]
				 * 0x55 = [01, 01, 01, 01] -> [a[1], a[1], a[1], a[1]]
				 * 0xAA = [10, 10, 10, 10] -> [a[2], a[2], a[2], a[2]]
				 * 0xFF = [11, 11, 11, 11] -> [a[3], a[3], a[3], a[3]]
				 */
				__m128 t0 = _mm_mul_ps(_mm_shuffle_ps(ai, ai, 0x00), b0);
				__m128 t1 = _mm_mul_ps(_mm_shuffle_ps(ai, ai, 0x55), b1);
				__m128 t2 = _mm_mul_ps(_mm_shuffle_ps(ai, ai, 0xAA), b2);
				__m128 t3 = _mm_mul_ps(_mm_shuffle_ps(ai, ai, 0xFF), b3);
				__m128 sum = _mm_add_ps(_mm_add_ps(t0, t1), _mm_add_ps(t2, t3));
				_mm_storeu_ps(&result.m[r][0], sum);
			};

			row(a0, 0);
			row(a1, 1);
			row(a2, 2);
			row(a3, 3);
			*this = result;
			return *this;
		}
		else 
		{
            *this = Multiply_Natively(*this, rhs);
			return *this;
		}
		
	#else
        *this = Multiply_Natively(*this, rhs);
		return *this;
	#endif
		
		
	}
	self_type& transpose() noexcept
	{
	#if defined(__SSE2__)
		if constexpr (std::is_same_v<Ty, float> && N == 4)
		{
			__m128 row0 = _mm_loadu_ps(&m[0][0]);
			__m128 row1 = _mm_loadu_ps(&m[1][0]);
			__m128 row2 = _mm_loadu_ps(&m[2][0]);
			__m128 row3 = _mm_loadu_ps(&m[3][0]);

			_MM_TRANSPOSE4_PS(row0, row1, row2, row3);

			_mm_storeu_ps(&m[0][0], row0);
			_mm_storeu_ps(&m[1][0], row1);
			_mm_storeu_ps(&m[2][0], row2);
			_mm_storeu_ps(&m[3][0], row3);
		}
		else
		{
            Transpose_Natively(*this);
			return *this;
		}
	#endif
        Transpose_Natively(*this);
		return *this;
	}

   
	self_type inverse() noexcept
	{

	}
	bool is_invertible() noexcept
	{
		return approx_equal(det(), 0.f, 1e-6);
	}
	double det() const noexcept
	{
		if constexpr (N == 1) 
		{
			return m[0][0];
		}
		else if constexpr (N == 2)
		{
			return m[0][0]*m[1][1] - m[0][1]*m[1][0];
		}
		else if constexpr (N == 3)
		{
			return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
				 - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
				 + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
		}
		else if constexpr (N == 4)
		{
			float a = m[0][0], b = m[0][1], c = m[0][2], d = m[0][3];
			float e = m[1][0], f = m[1][1], g = m[1][2], h = m[1][3];
			float i = m[2][0], j = m[2][1], k = m[2][2], l = m[2][3];
			float m0 = m[3][0], n = m[3][1], o = m[3][2], p = m[3][3];

			return a * (f * (k * p - l * o) - g * (j * p - l * n) + h * (j * o - k * n))
				 - b * (e * (k * p - l * o) - g * (i * p - l * m0) + h * (i * o - k * m0))
				 + c * (e * (j * p - l * n) - f * (i * p - l * m0) + h * (i * n - j * m0))
				 - d * (e * (j * o - k * n) - f * (i * o - k * m0) + g * (i * n - j * m0));
			}
		else
		{
			TMatrix<Ty, N, N> A = *this;
			int parity = 1;  // 符号: 1 为正，-1 为负
			for (size_t k = 0; k < N; ++k) 
			{
				// 部分选主元：找当前列绝对值最大行
				size_t pivot = k;
				for (size_t i = k + 1; i < N; ++i)
				{
					if (std::abs(A.m[i][k]) > std::abs(A.m[pivot][k]))
					{
						pivot = i;
					}
				}
				if (pivot != k) 
				{
					std::swap(A.m[k], A.m[pivot]);
					parity = -parity;
				}
				if (std::abs(A.m[k][k]) < 1e-15)  // 奇异矩阵
				{
					return Ty(0);
				}
				// 消元
				for (size_t i = k + 1; i < N; ++i) 
				{
					Ty factor = A.m[i][k] / A.m[k][k];
					for (size_t j = k + 1; j < N; ++j)
					{
						A.m[i][j] -= factor * A.m[k][j];
					}
				}
			}
			Ty det = parity;
			for (size_t i = 0; i < N; ++i) det *= A.m[i][i];
			return det;
		}
	}
};

template <typename Ty, size_t N>
	requires std::is_floating_point_v<Ty> && (N >= 2)
constexpr static TMatrix <Ty, N, N> Givens_Matrix(size_t i, size_t j, Ty radians) noexcept
{
	TMatrix <Ty, N, N> m = TMatrix<Ty, N, N>::identity();
	Ty c = std::cos(radians);
	Ty s = std::sin(radians);
	m.m[i][i] = c;
	m.m[i][j] = -s;
	m.m[j][i] = s;
	m.m[j][j] = c;
	return m;
}

template <typename Ty, size_t Rows, size_t Cols>
[[maybe_unused]] inline TMatrix<Ty, Rows, Cols> Fill_Matrix_With(TMatrix<Ty, Rows, Cols>& mat, Ty value) noexcept 
{
    for (auto& row : mat.m) 
    {
        std::fill(row.begin(), row.end(), value);
    }
    return mat;
}


template <typename Ty, size_t Rows, size_t Cols>
inline TMatrix<Ty, Rows, Cols> Random_Matrix(Ty left, Ty right) noexcept
{
    TMatrix<Ty, Rows, Cols> result{};
    std::uniform_real_distribution<Ty> dist(left, right);
    for (size_t i = 0; i < Rows; ++i)
    {
        for (size_t j = 0; j < Cols; ++j)
        {
            result.m[i][j] = dist(get_rng());
        }
    }
            
    return result;
}

template <typename Ty, size_t Rows, size_t Cols>
[[maybe_unused]] inline TMatrix <Ty, Cols, Rows> Transpose_Natively(TMatrix <Ty, Rows, Cols>& mat) noexcept
{
    for (size_t i = 0; i < Rows; ++i) 
    {
        for (size_t j = i + 1; j < Cols; ++j) 
        {
            std::swap(mat.m[i][j], mat.m[j][i]);
        }
    }
    return mat;
}

template <typename Ty, size_t Cols, size_t Middle, size_t Rows>
[[nodiscard]] constexpr inline TMatrix <Ty, Cols, Rows> Multiply_Natively(const TMatrix <Ty, Cols, Middle>& mat1, const TMatrix <Ty, Middle, Rows>& mat2) noexcept
{
    TMatrix <Ty, Cols, Rows> result = zero();
    for (size_t i = 0; i < Cols; ++i) 
    {
        for (size_t k = 0; k < Middle; ++k) 
        {
            Ty temp = mat1.m[i][k];
            for (size_t j = 0; j < Rows; ++j) 
            {
                result.m[i][j] += temp * mat2.m[k][j];
            }
        }
    }
}

template <typename Ty, size_t N>
[[nodiscard]] constexpr inline TMatrix <Ty, N, N> Multiply_Natively(const TMatrix <Ty, N, N>& mat1, const TMatrix <Ty, N, N>& mat2) noexcept
{
	TMatrix <Ty, N, N> result = zero();
    if constexpr (N == 4)
    {
        result.m[0][0] = mat1.m[0][0] * mat2.m[0][0] + mat1.m[0][1] * mat2.m[1][0] + mat1.m[0][2] * mat2.m[2][0] + mat1.m[0][3] * mat2.m[3][0];
        result.m[0][1] = mat1.m[0][0] * mat2.m[0][1] + mat1.m[0][1] * mat2.m[1][1] + mat1.m[0][2] * mat2.m[2][1] + mat1.m[0][3] * mat2.m[3][1];
        result.m[0][2] = mat1.m[0][0] * mat2.m[0][2] + mat1.m[0][1] * mat2.m[1][2] + mat1.m[0][2] * mat2.m[2][2] + mat1.m[0][3] * mat2.m[3][2];
        result.m[0][3] = mat1.m[0][0] * mat2.m[0][3] + mat1.m[0][1] * mat2.m[1][3] + mat1.m[0][2] * mat2.m[2][3] + mat1.m[0][3] * mat2.m[3][3];
        result.m[1][0] = mat1.m[1][0] * mat2.m[0][0] + mat1.m[1][1] * mat2.m[1][0] + mat1.m[1][2] * mat2.m[2][0] + mat1.m[1][3] * mat2.m[3][0];
        result.m[1][1] = mat1.m[1][0] * mat2.m[0][1] + mat1.m[1][1] * mat2.m[1][1] + mat1.m[1][2] * mat2.m[2][1] + mat1.m[1][3] * mat2.m[3][1];
        result.m[1][2] = mat1.m[1][0] * mat2.m[0][2] + mat1.m[1][1] * mat2.m[1][2] + mat1.m[1][2] * mat2.m[2][2] + mat1.m[1][3] * mat2.m[3][2];
        result.m[1][3] = mat1.m[1][0] * mat2.m[0][3] + mat1.m[1][1] * mat2.m[1][3] + mat1.m[1][2] * mat2.m[2][3] + mat1.m[1][3] * mat2.m[3][3];
        result.m[2][0] = mat1.m[2][0] * mat2.m[0][0] + mat1.m[2][1] * mat2.m[1][0] + mat1.m[2][2] * mat2.m[2][0] + mat1.m[2][3] * mat2.m[3][0];
        result.m[2][1] = mat1.m[2][0] * mat2.m[0][1] + mat1.m[2][1] * mat2.m[1][1] + mat1.m[2][2] * mat2.m[2][1] + mat1.m[2][3] * mat2.m[3][1];
        result.m[2][2] = mat1.m[2][0] * mat2.m[0][2] + mat1.m[2][1] * mat2.m[1][2] + mat1.m[2][2] * mat2.m[2][2] + mat1.m[2][3] * mat2.m[3][2];
        result.m[2][3] = mat1.m[2][0] * mat2.m[0][3] + mat1.m[2][1] * mat2.m[1][3] + mat1.m[2][2] * mat2.m[2][3] + mat1.m[2][3] * mat2.m[3][3];
        result.m[3][0] = mat1.m[3][0] * mat2.m[0][0] + mat1.m[3][1] * mat2.m[1][0] + mat1.m[3][2] * mat2.m[2][0] + mat1.m[3][3] * mat2.m[3][0];
        result.m[3][1] = mat1.m[3][0] * mat2.m[0][1] + mat1.m[3][1] * mat2.m[1][1] + mat1.m[3][2] * mat2.m[2][1] + mat1.m[3][3] * mat2.m[3][1];
        result.m[3][2] = mat1.m[3][0] * mat2.m[0][2] + mat1.m[3][1] * mat2.m[1][2] + mat1.m[3][2] * mat2.m[2][2] + mat1.m[3][3] * mat2.m[3][2];
        result.m[3][3] = mat1.m[3][0] * mat2.m[0][3] + mat1.m[3][1] * mat2.m[1][3] + mat1.m[3][2] * mat2.m[2][3] + mat1.m[3][3] * mat2.m[3][3];
    }
    else if constexpr (N == 3)
    {
        result.m[0][0] = mat1.m[0][0]*mat2.m[0][0] + mat1.m[0][1]*mat2.m[1][0] + mat1.m[0][2]*mat2.m[2][0];
        result.m[0][1] = mat1.m[0][0]*mat2.m[0][1] + mat1.m[0][1]*mat2.m[1][1] + mat1.m[0][2]*mat2.m[2][1];
        result.m[0][2] = mat1.m[0][0]*mat2.m[0][2] + mat1.m[0][1]*mat2.m[1][2] + mat1.m[0][2]*mat2.m[2][2];

        result.m[1][0] = mat1.m[1][0]*mat2.m[0][0] + mat1.m[1][1]*mat2.m[1][0] + mat1.m[1][2]*mat2.m[2][0];
        result.m[1][1] = mat1.m[1][0]*mat2.m[0][1] + mat1.m[1][1]*mat2.m[1][1] + mat1.m[1][2]*mat2.m[2][1];
        result.m[1][2] = mat1.m[1][0]*mat2.m[0][2] + mat1.m[1][1]*mat2.m[1][2] + mat1.m[1][2]*mat2.m[2][2];

        result.m[2][0] = mat1.m[2][0]*mat2.m[0][0] + mat1.m[2][1]*mat2.m[1][0] + mat1.m[2][2]*mat2.m[2][0];
        result.m[2][1] = mat1.m[2][0]*mat2.m[0][1] + mat1.m[2][1]*mat2.m[1][1] + mat1.m[2][2]*mat2.m[2][1];
        result.m[2][2] = mat1.m[2][0]*mat2.m[0][2] + mat1.m[2][1]*mat2.m[1][2] + mat1.m[2][2]*mat2.m[2][2];
    }
    else if constexpr (N == 2)
    {
        result.m[0][0] = mat1.m[0][0]*mat2.m[0][0] + mat1.m[0][1]*mat2.m[1][0];
        result.m[0][1] = mat1.m[0][0]*mat2.m[0][1] + mat1.m[0][1]*mat2.m[1][1];
        result.m[1][0] = mat1.m[1][0]*mat2.m[0][0] + mat1.m[1][1]*mat2.m[1][0];
        result.m[1][1] = mat1.m[1][0]*mat2.m[0][1] + mat1.m[1][1]*mat2.m[1][1];
    }
    else if constexpr (N == 1)
    {
        result.m[0][0] = mat1.m[0][0] * mat2.m[0][0];
    }
    else
    {	
        for (size_t i = 0; i < N; ++i) 
        {
            for (size_t k = 0; k < N; ++k) 
            {
                Ty temp = mat1.m[i][k];
                for (size_t j = 0; j < N; ++j) 
                {
                    result.m[i][j] += temp * mat2.m[k][j];
                }
            }
        }
    }
    return result;
}

template <typename Ty, size_t Rows, size_t Cols>
[[nodiscard]] constexpr inline vec<Ty, Rows> Multiply_Natively(const vec<Ty, Rows>& left, const details::TMatrix <Ty, Rows, Cols>& mat) noexcept
{
    vec<Ty, Rows> result{};
    for (size_t i = 0; i < Rows; ++i) 
    {
        Ty sum = Ty(0);
        for (size_t j = 0; j < Cols; ++j) 
        {
            sum += left.coordinates[j] * mat.m[j][i];
        }
        result.coordinates[i] = sum;
    }
    return result;
}
} // TMatrix namespace details 

template <typename Ty, size_t N>
[[nodiscard]] constexpr inline vec<Ty, N> Multiply_Natively(const details::TMatrix <Ty, N, N>& mat, const vec<Ty, N>& right) noexcept
{
#if defined(__SSE2__)
    if constexpr (std::is_same_v<Ty, float> && N == 4)
    {
        vec<Ty, N> result{};
        __m128 v = _mm_loadu_ps(&right.coordinates[0]);

        __m128 r0 = _mm_loadu_ps(&mat.m[0][0]);
        __m128 r1 = _mm_loadu_ps(&mat.m[1][0]);
        __m128 r2 = _mm_loadu_ps(&mat.m[2][0]);
        __m128 r3 = _mm_loadu_ps(&mat.m[3][0]);

        __m128 p0 = _mm_mul_ps(r0, v);
        __m128 p1 = _mm_mul_ps(r1, v);
        __m128 p2 = _mm_mul_ps(r2, v);
        __m128 p3 = _mm_mul_ps(r3, v);

         auto horizontal_sum = [](__m128 a) -> float {
            // a = [a0, a1, a2, a3]; [a1, a0, a3, a2]
            __m128 t1 = _mm_shuffle_ps(a, a, _MM_SHUFFLE(2, 3, 0, 1));
            // [a0+a1, a1+a0, a2+a3, a3+a2]
            __m128 s1 = _mm_add_ps(a, t1);
            // [s1_1, s1_0, s1_3, s1_2]
            __m128 t2 = _mm_shuffle_ps(s1, s1, _MM_SHUFFLE(1, 0, 3, 2)); 
            // 最低位 = s1_0 + s1_1 = 总和
            __m128 s2 = _mm_add_ss(s1, t2);                               
            return _mm_cvtss_f32(s2);
        };
        result.coordinates[0] = horizontal_sum(p0);
        result.coordinates[1] = horizontal_sum(p1);
        result.coordinates[2] = horizontal_sum(p2);
        result.coordinates[3] = horizontal_sum(p3);
        return result;
    }
    else
    {
        return Multiply_Natively(mat, right);
    }
#else
    return Multiply_Natively(mat, right);
#endif
    
   
}
template <typename Ty, size_t N>
[[nodiscard]] constexpr static vec<Ty, N> operator*(const vec<Ty, N>& left, const details::TMatrix <Ty, N, N>& mat) noexcept
{   
   
#if defined(__SSE2__)
    if constexpr (std::is_same_v<Ty, float> && N == 4) {
        vec<Ty, N> result{};
        __m128 v   = _mm_loadu_ps(&left[0]);             // 加载行向量 v = [v0,v1,v2,v3]

        // 加载矩阵的 4 行
        __m128 r0 = _mm_loadu_ps(&mat.m[0][0]);
        __m128 r1 = _mm_loadu_ps(&mat.m[1][0]);
        __m128 r2 = _mm_loadu_ps(&mat.m[2][0]);
        __m128 r3 = _mm_loadu_ps(&mat.m[3][0]);

        // 转置：将行寄存器变为列寄存器
        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
        // 现在 r0 = 原第 0 列，r1 = 原第 1 列，r2 = 原第 2 列，r3 = 原第 3 列

        // 水平求和辅助函数（与 mat*vec 中的相同）
        auto horizontal_sum = [](__m128 a) -> float {
            __m128 t1 = _mm_shuffle_ps(a, a, _MM_SHUFFLE(2, 3, 0, 1));
            __m128 s1 = _mm_add_ps(a, t1);
            __m128 t2 = _mm_shuffle_ps(s1, s1, _MM_SHUFFLE(1, 0, 3, 2));
            __m128 s2 = _mm_add_ss(s1, t2);
            return _mm_cvtss_f32(s2);
        };

        // 逐列点积并存储结果
        result[0] = horizontal_sum(_mm_mul_ps(v, r0));
        result[1] = horizontal_sum(_mm_mul_ps(v, r1));
        result[2] = horizontal_sum(_mm_mul_ps(v, r2));
        result[3] = horizontal_sum(_mm_mul_ps(v, r3));
        return result;
    }
    else {
        return Multiply_Natively(left, mat);
    }
#else
    return Multiply_Natively(left, mat);
#endif
}

/**
 * @brief 创建透视投影矩阵 (右手系 Y-up)
 * @param fov 视野角度: 相机在竖直方向能看到的角度范围
 * @param aspect 宽高比: 用于将方形视锥体拉伸到实际屏幕比例
 * @param near 近平面: 任何离相机更近的物体将被裁剪
 * @param far 远平面: 任何离相机更远的物体将被裁剪
 */
template <typename Ty, size_t N>
constexpr inline details::TMatrix<Ty, N, N> perspective(Ty fov, Ty aspect, Ty near, Ty far) noexcept
{
    static_assert(N == 4, "Perspective projection matrix requires N=4");
    static_assert(std::is_floating_point_v<Ty>, "Floating-point type required for perspective");

    ASSUME(fov > Ty(0));
    ASSUME(aspect > Ty(0));
    ASSUME(near > Ty(0));
    ASSUME(far > near);

    assert(fov > Ty(0) && "Field of view must be positive");
    assert(aspect > Ty(0) && "Aspect ratio must be positive");
    assert(near > Ty(0) && "Near plane must be positive");
    assert(far > near && "Far plane must be greater than near plane");

    details::TMatrix<Ty, 4, 4> result = details::TMatrix<Ty, 4, 4>::zero();

    Ty tan_half_fov = std::tan(fov * Ty(0.5));
    Ty z_range = near - far;
    Ty z_range_inv = Ty(1) / z_range;

    result.m[0][0] = Ty(1) / (aspect * tan_half_fov);
    result.m[1][1] = Ty(1) / tan_half_fov;
    result.m[2][2] = (far + near) * z_range_inv;
    result.m[2][3] = Ty(2) * far * near * z_range_inv;
    result.m[3][2] = -Ty(1);

    return result;
}


namespace details
{
/**
 * @about 旋转矩阵: 
 * >    在 N 维空间中, 旋转是一个保持原点不变, 保距且行列式为 1 的线性变换. 
 * >    任意 N 维旋转都可以分解为一系列平面旋转(Givens 旋转)的乘积.每个平面旋转仅改变
 * >    两个坐标轴构成的平面上的分量, 其余坐标保持不变.
 * @about Givens 旋转: 
 * >    在 N 维空间中, Givens 旋转是一个特殊的旋转, 它仅在两个坐标轴构成的平面上进行
 * >    旋转, 平面 (i,j) 上的旋转角度\theta (右手法则, 设 i < j), 
 * >    则对应的 N*N 矩阵 G 满足:
 * >         G[k][k] = 1,  k != i, j
 * >         G[i][i] = G[j][j] = cos(\theta)
 * >         G[i][j] = -sin(\theta)
 * >         G[j][i] = sin(\theta)
 * >    其他元素为 0
 * @brief 获取 N 维 旋转矩阵
 * @param InputIter 是一个输入迭代器, 其值类型为 rotate2d, 用于指定每个平面旋转的参数.
 */
template <typename Ty>
constexpr static TMatrix<Ty, 3, 3> Get_Rotate3D_Matrix(Ty radians, Ty x, Ty y, Ty z) noexcept
{
    static_assert(std::is_floating_point_v<Ty>, "rate angle only makes sense for floating point types");
    TMatrix<Ty, 3, 3> result = TMatrix<Ty, 3, 3>::identity();

    // 归一化旋转轴
    Ty len = std::sqrt(x*x + y*y + z*z);
    // 零轴, 返回单位矩阵
    if (len < epsilon) {
        return result;   
    }
    x /= len;
    y /= len;
    z /= len;

    Ty c = std::cos(radians);
    Ty s = std::sin(radians);
    Ty t = Ty(1) - c;

    // 罗德里格斯公式展开
    result.m[0][0] = x * x * t + c;
    result.m[0][1] = x * y * t - z * s;
    result.m[0][2] = x * z * t + y * s;

    result.m[1][0] = y * x * t + z * s;
    result.m[1][1] = y * y * t + c;
    result.m[1][2] = y * z * t - x * s;

    result.m[2][0] = z * x * t - y * s;
    result.m[2][1] = z * y * t + x * s;
    result.m[2][2] = z * z * t + c;

    return result;
}

/**
 * @about ![Rodrigues' rotation formula](https://zhuanlan.zhihu.com/p/451579313)
 * >   给定一个单位旋转轴 k = (x,y,z) 和一个旋转角度 \theta, 
 * >   则绕 k 旋转 \theta 的旋转矩阵 R 可以表示为:
 * >   R = I + sin(\theta) * K + (1 - cos(\theta)) * K^2
 * >   其中 I 是单位矩阵, K 是 k 的反对称矩阵:
 * >       K = |  0  -z   y |
 * >           |  z   0  -x |
 * >           | -y   x   0 |
 */
template <typename Ty>
constexpr static TMatrix<Ty, 4, 4> Get_Rotate4D_Matrix(Ty radians, Ty x, Ty y, Ty z, Ty w) noexcept
{
    static_assert(std::is_floating_point_v<Ty>, "rate angle only makes sense for floating point types");
    (void)w;
    TMatrix<Ty, 4, 4> result = TMatrix<Ty, 4, 4>::identity();

    // 归一化旋转轴
    Ty len = std::sqrt(x*x + y*y + z*z);
    // 零轴, 返回单位矩阵
    if (len < epsilon) {
        return result;   
    }
    x /= len;
    y /= len;
    z /= len;

    Ty c = std::cos(radians);
    Ty s = std::sin(radians);
    Ty t = Ty(1) - c;

    // 罗德里格斯公式展开
    result.m[0][0] = x * x * t + c;
    result.m[0][1] = x * y * t - z * s;
    result.m[0][2] = x * z * t + y * s;

    result.m[1][0] = y * x * t + z * s;
    result.m[1][1] = y * y * t + c;
    result.m[1][2] = y * z * t - x * s;

    result.m[2][0] = z * x * t - y * s;
    result.m[2][1] = z * y * t + x * s;
    result.m[2][2] = z * z * t + c;

    return result;
}


template <typename Ty, size_t N>
constexpr static details::TMatrix<Ty, N, N> Get_Translation_Matrix(const Ty (&offset)[N]) noexcept
{
    auto result = details::TMatrix<Ty, N, N>::identity();
    if constexpr (N == 4)
    {
        result.m[0][3] = offset[0];
        result.m[1][3] = offset[1];
        result.m[2][3] = offset[2];
    }
    else
    {
        for (size_t i = 0; i < N-1; ++i) {
            result.m[i][N-1] = offset[i];
        }
    }
    return result;
}
template <typename Ty, size_t N>
constexpr static details::TMatrix<Ty, N, N> Get_Translation_Matrix(const vec<Ty, N>& offset) noexcept
{
    return Get_Translation_Matrix(offset.coordinates);
}
}
/**
 * @brief 将输入 mat 进行平移变换, 返回一个新的矩阵:
 */
template <typename Ty, size_t N>
constexpr static details::TMatrix<Ty, N, N> translate(details::TMatrix<Ty, N, N> mat, const Ty (&offset)[N]) noexcept
{
    return mat.multiply(details::Get_Translation_Matrix(offset));
}

template <typename Ty, size_t N>
constexpr static details::TMatrix<Ty, N, N> translate(details::TMatrix<Ty, N, N> mat, const vec<Ty, N>& offset) noexcept
{
    return mat.multiply(details::Get_Translation_Matrix(offset));
}

template <typename Ty, typename... Args>
constexpr inline details::TMatrix<Ty, sizeof...(Args), sizeof...(Args)> scale(Args... args) noexcept
{
	static_assert((std::is_convertible_v<Args, Ty> && ...), "All arguments must be convertible to Ty");
	using matix = details::TMatrix<Ty, sizeof...(Args), sizeof...(Args)>;
	matix result = matix::zero();

	for (size_t i = 0; i < sizeof...(Args); ++i)
	{
        result.m[i][i] = static_cast<Ty>(std::get<i>(std::forward_as_tuple(args...)));
    }
	return result;
}

namespace details
{
    
template <typename Ty, size_t N, size_t... Is>
constexpr inline details::TMatrix<Ty, N, N> Scale_Impl(const Ty (&factors)[N], std::index_sequence<Is...>) noexcept
{
	return scale<Ty>(factors[Is]...);
}
template <typename Ty, size_t N, size_t... Is>
constexpr inline details::TMatrix<Ty, N, N> Scale_Impl(const std::array<Ty, N>& factors, std::index_sequence<Is...>) noexcept
{
	return scale<Ty>(factors[Is]...);
}

/**
 * @biref 获取 N 维 缩放矩阵: 缩放矩阵是一个对角矩阵, 第 i 个对角元为第 i 轴的缩放因子
 * @param factors C-style 缩放因子, 用于指定对应轴的缩放因子
 */ 
template <typename Ty, size_t N>
constexpr inline details::TMatrix<Ty, N, N> Get_Scaling_Matrix(const Ty (&factors)[N]) noexcept
{
	return details::Scale_Impl<Ty>(factors, std::make_index_sequence<N>{});
}

template <typename Ty, size_t N>
constexpr inline details::TMatrix<Ty, N, N> Get_Scaling_Matrix(const vec<Ty, N>& factors) noexcept
{
	return Get_Scaling_Matrix(factors.coordinates);
}
}

/**
 * @brief 将输入 mat 进行缩放变换, 返回一个新的矩阵:
 * @param factors C-style 缩放因子, 用于指定对应轴的缩放因子
 */
template <typename Ty, size_t N>
constexpr inline details::TMatrix<Ty, N, N> scale(details::TMatrix<Ty, N, N> mat, const vec<Ty, N>& factors) noexcept
{
    return mat.multiply(details::Get_Scaling_Matrix(factors));
}

template <typename Ty, size_t N>
constexpr inline details::TMatrix<Ty, N, N> scale(details::TMatrix<Ty, N, N> mat, const Ty (&factors)[N]) noexcept
{
	return mat * details::Get_Scaling_Matrix(factors);
}

template <typename Ty, size_t N>
details::TMatrix<Ty, N, N> operator+(const details::TMatrix <Ty, N, N> lhs, const details::TMatrix <Ty, N, N>& rhs) noexcept
{
    details::TMatrix<Ty, N, N> result = details::TMatrix<Ty, N, N>::zero();
    std::transform(&lhs.m[0][0], &lhs.m[0][0] + N*N, &rhs.m[0][0], &result.m[0][0], std::plus<>{});
    return result;
}
template <typename Ty, size_t N>
details::TMatrix <Ty, N, N> operator+(Ty lhs, const details::TMatrix <Ty, N, N>& rhs) noexcept
{
    details::TMatrix<Ty, N, N> result = details::TMatrix<Ty, N, N>::zero();
    std::for_each(&rhs.m[0][0], &rhs.m[0][0] + N*N, [lhs](Ty& v){ v += lhs; });
    return result;
}
template <typename Ty, size_t N>
details::TMatrix <Ty, N, N> operator+(const details::TMatrix <Ty, N, N> lhs, Ty rhs) noexcept
{
    details::TMatrix<Ty, N, N> result = details::TMatrix<Ty, N, N>::zero();
    std::for_each(&lhs.m[0][0], &lhs.m[0][0] + N*N, [rhs](Ty& v){ v += rhs; });
    return result;
}

template <typename Ty, size_t N>
details::TMatrix <Ty, N, N> operator-(const details::TMatrix <Ty, N, N> lhs, const details::TMatrix <Ty, N, N>& rhs) noexcept
{
    details::TMatrix<Ty, N, N> result = details::TMatrix<Ty, N, N>::zero();
    std::transform(&lhs.m[0][0], &lhs.m[0][0] + N*N, &rhs.m[0][0], &result.m[0][0], std::minus<>{});
    return result;
}
template <typename Ty, size_t N>
details::TMatrix <Ty, N, N> operator-(Ty lhs, const details::TMatrix <Ty, N, N>& rhs) noexcept
{
    details::TMatrix<Ty, N, N> result = details::TMatrix<Ty, N, N>::zero();
    std::for_each(&rhs.m[0][0], &rhs.m[0][0] + N*N, [lhs](Ty& v){ v = lhs - v; });
    return result;
}
template <typename Ty, size_t N>
details::TMatrix <Ty, N, N> operator-(const details::TMatrix <Ty, N, N> lhs, Ty rhs) noexcept
{
    details::TMatrix<Ty, N, N> result = details::TMatrix<Ty, N, N>::zero();
    std::for_each(&lhs.m[0][0], &lhs.m[0][0] + N*N, [rhs](Ty& v){ v -= rhs; });
    return result;
}

inline details::TMatrix<float, 3, 3> pitch_rotate(float radians) noexcept
{
    return details::Get_Rotate3D_Matrix(radians, 0.f, 1.f, 0.f);
}

inline details::TMatrix<float, 3, 3> yaw_rotate(float radians) noexcept
{
    return details::Get_Rotate3D_Matrix(radians, 0.f, 0.f, 1.f);
}

inline details::TMatrix<float, 3, 3> roll_rotate(float radians) noexcept
{
    return details::Get_Rotate3D_Matrix(radians, 1.f, 0.f, 0.f);
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


namespace core::math
{
struct quaternion
{
    vec4f self; // (x, y, z, w)

    float x() const noexcept { return self.x(); }
    float y() const noexcept { return self.y(); }
    float z() const noexcept { return self.z(); }
    float w() const noexcept { return self.w(); }

    quaternion() noexcept : self(0.f, 0.f, 0.f, 1.f) {}
    quaternion(float x, float y, float z, float w) noexcept : self(x, y, z, w) {}

    static quaternion axis_angle(const vec3f& axis, float radians) {
        float half = radians * 0.5f;
        float s = std::sin(half);
        return quaternion(std::cos(half), axis.x()*s, axis.y()*s, axis.z()*s);
    }

    quaternion operator*(const quaternion& q) const {
        return quaternion(
            self.w()*q.w() - self.x()*q.x() - self.y()*q.y() - self.z()*q.z(),
            self.w()*q.x() + self.x()*q.w() + self.y()*q.z() - self.z()*q.y(),
            self.w()*q.y() - self.x()*q.z() + self.y()*q.w() + self.z()*q.x(),
            self.w()*q.z() + self.x()*q.y() - self.y()*q.x() + self.z()*q.w()
        );
    }

    // @brief 共轭: 对于单位四元数, 共轭等价于逆. 共轭四元数表示相反的旋转.
    quaternion conj() const { return quaternion(w(), -x(), -y(), -z()); }

        // 旋转向量
    vec3f rotate(const vec3f& v) const 
    {
        // v' = q * v * q^{-1}，单位四元数时 q^{-1} = conj(q)
        quaternion qv(0, v.x(), v.y(), v.z());
        quaternion result = (*this) * qv * this->conj();
        return vec3f(result.x(), result.y(), result.z());
    }

    // 归一化
    void normalize() 
    {
        float len = std::sqrt(w()*w() + x()*x() + y()*y() + z()*z());
        if (len > tiny) 
        {
            self = vec4f(x()/len, y()/len, z()/len, w()/len);
        }
    }

    // 点积
    float dot(const quaternion& q) const 
    {
        return w()*q.w() + x()*q.x() + y()*q.y() + z()*q.z();
    }

    // 球面线性插值
    static quaternion slerp(const quaternion& a, const quaternion& b, float t) 
    {
        float cos_omega = a.dot(b);
        quaternion end = b;
        // 如果点积为负，取相反路径以确保最短弧
        if (cos_omega < 0.0f) 
        {
            end = quaternion(-b.w(), -b.x(), -b.y(), -b.z());
            cos_omega = -cos_omega;
        }

        float k0, k1;
        if (cos_omega > 0.9999f) { // 线性插值
            k0 = 1.0f - t;
            k1 = t;
        } else {
            float sin_omega = std::sqrt(1.0f - cos_omega*cos_omega);
            float omega = std::atan2(sin_omega, cos_omega);
            float inv_sin = 1.0f / sin_omega;
            k0 = std::sin((1.0f - t)*omega) * inv_sin;
            k1 = std::sin(t*omega) * inv_sin;
        }
        return quaternion(
            k0*a.w() + k1*end.w(),
            k0*a.x() + k1*end.x(),
            k0*a.y() + k1*end.y(),
            k0*a.z() + k1*end.z()
        );
    }
};
struct rotator
{
    rotator() noexcept : quat(1.0f, 0.0f, 0.0f, 0.0f) {}
    float pitch() const noexcept 
    {
        float w = quat.x();
        float x = quat.y();
        float y = quat.z();
        float z = quat.w();
        float sinp = 2.0f * (w * x - y * z);
        if (std::abs(sinp) >= 1.0f)
        {
            return std::copysign(float(pi) / 2.0f, sinp);
        }
        return std::asin(sinp);
    }
    
    float yaw() const noexcept 
    {
        float w = quat.x();
        float x = quat.y();
        float y = quat.z();
        float z = quat.w();

        float siny = 2.0f * (w * y + x * z);
        float cosy = 1.0f - 2.0f * (y * y + x * x);
        return std::atan2(siny, cosy);
    }
    
    float roll() const noexcept 
    {
        float w = quat.x();
        float x = quat.y();
        float y = quat.z();
        float z = quat.w();

        float sinr = 2.0f * (w * z + x * y);
        float cosr = 1.0f - 2.0f * (z * z + x * x);
        return std::atan2(sinr, cosr);
    }

    /**
     * @brief 从欧拉角创建旋转器
     * @param pitch 绕 X 轴旋转角度
     * @param yaw 绕 Y 轴旋转角度
     * @param roll 绕 Z 轴旋转角度
     * @return 对应的旋转器
     */
    static rotator from_euler(float pitch, float yaw, float roll) noexcept
    {
        // 采用 extrinsic Z-Y-X 顺序：先 roll (Z), 再 yaw (Y), 最后 pitch (X)
        // 组合四元数 q = q_pitch * q_yaw * q_roll
        quaternion q_roll = quaternion::axis_angle(vec3f(0.0f, 0.0f, 1.0f), roll);
        quaternion q_yaw  = quaternion::axis_angle(vec3f(0.0f, 1.0f, 0.0f), yaw);
        quaternion q_pitch= quaternion::axis_angle(vec3f(1.0f, 0.0f, 0.0f), pitch);
        rotator r;
        r.quat = q_pitch * q_yaw * q_roll;
        return r;
    }

    /**
     * @brief 从旋转矩阵中创建旋转器
     * @param m 旋转矩阵 (行主序)
     * @return 对应的旋转器
     */
    static rotator from_matrix(const mat3f& m) noexcept
    {
       // m 行主序，m(row, col)
        float m00 = m.m[0][0], m01 = m.m[0][1], m02 = m.m[0][2];
        float m10 = m.m[1][0], m11 = m.m[1][1], m12 = m.m[1][2];
        float m20 = m.m[2][0], m21 = m.m[2][1], m22 = m.m[2][2];

        float trace = m00 + m11 + m22;
        float w, x, y, z;

        if (trace > 0.0f) 
        {
            float s = std::sqrt(trace + 1.0f) * 2.0f; // s = 4*w
            w = s / 4.0f;
            x = (m21 - m12) / s;
            y = (m02 - m20) / s;
            z = (m10 - m01) / s;
        } 
        else if (m00 > m11 && m00 > m22) 
        {
            float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f; // s = 4*x
            w = (m21 - m12) / s;
            x = s / 4.0f;
            y = (m01 + m10) / s;
            z = (m02 + m20) / s;
        } 
        else if (m11 > m22) 
        {
            float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f; // s = 4*y
            w = (m02 - m20) / s;
            x = (m01 + m10) / s;
            y = s / 4.0f;
            z = (m12 + m21) / s;
        } 
        else 
        {
            float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f; // s = 4*z
            w = (m10 - m01) / s;
            x = (m02 + m20) / s;
            y = (m12 + m21) / s;
            z = s / 4.0f;
        }

        rotator r;
        // 注意构造顺序: quaternion(实部w, 虚部x, 虚部y, 虚部z)
        r.quat = quaternion(w, x, y, z);
        r.normalize();
        return r;
    }

    /**
     * @brief 从轴角创建旋转器
     * @param axis 旋转轴
     * @param angle 旋转角度
     * @return 对应的旋转器
     */
    static rotator from_axis_angle(vec3f axis, float angle) noexcept
    {
        rotator r;
        r.quat = quaternion::axis_angle(axis, angle);
        return r;
    }

    static rotator from_vectors(vec3f from, vec3f to) noexcept
    {
       from = from.normalized();
        to = to.normalized();
        float dot = vec3f::dot(from, to);
        if (dot > 0.9999f) 
        {
            // 几乎同向，返回单位旋转
            return rotator();
        }
        if (dot < -0.9999f) 
        {
            // 反向，找正交轴旋转180°
            vec3f axis = vec3f::cross(from, vec3f(1.0f, 0.0f, 0.0f));
            if (axis.length() < tiny)
            {
                axis = vec3f::cross(from, vec3f(0.0f, 1.0f, 0.0f));
            }
            axis = axis.normalized();
            return from_axis_angle(axis, float(pi));
        }
        vec3f axis = vec3f::cross(from, to).normalized();
        float angle = std::acos(dot);
        return from_axis_angle(axis, angle);
    }

    static rotator from_string(const std::string& str) noexcept
    {
       
    }
    static rotator step(rotator current, rotator target, float step_size) noexcept
    {
       // 朝 target 旋转，最大角度 step_size 弧度
        float dot = current.quat.x() * target.quat.x()
                + current.quat.y() * target.quat.y()
                + current.quat.z() * target.quat.z()
                + current.quat.w() * target.quat.w();
        // 确保取最短路径
        if (dot < 0.0f) 
        {
            dot = -dot;
            // 注意：此时不能直接翻转 target，在 slerp 中会处理
        }
        // 四元数点积 = cos(θ/2)，实际角度 = 2*acos(|dot|)
        float angle = 2.0f * std::acos(std::min(dot, 1.0f));
        if (angle < step_size + tiny) 
        {
            return target;
        }
        float t = step_size / angle;
        rotator result;
        result.quat = quaternion::slerp(current.quat, target.quat, t);
        return result;
    }

    rotator& operator+=(const rotator& other) noexcept
    {
        quat = quat * other.quat;
        return *this;
    }

    rotator& operator-=(const rotator& other) noexcept
    {
        quat = quat * other.quat.conj();
        return *this;
    }

    bool operator==(const rotator& other) const noexcept {
        return equals(other);
    }

    bool operator!=(const rotator& other) const noexcept {
        return !equals(other);
    }
    std::partial_ordering operator<=>(const rotator& other) noexcept
    {
        if (equals(other))
        {
            return std::partial_ordering::equivalent;
        }
        return std::partial_ordering::unordered;
    }

    /**
     * @brief 判断两个旋转器是否相等
     * @param other 另一个旋转器
     * @param epsilon 允许的误差
     * @return 是否相等
     * @about 四元数双覆盖
     */
    bool equals(const rotator& other, float epsilon = tiny) const noexcept
    {
        return (std::abs(quat.x() - other.quat.x()) < epsilon &&
            std::abs(quat.y() - other.quat.y()) < epsilon &&
            std::abs(quat.z() - other.quat.z()) < epsilon &&
            std::abs(quat.w() - other.quat.w()) < epsilon)
        || (std::abs(quat.x() + other.quat.x()) < epsilon &&
            std::abs(quat.y() + other.quat.y()) < epsilon &&
            std::abs(quat.z() + other.quat.z()) < epsilon &&
            std::abs(quat.w() + other.quat.w()) < epsilon);
    }

    friend uint32_t hash(rotator r) noexcept
    {
        auto h = [](float f) -> uint32_t 
        {
            return static_cast<uint32_t>(std::hash<float>()(f));
        };
        return h(r.quat.x()) ^ (h(r.quat.y()) << 1) ^ (h(r.quat.z()) << 2) ^ (h(r.quat.w()) << 3);
    }
    friend rotator operator+(const rotator& lhs, const rotator& rhs) noexcept
    {
        rotator res = lhs;
        res += rhs;
        return res;
    }
    friend rotator operator-(const rotator& lhs, const rotator& rhs) noexcept
    {
        rotator res = lhs;
        res -= rhs;
        return res;
    }

    friend rotator operator*(const rotator& r, float scalar)
    {
        rotator res;
        float w = r.quat.x();   // 实部
        float x = r.quat.y();
        float y = r.quat.z();
        float z = r.quat.w();
        float angle = 2.0f * std::acos(std::clamp(w, -1.0f, 1.0f));
        if (angle < tiny) 
        {
            res.quat = quaternion(1.0f, 0.0f, 0.0f, 0.0f);
            return res;
        }
        float half = angle * scalar * 0.5f;
        float s = std::sin(half) / std::sin(angle * 0.5f); // 归一化轴
        res.quat = quaternion(std::cos(half), x * s, y * s, z * s);
        return res;
    }
    friend rotator operator*(float scalar, const rotator& r)
    {
        return r * scalar;
    }

    rotator normalized() const noexcept
    {
        rotator res = *this;
        res.normalize();
        return res;
    }
    rotator inverse() const noexcept
    {
        rotator res;
        res.quat = quat.conj();
        return res;
    }
    void normalize() noexcept
    {
        quat.normalize();
    }
    bool is_normalized() const noexcept
    {
        float len = std::sqrt(quat.x()*quat.x() + quat.y()*quat.y() + quat.z()*quat.z() + quat.w()*quat.w());
        return std::abs(len - 1.0f) < tiny;
    }
    bool valid() const noexcept
    {
        return std::isfinite(quat.x()) && std::isfinite(quat.y()) &&
           std::isfinite(quat.z()) && std::isfinite(quat.w());
    }

    vec3f get_forward_vector() const noexcept
    {
        // 假设前向为 +X
        return quat.rotate(vec3f(1.0f, 0.0f, 0.0f));
    }
    vec3f get_right_vector() const noexcept
    {
        // 假设右向为 +Y
        return quat.rotate(vec3f(0.0f, 1.0f, 0.0f));
    }
    vec3f get_up_vector() const noexcept
    {
        // 假设上向为 +Z
        return quat.rotate(vec3f(0.0f, 0.0f, 1.0f));
    }

    // 返回从 other 旋转到 this 的旋转差
    rotator diff(rotator other) noexcept
    {
        rotator res;
        res.quat = quat * other.quat.conj();
        return res;
    }

    quaternion to_quaternion() const noexcept
    {
        return quat;
    }
    mat3f to_matrix3f() const noexcept
    {
         // 行主序矩阵，行向量变换
        vec3f right = get_right_vector();
        vec3f up = get_up_vector();
        vec3f forward = get_forward_vector();
        // 假设 mat3f 可通过构造函数或赋值填充
        mat3f m;
        m.m[0][0] = right.x();  m.m[0][1] = right.y();  m.m[0][2] = right.z();
        m.m[1][0] = up.x();     m.m[1][1] = up.y();     m.m[1][2] = up.z();
        m.m[2][0] = forward.x();m.m[2][1] = forward.y();m.m[2][2] = forward.z();
        return m;
    }
    mat4f to_matrix4f() const noexcept
    {
        mat4f m;
        // 左上 3x3 旋转部分（行主序）
        vec3f right = get_right_vector();
        vec3f up = get_up_vector();
        vec3f forward = get_forward_vector();
        m.m[0][0] = right.x();  m.m[0][1] = right.y();  m.m[0][2] = right.z();   m.m[0][3] = 0.0f;
        m.m[1][0] = up.x();     m.m[1][1] = up.y();     m.m[1][2] = up.z();      m.m[1][3] = 0.0f;
        m.m[2][0] = forward.x();m.m[2][1] = forward.y();m.m[2][2] = forward.z(); m.m[2][3] = 0.0f;
        m.m[3][0] = 0.0f;       m.m[3][1] = 0.0f;       m.m[3][2] = 0.0f;        m.m[3][3] = 1.0f;
        return m;
    }
    std::string to_string() const noexcept;

    quaternion data() const noexcept
    {
        return quat;
    }
     quaternion data() noexcept
    {
        return quat;
    }
private:
    quaternion quat;
};

inline rotator clamp(rotator value, rotator min, rotator max) noexcept
{
    // 提取欧拉角（Z-Y-X 顺序：roll, yaw, pitch）
    float pitch = value.pitch();
    float yaw   = value.yaw();
    float roll  = value.roll();

    // 逐分量钳制
    pitch = std::clamp(pitch, min.pitch(), max.pitch());
    yaw   = std::clamp(yaw,   min.yaw(),   max.yaw());
    roll  = std::clamp(roll,  min.roll(),  max.roll());

    // 重建旋转器
    return rotator::from_euler(pitch, yaw, roll);
}

inline bool approx_zero(rotator r, float epsilon = 1e-6f) noexcept
{
    // 通过四元数的实部计算旋转角度，再与阈值比较
    quaternion q = r.to_quaternion();
    float w = std::abs(q.x());          // 实部
    // 角度 = 2 * acos(|w|)，注意 cos(0) = 1, cos(π) = 0 但 θ=0 或 2π 都对应 w = ±1
    // 这里用 1 - |w| 近似小角度，或直接计算角度避免分支
    float cos_half_angle = std::min(w, 1.0f);
    float angle = 2.0f * std::acos(cos_half_angle);
    return angle < epsilon;
}

inline rotator lerp(rotator a, rotator b, float t) noexcept
{
    quaternion qa = a.to_quaternion();
    quaternion qb = b.to_quaternion();

    // 取最短路径
    if (qa.dot(qb) < 0.0f) 
    {
        qb = quaternion(-qb.x(), -qb.y(), -qb.z(), -qb.w());
    }

    // 线性混合
    quaternion qr(
        qa.x() + t * (qb.x() - qa.x()),
        qa.y() + t * (qb.y() - qa.y()),
        qa.z() + t * (qb.z() - qa.z()),
        qa.w() + t * (qb.w() - qa.w())
    );
    qr.normalize();

    rotator res;
    // 将结果写回 rotator（需要访问私有成员，这里假设提供相应构造函数或通过 from_quaternion；由于 friend 关系，可直接设置 quat）
    // 简单起见，这里展示构建方式，实际可以在 rotator 中增加 from_quaternion 或直接赋值
    // 下面假设 rotator 有一个接受 quaternion 的私有构造函数或直接设置 quat
    res.data() = qr;
    return res;
}
inline rotator slerp(rotator a, rotator b, float t) noexcept
{
    rotator res;
    res.data() = quaternion::slerp(a.to_quaternion(), b.to_quaternion(), t);
    return res;
}
inline rotator diff(rotator a, rotator b) noexcept
{
     return b - a;
}
}


namespace core::geometry
{
using point2d = math::vec2f;
using point3d = math::vec3f;

struct circle;
struct line3d;
struct line2d;
struct triangle;
struct sphere;
struct standing_rectangle;
struct standing_cube;
struct standing_cylinder;
struct capsule;
struct cube;
struct cylinder;

struct line2d
{
    point2d start;
    point2d end;

    /**
     * @brief 判断点是否在线段上
     * @param point 待判断的点
     * @param inclusive 是否包含端点, true 则只计算 start 和 end 之间; false 则仅计算是否共线
     */
    bool contains(point2d point, bool inclusive = false) const noexcept
    {
        return math::is_collinear(point, start, end) && 
            (inclusive ? math::is_between(point, start, end) : true);
    }

    float length() const noexcept
    {
        return math::distance(start, end);
    }

    float angle() const noexcept
    {
        return math::angle(start, end);
    }

    float radians() const noexcept
    {
        return math::radians(start, end);
    }

};

struct line3d
{
    point3d start;
    point3d end;

    /**
     * @brief 判断点是否在线段上
     * @param point 待判断的点
     * @param inclusive 是否包含端点, true 则只计算 start 和 end 之间; false 则仅计算是否共线
     */
    bool contains(point3d point, bool inclusive = false) const noexcept
    {
        return math::is_collinear(point, start, end) && 
            (inclusive ? math::is_between(point, start, end) : true);
    }

    float length() const noexcept
    {
        return math::distance(start, end);
    }
};


inline bool is_point_in_circle(point2d point, circle c) noexcept;
struct circle 
{
    point2d center;
    float radius;

    /**
     * @brief 判断点是否在圆内
     * @param point 待判断的点
     */
    bool contains(point2d point) const noexcept
    {
        return is_point_in_circle(point, *this);
    }
};

inline bool is_point_in_circle(point2d point, circle c) noexcept
{
    float dx = point.x() - c.center.x();
    float dy = point.y() - c.center.y();
    return (dx * dx + dy * dy) <= (c.radius * c.radius);
}
struct standing_rectangle
{
    point2d position; // 左下角坐标
    point2d size;     // 宽度和高度
    bool contains(point2d point) const noexcept {
        return point.x() >= position.x() &&
               point.x() <= position.x() + size.x() &&
               point.y() >= position.y() &&
               point.y() <= position.y() + size.y();
    }
};

// !TODO: 这里的 point2d 应该是 point3d 因为是空间 
struct triangle
{
    point2d p1;
    point2d p2;
    point2d p3;

    /**
     * @brief 重心坐标是点相对于三角形顶点的线性组合系数, 可以用来判断点在三角形内的位置关系.
     * @param p 待计算重心坐标的点
     * @return 返回重心坐标 (alpha, beta, gamma)
     */
    point3d barycentric(point2d p) const noexcept
    {
        float v0x = p2.x() - p1.x(), v0y = p2.y() - p1.y();
        float v1x = p3.x() - p1.x(), v1y = p3.y() - p1.y();
        float v2x = p.x()  - p1.x(), v2y = p.y()  - p1.y();

        float d00 = v0x * v0x + v0y * v0y;
        float d01 = v0x * v1x + v0y * v1y;
        float d11 = v1x * v1x + v1y * v1y;
        float d20 = v2x * v0x + v2y * v0y;
        float d21 = v2x * v1x + v2y * v1y;

        float denom = d00 * d11 - d01 * d01;

        // 退化三角形 (面积接近 0)
        if (std::abs(denom) < 1e-12f)
        {
            return {1.0f, 0.0f, 0.0f}; // 可任意返回，这里默认落在 p1
        }
            
        float inv_denom = 1.0f / denom;
        float beta  = (d11 * d20 - d01 * d21) * inv_denom;
        float gamma = (d00 * d21 - d01 * d20) * inv_denom;
        float alpha = 1.0f - beta - gamma;

        return {alpha, beta, gamma};
    }

    bool contains(point2d point) const noexcept
    {
        point3d bary = barycentric(point);
        return bary.x() >= -math::tiny && bary.y() >= -math::tiny && bary.z() >= -math::tiny;
    }
};

/**
 * @brief 在三角形内插值, 根据点的重心坐标计算对应的 y 值
 * @param tri 三角形
 * @param point 待插值的点
 * @param v1, v2, v3 分别对应三角形三个顶点的属性值
 * @return 返回插值结果   
 */
template<typename Ty>
inline Ty interpolate(const triangle& tri, math::vec2f point, Ty v1, Ty v2, Ty v3) noexcept
{
    auto [alpha, beta, gamma] = tri.barycentric(point).coordinates;
    return alpha * v1 + beta * v2 + gamma * v3;
}

struct sphere
{
    point3d center; // 中心坐标
    float radius;       // 半径

    bool contains(point3d point) const noexcept
    {
        float dx = point.x() - center.x();
        float dy = point.y() - center.y();
        float dz = point.z() - center.z();
        return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
    }
};

struct standing_cube
{
    point3d position; // 左下角坐标
    point3d size;     // 宽度、高度和深度

    bool contains(point3d point) const noexcept
    {
        return point.x() >= position.x() &&
               point.x() <= position.x() + size.x() &&
               point.y() >= position.y() &&
               point.y() <= position.y() + size.y() &&
               point.z() >= position.z() &&
               point.z() <= position.z() + size.z();
    }
};

inline bool is_point_in_cylinder(point3d point, standing_cylinder c) noexcept;
struct standing_cylinder
{
    point3d base_center; // 底面圆心坐标
    float radius;        // 半径
    float height;        // 高度

    bool contains(point3d point) const noexcept
    {
        is_point_in_cylinder(point, *this);
    }
};

inline bool is_point_in_cylinder(point3d point, standing_cylinder c) noexcept
{
    bool is_in_base_circle = is_point_in_circle(
        {point.x(), point.z()}, 
        {{c.base_center.x(), c.base_center.z()}, c.radius}
    );
    bool is_in_height = point.y() >= c.base_center.y() && point.y() <= c.base_center.y() + c.height;
    return is_in_base_circle && is_in_height;
}
struct standing_capsule
{
    point3d bottom_point; // 底面圆心坐标
    point3d top_point; // 顶面圆心坐标
    float radius;   // 半径

    bool contains(point3d point) const noexcept
    {
       return is_point_in_cylinder(
               point,
               {bottom_point, radius, std::abs(top_point.z() - bottom_point.z())}
           ) ||
           sphere{bottom_point, radius}.contains(point) ||
           sphere{top_point, radius}.contains(point);
    }
};

struct cube
{

};

struct cylinder
{

};

struct capsule
{

};


}
