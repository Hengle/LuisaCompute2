use std::ffi::{CStr, CString};
use std::fmt::{Debug, Display};
use std::{
    path::{Path, PathBuf},
    sync::Arc,
};

use crate::proxy::ProxyBackend;
use api::{CreatedSwapchainInfo, PixelFormat};
use libc::{c_char, c_void};
use libloading::Library;
use luisa_compute_api_types as api;
use luisa_compute_ir::{
    ir::{self, KernelModule},
    CArc, CArcSharedBlock,
};

pub mod proxy;

#[derive(Debug, Copy, Clone, Ord, PartialOrd, Eq, PartialEq, Hash)]
pub enum BackendErrorKind {
    BackendNotFound,
    KernelExecution,
    KernelCompilation,
    Network,
    Other,
}

#[derive(Clone)]
pub struct BackendError {
    pub kind: BackendErrorKind,
    pub message: String,
}

impl Debug for BackendError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // f.debug_struct("BackendError")
        //     .field("kind", &self.kind)
        //     .field("message", &self.message)
        //     .finish()
        writeln!(
            f,
            "BackendError: {:?}, message:\n {}",
            self.kind, self.message
        )?;
        writeln!(f, "note: run with `LUISA_BACKTRACE=1` environment variable to display a backtrace for DSL code")
    }
}

pub type Result<T> = std::result::Result<T, BackendError>;

pub(crate) struct Interface {
    #[allow(dead_code)]
    pub(crate) lib: libloading::Library,
    pub(crate) inner: api::LibInterface,
}

unsafe impl Send for Interface {}

unsafe impl Sync for Interface {}

impl Interface {
    pub unsafe fn new(lib_path: &Path) -> std::result::Result<Self, libloading::Error> {
        let lib = libloading::Library::new(lib_path)?;
        let create_interface: unsafe extern "C" fn() -> api::LibInterface =
            *lib.get(b"luisa_compute_lib_interface\0")?;
        let interface = create_interface();
        Ok(Self {
            lib,
            inner: interface,
        })
    }
}

struct BackendProvider {
    pub(crate) context: api::Context,
    pub(crate) interface: Arc<Interface>,
}

impl BackendProvider {
    unsafe fn new(path: &PathBuf) -> std::result::Result<Self, libloading::Error> {
        let interface = Interface::new(path.as_ref())
            .map_err(|e| {
                log::warn!("failed to load {}: {}", path.display(), e);
                e
            })
            .map(Arc::new)?;
        let parent = path.parent().unwrap();
        let lib_path_c_str = std::ffi::CString::new(parent.to_str().unwrap()).unwrap();


        unsafe extern "C" fn callback(info: api::LoggerMessage) {
            let level = CStr::from_ptr(info.level as *mut c_char).to_str().unwrap();
            let msg = CStr::from_ptr(info.message as *mut c_char)
                .to_str()
                .unwrap();
            let target = if info.target.is_null() {
                "lc-cpp".to_string()
            } else {
                CStr::from_ptr(info.target as *mut c_char)
                    .to_str()
                    .unwrap()
                    .to_string()
            };
            let target = &target;
            match level {
                "I" | "Info" => log::log!(target: target, log::Level::Info, "{}", msg),
                "W" | "Warning" => log::log!(target: target, log::Level::Warn, "{}", msg),
                "E" | "C" | "Error" => {
                    log::log!(target: target, log::Level::Error, "{}", msg)
                }
                "D" | "Debug" => log::log!(target: target, log::Level::Debug, "{}", msg),
                "T" | "Trace" => log::log!(target: target, log::Level::Trace, "{}", msg),
                _ => panic!("unknown log level: {}", level),
            }
        }
        (interface.inner.set_logger_callback)(callback);
        let context = (interface.inner.create_context)(lib_path_c_str.as_c_str().as_ptr());
        Ok(Self { interface, context })
    }
}

pub struct Context {
    pub(crate) cpp: std::result::Result<BackendProvider, libloading::Error>,
    pub(crate) rust: std::result::Result<BackendProvider, libloading::Error>,
}

