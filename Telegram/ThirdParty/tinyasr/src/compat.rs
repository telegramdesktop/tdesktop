//! Float math that lives in `std` because it lowers to libm calls.
//!
//! In `std` builds this module is empty and the inherent methods are used; `no_std` builds call the host's C libm
//! (on wasm32, Rust's compiler-builtins provides these symbols).

#[cfg(not(feature = "std"))]
pub(crate) use libm_shim::{F32Ext, F64Ext};

#[cfg(not(feature = "std"))]
mod libm_shim {
    #![allow(unsafe_code)]

    unsafe extern "C" {
        fn sqrtf(x: f32) -> f32;
        fn floorf(x: f32) -> f32;
        fn logf(x: f32) -> f32;
        fn atanf(x: f32) -> f32;
        fn sinf(x: f32) -> f32;
        fn cosf(x: f32) -> f32;
        fn sin(x: f64) -> f64;
        fn cos(x: f64) -> f64;
        fn log(x: f64) -> f64;
        fn pow(x: f64, y: f64) -> f64;
        fn round(x: f64) -> f64;
    }

    /// The libm-backed `f32` methods tinyasr uses.
    pub(crate) trait F32Ext: Sized {
        fn sqrt(self) -> Self;
        fn floor(self) -> Self;
        fn ln(self) -> Self;
        fn atan(self) -> Self;
        fn sin_cos(self) -> (f32, f32);
    }

    impl F32Ext for f32 {
        #[inline]
        fn sqrt(self) -> Self {
            unsafe { sqrtf(self) }
        }
        #[inline]
        fn floor(self) -> Self {
            unsafe { floorf(self) }
        }
        #[inline]
        fn ln(self) -> Self {
            unsafe { logf(self) }
        }
        #[inline]
        fn atan(self) -> Self {
            unsafe { atanf(self) }
        }
        #[inline]
        fn sin_cos(self) -> (f32, f32) {
            unsafe { (sinf(self), cosf(self)) }
        }
    }

    /// The libm-backed `f64` methods tinyasr uses (tables built once at load).
    pub(crate) trait F64Ext {
        fn sin(self) -> Self;
        fn cos(self) -> Self;
        fn ln(self) -> Self;
        fn powf(self, y: Self) -> Self;
        fn round(self) -> Self;
    }

    impl F64Ext for f64 {
        fn sin(self) -> Self {
            unsafe { sin(self) }
        }
        fn cos(self) -> Self {
            unsafe { cos(self) }
        }
        fn ln(self) -> Self {
            unsafe { log(self) }
        }
        fn powf(self, y: Self) -> Self {
            unsafe { pow(self, y) }
        }
        fn round(self) -> Self {
            unsafe { round(self) }
        }
    }
}
