
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <memory>
#include <system_error>
#include <stdexcept>
#include <utility>

namespace core::mem::details
{

enum class virtual_memory_page_protection
{
	None = 0,
	Read = 1,
	ReadWrite = 2,
	NoAccess = 3,
};

enum class page_kind
{
	Normal = 0,
	Huge2MB, 
	Huge1GB,
	// ...
};

using NumaNode = int16_t;

/**
 * @about: 操作系统的 reserve-commit 模型:
 * > Reserve: 预留一段虚拟地址空间, 但不分配物理内存, 也不设置访问权限. 提前锁定一块超
 * >          大连续虚拟地址, 后续只在内部按需提交少量物理页, 保证运行时内存地址永远连续,
 * >          彻底规避堆碎片
 *     1. 向操作系统锁定一整块连续虚拟地址区间, 防止其他分配操作占用这片地址
 *     2. 仅修改进程页表元数据, 无物理页分配, 无系统存储预留
 *     3. 分配粒度固定 64KB(x64/x86 Windows 统一), 预留区间起始地址必须对齐 64KB; 
 *        页粒度 4KB，提交按单页粒度操作
 * > Commit: 在已预留的虚拟地址空间中分配物理内存, 并设置访问权限
 *     1. 页表建立虚拟地址 <=>  物理页映射. 首次访问触发缺页异常, 内核分配真实物理内存
 *     2. 提交后的页面可读可写, 可单独 Decommit (释放物理存储, 退回 Reserved 状态),
 *        可整体 Release (释放所有物理存储并归还地址空间回到 Free 状态)
 *
 * 这个模型是应用在 Windows 上的内存模型, 在 Linux 使用 mmap 模拟
 */


struct reserve_options
{
	std::size_t size_bytes = 0; // 要保留的字节数
	std::size_t alignment = 0; // 对齐要求, 0 表示使用系统默认对齐
	page_kind kind = page_kind::Normal; // 页类型
	NumaNode numa_node = -1; // NUMA 节点, -1 表示不指定
	bool guard_front = false; // 是否在前面添加一个不可访问的保护页
	bool guard_back = false; // 是否在后面添加一个不可访问的保护页
};

struct virtual_region 
{
	std::byte* base = nullptr; // 保留区域的起始地址
	std::size_t reserved_size = 0; // 保留区域的大小(包括保护页)
	std::size_t committed_offset = 0; // 已经提交的区域的起始偏移
	std::size_t committed_size = 0; // 已经提交的字节数
	std::size_t page_size = 0; // 实际使用的页大小 (普通页 4K, 大页 2M/1G...)
	std::size_t allocation_granularity = 0; // 系统分配粒度 (通常是 64K)
	bool huge_page = false; // 是否使用大页
};

class virtual_memory_backend_interface
{
public:
    virtual ~virtual_memory_backend_interface() = default;

    // 保留地址空间并返回 virtual_region
    // 成功时返回有效的区域；失败时抛出 std::system_error。
    virtual virtual_region reserve(const reserve_options& opts) = 0;

    // 在已保留区域内提交从 offset 开始的 bytes 字节物理内存
    // offset 和 bytes 会被调整为页面大小对齐
    virtual void commit(virtual_region& region, std::size_t offset, std::size_t bytes) = 0;

    // 释放已提交的物理内存(保留地址空间不变)
    virtual void decommit(virtual_region& region, std::size_t offset, std::size_t bytes) = 0;

    // 修改区域内存页的保护属性
    virtual void protect(virtual_region& region, std::size_t offset, std::size_t bytes, virtual_memory_page_protection prot) = 0;

    // 完全释放该虚拟区域(释放所有提交的物理内存并归还地址空间)
    virtual void release(virtual_region& region) = 0;

};

}