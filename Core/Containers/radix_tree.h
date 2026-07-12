#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace core::containers
{

template <
    typename ValueType,
    std::size_t KeyShift = 0,
    std::size_t Level0Bits = 16,
    std::size_t Level1Bits = 10,
    std::size_t Level2Bits = 10,
    std::size_t Level1NodeCapacity = 64,
    std::size_t Level2NodeCapacity = 512>
class RadixTreeMap
{
public:
    static_assert(std::is_pointer_v<ValueType>, "RadixTreeMap currently supports pointer value types only.");
    static_assert(Level0Bits > 0 && Level1Bits > 0 && Level2Bits > 0, "All radix levels must be non-zero.");
    static_assert(KeyShift < 64, "KeyShift must be less than 64.");
    static_assert(Level0Bits + Level1Bits + Level2Bits <= (64 - KeyShift), "Radix layout exceeds key width after shift.");
    static_assert(Level1NodeCapacity > 0 && Level2NodeCapacity > 0, "Pool capacities must be non-zero.");

    static constexpr std::size_t IndexedBits = Level0Bits + Level1Bits + Level2Bits;

    static constexpr std::size_t Level0Size = std::size_t{1} << Level0Bits;
    static constexpr std::size_t Level1Size = std::size_t{1} << Level1Bits;
    static constexpr std::size_t Level2Size = std::size_t{1} << Level2Bits;

    static constexpr std::uint64_t Level0Mask = (std::uint64_t{1} << Level0Bits) - 1;
    static constexpr std::uint64_t Level1Mask = (std::uint64_t{1} << Level1Bits) - 1;
    static constexpr std::uint64_t Level2Mask = (std::uint64_t{1} << Level2Bits) - 1;

    RadixTreeMap() = default;

    RadixTreeMap(const RadixTreeMap&) = delete;
    RadixTreeMap& operator=(const RadixTreeMap&) = delete;

    bool insertOrAssign(const std::uint64_t Key, const ValueType Value)
    {
        if (Value == nullptr)
        {
            erase(Key);
            return true;
        }

        std::size_t Level0Index = 0;
        std::size_t Level1Index = 0;
        std::size_t Level2Index = 0;
        if (!decodeKey(Key, Level0Index, Level1Index, Level2Index))
        {
            ++KeyOutOfRangeFailureCount;
            return false;
        }

        bool allocatedLevel1 = false;
        Level1Node*& level1Slot = Root[Level0Index];
        if (!level1Slot)
        {
            level1Slot = Level1Pool.acquire();
            if (!level1Slot)
            {
                ++PoolExhaustionFailureCount;
                return false;
            }

            allocatedLevel1 = true;
        }

        Level2Node*& level2Slot = level1Slot->Children[Level1Index];
        if (!level2Slot)
        {
            level2Slot = Level2Pool.acquire();
            if (!level2Slot)
            {
                ++PoolExhaustionFailureCount;
                if (allocatedLevel1 && level1Slot->UsedCount == 0)
                {
                    Level1Pool.release(level1Slot);
                    level1Slot = nullptr;
                }
                return false;
            }

            ++level1Slot->UsedCount;
        }

        ValueType& cell = level2Slot->Values[Level2Index];
        if (cell == nullptr)
        {
            cell = Value;
            ++level2Slot->UsedCount;
            ++Size;
            return true;
        }

        cell = Value;
        return true;
    }

    [[nodiscard]] ValueType find(const std::uint64_t Key) const noexcept
    {
        std::size_t Level0Index = 0;
        std::size_t Level1Index = 0;
        std::size_t Level2Index = 0;
        if (!decodeKey(Key, Level0Index, Level1Index, Level2Index))
        {
            return nullptr;
        }

        const Level1Node* level1Slot = Root[Level0Index];
        if (!level1Slot)
        {
            return nullptr;
        }

        const Level2Node* level2Slot = level1Slot->Children[Level1Index];
        if (!level2Slot)
        {
            return nullptr;
        }

        return level2Slot->Values[Level2Index];
    }

    [[nodiscard]] bool contains(const std::uint64_t Key) const noexcept
    {
        return find(Key) != nullptr;
    }

    bool erase(const std::uint64_t Key) noexcept
    {
        std::size_t Level0Index = 0;
        std::size_t Level1Index = 0;
        std::size_t Level2Index = 0;
        if (!decodeKey(Key, Level0Index, Level1Index, Level2Index))
        {
            return false;
        }

        Level1Node*& level1Slot = Root[Level0Index];
        if (!level1Slot)
        {
            return false;
        }

        Level2Node*& level2Slot = level1Slot->Children[Level1Index];
        if (!level2Slot)
        {
            return false;
        }

        ValueType& cell = level2Slot->Values[Level2Index];
        if (cell == nullptr)
        {
            return false;
        }

        cell = nullptr;
        --Size;
        --level2Slot->UsedCount;

        if (level2Slot->UsedCount == 0)
        {
            Level2Pool.release(level2Slot);
            level2Slot = nullptr;
            --level1Slot->UsedCount;
        }

        if (level1Slot->UsedCount == 0)
        {
            Level1Pool.release(level1Slot);
            level1Slot = nullptr;
        }

        return true;
    }

    void clear() noexcept
    {
        for (Level1Node*& level1Slot : Root)
        {
            if (!level1Slot)
            {
                continue;
            }

            for (Level2Node*& level2Slot : level1Slot->Children)
            {
                if (!level2Slot)
                {
                    continue;
                }

                Level2Pool.release(level2Slot);
                level2Slot = nullptr;
            }

            Level1Pool.release(level1Slot);
            level1Slot = nullptr;
        }

        Size = 0;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return Size;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return Size == 0;
    }

    [[nodiscard]] std::size_t level1NodesCapacity() const noexcept
    {
        return Level1Pool.capacity();
    }

    [[nodiscard]] std::size_t level1NodesUsed() const noexcept
    {
        return Level1Pool.used();
    }

    [[nodiscard]] std::size_t level2NodesCapacity() const noexcept
    {
        return Level2Pool.capacity();
    }

    [[nodiscard]] std::size_t level2NodesUsed() const noexcept
    {
        return Level2Pool.used();
    }

    [[nodiscard]] std::size_t poolExhaustionFailureCount() const noexcept
    {
        return PoolExhaustionFailureCount;
    }

    [[nodiscard]] std::size_t keyOutOfRangeFailureCount() const noexcept
    {
        return KeyOutOfRangeFailureCount;
    }

private:
    struct Level2Node
    {
        Level2Node* PoolNext = nullptr;
        std::array<ValueType, Level2Size> Values{};
        std::size_t UsedCount = 0;

        void reset() noexcept
        {
            Values.fill(nullptr);
            UsedCount = 0;
        }
    };

    struct Level1Node
    {
        Level1Node* PoolNext = nullptr;
        std::array<Level2Node*, Level1Size> Children{};
        std::size_t UsedCount = 0;

        void reset() noexcept
        {
            Children.fill(nullptr);
            UsedCount = 0;
        }
    };

    template <typename NodeType, std::size_t Capacity>
    class FixedNodePool
    {
    public:
        static_assert(Capacity > 0, "Pool capacity must be non-zero.");

        FixedNodePool()
        {
            reset();
        }

        FixedNodePool(const FixedNodePool&) = delete;
        FixedNodePool& operator=(const FixedNodePool&) = delete;

        [[nodiscard]] NodeType* acquire() noexcept
        {
            if (!FreeList)
            {
                return nullptr;
            }

            NodeType* node = FreeList;
            FreeList = node->PoolNext;
            node->PoolNext = nullptr;
            node->reset();
            --FreeCount;
            return node;
        }

        void release(NodeType* Node) noexcept
        {
            if (!Node)
            {
                return;
            }

            Node->reset();
            Node->PoolNext = FreeList;
            FreeList = Node;
            ++FreeCount;
        }

        [[nodiscard]] constexpr std::size_t capacity() const noexcept
        {
            return Capacity;
        }

        [[nodiscard]] std::size_t freeCount() const noexcept
        {
            return FreeCount;
        }

        [[nodiscard]] std::size_t used() const noexcept
        {
            return Capacity - FreeCount;
        }

    private:
        void reset() noexcept
        {
            FreeList = nullptr;
            FreeCount = 0;
            for (NodeType& node : Storage)
            {
                node.reset();
                node.PoolNext = FreeList;
                FreeList = &node;
                ++FreeCount;
            }
        }

        std::array<NodeType, Capacity> Storage{};
        NodeType* FreeList = nullptr;
        std::size_t FreeCount = 0;
    };

    [[nodiscard]] static bool decodeKey(
        const std::uint64_t Key,
        std::size_t& OutLevel0Index,
        std::size_t& OutLevel1Index,
        std::size_t& OutLevel2Index) noexcept
    {
        const std::uint64_t normalized = Key >> KeyShift;
        if ((normalized >> IndexedBits) != 0)
        {
            return false;
        }

        OutLevel2Index = static_cast<std::size_t>(normalized & Level2Mask);
        OutLevel1Index = static_cast<std::size_t>((normalized >> Level2Bits) & Level1Mask);
        OutLevel0Index = static_cast<std::size_t>((normalized >> (Level2Bits + Level1Bits)) & Level0Mask);
        return true;
    }

    std::array<Level1Node*, Level0Size> Root{};
    FixedNodePool<Level1Node, Level1NodeCapacity> Level1Pool;
    FixedNodePool<Level2Node, Level2NodeCapacity> Level2Pool;
    std::size_t Size = 0;
    std::size_t PoolExhaustionFailureCount = 0;
    std::size_t KeyOutOfRangeFailureCount = 0;
};

} // namespace core::containers
