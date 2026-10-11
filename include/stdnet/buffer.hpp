// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace stdnet {

// [buffer.mutable] -- a non-owning view of writable bytes.
class mutable_buffer {
public:
    constexpr mutable_buffer() noexcept = default;
    constexpr mutable_buffer(void* p, std::size_t n) noexcept : data_(p), size_(n) {}
    constexpr mutable_buffer(std::span<std::byte> s) noexcept : data_(s.data()), size_(s.size()) {}

    constexpr void* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr std::span<std::byte> bytes() const noexcept { return {static_cast<std::byte*>(data_), size_}; }

    constexpr mutable_buffer& operator+=(std::size_t n) noexcept {
        std::size_t off = std::min(n, size_);
        data_ = static_cast<std::byte*>(data_) + off;
        size_ -= off;
        return *this;
    }

private:
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

// [buffer.const] -- a non-owning view of read-only bytes.
class const_buffer {
public:
    constexpr const_buffer() noexcept = default;
    constexpr const_buffer(const void* p, std::size_t n) noexcept : data_(p), size_(n) {}
    constexpr const_buffer(const mutable_buffer& b) noexcept : data_(b.data()), size_(b.size()) {}
    constexpr const_buffer(std::span<const std::byte> s) noexcept : data_(s.data()), size_(s.size()) {}

    constexpr const void* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr std::span<const std::byte> bytes() const noexcept {
        return {static_cast<const std::byte*>(data_), size_};
    }

    constexpr const_buffer& operator+=(std::size_t n) noexcept {
        std::size_t off = std::min(n, size_);
        data_ = static_cast<const std::byte*>(data_) + off;
        size_ -= off;
        return *this;
    }

private:
    const void* data_ = nullptr;
    std::size_t size_ = 0;
};

constexpr mutable_buffer operator+(mutable_buffer b, std::size_t n) noexcept { return b += n; }
constexpr const_buffer operator+(const_buffer b, std::size_t n) noexcept { return b += n; }

// [buffer.seq.concepts]
// A buffer sequence is either a single buffer or a forward range of buffers.
template <class T>
concept mutable_buffer_sequence =
    std::convertible_to<const T&, mutable_buffer> ||
    (std::ranges::forward_range<const T> &&
     std::convertible_to<std::ranges::range_reference_t<const T>, mutable_buffer>);

template <class T>
concept const_buffer_sequence =
    std::convertible_to<const T&, const_buffer> ||
    (std::ranges::forward_range<const T> && std::convertible_to<std::ranges::range_reference_t<const T>, const_buffer>);

// Invokes f(buffer) for every buffer of the sequence.
template <class Buffer, class Seq, class F>
constexpr void for_each_buffer(const Seq& seq, F&& f) {
    if constexpr (std::convertible_to<const Seq&, Buffer>) {
        f(Buffer(seq));
    } else {
        for (auto&& b : seq) f(Buffer(b));
    }
}

template <class Seq>
    requires const_buffer_sequence<Seq>
constexpr std::size_t buffer_size(const Seq& seq) noexcept {
    std::size_t total = 0;
    for_each_buffer<const_buffer>(seq, [&](const_buffer b) { total += b.size(); });
    return total;
}

// Copies bytes from a const buffer sequence into a mutable buffer sequence; returns the count copied.
template <mutable_buffer_sequence Dst, const_buffer_sequence Src>
std::size_t buffer_copy(const Dst& dst, const Src& src, std::size_t max_size = static_cast<std::size_t>(-1)) noexcept {
    std::vector<const_buffer> sources;
    for_each_buffer<const_buffer>(src, [&](const_buffer b) { sources.push_back(b); });
    std::size_t copied = 0, si = 0;
    for_each_buffer<mutable_buffer>(dst, [&](mutable_buffer d) {
        while (d.size() > 0 && si < sources.size() && copied < max_size) {
            std::size_t n = std::min({d.size(), sources[si].size(), max_size - copied});
            if (n > 0) std::memcpy(d.data(), sources[si].data(), n);
            d += n;
            sources[si] += n;
            copied += n;
            if (sources[si].size() == 0) ++si;
        }
    });
    return copied;
}

// [buffer.creation]
constexpr mutable_buffer buffer(void* p, std::size_t n) noexcept { return {p, n}; }
constexpr const_buffer buffer(const void* p, std::size_t n) noexcept { return {p, n}; }
constexpr mutable_buffer buffer(const mutable_buffer& b) noexcept { return b; }
constexpr const_buffer buffer(const const_buffer& b) noexcept { return b; }
constexpr mutable_buffer buffer(const mutable_buffer& b, std::size_t n) noexcept { return {b.data(), std::min(b.size(), n)}; }
constexpr const_buffer buffer(const const_buffer& b, std::size_t n) noexcept { return {b.data(), std::min(b.size(), n)}; }

namespace detail {
template <class R>
concept byte_contiguous_range = std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
                                std::is_trivially_copyable_v<std::ranges::range_value_t<R>>;
}

// Any contiguous range of trivially copyable objects: std::string, std::vector, std::array, C arrays, spans...
template <class R>
    requires detail::byte_contiguous_range<R> && (!std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<R>>>) &&
             (!std::convertible_to<R, const_buffer>)
constexpr mutable_buffer buffer(R&& r) noexcept {
    return {std::ranges::data(r), std::ranges::size(r) * sizeof(std::ranges::range_value_t<R>)};
}

template <class R>
    requires detail::byte_contiguous_range<R> && std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<R>>> &&
             (!std::convertible_to<R, const_buffer>)
constexpr const_buffer buffer(R&& r) noexcept {
    return {std::ranges::data(r), std::ranges::size(r) * sizeof(std::ranges::range_value_t<R>)};
}

template <class R>
    requires detail::byte_contiguous_range<R> && (!std::convertible_to<R, const_buffer>)
constexpr auto buffer(R&& r, std::size_t n) noexcept {
    return buffer(buffer(std::forward<R>(r)), n);
}

} // namespace stdnet
