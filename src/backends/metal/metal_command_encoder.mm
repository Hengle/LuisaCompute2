//
// Created by Mike Smith on 2021/3/19.
//

#import <core/clock.h>
#import <core/platform.h>
#import <ast/function.h>
#import <backends/metal/metal_command_encoder.h>

namespace luisa::compute::metal {

MetalCommandEncoder::MetalCommandEncoder(MetalDevice *device, id<MTLCommandBuffer> cb) noexcept
    : _device{device}, _command_buffer{cb} {}

void MetalCommandEncoder::visit(const BufferCopyCommand *command) noexcept {
    auto blit_encoder = [_command_buffer blitCommandEncoder];
    [blit_encoder copyFromBuffer:_device->buffer(command->src_handle())
                    sourceOffset:command->src_offset()
                        toBuffer:_device->buffer(command->dst_handle())
               destinationOffset:command->dst_offset()
                            size:command->size()];
    [blit_encoder endEncoding];
}

void MetalCommandEncoder::visit(const BufferUploadCommand *command) noexcept {
    auto buffer = _device->buffer(command->handle());
    auto temporary = _allocate_temporary_buffer(command->data(), command->size());
    auto blit_encoder = [_command_buffer blitCommandEncoder];
    [blit_encoder copyFromBuffer:temporary
                    sourceOffset:0u
                        toBuffer:buffer
               destinationOffset:command->offset()
                            size:command->size()];
    [blit_encoder endEncoding];
}

void MetalCommandEncoder::visit(const BufferDownloadCommand *command) noexcept {
    auto buffer = _device->buffer(command->handle());
    auto size = command->size();
    auto temporary = _allocate_temporary_buffer(nullptr, command->size());
    auto blit_encoder = [_command_buffer blitCommandEncoder];
    [blit_encoder copyFromBuffer:buffer
                    sourceOffset:command->offset()
                        toBuffer:temporary
               destinationOffset:0u
                            size:size];
    [blit_encoder endEncoding];
    auto host_ptr = command->data();
    [_command_buffer addCompletedHandler:^(id<MTLCommandBuffer>) {
      std::memcpy(host_ptr, temporary.contents, command->size());
    }];
}

void MetalCommandEncoder::visit(const TextureUploadCommand *command) noexcept {
    auto offset = command->offset();
    auto size = command->size();
    auto pixel_bytes = pixel_storage_size(command->storage());
    auto pitch_bytes = pixel_bytes * size.x;
    auto image_bytes = pitch_bytes * size.y * size.z;
    auto temporary = _allocate_temporary_buffer(command->data(), image_bytes);
    auto blit_encoder = [_command_buffer blitCommandEncoder];
    [blit_encoder copyFromBuffer:temporary
                    sourceOffset:0u
               sourceBytesPerRow:pitch_bytes
             sourceBytesPerImage:image_bytes
                      sourceSize:MTLSizeMake(size.x, size.y, size.z)
                       toTexture:_device->texture(command->handle())
                destinationSlice:0u
                destinationLevel:command->level()
               destinationOrigin:MTLOriginMake(offset.x, offset.y, offset.z)];
    [blit_encoder endEncoding];
}

void MetalCommandEncoder::visit(const TextureDownloadCommand *command) noexcept {
    auto offset = command->offset();
    auto size = command->size();
    auto pixel_bytes = pixel_storage_size(command->storage());
    auto pitch_bytes = pixel_bytes * size.x;
    auto image_bytes = pitch_bytes * size.y * size.z;
    auto texture = _device->texture(command->handle());
    auto [buffer, buffer_offset] = _wrap_output_buffer(command->data(), image_bytes);
    auto blit_encoder = [_command_buffer blitCommandEncoder];
    [blit_encoder copyFromTexture:texture
                      sourceSlice:0u
                      sourceLevel:command->level()
                     sourceOrigin:MTLOriginMake(offset.x, offset.y, offset.z)
                       sourceSize:MTLSizeMake(size.x, size.y, size.z)
                         toBuffer:buffer
                destinationOffset:buffer_offset
           destinationBytesPerRow:pitch_bytes
         destinationBytesPerImage:image_bytes];
    [blit_encoder endEncoding];
}

void MetalCommandEncoder::visit(const KernelLaunchCommand *command) noexcept {

    auto function = Function::kernel(command->kernel_uid());
    auto kernel = _device->kernel(command->kernel_uid());
    auto argument_index = 0u;

    auto launch_size = command->launch_size();
    auto block_size = function.block_size();
    auto blocks = (launch_size + block_size - 1u) / block_size;
    LUISA_VERBOSE_WITH_LOCATION(
        "Dispatch kernel #{} in ({}, {}, {}) blocks "
        "with block_size ({}, {}, {}).",
        command->kernel_uid(),
        blocks.x, blocks.y, blocks.z,
        block_size.x, block_size.y, block_size.z);

    auto argument_encoder = kernel.encoder;
    auto argument_buffer_pool = _device->argument_buffer_pool();
    auto argument_buffer = argument_buffer_pool->allocate();
    auto compute_encoder = [_command_buffer computeCommandEncoderWithDispatchType:MTLDispatchTypeConcurrent];
    [compute_encoder setComputePipelineState:kernel.handle];
    [argument_encoder setArgumentBuffer:argument_buffer.handle() offset:argument_buffer.offset()];
    command->decode([&](auto vid, auto argument) noexcept {
        using T = decltype(argument);
        auto mark_usage = [compute_encoder](id<MTLResource> res, auto usage) noexcept {
            switch (usage) {
                case Variable::Usage::READ:
                    [compute_encoder useResource:res
                                           usage:MTLResourceUsageRead];
                    break;
                case Variable::Usage::WRITE:
                    [compute_encoder useResource:res
                                           usage:MTLResourceUsageWrite];
                    break;
                case Variable::Usage::READ_WRITE:
                    [compute_encoder useResource:res
                                           usage:MTLResourceUsageRead
                                                 | MTLResourceUsageWrite];
                default: break;
            }
        };
        if constexpr (std::is_same_v<T, KernelLaunchCommand::BufferArgument>) {
            LUISA_VERBOSE_WITH_LOCATION(
                "Encoding buffer #{} at index {} with offset {}.",
                argument.handle, argument_index, argument.offset);
            auto buffer = _device->buffer(argument.handle);
            [argument_encoder setBuffer:buffer
                                 offset:argument.offset
                                atIndex:kernel.arguments[argument_index++].argumentIndex];
            mark_usage(buffer, function.variable_usage(vid));
        } else if constexpr (std::is_same_v<T, KernelLaunchCommand::TextureArgument>) {
            LUISA_VERBOSE_WITH_LOCATION(
                "Encoding texture #{} at index {}.",
                argument.handle, argument_index);
            auto texture = _device->texture(argument.handle);
            auto arg_id = kernel.arguments[argument_index++].argumentIndex;
            [argument_encoder setTexture:texture atIndex:arg_id];
            mark_usage(texture, function.variable_usage(vid));
        } else {// uniform
            auto ptr = [argument_encoder constantDataAtIndex:kernel.arguments[argument_index++].argumentIndex];
            std::memcpy(ptr, argument.data(), argument.size_bytes());
        }
    });
    auto ptr = [argument_encoder constantDataAtIndex:kernel.arguments[argument_index].argumentIndex];
    std::memcpy(ptr, &launch_size, sizeof(launch_size));
    [compute_encoder setBuffer:argument_buffer.handle() offset:argument_buffer.offset() atIndex:0];
    [compute_encoder dispatchThreadgroups:MTLSizeMake(blocks.x, blocks.y, blocks.z)
                    threadsPerThreadgroup:MTLSizeMake(block_size.x, block_size.y, block_size.z)];
    [compute_encoder endEncoding];

    [_command_buffer addCompletedHandler:^(id<MTLCommandBuffer>) {
      auto arg_buffer = argument_buffer;
      argument_buffer_pool->recycle(arg_buffer);
    }];
}

id<MTLBuffer> MetalCommandEncoder::_allocate_temporary_buffer(const void *data, size_t size) noexcept {
    Clock clock;
    auto temporary = data == nullptr
                         ? [_device->handle() newBufferWithLength:size
                                                          options:MTLResourceStorageModeShared
                                                                  | MTLResourceHazardTrackingModeUntracked]
                         : [_device->handle() newBufferWithBytes:data
                                                          length:size
                                                         options:MTLResourceStorageModeShared
                                                                 | MTLResourceHazardTrackingModeUntracked];
    LUISA_VERBOSE_WITH_LOCATION(
        "Allocated temporary buffer with size {} in {} ms.",
        size, clock.toc());
    return temporary;
}

}
