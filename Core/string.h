
#pragma once

#include <compare>
#include <memory>
#include <string>
#include <string_view>
#include <format>
#include <atomic>

#include "common.h"
#include "std_interface.h"
namespace core
{

using std::string;
using std::string_view;

class string_builder;

template <typename CharType, size_t N, typename Traits = std::char_traits<CharType>>
class basic_fixed_string
{
private:
	friend class string_builder;

public:
	using value_type = CharType;
	using traits_type = Traits;
	using size_type = size_t;
	using reference = value_type&;
	using const_reference = const value_type&;
	using pointer = value_type*;
	using const_pointer = const value_type*;
	using difference_type = std::ptrdiff_t;

private:
	template <bool IsConst>
	class iterator_impl {
		friend class basic_fixed_string;

		using pointer = std::conditional_t<IsConst, const basic_fixed_string*, basic_fixed_string*>;
		using reference = std::conditional_t<IsConst, const value_type&, value_type&>;
		using buffer_type = std::conditional_t<IsConst, const basic_fixed_string, basic_fixed_string>;
		using value_type = std::conditional_t<IsConst, const value_type, value_type>;

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
	using iterator               = iterator_impl<false>;
	using const_iterator         = iterator_impl<true>;
	using reverse_iterator       = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	constexpr size_type npos() const noexcept { return N; }

	basic_fixed_string() noexcept = default;
	basic_fixed_string(const value_type* str) noexcept
	{
		
	}

	template <typename Range>
	basic_fixed_string(std::from_range_t, Range&& range) noexcept
	{
		
	}

	basic_fixed_string(size_type count, value_type ch) noexcept
	{

	}

	template <typename InputIter>
	basic_fixed_string(InputIter first, InputIter last) noexcept
	{
		
	}

	template <typename Alloc = std::allocator<value_type>>
	basic_fixed_string(std::basic_string<value_type, traits_type, Alloc> str) noexcept
	{
		
	}

	basic_fixed_string(std::basic_string_view<value_type, traits_type> str) noexcept
	{
		
	}

	basic_fixed_string(const basic_fixed_string&) = default;
	basic_fixed_string& operator=(const basic_fixed_string&) = default;
	basic_fixed_string(basic_fixed_string&&) noexcept = default;
	basic_fixed_string& operator=(basic_fixed_string&&) noexcept = default;

	value_type& operator[](size_type pos) noexcept
	{
		
	}

	bool operator==(const basic_fixed_string& other) const noexcept
	{
		
	}

	std::strong_ordering operator<=>(const basic_fixed_string& other) const noexcept
	{
		
	}
public:
	value_type at(size_type pos) const
	{
		
	}

