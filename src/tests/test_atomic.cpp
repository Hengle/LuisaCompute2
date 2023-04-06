//
// Created by Mike Smith on 2021/6/23.
//

#include <core/clock.h>
#include <core/logging.h>
#include <runtime/context.h>
#include <runtime/device.h>
#include <runtime/stream.h>
#include <dsl/syntax.h>
#include <dsl/sugar.h>

using namespace luisa;
using namespace luisa::compute;

struct Something {
    uint x;
    float3 v;
};

LUISA_STRUCT(Something, x, v){};

int main(int argc, char *argv[]) {

    log_level_verbose();

    Context context{argv[0]};
    if (argc <= 1) {
        LUISA_INFO("Usage: {} <backend>. <backend>: cuda, dx, ispc, metal", argv[0]);
        exit(1);
    }
    auto device = context.create_device(argv[1]);

    auto buffer = device.create_buffer<uint>(4u);
    Kernel1D count_kernel = [&]() noexcept {
        Constant<uint> constant{1u};
        Var x = buffer->atomic(3u).fetch_add(constant[0]);
        if_(x == 0u, [&] {
            buffer->write(0u, 1u);
        });
    };
    auto count = device.compile(count_kernel);

    auto host_buffer = make_uint4(0u);
    auto stream = device.create_stream();

    Clock clock;
    clock.tic();
    stream << buffer.copy_from(&host_buffer)
           << count().dispatch(102400u)
           << buffer.copy_to(&host_buffer)
           << synchronize();
    auto time = clock.toc();
    LUISA_INFO("Count: {} {}, Time: {} ms", host_buffer.x, host_buffer.w, time);
    LUISA_ASSERT(host_buffer.x == 1u && host_buffer.w == 102400u,
                 "Atomic operation failed.");

    auto atomic_float_buffer = device.create_buffer<float>(1u);
    Kernel1D add_kernel = [&](BufferFloat buffer) noexcept {
        buffer.atomic(0u).fetch_sub(-1.f);
    };
    auto add_shader = device.compile(add_kernel);

    Kernel1D vector_atomic_kernel = [](BufferFloat3 buffer) noexcept {
        buffer.atomic(0u).x.fetch_add(1.f);
    };

    Kernel1D matrix_atomic_kernel = [](BufferFloat2x2 buffer) noexcept {
        buffer.atomic(0u)[1].x.fetch_add(1.f);
    };

    Kernel1D array_atomic_kernel = [](BufferVar<std::array<std::array<float4, 3u>, 5u>> buffer) noexcept {
        buffer.atomic(0u)[1][2][3].fetch_add(1.f);
    };

    Kernel1D struct_atomic_kernel = [](BufferVar<Something> buffer) noexcept {
        // TODO
    };

    auto result = 0.f;
    stream << atomic_float_buffer.copy_from(&result)
           << add_shader(atomic_float_buffer).dispatch(1024u)
           << atomic_float_buffer.copy_to(&result)
           << synchronize();
    LUISA_INFO("Atomic float result: {}.", result);
    LUISA_ASSERT(result == 1024.f, "Atomic float operation failed.");
}
