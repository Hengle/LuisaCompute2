//
// Created by Mike Smith on 2021/6/24.
//

#include <ast/function_builder.h>
#include <runtime/shader.h>
#include <rtx/accel.h>

namespace luisa::compute {

namespace detail {

ShaderInvokeBase &ShaderInvokeBase::operator<<(const Accel &accel) noexcept {
    auto v = _kernel.arguments()[_argument_index++].uid();
    _dispatch_command()->encode_texture_heap(v, accel.handle());
    return *this;
}

}// namespace detail

Accel Device::create_accel() noexcept { return _create<Accel>(); }

Accel::Accel(Device::Handle device) noexcept
    : _device{std::move(device)},
      _handle{_device->create_accel()} {}

void Accel::_destroy() noexcept {
    if (*this) { _device->destroy_accel(_handle); }
}

Accel::~Accel() noexcept { _destroy(); }

Command *Accel::trace_closest(BufferView<Ray> rays, BufferView<Hit> hits) const noexcept {
    _check_built();
    return nullptr;
}

Command *Accel::trace_closest(BufferView<Ray> rays, BufferView<uint32_t> indices, BufferView<Hit> hits) const noexcept {
    _check_built();
    return nullptr;
}

Command *Accel::trace_closest(BufferView<Ray> rays, BufferView<Hit> hits, BufferView<uint> ray_count) const noexcept {
    _check_built();
    return nullptr;
}

Command *Accel::trace_closest(BufferView<Ray> rays, BufferView<uint32_t> indices, BufferView<Hit> hits, BufferView<uint> ray_count) const noexcept {
    _check_built();
    return nullptr;
}

Command *Accel::trace_any(BufferView<Ray> rays, BufferView<bool> hits) const noexcept {
    _check_built();
    return nullptr;
}

Command *Accel::trace_any(BufferView<Ray> rays, BufferView<uint32_t> indices, BufferView<bool> hits) const noexcept {
    _check_built();
    return nullptr;
}

Command *Accel::trace_any(BufferView<Ray> rays, BufferView<bool> hits, BufferView<uint> ray_count) const noexcept {
    _check_built();
    return nullptr;
}

Command *Accel::trace_any(BufferView<Ray> rays, BufferView<uint32_t> indices, BufferView<bool> hits, BufferView<uint> ray_count) const noexcept {
    _check_built();
    return nullptr;
}

Command *Accel::update() noexcept {
    if (!_built) [[unlikely]] {
        LUISA_ERROR_WITH_LOCATION(
            "Geometry #{} is not built when updating.",
            _handle);
    }
    std::span<const float4x4> transforms;
    if (_dirty) {// dirty indicates we need to update instance transforms
        transforms = _instance_transforms;
        _dirty = false;
    }
    return AccelUpdateCommand::create(_handle, transforms);
}

Command *Accel::build(AccelBuildHint mode) noexcept {
    _built = true;
    _dirty = false;
    return AccelBuildCommand::create(
        _handle, mode,
        _instance_mesh_handles, _instance_transforms);
}

void Accel::_mark_dirty() noexcept { _dirty = true; }
void Accel::_mark_should_rebuild() noexcept { _built = false; }

Instance Accel::add(const Mesh &mesh, float4x4 transform) noexcept {
    auto instance_index = _instance_mesh_handles.size();
    _instance_mesh_handles.emplace_back(mesh.handle());
    _instance_transforms.emplace_back(transform);
    _mark_should_rebuild();// adding instances requires rebuilding
    return {this, instance_index};
}

Instance Accel::instance(size_t i) noexcept {
    return {this, i};
}

void Accel::_check_built() const noexcept {
    if (!_built) {
        LUISA_ERROR_WITH_LOCATION(
            "Geometry #{} is not built.",
            _handle);
    }
}

Accel &Accel::operator=(Accel &&rhs) noexcept {
    if (&rhs != this) {
        _destroy();
        _device = std::move(rhs._device);
        _handle = rhs._handle;
        _instance_mesh_handles = std::move(rhs._instance_mesh_handles);
        _instance_transforms = std::move(rhs._instance_transforms);
        _built = rhs._built;
        _dirty = rhs._dirty;
    }
    return *this;
}

detail::Expr<Hit> Accel::trace_closest(detail::Expr<Ray> ray) const noexcept {
    return detail::Expr<Accel>{*this}.trace_closest(ray);
}

detail::Expr<bool> Accel::trace_any(detail::Expr<Ray> ray) const noexcept {
    return detail::Expr<Accel>{*this}.trace_any(ray);
}

void Instance::set_transform(float4x4 m) noexcept {
    _geometry->_instance_transforms[_index] = m;
    _geometry->_mark_dirty();
}

void Instance::set_mesh(const Mesh &mesh) noexcept {
    _geometry->_instance_mesh_handles[_index] = mesh.handle();
    _geometry->_mark_should_rebuild();
}

uint64_t Instance::mesh_handle() const noexcept {
    return _geometry->_instance_mesh_handles[_index];
}

}// namespace luisa::compute
