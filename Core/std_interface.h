
#pragma once

#include <vector>
#include <memory>
namespace core
{

template <typename Ty, class Allocator = std::allocator<Ty>>
using dynamic_array = std::vector<Ty, Allocator>;

}