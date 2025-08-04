#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t size)
        : buffer_{Allocate(size)}
        , capacity_{size} {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept {
        swap(other);
    }

    RawMemory& operator=(RawMemory&& other) noexcept {
        if(this != &other) {
            buffer_ = std::exchange(other.buffer_, nullptr);
            capacity_ = std::exchange(other.capacity_, 0);
        }
        return *this;
    }

    ~RawMemory() noexcept {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    static T* Allocate(size_t size) {
        return size ? static_cast<T*>(operator new(sizeof(T) * size)) : nullptr;
    }

    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

// ********* constructors, assignment operators and destructor **********************
    Vector() = default;

    explicit Vector(size_t size) : data_{size}, size_{size} {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);  
    }

    Vector(const Vector& other) : data_{other.Size()}, size_{other.size_} {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.Size(), data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    Vector& operator=(const Vector& other) {
        if(this != &other) {
            // if need to increase capacity
            if(Capacity() < other.size_) {
                Vector copy_other(other);
                Swap(copy_other);
                return *this;
            }
            // if capacity is enough
            if(size_ < other.size_) {
                std::copy_n(other.begin(), size_, begin());
                std::uninitialized_copy(other.begin() + size_, other.end(), end());
            } else {
                std::copy(other.begin(), other.end(), begin());
                std::destroy(begin() + other.size_, end());
            }
            size_ = other.size_;
        }
        return *this;
    }

    Vector& operator=(Vector&& other) noexcept {
        if(this != &other) {
            data_ = std::move(other.data_);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    ~Vector() noexcept {
        std::destroy_n(begin(), size_);
    }

// ********* methods for getting iterators **********************
    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return begin() + size_;
    }

    const_iterator begin() const noexcept {
        return cbegin();
    }

    const_iterator end() const noexcept {
        return cend();
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return cbegin() + size_;
    }

// ********* methods for adding and inserting elements **********************
    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    template <typename...Args>
    T& EmplaceBack(Args&&... args) {
        if(size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new(new_data.GetAddress() + size_) T(std::forward<Args>(args)...);

            UninitializedMoveOrCopy(begin(), end(), new_data.GetAddress());

            data_.Swap(new_data);
            std::destroy_n(new_data.GetAddress(), size_);
        } else {
            new(data_.GetAddress() + size_) T(std::forward<Args>(args)...);
        }
        ++size_;
        return data_[size_ - 1];
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(pos >= begin() && pos <= end());
        iterator insert_element = end();
        auto shift = pos - begin();
        if(size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            insert_element = new(new_data.GetAddress() + shift) T(std::forward<Args>(args)...);

            try{  // if an exception occurs when moving/copying elements after emplace position
                UninitializedMoveOrCopy(begin(), begin() + shift, new_data.GetAddress());
            } catch (...) {
                std::destroy_at(insert_element);
                throw;
            }
            try{  // if an exception occurs when moving/copying elements before emplace position
                UninitializedMoveOrCopy(begin() + shift, end(), new_data.GetAddress() + shift + 1);
            } catch (...) {
                std::destroy_n(insert_element, size_ - shift + 1);
                throw;
            }

            std::destroy(begin(), end());
            data_.Swap(new_data);
        } else {
            if(pos != end()) {
                T temp(std::forward<Args>(args)...);
                new(end()) T(std::move(*(end() - 1)));
                try {  // if an exception occurs when moving/copying elements after emplace position
                    std::move_backward(begin() + shift, end() - 1, end());
                } catch (...) {
                    std::destroy_at(end());
                    throw;
                }
                data_[shift] = std::move(temp);
                insert_element = begin() + shift;
            } else {
                insert_element = new(begin() + shift) T(std::forward<Args>(args)...);
            }
        }
        ++size_;
        return insert_element;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

// ********* methods for deleting elements **********************
    void PopBack() {
            std::destroy_at(end() - 1);
            --size_;
    }

    iterator Erase(const_iterator pos) { /*noexcept(std::is_nothrow_move_assignable_v<T>)*/
        assert(pos >= begin() && pos < end());
        auto shift = pos - begin();
        std::move(begin() + shift + 1, end(), begin() + shift);
        std::destroy_at(end() - 1);
        --size_;
        return begin() + shift;
    }

// ********* methods for changing the size and capacity **********************
    void Resize(size_t new_size) {
        if(size_ < new_size) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(begin() + size_, new_size - size_);
        } else {
            std::destroy_n(begin() + new_size, size_ - new_size);
        }
        size_ = new_size;
    }

    void Reserve(size_t new_capacity) {
        if(new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);

        UninitializedMoveOrCopy(begin(), end(), new_data.GetAddress());

        data_.Swap(new_data);
        std::destroy_n(new_data.GetAddress(), size_);
    }

// ********* methods for getting size and capacity **********************    
    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

// ********* methods for accessing elements **********************
    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    // method that changes the contents of the current vector and other
    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }
private:
    // checks the possibility of moving/copying and moves/copies to uninitialized memory
    void UninitializedMoveOrCopy(iterator source_begin, iterator source_end, iterator dest) {
        if constexpr(std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move(source_begin, source_end, dest);
        } else {
            std::uninitialized_copy(source_begin, source_end, dest);
        }
    }
private: 
    RawMemory<T> data_;
    size_t size_ = 0;
};