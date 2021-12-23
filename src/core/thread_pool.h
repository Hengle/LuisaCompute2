//
// Created by Mike Smith on 2021/12/23.
//

#pragma once

#include <mutex>
#include <future>
#include <thread>
#include <memory>
#include <atomic>
#include <barrier>
#include <concepts>
#include <functional>
#include <condition_variable>

#include <core/allocator.h>
#include <core/basic_types.h>

namespace luisa {

class ThreadPool {

public:
    using barrier_type = std::barrier<decltype([]() noexcept {})>;

private:
    luisa::vector<std::thread> _threads;
    luisa::queue<std::function<void()>> _tasks;
    std::mutex _mutex;
    barrier_type _synchronize_barrier;
    barrier_type _dispatch_barrier;
    std::condition_variable _cv;
    bool _should_stop;

private:
    void _dispatch(std::function<void()> task) noexcept;
    void _dispatch_all(std::function<void()> task) noexcept;

public:
    explicit ThreadPool(size_t num_threads = 0u) noexcept;
    ~ThreadPool() noexcept;
    ThreadPool(ThreadPool &&) noexcept = delete;
    ThreadPool(const ThreadPool &) noexcept = delete;
    ThreadPool &operator=(ThreadPool &&) noexcept = delete;
    ThreadPool &operator=(const ThreadPool &) noexcept = delete;
    [[nodiscard]] static ThreadPool &global() noexcept;

public:
    void barrier() noexcept;
    void synchronize() noexcept;

    template<typename F>
        requires std::invocable<F>
    auto dispatch(F f) noexcept {
        using R = std::invoke_result_t<F>;
        auto promise = luisa::new_with_allocator<std::promise<R>>(
            std::allocator_arg, luisa::allocator{});
        auto future = promise->get_future().share();
        _dispatch([func = std::move(f), promise, future]() mutable noexcept {
            if constexpr (std::same_as<R, void>) {
                func();
                promise->set_value();
            } else {
                promise->set_value(func());
            }
            luisa::delete_with_allocator(promise);
        });
        return future;
    }

    template<typename F>
    void parallel(uint n, F f) noexcept {
        auto counter = luisa::make_shared<std::atomic_uint>(0u);
        _dispatch_all([=]() mutable noexcept {
            for (auto i = counter->fetch_add(1u);
                 i < n;
                 i = counter->fetch_add(1u)) { f(i); }
        });
    }

    template<typename F>
    void parallel(uint2 n, F f) noexcept {
        parallel(n.x * n.y, [=, f = std::move(f)](auto i) mutable noexcept {
            f(i % n.x, i / n.x);
        });
    }

    template<typename F>
    void parallel(uint nx, uint ny, F f) noexcept {
        parallel(make_uint2(nx, ny), std::move(f));
    }

    template<typename F>
    void parallel(uint3 n, F f) noexcept {
        parallel(n.x * n.y * n.z, [=, f = std::move(f)](auto i) mutable noexcept {
            f(i % n.x, i / n.x % n.y, i / n.x / n.y);
        });
    }

    template<typename F>
    void parallel(uint nx, uint ny, uint nz, F f) noexcept {
        parallel(make_uint3(nx, ny, nz), std::move(f));
    }
};

}// namespace luisa
