#pragma once
#include <thread>
#include <util/MetaLib.h>
#include <util/Memory.h>
#include <util/VAllocator.h>
#include <util/spin_mutex.h>
#include <optional>
namespace vstd {
template<typename T, VEngine_AllocType allocType = VEngine_AllocType::VEngine>
class LockFreeArrayQueue {
    size_t head;
    size_t tail;
    size_t capacity;
    mutable luisa::spin_mutex mtx;
    T *arr;
    VAllocHandle<allocType> allocHandle;

    static constexpr size_t GetIndex(size_t index, size_t capacity) noexcept {
        return index & capacity;
    }
    using SelfType = LockFreeArrayQueue<T, allocType>;

public:
    LockFreeArrayQueue(size_t capacity) : head(0), tail(0) {
        if (capacity < 32) capacity = 32;
        capacity = [](size_t capacity) {
            size_t ssize = 1;
            while (ssize < capacity)
                ssize <<= 1;
            return ssize;
        }(capacity);
        this->capacity = capacity - 1;
        std::lock_guard<luisa::spin_mutex> lck(mtx);
        arr = (T *)allocHandle.Malloc(sizeof(T) * capacity);
    }
    LockFreeArrayQueue(SelfType &&v)
        : head(v.head),
          tail(v.tail),
          capacity(v.capacity),
          arr(v.arr) {
        v.arr = nullptr;
    }
    void operator=(SelfType &&v) {
        this->~SelfType();
        new (this) SelfType(std::move(v));
    }
    LockFreeArrayQueue() : LockFreeArrayQueue(64) {}

    template<typename... Args>
    void Push(Args &&...args) {
        std::lock_guard<luisa::spin_mutex> lck(mtx);
        size_t index = head++;
        if (head - tail > capacity) {
            auto newCapa = (capacity + 1) * 2;
            T *newArr = (T *)allocHandle.Malloc(sizeof(T) * newCapa);
            newCapa--;
            for (size_t s = tail; s != index; ++s) {
                T *ptr = arr + GetIndex(s, capacity);
                new (newArr + GetIndex(s, newCapa)) T(*ptr);
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    ptr->~T();
                }
            }
            allocHandle.Free(arr);
            arr = newArr;
            capacity = newCapa;
        }
        new (arr + GetIndex(index, capacity)) T(std::forward<Args>(args)...);
    }
    template<typename... Args>
    bool TryPush(Args &&...args) {
        std::unique_lock<luisa::spin_mutex> lck(mtx, std::try_to_lock);
        if (!lck.owns_lock()) return false;
        size_t index = head++;
        if (head - tail > capacity) {
            auto newCapa = (capacity + 1) * 2;
            T *newArr = (T *)allocHandle.Malloc(sizeof(T) * newCapa);
            newCapa--;
            for (size_t s = tail; s != index; ++s) {
                T *ptr = arr + GetIndex(s, capacity);
                new (newArr + GetIndex(s, newCapa)) T(*ptr);
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    ptr->~T();
                }
            }
            allocHandle.Free(arr);
            arr = newArr;
            capacity = newCapa;
        }
        new (arr + GetIndex(index, capacity)) T(std::forward<Args>(args)...);
        return true;
    }
    template<typename... Args>
    void PushInPlaceNew(Args &&...args) {
        std::lock_guard<luisa::spin_mutex> lck(mtx);
        size_t index = head++;
        if (head - tail > capacity) {
            auto newCapa = (capacity + 1) * 2;
            T *newArr = (T *)allocHandle.Malloc(sizeof(T) * newCapa);
            newCapa--;
            for (size_t s = tail; s != index; ++s) {
                T *ptr = arr + GetIndex(s, capacity);
                new (newArr + GetIndex(s, newCapa)) T(*ptr);
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    ptr->~T();
                }
            }
            allocHandle.Free(arr);
            arr = newArr;
            capacity = newCapa;
        }
        new (arr + GetIndex(index, capacity)) T{std::forward<Args>(args)...};
    }
    bool Pop(T *ptr) {
        constexpr bool isTrivial = std::is_trivially_destructible_v<T>;
        if constexpr (!isTrivial) {
            ptr->~T();
        }

        std::lock_guard<luisa::spin_mutex> lck(mtx);
        if (head == tail)
            return false;
        auto &&value = arr[GetIndex(tail++, capacity)];
        if (std::is_trivially_move_assignable_v<T>) {
            *ptr = std::move(value);
        } else {
            new (ptr) T(std::move(value));
        }
        if constexpr (!isTrivial) {
            value.~T();
        }
        return true;
    }
    std::optional<T> Pop() {
        mtx.lock();
        if (head == tail) {
            mtx.unlock();
            return std::optional<T>();
        }
        auto value = &arr[GetIndex(tail++, capacity)];
        auto disp = vstd::create_disposer([value, this]() {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                value->~T();
            }
            mtx.unlock();
        });
        return std::optional<T>(std::move(*value));
    }
    std::optional<T> TryPop() {
        std::unique_lock<luisa::spin_mutex> lck(mtx, std::try_to_lock);
        if (!lck.owns_lock()) {
            return std::optional<T>();
        }
        if (head == tail) {
            return std::optional<T>();
        }
        auto value = &arr[GetIndex(tail++, capacity)];
        auto disp = vstd::create_disposer([value, this]() {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                value->~T();
            }
        });
        return std::optional<T>(std::move(*value));
    }
    ~LockFreeArrayQueue() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t s = tail; s != head; ++s) {
                arr[GetIndex(s, capacity)].~T();
            }
        }
        allocHandle.Free(arr);
    }
    size_t Length() const {
        std::lock_guard<luisa::spin_mutex> lck(mtx);
        return head - tail;
    }
};
}// namespace vstd