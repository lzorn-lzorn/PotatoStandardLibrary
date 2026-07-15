
#pragma once

#include <type_traits>
#include <cstdint>
#include <vector>
#include <memory>

#include "platform.h"
#include "exception_handler.h"

namespace core
{
template <typename Ty, class Allocator = std::allocator<Ty>>
using dynamic_array = std::vector<Ty, Allocator>;

template<typename E>
concept enum_flag = std::is_enum_v<E> && std::is_unsigned_v<std::underlying_type_t<E>>;

template<enum_flag E>
struct flags {
    using underlying = std::underlying_type_t<E>;
    underlying value = 0;

    constexpr flags() noexcept = default;
    constexpr flags(E e) noexcept : value(static_cast<underlying>(e)) {}
    explicit constexpr flags(underlying v) noexcept : value(v) {}

    constexpr flags operator|(flags other) const noexcept { return flags(value | other.value); }
    constexpr flags operator&(flags other) const noexcept { return flags(value & other.value); }
    constexpr flags operator~() const noexcept { return flags(~value); }

    constexpr bool operator==(flags other) const noexcept { return value == other.value; }
    constexpr bool operator!=(flags other) const noexcept { return value != other.value; }

    constexpr explicit operator bool() const noexcept { return value != 0; }
    constexpr bool has(E e) const noexcept { return (value & static_cast<underlying>(e)) != 0; }
    constexpr void set(E e) noexcept { value |= static_cast<underlying>(e); }
    constexpr void clear(E e) noexcept { value &= ~static_cast<underlying>(e); }
};

// 枚举 | 枚举
template<enum_flag E>
constexpr flags<E> operator|(E lhs, E rhs) noexcept {
    return flags<E>(lhs) | rhs;
}

// 枚举 | flags
template<enum_flag E>
constexpr flags<E> operator|(E lhs, flags<E> rhs) noexcept {
    return flags<E>(lhs) | rhs;
}

// flags | 枚举
template<enum_flag E>
constexpr flags<E> operator|(flags<E> lhs, E rhs) noexcept {
    return lhs | flags<E>(rhs);
}

}