//! The lang items a `no_std` static/shared library has to carry.
//!
//! A Rust library normally leaves both to the final binary, but a C or C++ host cannot define Rust
//! lang items, so the build without `std` provides them itself. `std` builds don't have
//! this module. The allocator forwards to the C runtime the host already links; panics abort.

#![allow(unsafe_code)]

use core::alloc::{GlobalAlloc, Layout};

unsafe extern "C" {
    fn malloc(size: usize) -> *mut u8;
    fn free(ptr: *mut u8);
    fn abort() -> !;
}

/// Forwards to the host C allocator. Only pointer alignment is assumed from `malloc`; stricter
/// alignments are over-allocated and aligned by hand, with the original
/// pointer stored in the word just below the returned address.
struct HostAlloc;

const MALLOC_ALIGN: usize = core::mem::align_of::<usize>();

unsafe impl GlobalAlloc for HostAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if layout.align() <= MALLOC_ALIGN {
            return unsafe { malloc(layout.size()) };
        }
        let Some(padded) = layout.size().checked_add(layout.align() + core::mem::size_of::<usize>()) else {
            return core::ptr::null_mut();
        };
        let raw = unsafe { malloc(padded) };
        if raw.is_null() {
            return raw;
        }
        let reserved = unsafe { raw.add(core::mem::size_of::<*mut u8>()) };
        let aligned = unsafe { reserved.add(reserved.align_offset(layout.align())) };
        // SAFETY: `aligned` is at least one word above `raw` and inside the padded block.
        unsafe { (aligned as *mut *mut u8).sub(1).write(raw) };
        aligned
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        if layout.align() <= MALLOC_ALIGN {
            unsafe { free(ptr) };
        } else {
            // SAFETY: alloc stored the original pointer just below the address it returned.
            unsafe { free((ptr as *mut *mut u8).sub(1).read()) };
        }
    }
}

#[global_allocator]
static ALLOCATOR: HostAlloc = HostAlloc;

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    // No message: formatting would pull in core::fmt, which a no_std build tries to leave out.
    unsafe { abort() }
}

/// Builds abort on panic, but Android's precompiled `core`/`alloc` still reference this symbol.
/// Only defined on Android: elsewhere a second Rust staticlib (the wallet engine) already
/// provides it, so exporting it twice makes the final link fail with a duplicate definition.
#[cfg(target_os = "android")]
#[unsafe(no_mangle)]
pub extern "C" fn rust_eh_personality() {}