impl Context {
    pub fn new<P: AsRef<Path>>(path: P) -> Self {
        unsafe {
            let lib_path = path.as_ref().to_path_buf();
            let cpp_dll = if cfg!(target_os = "windows") {
                lib_path.join("lc-api.dll")
            } else if cfg!(target_os = "linux") {
                lib_path.join("liblc-api.so")
            } else {
                todo!()
            };
            let rust_dll = if cfg!(target_os = "windows") {
                lib_path.join("luisa_compute_backend_impl.dll")
            } else if cfg!(target_os = "linux") {
                lib_path.join("libluisa_compute_backend_impl.so")
            } else {
                todo!()
            };
            let cpp = BackendProvider::new(&cpp_dll);
            let rust = BackendProvider::new(&rust_dll);
            Self { cpp, rust }
        }
    }

    pub fn create_device(
        &self,
        device: &str,
        config: serde_json::Value,
    ) -> crate::Result<ProxyBackend> {
        match device {
            "cpu" | "remote" => {
                match &self.rust {
                    Ok(provider) => Ok(ProxyBackend::new(provider, device, config)),
                    Err(err)=>{
                        let libname = if cfg!(target_os = "windows") {
                            "luisa_compute_backend_impl.dll"
                        } else {
                            "libluisa_compute_backend_impl.so"
                        };

                        let err = err.to_string();
                        Err(BackendError {
                            kind: BackendErrorKind::BackendNotFound,
                            message: format!("device {0} not found. {0} device may not be enabled or {1} is not found. detailed error: {2}", device, libname, err),
                        })
                    }
                }
            }
            "cuda" | "dx" | "metal" => {
                match &self.cpp {
                    Ok(provider) => Ok(ProxyBackend::new(provider, device, config)),
                    Err(err)=>{
                        let libname = if cfg!(target_os = "windows") {
                            "lc-api.dll"
                        } else {
                            "liblc-api.so"
                        };

                        let err = err.to_string();
                        Err(BackendError {
                            kind: BackendErrorKind::BackendNotFound,
                            message: format!("device {0} not found. {0} device may not be enabled or {1} is not found. detailed error: {2}", device, libname, err),
                        })
                    }
                }
            }
            _ => panic!("unsupported device: {}", device),
        }
    }
}

#[repr(C)]
pub struct RustcInfo {
    pub channel: &'static str,
    pub version: &'static str,
    pub date: &'static str,
}

pub trait Backend: Sync + Send {
    fn create_buffer(&self, ty: &CArc<ir::Type>, count: usize) -> Result<api::CreatedBufferInfo>;
    fn destroy_buffer(&self, buffer: api::Buffer);
    fn create_texture(
        &self,
        format: PixelFormat,
        dimension: u32,
        width: u32,
        height: u32,
        depth: u32,
        mipmap_levels: u32,
    ) -> Result<api::CreatedResourceInfo>;
    fn destroy_texture(&self, texture: api::Texture);
    fn create_bindless_array(&self, size: usize) -> Result<api::CreatedResourceInfo>;
    fn destroy_bindless_array(&self, array: api::BindlessArray);
    fn create_stream(&self, tag: api::StreamTag) -> Result<api::CreatedResourceInfo>;
    fn destroy_stream(&self, stream: api::Stream);
    fn synchronize_stream(&self, stream: api::Stream) -> Result<()>;
    fn dispatch(
        &self,
        stream: api::Stream,
        command_list: &[api::Command],
        callback: (extern "C" fn(*mut u8), *mut u8),
    ) -> Result<()>;
    fn create_swapchain(
        &self,
        window_handle: u64,
        stream_handle: api::Stream,
        width: u32,
        height: u32,
        allow_hdr: bool,
        vsync: bool,
        back_buffer_size: u32,
    ) -> Result<CreatedSwapchainInfo>;
    fn destroy_swapchain(&self, swap_chain: api::Swapchain);
    fn present_display_in_stream(
        &self,
        stream_handle: api::Stream,
        swapchain_handle: api::Swapchain,
        image_handle: api::Texture,
    );
    fn create_shader(
        &self,
        kernel: &KernelModule,
        options: &api::ShaderOption,
    ) -> Result<api::CreatedShaderInfo>;
    fn shader_cache_dir(&self, shader: api::Shader) -> Option<PathBuf>;
    fn destroy_shader(&self, shader: api::Shader);
    fn create_event(&self) -> Result<api::CreatedResourceInfo>;
    fn destroy_event(&self, event: api::Event);
    fn signal_event(&self, event: api::Event, stream: api::Stream);
    fn wait_event(&self, event: api::Event, stream: api::Stream) -> Result<()>;
    fn synchronize_event(&self, event: api::Event) -> Result<()>;
    fn create_mesh(&self, option: api::AccelOption) -> Result<api::CreatedResourceInfo>;
    fn create_procedural_primitive(
        &self,
        option: api::AccelOption,
    ) -> Result<api::CreatedResourceInfo>;
    fn destroy_mesh(&self, mesh: api::Mesh);
    fn destroy_procedural_primitive(&self, primitive: api::ProceduralPrimitive);
    fn create_accel(&self, option: api::AccelOption) -> Result<api::CreatedResourceInfo>;
    fn destroy_accel(&self, accel: api::Accel);
    fn query(&self, property: &str) -> Option<String>;
}