	template <typename Alloc = std::allocator<value_type>>
	size_type find(const std::basic_string<value_type, traits_type, Alloc>& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type find(const basic_fixed_string& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type find(const value_type* s, size_type pos, size_type count) const noexcept
	{
		
	}

	size_type find(const std::basic_string_view<value_type, traits_type>& str, size_type pos = 0) const noexcept
	{
		
	}

	template <typename Alloc = std::allocator<value_type>>
	size_type rfind(const std::basic_string<value_type, traits_type, Alloc>& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type rfind(const basic_fixed_string& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type rfind(const value_type* s, size_type pos, size_type count) const noexcept
	{
		
	}

	size_type rfind(const std::basic_string_view<value_type, traits_type>& str, size_type pos = 0) const noexcept
	{
		
	}

	template <typename Alloc = std::allocator<value_type>>
	size_type find_first_of(const std::basic_string<value_type, traits_type, Alloc>& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type find_first_of(const basic_fixed_string& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type find_first_of(const value_type* s, size_type pos, size_type count) const noexcept
	{
		
	}

	size_type find_first_of(const std::basic_string_view<value_type, traits_type>& str, size_type pos = 0) const noexcept
	{
		
	}

	template <typename Alloc = std::allocator<value_type>>
	size_type find_last_of(const std::basic_string<value_type, traits_type, Alloc>& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type find_last_of(const basic_fixed_string& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type find_last_of(const value_type* s, size_type pos, size_type count) const noexcept
	{
		
	}

	size_type find_last_of(const std::basic_string_view<value_type, traits_type>& str, size_type pos = 0) const noexcept
	{
		
	}

	template <typename Alloc = std::allocator<value_type>>
	size_type find_first_not_of(const std::basic_string<value_type, traits_type, Alloc>& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type find_first_not_of(const basic_fixed_string& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type find_first_not_of(const value_type* s, size_type pos, size_type count) const noexcept
	{
		
	}

	size_type find_first_not_of(const std::basic_string_view<value_type, traits_type>& str, size_type pos = 0) const noexcept
	{
		
	}

	template <typename Alloc = std::allocator<value_type>>
	size_type find_last_not_of(const std::basic_string<value_type, traits_type, Alloc>& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type find_last_not_of(const basic_fixed_string& str, size_type pos = 0) const noexcept
	{
		
	}

	size_type find_last_not_of(const value_type* s, size_type pos, size_type count) const noexcept
	{
		
	}

	size_type find_last_not_of(const std::basic_string_view<value_type, traits_type>& str, size_type pos = 0) const noexcept
	{
		
	}

	constexpr bool starts_with(const basic_fixed_string& str) const noexcept
	{
		
	}

	constexpr bool starts_with(const value_type* str) const noexcept
	{
		
	}

	constexpr bool starts_with(value_type str) const noexcept
	{
		
	}

	constexpr bool starts_with(const std::basic_string_view<value_type, traits_type>& str) const noexcept
	{
		
	}

	constexpr bool ends_with(const basic_fixed_string& str) const noexcept
	{
		
	}

	constexpr bool ends_with(const value_type* str) const noexcept
	{
		
	}

	constexpr bool ends_with(value_type str) const noexcept
	{
		
	}

	constexpr bool ends_with(const std::basic_string_view<value_type, traits_type>& str) const noexcept
	{
		
	}
	
	
	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static int32_t stoi(const basic_fixed_string<OtherCharType, M, OtherTraits>& str, std::size_t* pos = nullptr, int base = 10) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static int64_t stoll(const basic_fixed_string<OtherCharType, M, OtherTraits>& str, std::size_t* pos = nullptr, int base = 10) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static uint32_t stoui(const basic_fixed_string<OtherCharType, M, OtherTraits>& str, std::size_t* pos = nullptr, int base = 10) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static uint64_t stoull(const basic_fixed_string<OtherCharType, M, OtherTraits>& str, std::size_t* pos = nullptr, int base = 10) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static float stof(const basic_fixed_string<OtherCharType, M, OtherTraits>& str, std::size_t* pos = nullptr) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static double stod(const basic_fixed_string<OtherCharType, M, OtherTraits>& str, std::size_t* pos = nullptr) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static basic_fixed_string<OtherCharType, M, OtherTraits> to_string(int32_t value) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static basic_fixed_string<OtherCharType, M, OtherTraits> to_string(int64_t value) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static basic_fixed_string<OtherCharType, M, OtherTraits> to_string(uint32_t value) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static basic_fixed_string<OtherCharType, M, OtherTraits> to_string(uint64_t value) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static basic_fixed_string<OtherCharType, M, OtherTraits> to_string(float value) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static basic_fixed_string<OtherCharType, M, OtherTraits> to_string(double value) {}

	template <typename OtherCharType, size_t M, typename OtherTraits = std::char_traits<OtherCharType>>
	static basic_fixed_string<OtherCharType, M, OtherTraits> to_string(long double value) {}

	void clear() noexcept {}

	reference front() noexcept
	{
		
	}

	const_reference front() const noexcept
	{
		
	}

	reference back() noexcept
	{
		
	}

	const_reference back() const noexcept
	{
		
	}

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

private:
	value_type m_Buffer[N - 1] { 0 }; 
	size_type m_Size { 0 };
};

class string_builder
{
public:
	
};



// 如果 string 至少有一个字符并且所有字符都是字母或数字则返回 True,否则返回 False
template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline bool isalnum(const std::basic_string<CharType, Traits, Alloc>& str)
{
	
}

template <typename CharType, typename Traits = std::char_traits<CharType>>
inline bool isalnum(const std::basic_string_view<CharType, Traits>& str)
{
	
}


template <typename CharType, size_t N,typename Traits = std::char_traits<CharType>>
inline bool isalnum(const basic_fixed_string<CharType, N, Traits>& str)
{
	
}

// 如果 string 至少有一个字符并且所有字符都是字母则返回 True, 否则返回 False
template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline bool isalpha(const std::basic_string<CharType, Traits, Alloc>& str)
{
	
}

template <typename CharType, typename Traits = std::char_traits<CharType>>
inline bool isalpha(const std::basic_string_view<CharType, Traits>& str)
{
	
}

template <typename CharType, size_t N,typename Traits = std::char_traits<CharType>>
inline bool isalpha(const basic_fixed_string<CharType, N, Traits>& str)
{
	
}

// 如果 string 只包含数字则返回 True 否则返回 False.
template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline bool isdigit(const std::basic_string<CharType, Traits, Alloc>& str)
{
	
}

template <typename CharType, typename Traits = std::char_traits<CharType>>
inline bool isdigit(const std::basic_string_view<CharType, Traits>& str)
{
	
}


template <typename CharType, size_t N,typename Traits = std::char_traits<CharType>>
inline bool isdigit(const basic_fixed_string<CharType, N, Traits>& str)
{
	
}

template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline bool isnumeric(const std::basic_string<CharType, Traits, Alloc>& str)
{
	
}

template <typename CharType, typename Traits = std::char_traits<CharType>>
inline bool isnumeric(const std::basic_string_view<CharType, Traits>& str)
{
	
}


template <typename CharType, size_t N,typename Traits = std::char_traits<CharType>>
inline bool isnumeric(const basic_fixed_string<CharType, N, Traits>& str)
{
	
}


// 如果 string 中只包含空格，则返回 True，否则返回 False.
template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline bool isspace(const std::basic_string<CharType, Traits, Alloc>& str)
{
	
}

template <typename CharType, typename Traits = std::char_traits<CharType>>
inline bool isspace(const std::basic_string_view<CharType, Traits>& str)
{
	
}


template <typename CharType, size_t N,typename Traits = std::char_traits<CharType>>
inline bool isspace(const basic_fixed_string<CharType, N, Traits>& str)
{
	
}



template <typename CharType, size_t N, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline core::dynamic_array<std::basic_string<CharType, Traits, Alloc>, Alloc> split(const basic_fixed_string<CharType, N, Traits>& str, CharType delimiter) 
{
	
}

template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline core::dynamic_array<std::basic_string<CharType, Traits, Alloc>, Alloc> split(const std::basic_string<CharType, Traits, Alloc>& str, CharType delimiter) 
{
	
}

// 	截掉 string 左边的所有空格
template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline std::basic_string<CharType, Traits, Alloc> lstrip(const std::basic_string<CharType, Traits, Alloc>& str) 
{
	
}

// 	截掉 string 右边的所有空格
template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline std::basic_string<CharType, Traits, Alloc> rstrip(const std::basic_string<CharType, Traits, Alloc>& str) 
{
	
}

template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline std::basic_string<CharType, Traits, Alloc> strip(const std::basic_string<CharType, Traits, Alloc>& str) 
{
	
}

template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline std::basic_string<CharType, Traits, Alloc> counte(const std::basic_string<CharType, Traits, Alloc>& str, CharType ch) 
{
	
}


template <typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline std::basic_string<CharType, Traits, Alloc> counte(const std::basic_string<CharType, Traits, Alloc>& str, const std::basic_string<CharType, Traits, Alloc>& target) 
{
	
}

template <typename InputIter, typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline std::basic_string<CharType, Traits, Alloc> join(InputIter begin, InputIter end, CharType delimiter = '\0') 
{
	
}

template <typename InputIter, typename CharType, typename Traits = std::char_traits<CharType>, typename Alloc = std::allocator<CharType>>
inline std::basic_string<CharType, Traits, Alloc> join(InputIter begin, size_t size, CharType delimiter = '\0') 
{
	
} 


namespace details
{
using NameUidType = uint64_t;
constexpr NameUidType InvalidNameUid = 0;
static inline std::atomic<NameUidType> G_NameUid { 1 };

inline NameUidType GetNameUid()
{
	return G_NameUid.fetch_add(1, std::memory_order_relaxed);
}
}

struct name
{

};

struct tag
{

};

struct text
{

};

}