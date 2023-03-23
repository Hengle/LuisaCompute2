//
// Created by Mike on 7/28/2021.
//

#pragma once

#include <cuda.h>

#include <runtime/rhi/device_interface.h>
#include <backends/common/default_binary_io.h>
#include <backends/cuda/cuda_error.h>
#include <backends/cuda/cuda_mipmap_array.h>
#include <backends/cuda/cuda_stream.h>
#include <backends/cuda/cuda_compiler.h>
#include <backends/cuda/optix_api.h>

namespace luisa::compute::cuda {

/**
 * @brief CUDA device
 * 
 */
class CUDADevice final : public DeviceInterface {

    class ContextGuard {

    private:
        CUcontext _ctx;

    public:
        explicit ContextGuard(CUcontext ctx) noexcept : _ctx{ctx} {
            LUISA_CHECK_CUDA(cuCtxPushCurrent(_ctx));
        }
        ~ContextGuard() noexcept {
            CUcontext ctx = nullptr;
            LUISA_CHECK_CUDA(cuCtxPopCurrent(&ctx));
            if (ctx != _ctx) [[unlikely]] {
                LUISA_ERROR_WITH_LOCATION(
                    "Invalid CUDA context {} (expected {}).",
                    fmt::ptr(ctx), fmt::ptr(_ctx));
            }
        }
    };

public:
    /**
     * @brief Device handle of CUDA
     * 
     */
    class Handle {

    private:
        CUcontext _context{nullptr};
        CUdevice _device{0};
        uint32_t _compute_capability{};
        uint32_t _driver_version{};
        CUuuid _uuid{};
        // will be lazily initialized
        mutable optix::DeviceContext _optix_context{nullptr};
        mutable spin_mutex _mutex{};

    public:
        explicit Handle(size_t index) noexcept;
        ~Handle() noexcept;
        Handle(Handle &&) noexcept = delete;
        Handle(const Handle &) noexcept = delete;
        Handle &operator=(Handle &&) noexcept = delete;
        Handle &operator=(const Handle &) noexcept = delete;
        [[nodiscard]] std::string_view name() const noexcept;
        [[nodiscard]] auto uuid() const noexcept { return _uuid; }
        [[nodiscard]] auto device() const noexcept { return _device; }
        [[nodiscard]] auto context() const noexcept { return _context; }
        [[nodiscard]] auto driver_version() const noexcept { return _driver_version; }
        [[nodiscard]] auto compute_capability() const noexcept { return _compute_capability; }
        [[nodiscard]] optix::DeviceContext optix_context() const noexcept;
    };

private:
    Handle _handle;
    CUmodule _builtin_kernel_module{nullptr};
    CUfunction _accel_update_function{nullptr};
    CUfunction _bindless_array_update_function{nullptr};
    luisa::unique_ptr<CUDACompiler> _compiler;
    luisa::unique_ptr<DefaultBinaryIO> _default_io;
    const BinaryIO *_io{nullptr};

private:
    [[nodiscard]] ShaderCreationInfo _create_shader(const string &source,
                                                    ShaderOption option,
                                                    uint3 block_size,
                                                    bool is_raytracing) noexcept;

public:
    CUDADevice(Context &&ctx,
               size_t device_id,
               const BinaryIO *io) noexcept;
    ~CUDADevice() noexcept override;
    [[nodiscard]] auto &handle() const noexcept { return _handle; }
    template<typename F>
    decltype(auto) with_handle(F &&f) const noexcept {
        ContextGuard guard{_handle.context()};
        return f();
    }
    void *native_handle() const noexcept override { return _handle.context(); }
    [[nodiscard]] auto accel_update_function() const noexcept { return _accel_update_function; }
    [[nodiscard]] auto bindless_array_update_function() const noexcept { return _bindless_array_update_function; }
    [[nodiscard]] auto compiler() const noexcept { return _compiler.get(); }
    [[nodiscard]] auto io() const noexcept { return _io; }
    bool is_c_api() const noexcept override { return false; }
    BufferCreationInfo create_buffer(const Type *element, size_t elem_count) noexcept override;
    BufferCreationInfo create_buffer(const ir::CArc<ir::Type> *element, size_t elem_count) noexcept override;
    void destroy_buffer(uint64_t handle) noexcept override;
    ResourceCreationInfo create_texture(PixelFormat format, uint dimension, uint width, uint height, uint depth, uint mipmap_levels) noexcept override;
    void destroy_texture(uint64_t handle) noexcept override;
    ResourceCreationInfo create_bindless_array(size_t size) noexcept override;
    void destroy_bindless_array(uint64_t handle) noexcept override;
    ResourceCreationInfo create_depth_buffer(DepthFormat format, uint width, uint height) noexcept override;
    void destroy_depth_buffer(uint64_t handle) noexcept override;
    ResourceCreationInfo create_stream(StreamTag stream_tag) noexcept override;
    void destroy_stream(uint64_t handle) noexcept override;
    void synchronize_stream(uint64_t stream_handle) noexcept override;
    void dispatch(uint64_t stream_handle, CommandList &&list) noexcept override;
    SwapChainCreationInfo create_swap_chain(uint64_t window_handle, uint64_t stream_handle, uint width, uint height, bool allow_hdr, bool vsync, uint back_buffer_size) noexcept override;
    void destroy_swap_chain(uint64_t handle) noexcept override;
    void present_display_in_stream(uint64_t stream_handle, uint64_t swapchain_handle, uint64_t image_handle) noexcept override;
    ShaderCreationInfo create_shader(const ShaderOption &option, Function kernel) noexcept override;
    ShaderCreationInfo create_shader(const ShaderOption &option, const ir::KernelModule *kernel) noexcept override;
    ShaderCreationInfo load_shader(luisa::string_view name, luisa::span<const Type *const> arg_types) noexcept override;
    void destroy_shader(uint64_t handle) noexcept override;
    ResourceCreationInfo create_event() noexcept override;
    void destroy_event(uint64_t handle) noexcept override;
    void signal_event(uint64_t handle, uint64_t stream_handle) noexcept override;
    void wait_event(uint64_t handle, uint64_t stream_handle) noexcept override;
    void synchronize_event(uint64_t handle) noexcept override;
    ResourceCreationInfo create_mesh(const AccelOption &option) noexcept override;
    void destroy_mesh(uint64_t handle) noexcept override;
    ResourceCreationInfo create_procedural_primitive(const AccelOption &option) noexcept override;
    void destroy_procedural_primitive(uint64_t handle) noexcept override;
    ResourceCreationInfo create_accel(const AccelOption &option) noexcept override;
    void destroy_accel(uint64_t handle) noexcept override;
    string query(luisa::string_view property) noexcept override;
    void set_name(luisa::compute::Resource::Tag resource_tag, uint64_t resource_handle, luisa::string_view name) noexcept override;
    DeviceExtension *extension(luisa::string_view name) noexcept override;
};

}// namespace luisa::compute::cuda