// #[no_mangle]
// pub extern "C" fn lc_rs_destroy_backend(ptr: *mut c_void) {
//     unsafe {
//         let ptr = ptr as *mut Box<dyn Backend>;
//         drop(Box::from_raw(ptr));
//     }
// }
//
// #[no_mangle]
// pub extern "C" fn lc_rs_create_backend(name: *const std::ffi::c_char) -> *mut c_void {
//     let name = unsafe { std::ffi::CStr::from_ptr(name) };
//     let name = name.to_str().unwrap();
//     let backend = match name {
//         #[cfg(feature = "cpu")]
//         "rust" => Box::new(rust::RustBackend::new()),
//         #[cfg(feature = "remote")]
//         "remote" => Box::new(remote::RemoteBackend::new()),
//         _ => panic!("unknown backend"),
//     };
// }
//
fn get_backend<'a, B: Backend>(backend: api::Device) -> &'a B {
    unsafe { &*(backend.0 as *mut B) }
}

fn map<T>(a: crate::Result<T>) -> api::Result<T> {
    unsafe {
        match a {
            crate::Result::Ok(a) => api::Result::Ok(a),
            crate::Result::Err(a) => api::Result::Err(api::BackendError {
                kind: match a.kind {
                    crate::BackendErrorKind::BackendNotFound => {
                        api::BackendErrorKind::BackendNotFound
                    }
                    crate::BackendErrorKind::KernelExecution => {
                        api::BackendErrorKind::KernelExecution
                    }
                    crate::BackendErrorKind::KernelCompilation => {
                        api::BackendErrorKind::KernelCompilation
                    }
                    crate::BackendErrorKind::Network => api::BackendErrorKind::Network,
                    crate::BackendErrorKind::Other => api::BackendErrorKind::Other,
                },
                message: CString::new(a.message).unwrap().into_raw(),
            }),
        }
    }
}

extern "C" fn create_buffer<B: Backend>(
    backend: api::Device,
    ty: *const c_void,
    count: usize,
) -> api::Result<api::CreatedBufferInfo> {
    let backend: &B = get_backend(backend);
    let ty = unsafe { &*(ty as *const CArc<ir::Type>) };
    map(backend.create_buffer(ty, count))
}

pub extern "C" fn destroy_buffer<B: Backend>(backend: api::Device, buffer: api::Buffer) {
    let backend: &B = get_backend(backend);
    backend.destroy_buffer(buffer)
}

//
pub extern "C" fn create_texture<B: Backend>(
    backend: api::Device,
    format: PixelFormat,
    dimension: u32,
    width: u32,
    height: u32,
    depth: u32,
    mipmap_levels: u32,
) -> api::Result<api::CreatedResourceInfo> {
    let backend: &B = get_backend(backend);
    map(backend.create_texture(format, dimension, width, height, depth, mipmap_levels))
}
//

