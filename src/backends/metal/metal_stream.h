//
// Created by Mike Smith on 2023/4/15.
//

#pragma once

#include <core/stl/queue.h>
#include <core/stl/string.h>

#include <runtime/rhi/stream_tag.h>
#include <runtime/command_list.h>
#include <backends/metal/metal_api.h>
#include <backends/metal/metal_stage_buffer_pool.h>

namespace luisa::compute::metal {

class MetalEvent;
class MetalTexture;
class MetalSwapchain;
class MetalCommandEncoder;

class MetalStream {

public:
    using CallbackContainer = luisa::vector<MetalCallbackContext *>;

private:
    MTL::CommandQueue *_queue;
    spin_mutex _upload_pool_creation_mutex;
    spin_mutex _download_pool_creation_mutex;
    luisa::unique_ptr<MetalStageBufferPool> _upload_pool;
    luisa::unique_ptr<MetalStageBufferPool> _download_pool;
    luisa::queue<CallbackContainer> _callback_lists;
    spin_mutex _callback_mutex;

protected:
    void _do_dispatch(MetalCommandEncoder &encoder, CommandList &&list) noexcept;
    virtual void _encode(MetalCommandEncoder &encoder, Command *command) noexcept;

public:
    MetalStream(MTL::Device *device, size_t max_commands) noexcept;
    virtual ~MetalStream() noexcept;
    virtual void signal(MetalEvent *event) noexcept;
    virtual void wait(MetalEvent *event) noexcept;
    virtual void synchronize() noexcept;
    virtual void dispatch(CommandList &&list) noexcept;
    void present(MetalSwapchain *swapchain, MetalTexture *image) noexcept;
    virtual void set_name(luisa::string_view name) noexcept;
    [[nodiscard]] auto device() const noexcept { return _queue->device(); }
    [[nodiscard]] auto queue() const noexcept { return _queue; }
    [[nodiscard]] MetalStageBufferPool *upload_pool() noexcept;
    [[nodiscard]] MetalStageBufferPool *download_pool() noexcept;
    virtual void submit(MTL::CommandBuffer *command_buffer, CallbackContainer &&callbacks) noexcept;
};

}// namespace luisa::compute::metal
