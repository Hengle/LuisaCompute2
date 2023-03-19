//
// Created by Mike on 3/18/2023.
//

#include <runtime/rhi/command.h>
#include <backends/cuda/cuda_error.h>
#include <backends/cuda/cuda_stream.h>
#include <backends/cuda/cuda_buffer.h>
#include <backends/cuda/cuda_accel.h>
#include <backends/cuda/cuda_mipmap_array.h>
#include <backends/cuda/cuda_bindless_array.h>
#include <backends/cuda/cuda_command_encoder.h>
#include <backends/cuda/cuda_shader_native.h>

namespace luisa::compute::cuda {

CUDAShaderNative::CUDAShaderNative(const char *ptx, size_t ptx_size,
                                   const char *entry, uint3 block_size) noexcept
    : _entry{entry},
      _block_size{block_size.x, block_size.y, block_size.z} {
    auto ret = cuModuleLoadData(&_module, ptx);
    if (ret == CUDA_ERROR_UNSUPPORTED_PTX_VERSION) {

        LUISA_WARNING_WITH_LOCATION(
            "The PTX version is not supported by the installed CUDA driver. "
            "Trying to patch the PTX to make it compatible with the driver. "
            "This might cause unexpected behavior. "
            "Please consider upgrading your CUDA driver.");

        // For users with newer CUDA and older driver,
        // the generated PTX might be reported invalid.
        // We have to patch the ".version 7.x" instruction.
        using namespace std::string_view_literals;
        luisa::string s{ptx, ptx_size};
        auto pattern = ".version 7."sv;
        if (auto p = s.find(pattern); p != luisa::string_view::npos) {
            auto begin = p + pattern.size();
            auto end = begin;
            for (; isdigit(s[end]); end++) {}
            s.replace(begin, end - begin, "0");
        }
        ret = cuModuleLoadData(&_module, s.c_str());
    }
    LUISA_CHECK_CUDA(ret);
    LUISA_CHECK_CUDA(cuModuleGetFunction(&_function, _module, entry));
}

CUDAShaderNative::~CUDAShaderNative() noexcept {
    LUISA_CHECK_CUDA(cuModuleUnload(_module));
}

void CUDAShaderNative::launch(CUDACommandEncoder &encoder, ShaderDispatchCommand *command) const noexcept {

    // TODO: support indirect dispatch
    LUISA_ASSERT(!command->is_indirect(), "Indirect dispatch is not supported on CUDA backend.");

    static thread_local std::array<std::byte, 65536u> argument_buffer;// should be enough
    static thread_local std::array<void *, 256u> arguments;           // should be enough, too

    auto argument_buffer_offset = static_cast<size_t>(0u);
    auto argument_count = 0u;
    auto allocate_argument = [&](size_t bytes) noexcept {
        static constexpr auto alignment = 16u;
        auto offset = (argument_buffer_offset + alignment - 1u) / alignment * alignment;
        argument_buffer_offset = offset + bytes;
        if (argument_buffer_offset > argument_buffer.size()) {
            LUISA_ERROR_WITH_LOCATION(
                "Too many arguments in ShaderDispatchCommand");
        }
        return arguments[argument_count++] = argument_buffer.data() + offset;
    };

    for (auto &&arg : command->arguments()) {
        using Tag = ShaderDispatchCommand::Argument::Tag;
        switch (arg.tag) {
            case Tag::BUFFER: {
                auto buffer = reinterpret_cast<const CUDABuffer *>(arg.buffer.handle);
                auto binding = buffer->binding(arg.buffer.offset, arg.buffer.size);
                auto ptr = allocate_argument(sizeof(binding));
                std::memcpy(ptr, &binding, sizeof(binding));
                break;
            }
            case Tag::TEXTURE: {
                auto texture = reinterpret_cast<const CUDAMipmapArray *>(arg.texture.handle);
                auto binding = texture->binding(arg.texture.level);
                auto ptr = allocate_argument(sizeof(binding));
                std::memcpy(ptr, &binding, sizeof(binding));
                break;
            }
            case Tag::UNIFORM: {
                auto uniform = command->uniform(arg.uniform);
                auto ptr = allocate_argument(uniform.size_bytes());
                std::memcpy(ptr, uniform.data(), uniform.size_bytes());
                break;
            }
            case Tag::BINDLESS_ARRAY: {
                auto array = reinterpret_cast<const CUDABindlessArray *>(arg.bindless_array.handle);
                auto binding = array->binding();
                auto ptr = allocate_argument(sizeof(binding));
                std::memcpy(ptr, &binding, sizeof(binding));
                break;
            }
            case Tag::ACCEL: {
                auto accel = reinterpret_cast<const CUDAAccel *>(arg.accel.handle);
                auto binding = accel->binding();
                auto ptr = allocate_argument(sizeof(binding));
                std::memcpy(ptr, &binding, sizeof(binding));
                break;
            }
        }
    }
    // the last argument is the launch size
    auto launch_size = command->dispatch_size();
    auto ptr = allocate_argument(sizeof(launch_size));
    std::memcpy(ptr, &launch_size, sizeof(launch_size));
    // launch configuration
    auto block_size = make_uint3(_block_size[0], _block_size[1], _block_size[2]);;
    auto blocks = (launch_size + block_size - 1u) / block_size;
    LUISA_VERBOSE_WITH_LOCATION(
        "Dispatching native shader #{} ({}) with {} argument(s) "
        "in ({}, {}, {}) blocks of size ({}, {}, {}).",
        command->handle(), _entry, arguments.size(),
        blocks.x, blocks.y, blocks.z,
        block_size.x, block_size.y, block_size.z);
    // launch
    auto cuda_stream = encoder.stream()->handle();
    LUISA_CHECK_CUDA(cuLaunchKernel(
        _function,
        blocks.x, blocks.y, blocks.z,
        block_size.x, block_size.y, block_size.z,
        0u, cuda_stream,
        arguments.data(), nullptr));

    //        static thread_local std::array<std::byte, 65536u> argument_buffer;// should be enough...
    //        static thread_local std::vector<void *> arguments;
    //        auto argument_buffer_offset = static_cast<size_t>(0u);
    //        auto allocate_argument = [&](size_t bytes) noexcept {
    //            static constexpr auto alignment = 16u;
    //            auto offset = (argument_buffer_offset + alignment - 1u) / alignment * alignment;
    //            argument_buffer_offset = offset + bytes;
    //            if (argument_buffer_offset > argument_buffer.size()) {
    //                LUISA_ERROR_WITH_LOCATION(
    //                    "Too many arguments in ShaderDispatchCommand");
    //            }
    //            return arguments.emplace_back(argument_buffer.data() + offset);
    //        };
    //        arguments.clear();
    //        arguments.reserve(32u);
    //        command->decode([&](auto argument) noexcept -> void {
    //            using T = decltype(argument);
    //            if constexpr (std::is_same_v<T, ShaderDispatchCommand::BufferArgument>) {
    //                auto ptr = allocate_argument(sizeof(CUdeviceptr));
    //                auto buffer = argument.handle + argument.offset;
    //                std::memcpy(ptr, &buffer, sizeof(CUdeviceptr));
    //            } else if constexpr (std::is_same_v<T, ShaderDispatchCommand::TextureArgument>) {
    //                auto mipmap_array = reinterpret_cast<CUDAMipmapArray *>(argument.handle);
    //                auto surface = mipmap_array->surface(argument.level);
    //                auto ptr = allocate_argument(sizeof(CUDASurface));
    //                std::memcpy(ptr, &surface, sizeof(CUDASurface));
    //            } else if constexpr (std::is_same_v<T, ShaderDispatchCommand::BindlessArrayArgument>) {
    //                auto ptr = allocate_argument(sizeof(CUDABindlessArray::SlotSOA));
    //                auto array = reinterpret_cast<CUDABindlessArray *>(argument.handle)->handle();
    //                std::memcpy(ptr, &array, sizeof(CUDABindlessArray::SlotSOA));
    //            } else if constexpr (std::is_same_v<T, ShaderDispatchCommand::AccelArgument>) {
    //                auto ptr = allocate_argument(sizeof(CUDAAccel::Binding));
    //                auto accel = reinterpret_cast<CUDAAccel *>(argument.handle);
    //                CUDAAccel::Binding binding{.handle = accel->handle(), .instances = accel->instance_buffer()};
    //                std::memcpy(ptr, &binding, sizeof(CUDAAccel::Binding));
    //            } else {// uniform
    //                static_assert(std::same_as<T, ShaderDispatchCommand::UniformArgument>);
    //                auto ptr = allocate_argument(argument.size);
    //                std::memcpy(ptr, argument.data, argument.size);
    //            }
    //        });
    //        // the last one is always the launch size
    //        auto launch_size = command->dispatch_size();
    //        auto ptr = allocate_argument(sizeof(luisa::uint3));
    //        std::memcpy(ptr, &launch_size, sizeof(luisa::uint3));
    //        auto block_size = command->kernel().block_size();
    //        auto blocks = (launch_size + block_size - 1u) / block_size;
    //        LUISA_VERBOSE_WITH_LOCATION(
    //            "Dispatching native shader #{} ({}) with {} argument(s) "
    //            "in ({}, {}, {}) blocks of size ({}, {}, {}).",
    //            command->handle(), _entry, arguments.size(),
    //            blocks.x, blocks.y, blocks.z,
    //            block_size.x, block_size.y, block_size.z);
    //        auto cuda_stream = stream->handle();
    //        LUISA_CHECK_CUDA(cuLaunchKernel(
    //            _function,
    //            blocks.x, blocks.y, blocks.z,
    //            block_size.x, block_size.y, block_size.z,
    //            0u, cuda_stream,
    //            arguments.data(), nullptr));
}

}// namespace luisa::compute::cuda