extern "C" fn destroy_texture<B: Backend>(backend: api::Device, texture: api::Texture) {
    let backend: &B = get_backend(backend);
    backend.destroy_texture(texture)
}

extern "C" fn create_bindless_array<B: Backend>(
    backend: api::Device,
    size: usize,
) -> api::Result<api::CreatedResourceInfo> {
    let backend: &B = get_backend(backend);
    map(backend.create_bindless_array(size))
}

extern "C" fn destroy_bindless_array<B: Backend>(backend: api::Device, array: api::BindlessArray) {
    let backend: &B = get_backend(backend);
    backend.destroy_bindless_array(array)
}

extern "C" fn create_stream<B: Backend>(
    backend: api::Device,
    tag: api::StreamTag,
) -> api::Result<api::CreatedResourceInfo> {
    let backend: &B = get_backend(backend);
    map(backend.create_stream(tag))
}

extern "C" fn destroy_stream<B: Backend>(backend: api::Device, stream: api::Stream) {
    let backend: &B = get_backend(backend);
    backend.destroy_stream(stream)
}

extern "C" fn synchronize_stream<B: Backend>(
    backend: api::Device,
    stream: api::Stream,
) -> api::Result<u8> {
    let backend: &B = get_backend(backend);
    map(backend.synchronize_stream(stream).map(|_| 0))
}

extern "C" fn dispatch<B: Backend>(
    backend: api::Device,
    stream: api::Stream,
    command_list: api::CommandList,
    callback: api::DispatchCallback,
    user_data: *mut u8,
) -> api::Result<u8> {
    let backend: &B = get_backend(backend);
    let command_list =
        unsafe { std::slice::from_raw_parts(command_list.commands, command_list.commands_count) };
    map(backend
        .dispatch(stream, command_list, (callback, user_data))
        .map(|_| 0))
}
//

unsafe extern "C" fn create_shader<B: Backend>(
    backend: api::Device,
    kernel: api::KernelModule,
    option: &api::ShaderOption,
) -> api::Result<api::CreatedShaderInfo> {
    let backend: &B = get_backend(backend);
    let kernel = &*(kernel.ptr as *const ir::KernelModule);
    map(backend.create_shader(kernel, option))
}

extern "C" fn destroy_shader<B: Backend>(backend: api::Device, shader: api::Shader) {
    let backend: &B = get_backend(backend);
    backend.destroy_shader(shader)
}

extern "C" fn create_event<B: Backend>(
    backend: api::Device,
) -> api::Result<api::CreatedResourceInfo> {
    let backend: &B = get_backend(backend);
    map(backend.create_event())
}

extern "C" fn destroy_event<B: Backend>(backend: api::Device, event: api::Event) {
    let backend: &B = get_backend(backend);
    backend.destroy_event(event)
}

extern "C" fn signal_event<B: Backend>(
    backend: api::Device,
    event: api::Event,
    stream: api::Stream,
) {
    let backend: &B = get_backend(backend);
    backend.signal_event(event, stream)
}

extern "C" fn wait_event<B: Backend>(
    backend: api::Device,
    event: api::Event,
    stream: api::Stream,
) -> api::Result<u8> {
    let backend: &B = get_backend(backend);
    map(backend.wait_event(event, stream).map(|_| 0u8))
}

extern "C" fn synchronize_event<B: Backend>(
    backend: api::Device,
    event: api::Event,
) -> api::Result<u8> {
    let backend: &B = get_backend(backend);
    map(backend.synchronize_event(event).map(|_| 0u8))
}

extern "C" fn create_accel<B: Backend>(
    backend: api::Device,
    option: &api::AccelOption,
) -> api::Result<api::CreatedResourceInfo> {
    let backend: &B = get_backend(backend);
    map(backend.create_accel(*option))
}

extern "C" fn destroy_accel<B: Backend>(backend: api::Device, accel: api::Accel) {
    let backend: &B = get_backend(backend);
    backend.destroy_accel(accel)
}

extern "C" fn create_mesh<B: Backend>(
    backend: api::Device,
    option: &api::AccelOption,
) -> api::Result<api::CreatedResourceInfo> {
    let backend: &B = get_backend(backend);
    map(backend.create_mesh(*option))
}

