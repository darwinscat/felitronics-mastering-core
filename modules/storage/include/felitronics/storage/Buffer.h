// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>

namespace felitronics::storage
{
// A prepared buffer owns one exact array. It keeps capacity across preparations, but has no iterator
// bookkeeping allocation, including on MSVC Debug. std::allocator preserves the platform's alignment.
template <class T>
class Buffer
{
    struct Delete
    {
        std::size_t capacity = 0;
        void operator() (T* p) const noexcept
        {
            std::destroy_n (p, capacity);
            std::allocator<T> {}.deallocate (p, capacity);
        }
    };
    std::unique_ptr<T[], Delete> data_ { nullptr, Delete {} };
    std::size_t size_ = 0;

public:
    Buffer() = default;
    Buffer (const Buffer& other) { assign (other); }
    Buffer& operator= (const Buffer& other) { if (this != &other) assign (other); return *this; }
    Buffer (Buffer&& other) noexcept : data_ (std::move (other.data_)), size_ (std::exchange (other.size_, 0)) {}
    Buffer& operator= (Buffer&& other) noexcept
    {
        data_ = std::move (other.data_);
        size_ = std::exchange (other.size_, 0);
        return *this;
    }

    void assign (std::size_t n, const T& value)
    {
        resize (n);
        std::fill_n (data(), n, value);
    }
    void resize (std::size_t n)
    {
        if (n > capacity())
        {
            // The algorithms destroy partially constructed elements on failure; the temporary owns
            // the allocation until construction succeeds, even in an exception-enabled consumer.
            struct Free
            {
                std::size_t n;
                void operator() (T* p) const noexcept { std::allocator<T> {}.deallocate (p, n); }
            };
            std::unique_ptr<T, Free> next (std::allocator<T> {}.allocate (n), Free { n });
            std::uninitialized_value_construct_n (next.get(), n);
            std::unique_ptr<T[], Delete> ready (next.release(), Delete { n });
            // Growth preserves every old element; state the destination bound for inlined range diagnostics too.
            std::copy_n (data(), std::min (size_, n), ready.get());
            data_ = std::move (ready);
        }
        if (n > size_) std::fill (data() + size_, data() + n, T {});
        size_ = n;
    }
    void clear() noexcept { size_ = 0; }
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return data_ ? data_.get_deleter().capacity : 0; }
    bool empty() const noexcept { return size_ == 0; }
    T* data() noexcept { return data_.get(); }
    const T* data() const noexcept { return data_.get(); }
    T* begin() noexcept { return data(); }
    const T* begin() const noexcept { return data(); }
    T* end() noexcept { return size_ ? data() + size_ : data(); }
    const T* end() const noexcept { return size_ ? data() + size_ : data(); }
    T& operator[] (std::size_t i) noexcept { return data()[i]; }
    const T& operator[] (std::size_t i) const noexcept { return data()[i]; }

private:
    void assign (const Buffer& other)
    {
        resize (other.size());
        std::copy_n (other.data(), size_, data());
    }
};
} // namespace felitronics::storage
