#pragma vengine_package ispc_vsproject

#include <backends/ispc/runtime/ispc_device.h>
#include <runtime/sampler.h>
#include "ispc_codegen.h"

namespace lc::ispc {
void *ISPCDevice::native_handle() const noexcept {
    return nullptr;
}

// buffer
uint64_t ISPCDevice::create_buffer(
    size_t size_bytes,
    uint64_t heap_handle,// == uint64(-1) when not from heap
    uint32_t index_in_heap) noexcept { return 0; }
void ISPCDevice::destroy_buffer(uint64_t handle) noexcept {}
void *ISPCDevice::buffer_native_handle(uint64_t handle) const noexcept {
    return nullptr;
}

// texture
uint64_t ISPCDevice::create_texture(
    PixelFormat format, uint dimension,
    uint width, uint height, uint depth,
    uint mipmap_levels,
    Sampler sampler,
    uint64_t heap_handle,// == uint64(-1) when not from heap
    uint32_t index_in_heap) { return 0; }
void ISPCDevice::destroy_texture(uint64_t handle) noexcept {}
void *ISPCDevice::texture_native_handle(uint64_t handle) const noexcept {
    return nullptr;
}

// texture heap
uint64_t ISPCDevice::create_heap(size_t size) noexcept { return 0; }
size_t ISPCDevice::query_heap_memory_usage(uint64_t handle) noexcept { return 0; }
void ISPCDevice::destroy_heap(uint64_t handle) noexcept {}

// stream
uint64_t ISPCDevice::create_stream() noexcept { return 0; }
void ISPCDevice::destroy_stream(uint64_t handle) noexcept {}
void ISPCDevice::synchronize_stream(uint64_t stream_handle) noexcept {}
void ISPCDevice::dispatch(uint64_t stream_handle, CommandList) noexcept {}
void *ISPCDevice::stream_native_handle(uint64_t handle) const noexcept {
    return nullptr;
}

// kernel
uint64_t ISPCDevice::create_shader(Function kernel, std::string_view meta_options) noexcept {
    std::string result;
    CodegenUtility::PrintFunction(kernel, result);
    auto f = fopen("test.ispc", "r");
    if (f) {
        auto disp = vstd::create_disposer([&] {
            fclose(f);
        });
        fwrite(result.data(), result.size(), 1, f);
    }
    return 0;
}
void ISPCDevice::destroy_shader(uint64_t handle) noexcept {}

// event
uint64_t ISPCDevice::create_event() noexcept { return 0; }
void ISPCDevice::destroy_event(uint64_t handle) noexcept {}
void ISPCDevice::signal_event(uint64_t handle, uint64_t stream_handle) noexcept {}
void ISPCDevice::wait_event(uint64_t handle, uint64_t stream_handle) noexcept {}
void ISPCDevice::synchronize_event(uint64_t handle) noexcept {}

// accel
uint64_t ISPCDevice::create_mesh() noexcept { return 0; }
void ISPCDevice::destroy_mesh(uint64_t handle) noexcept {}
uint64_t ISPCDevice::create_accel() noexcept { return 0; }
void ISPCDevice::destroy_accel(uint64_t handle) noexcept {}

}// namespace lc::ispc

LUISA_EXPORT_API luisa::compute::Device::Interface *create(const luisa::compute::Context &ctx, uint32_t id) noexcept {
    return luisa::new_with_allocator<lc::ispc::ISPCDevice>(ctx, id);
}

LUISA_EXPORT_API void destroy(luisa::compute::Device::Interface *device) noexcept {
    luisa::delete_with_allocator(device);
}