extern "C" fn destroy_mesh<B: Backend>(backend: api::Device, mesh: api::Mesh) {
    let backend: &B = get_backend(backend);
    backend.destroy_mesh(mesh)
}

extern "C" fn create_procedural_primitive<B: Backend>(
    backend: api::Device,
    option: &api::AccelOption,
) -> api::Result<api::CreatedResourceInfo> {
    let backend: &B = get_backend(backend);
    map(backend.create_procedural_primitive(*option))
}

extern "C" fn destroy_procedural_primitive<B: Backend>(
    backend: api::Device,
    primitive: api::ProceduralPrimitive,
) {
    let backend: &B = get_backend(backend);
    backend.destroy_procedural_primitive(primitive)
}

extern "C" fn query<B: Backend>(
    backend: api::Device,
    property: *const std::ffi::c_char,
) -> *mut std::ffi::c_char {
    let backend: &B = get_backend(backend);
    let property = unsafe { std::ffi::CStr::from_ptr(property) };
    let property = property.to_str().unwrap();
    let value = backend.query(property);
    let value = std::ffi::CString::new(value.unwrap_or("".into())).unwrap();
    value.into_raw()
}

extern "C" fn create_swapchain<B: Backend>(
    backend: api::Device,
    window_handle: u64,
    stream_handle: api::Stream,
    width: u32,
    height: u32,
    allow_hdr: bool,
    vsync: bool,
    back_buffer_size: u32,
) -> api::Result<api::CreatedSwapchainInfo> {
    let backend: &B = get_backend(backend);
    map(backend.create_swapchain(
        window_handle,
        stream_handle,
        width,
        height,
        allow_hdr,
        vsync,
        back_buffer_size,
    ))
}

extern "C" fn present_display_in_stream<B: Backend>(
    backend: api::Device,
    stream_handle: api::Stream,
    swapchain: api::Swapchain,
    image: api::Texture,
) {
    let backend: &B = get_backend(backend);
    backend.present_display_in_stream(stream_handle, swapchain, image)
}

extern "C" fn destroy_swapchain<B: Backend>(backend: api::Device, swapchain: api::Swapchain) {
    let backend: &B = get_backend(backend);
    backend.destroy_swapchain(swapchain)
}

extern "C" fn destroy_device<B: Backend>(device: api::DeviceInterface) {
    let backend: Box<B> = unsafe { Box::from_raw(device.device.0 as *mut B) };
    drop(backend)
}

#[inline]
pub fn create_device_interface<B: Backend>(backend: B) -> api::DeviceInterface {
    let backend = Box::new(backend);
    let backend_ptr = Box::into_raw(backend);
    api::DeviceInterface {
        device: api::Device(backend_ptr as u64),
        destroy_device: destroy_device::<B>,
        create_buffer: create_buffer::<B>,
        destroy_buffer: destroy_buffer::<B>,
        create_texture: create_texture::<B>,
        destroy_texture: destroy_texture::<B>,
        create_bindless_array: create_bindless_array::<B>,
        destroy_bindless_array: destroy_bindless_array::<B>,
        create_stream: create_stream::<B>,
        destroy_stream: destroy_stream::<B>,
        synchronize_stream: synchronize_stream::<B>,
        dispatch: dispatch::<B>,
        create_swapchain: create_swapchain::<B>,
        present_display_in_stream: present_display_in_stream::<B>,
        destroy_swapchain: destroy_swapchain::<B>,
        create_shader: create_shader::<B>,
        destroy_shader: destroy_shader::<B>,
        create_event: create_event::<B>,
        destroy_event: destroy_event::<B>,
        signal_event: signal_event::<B>,
        synchronize_event: synchronize_event::<B>,
        wait_event: wait_event::<B>,
        create_mesh: create_mesh::<B>,
        destroy_mesh: destroy_mesh::<B>,
        create_procedural_primitive: create_procedural_primitive::<B>,
        destroy_procedural_primitive: destroy_procedural_primitive::<B>,
        create_accel: create_accel::<B>,
        destroy_accel: destroy_accel::<B>,
        query: query::<B>,
    }
}
