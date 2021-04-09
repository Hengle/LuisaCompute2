//
// Created by Mike Smith on 2021/2/15.
//

#pragma once

#include <utility>

#include <core/spin_mutex.h>
#include <runtime/device.h>
#include <runtime/command_buffer.h>

namespace luisa::compute {

class Stream : public concepts::Noncopyable {

public:
    class Delegate {

    private:
        Stream *_stream;
        CommandBuffer _command_buffer;

    private:
        void _commit() noexcept;

    public:
        explicit Delegate(Stream *s) noexcept;
        Delegate(Delegate &&) noexcept;
        Delegate &operator=(Delegate &&) noexcept;
        ~Delegate() noexcept;
        Delegate &operator<<(CommandHandle cmd) noexcept;
    };

private:
    Device *_device;
    uint64_t _handle;

private:
    friend class Device;
    void _dispatch(CommandBuffer command_buffer) noexcept;

public:
    explicit Stream(Device &device) noexcept
        : _device{&device}, _handle{device.create_stream()} {}
    Stream(Stream &&s) noexcept;
    ~Stream() noexcept;
    Stream &operator=(Stream &&rhs) noexcept;
    Delegate operator<<(CommandHandle cmd) noexcept;
    void synchronize() noexcept;
};

}// namespace luisa::compute
