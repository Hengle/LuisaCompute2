//
// Created by Mike Smith on 2021/7/1.
//

#pragma once

#import <unordered_set>
#import <Metal/Metal.h>

#import <core/spin_mutex.h>
#import <core/allocator.h>
#import <runtime/bindless_array.h>

namespace luisa::compute::metal {

class MetalDevice;
class MetalStream;

struct MetalBindlessReousrce {
    id<MTLResource> handle;
    [[nodiscard]] auto operator<(const MetalBindlessReousrce &rhs) const noexcept {
        return handle < rhs.handle;
    }
};

class MetalBindlessArray {

private:
    MetalDevice *_device;
    id<MTLBuffer> _buffer{nullptr};
    id<MTLBuffer> _device_buffer{nullptr};
    id<MTLArgumentEncoder> _encoder{nullptr};
    id<MTLEvent> _event{nullptr};
    luisa::set<MetalBindlessReousrce> _resources;
    mutable uint64_t _event_value{0u};
    mutable __weak id<MTLCommandBuffer> _last_update{nullptr};
    mutable spin_mutex _mutex;
    mutable bool _dirty{true};
    static constexpr auto slot_size = 32u;
    std::vector<MetalBindlessReousrce> _buffer_slots;
    std::vector<MetalBindlessReousrce> _tex2d_slots;
    std::vector<MetalBindlessReousrce> _tex3d_slots;

public:
    MetalBindlessArray(MetalDevice *device, size_t size) noexcept;
    void emplace_buffer(size_t index, uint64_t buffer_handle) noexcept;
    void emplace_tex2d(size_t index, uint64_t texture_handle, Sampler sampler) noexcept;
    void emplace_tex3d(size_t index, uint64_t texture_handle, Sampler sampler) noexcept;
    void remove_buffer(size_t index) noexcept;
    void remove_tex2d(size_t index) noexcept;
    void remove_tex3d(size_t index) noexcept;
    [[nodiscard]] auto desc_buffer() const noexcept { return _device_buffer; }
    [[nodiscard]] id<MTLCommandBuffer> encode_update(MetalStream *stream, id<MTLCommandBuffer> cmd_buf) const noexcept;

    template<typename F>
    decltype(auto) traverse(F &&f) const noexcept {
        for (auto &&r : _resources) { f(r.handle); }
    }
};

}// namespace luisa::compute::metal
