#pragma once
#include <vstl/Common.h>
#include <runtime/device.h>
#include <DXRuntime/Device.h>
#include <DXRuntime/CommandQueue.h>
#include <DXRuntime/CommandAllocator.h>
#include <DXRuntime/CommandBuffer.h>
#include <runtime/command_list.h>
#include <DXRuntime/ResourceStateTracker.h>
using namespace luisa::compute;
namespace toolhub::directx {
class RenderTexture;
class LCSwapChain;
class LCCmdBuffer final : public vstd::IOperatorNewBase {
protected:
    uint64 lastFence = 0;
    Device *device;
    ResourceStateTracker tracker;

public:
    CommandQueue queue;
    LCCmdBuffer(
        Device *device,
        IGpuAllocator *resourceAllocator,
        D3D12_COMMAND_LIST_TYPE type);
    void Execute(
        vstd::span<CommandList const> const &c,
        size_t maxAlloc = std::numeric_limits<size_t>::max(),
        vstd::move_only_func<void()> *func = nullptr);
    void Sync();
    void Present(
        LCSwapChain* swapchain,
        RenderTexture *rt);
};

}// namespace toolhub::directx