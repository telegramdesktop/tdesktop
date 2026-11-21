//! Matrix kernels. Float: the micro-kernel is explicit NEON on aarch64; elsewhere it is written so LLVM
//! auto-vectorizes it (SIMD128 on wasm, SSE on x86-64). Integer weights (`Quant`): see below.

use alloc::vec;
use alloc::vec::Vec;

#[cfg(not(feature = "std"))]
use crate::compat::F32Ext;

// Micro-kernel tile: MR rows x NR columns of accumulators. aarch64 has 32 vector registers
// (4x16 = 16 accumulators); wasm SIMD / x86-64 SSE have 16 (2x16 = 8).
const NR: usize = 16; // output columns per panel
#[cfg(target_arch = "aarch64")]
const MR: usize = 4; // output rows per micro-kernel call
#[cfg(not(target_arch = "aarch64"))]
const MR: usize = 2;
const MB: usize = 64; // rows per cache block

/// Right-hand matrix packed into panels of NR output columns, k-major inside a panel, so the
/// micro-kernel reads NR contiguous weights per k step (outer-product form, no reductions).
pub struct Packed {
    pub n: usize,
    pub k: usize,
    data: Vec<f32>, // ceil(n / NR) panels x k x NR, zero-padded
}

impl Packed {
    /// From b (n x k): row j holds the weights of output column j (PyTorch Linear layout).
    pub fn from_rows(b: &[f32], n: usize, k: usize) -> Packed {
        let mut data = vec![0.0; n.div_ceil(NR) * k * NR];
        for j in 0..n {
            let (panel, l) = (j / NR, j % NR);
            for (p, v) in b[j * k..(j + 1) * k].iter().enumerate() {
                data[(panel * k + p) * NR + l] = *v;
            }
        }
        Packed { n, k, data }
    }

    fn panel(&self, i: usize) -> &[f32] {
        &self.data[i * self.k * NR..(i + 1) * self.k * NR]
    }
}

/// out (m x n) = a (m x k) * B (+ bias per column).
pub fn gemm(a: &[f32], m: usize, b: &Packed, bias: Option<&[f32]>) -> Vec<f32> {
    let (n, k) = (b.n, b.k);
    assert!(a.len() >= m * k);
    let mut out = vec![0.0; m * n];
    gemm_rows(&a[..m * k], b, bias, &mut out);
    out
}

fn gemm_rows(a: &[f32], b: &Packed, bias: Option<&[f32]>, out: &mut [f32]) {
    blocked(a, b, bias, out);
}

/// Weights as panels of NR output columns, k-major inside a panel (see `Packed`).
trait Panels {
    fn n(&self) -> usize;
    fn k(&self) -> usize;
    /// Panel `jp`: stored, or widened into `scratch` (k x NR).
    fn panel<'s>(&'s self, jp: usize, scratch: &'s mut [f32]) -> &'s [f32];
}

impl Panels for Packed {
    fn n(&self) -> usize {
        self.n
    }
    fn k(&self) -> usize {
        self.k
    }
    fn panel<'s>(&'s self, jp: usize, _: &'s mut [f32]) -> &'s [f32] {
        Packed::panel(self, jp)
    }
}

/// The cache-blocked loop around the micro-kernel: blocks of MB rows of `a`, one weight panel at a time.
fn blocked(a: &[f32], b: &(impl Panels + Sync), bias: Option<&[f32]>, out: &mut [f32]) {
    let (n, k) = (b.n(), b.k());
    let m = a.len() / k;
    // blocks of rows are independent: one task each
    let mb = crate::par::block_rows(m, MB);
    crate::par::chunks_mut(out, mb * n, m * n * k, |blk, out| {
        let i0 = blk * mb;
        let i1 = (i0 + mb).min(m);
        let mut tile = [[0f32; NR]; MR];
        let mut scratch = vec![0f32; k * NR];
        for jp in 0..n.div_ceil(NR) {
            let panel = b.panel(jp, &mut scratch);
            let j0 = jp * NR;
            let cols = NR.min(n - j0);
            let mut init = [0f32; NR];
            if let Some(bias) = bias {
                init[..cols].copy_from_slice(&bias[j0..j0 + cols]);
            }
            let mut i = i0;
            while i < i1 {
                let rows = MR.min(i1 - i);
                if rows == MR {
                    kernel::<MR>(&a[i * k..(i + MR) * k], k, panel, &init, &mut tile);
                } else {
                    for r in 0..rows {
                        let mut one = [[0f32; NR]; 1];
                        kernel::<1>(&a[(i + r) * k..(i + r + 1) * k], k, panel, &init, &mut one);
                        tile[r] = one[0];
                    }
                }
                for r in 0..rows {
                    let o = (i + r - i0) * n + j0;
                    out[o..o + cols].copy_from_slice(&tile[r][..cols]);
                }
                i += rows;
            }
        }
    });
}

/// out = init + rows(a) x panel, for R rows of `a` (each k long) and one panel of NR columns.
#[cfg(not(target_arch = "aarch64"))]
#[inline(always)]
fn kernel<const R: usize>(a: &[f32], k: usize, panel: &[f32], init: &[f32; NR], out: &mut [[f32; NR]; R]) {
    let rows: [&[f32]; R] = core::array::from_fn(|r| &a[r * k..(r + 1) * k]);
    let mut acc = [*init; R];
    for (p, b) in panel[..k * NR].chunks_exact(NR).enumerate() {
        for r in 0..R {
            let x = rows[r][p];
            for l in 0..NR {
                acc[r][l] += x * b[l];
            }
        }
    }
    *out = acc;
}

/// NEON version: R x 16 accumulators in q registers, fused multiply-add (fmla by element).
#[cfg(target_arch = "aarch64")]
#[inline(always)]
#[allow(unsafe_code)]
fn kernel<const R: usize>(a: &[f32], k: usize, panel: &[f32], init: &[f32; NR], out: &mut [[f32; NR]; R]) {
    use core::arch::aarch64::{float32x4_t, vdupq_n_f32, vfmaq_f32, vfmaq_laneq_f32, vld1q_f32, vst1q_f32};
    const { assert!(NR == 16) };
    assert!(a.len() >= R * k && panel.len() >= k * NR);
    // SAFETY: NEON is mandatory on aarch64; every load/store stays inside the slices checked above
    // (row r reads a[r*k + p], p < k; panel reads panel[p*16 .. p*16+16], p < k).
    unsafe {
        let ip = init.as_ptr();
        let start: [float32x4_t; 4] = [vld1q_f32(ip), vld1q_f32(ip.add(4)), vld1q_f32(ip.add(8)), vld1q_f32(ip.add(12))];
        let mut acc = [start; R];
        let (ap, bp) = (a.as_ptr(), panel.as_ptr());
        // 4 k at a time: one load of each row's 4 values, used by lane (the same multiply-adds in the same order)
        macro_rules! step {
            ($l:literal, $p:expr, $av:expr) => {
                let b = bp.add($p * NR);
                let (b0, b1, b2, b3) = (vld1q_f32(b), vld1q_f32(b.add(4)), vld1q_f32(b.add(8)), vld1q_f32(b.add(12)));
                for (acc, x) in acc.iter_mut().zip($av.iter()) {
                    acc[0] = vfmaq_laneq_f32::<$l>(acc[0], b0, *x);
                    acc[1] = vfmaq_laneq_f32::<$l>(acc[1], b1, *x);
                    acc[2] = vfmaq_laneq_f32::<$l>(acc[2], b2, *x);
                    acc[3] = vfmaq_laneq_f32::<$l>(acc[3], b3, *x);
                }
            };
        }
        let k4 = k / 4 * 4;
        let mut p = 0;
        while p < k4 {
            let av: [float32x4_t; R] = core::array::from_fn(|r| vld1q_f32(ap.add(r * k + p)));
            step!(0, p, av);
            step!(1, p + 1, av);
            step!(2, p + 2, av);
            step!(3, p + 3, av);
            p += 4;
        }
        for p in k4..k {
            let b = bp.add(p * NR);
            let (b0, b1, b2, b3) = (vld1q_f32(b), vld1q_f32(b.add(4)), vld1q_f32(b.add(8)), vld1q_f32(b.add(12)));
            for (r, acc) in acc.iter_mut().enumerate() {
                let x = vdupq_n_f32(*ap.add(r * k + p));
                acc[0] = vfmaq_f32(acc[0], b0, x);
                acc[1] = vfmaq_f32(acc[1], b1, x);
                acc[2] = vfmaq_f32(acc[2], b2, x);
                acc[3] = vfmaq_f32(acc[3], b3, x);
            }
        }
        for (o, acc) in out.iter_mut().zip(&acc) {
            for (j, v) in acc.iter().enumerate() {
                vst1q_f32(o.as_mut_ptr().add(4 * j), *v);
            }
        }
    }
}

/// exp for x <= ~88 via 2^x = 2^i * 2^f with a degree-5 polynomial (rel. err ~2e-7).
/// Branch-free so loops over it vectorize, unlike the libm call.
#[inline(always)]
#[allow(clippy::approx_constant)] // polynomial coefficients, not ln 2
pub fn exp(x: f32) -> f32 {
    let t = (x * core::f32::consts::LOG2_E).clamp(-126.0, 126.0);
    let i = t.floor();
    let f = t - i;
    let p = 1.0 + f * (0.693_147_2 + f * (0.240_226_5 + f * (0.055_504_11 + f * (0.009_618_129 + f * 0.001_333_355))));
    f32::from_bits(((i as i32 + 127) as u32) << 23) * p
}

/// ln for x > 0 (normal): x = m 2^e with m in [sqrt(1/2), sqrt 2), ln m = 2 atanh(s), s = (m - 1) / (m + 1), as an odd
/// series to s^11 (|s| < 0.172: rel. err < 1e-8). Branch-free so loops over it vectorize, unlike the libm call.
#[inline(always)]
pub fn ln(x: f32) -> f32 {
    let b = x.to_bits();
    // exponent relative to sqrt(1/2), so the mantissa lands in [sqrt(1/2), sqrt 2)
    let e = ((b.wrapping_sub(0x3f35_04f3)) as i32) >> 23;
    let m = f32::from_bits(b.wrapping_sub((e as u32) << 23));
    let s = (m - 1.0) / (m + 1.0);
    let z = s * s;
    let p = 2.0 + z * (0.666_666_7 + z * (0.4 + z * (0.285_714_3 + z * (0.222_222_2 + z * 0.181_818_2))));
    e as f32 * core::f32::consts::LN_2 + s * p
}

/// `swoosh` (model.rs) on NEON, 4 values at a time: softplus(v - shift) - 0.08 v - c with softplus(y) =
/// max(y, 0) + ln(1 + e^-|y|), the same polynomials as `exp` and `ln`; ln's division by a reciprocal estimate refined
/// twice (Newton), within an ulp or so. x.len() a multiple of 4.
#[cfg(target_arch = "aarch64")]
#[allow(unsafe_code, clippy::approx_constant)]
pub fn swoosh4(x: &mut [f32], shift: f32, c: f32) {
    use core::arch::aarch64::*;
    assert!(x.len().is_multiple_of(4));
    // SAFETY: NEON is mandatory on aarch64; every access is a whole 4-value chunk of x.
    unsafe {
        let f = |v: f32| vdupq_n_f32(v);
        for ch in x.chunks_exact_mut(4) {
            let v = vld1q_f32(ch.as_ptr());
            let y = vsubq_f32(v, f(shift));
            // e^-|y| = 2^i 2^t (as `exp`)
            let t = vmaxq_f32(vminq_f32(vmulq_f32(vnegq_f32(vabsq_f32(y)), f(core::f32::consts::LOG2_E)), f(126.0)), f(-126.0));
            let i = vrndmq_f32(t);
            let fr = vsubq_f32(t, i);
            let mut p = f(0.001_333_355);
            for k in [0.009_618_129, 0.055_504_11, 0.240_226_5, 0.693_147_2, 1.0] {
                p = vfmaq_f32(f(k), p, fr);
            }
            let e = vmulq_f32(vreinterpretq_f32_s32(vshlq_n_s32::<23>(vaddq_s32(vcvtq_s32_f32(i), vdupq_n_s32(127)))), p);
            // ln(1 + e) (as `ln`)
            let u = vaddq_f32(f(1.0), e);
            let b = vreinterpretq_u32_f32(u);
            let ex = vshrq_n_s32::<23>(vreinterpretq_s32_u32(vsubq_u32(b, vdupq_n_u32(0x3f35_04f3))));
            let m = vreinterpretq_f32_u32(vsubq_u32(b, vshlq_n_u32::<23>(vreinterpretq_u32_s32(ex))));
            let den = vaddq_f32(m, f(1.0));
            let mut r = vrecpeq_f32(den);
            r = vmulq_f32(r, vrecpsq_f32(den, r));
            r = vmulq_f32(r, vrecpsq_f32(den, r));
            let sv = vmulq_f32(vsubq_f32(m, f(1.0)), r);
            let z = vmulq_f32(sv, sv);
            let mut q = f(0.181_818_2);
            for k in [0.222_222_2, 0.285_714_3, 0.4, 0.666_666_7, 2.0] {
                q = vfmaq_f32(f(k), q, z);
            }
            let ln = vfmaq_f32(vmulq_f32(sv, q), vcvtq_f32_s32(ex), f(core::f32::consts::LN_2));
            let sp = vaddq_f32(vmaxq_f32(y, f(0.0)), ln);
            vst1q_f32(ch.as_mut_ptr(), vsubq_f32(vsubq_f32(sp, vmulq_f32(f(0.08), v)), f(c)));
        }
    }
}

#[inline(always)]
pub fn sigmoid(x: f32) -> f32 {
    1.0 / (1.0 + exp(-x))
}

#[inline(always)]
pub fn tanh(x: f32) -> f32 {
    1.0 - 2.0 / (exp(2.0 * x) + 1.0)
}

// ---------------------------------------------------------------------------------------------- integer weights
//
// `Quant`: weights as int4 (two per byte, as in the file) or int8 codes with one f32 scale per `group` weights of a row,
// repacked at load into panels of 8 output rows that every integer kernel reads directly:
//
// - a panel is k-major; for every 8 k (int8) it holds 4 vectors of 16 bytes, one per pair of outputs (2p, 2p + 1):
//   [output 2p: k..k+8 | output 2p+1: k..k+8] - the operand layout of `smmla` (i8mm), also used by the `sdot`,
//   plain NEON and wasm kernels;
// - int4: one 16-byte vector per pair for every 16 k: its low nibbles are the 8 k-step at k, its high nibbles the one at
//   k + 8 (two's complement nibbles). The kernels unpack `v << 4` and `v & 0xf0`: the weights times 16 (exact int8), so
//   int4 scales are stored / 16;
// - scales: per panel, per group, 8 f32 (one per output).
//
// Activations are quantized per call to int8 per row and per group (`pack_act`), interleaved the same way in pairs of
// rows, so one 16-byte load is [row 2r: k..k+8 | row 2r+1: k..k+8]. A kernel call computes 2 x 2 output blocks for 1 or
// 2 row pairs x the 4 output pairs of a panel; int32 sums are scaled into f32 at every group end.

/// How `Quant` multiplies, picked once from the CPU.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[allow(dead_code)] // each target builds only its own paths
pub enum Path {
    /// Weights widened to f32, float micro-kernel (exact; no activation quantization).
    F32,
    /// Portable integer arithmetic (to check accuracy off-device).
    Scalar,
    /// aarch64 without the dot-product extension: smull + sadalp.
    Neon,
    /// aarch64 `sdot` (ARMv8.2 dotprod).
    Dot,
    /// aarch64 `smmla` (ARMv8.6 i8mm).
    Mmla,
    /// wasm SIMD128: widening multiplies (`i16x8.extmul_*_i8x16_s`).
    Wasm,
}

/// Lanes of a 2 x 2 result block (row offset, output offset). Kernels hand back the `smmla` order; `Dot` accumulates in
/// its own (the activation scales are packed to match) and reorders at the end.
/// Row pairs per kernel call at most (i8mm: 4, the others 2).
const RPMAX: usize = 4;

const MMLA_LANES: [(usize, usize); 4] = [(0, 0), (0, 1), (1, 0), (1, 1)];
const DOT_LANES: [(usize, usize); 4] = [(0, 0), (1, 1), (1, 0), (0, 1)];

/// The integer kernel of this CPU. std builds: TINYASR_KERNEL=f32|scalar|neon|dot|i8mm forces one (for tests;
/// one the CPU lacks is ignored); INT8_DOT=0 is TINYASR_KERNEL=f32.
pub fn path() -> Path {
    #[cfg(all(target_arch = "wasm32", target_feature = "simd128"))]
    return Path::Wasm;
    #[cfg(all(target_arch = "aarch64", feature = "std"))]
    {
        static P: std::sync::OnceLock<Path> = std::sync::OnceLock::new();
        *P.get_or_init(|| {
            let mmla = std::arch::is_aarch64_feature_detected!("i8mm");
            let dot = std::arch::is_aarch64_feature_detected!("dotprod");
            if std::env::var("INT8_DOT").is_ok_and(|v| v == "0") {
                return Path::F32;
            }
            match std::env::var("TINYASR_KERNEL").as_deref() {
                Ok("f32") => return Path::F32,
                Ok("scalar") => return Path::Scalar,
                Ok("neon") => return Path::Neon,
                Ok("dot") if dot => return Path::Dot,
                _ => {}
            }
            if mmla && dot {
                Path::Mmla
            } else if dot {
                Path::Dot
            } else {
                Path::Neon
            }
        })
    }
    #[cfg(all(target_arch = "aarch64", not(feature = "std")))]
    {
        // 0 not known yet, else Path as u8 + 1
        use core::sync::atomic::{AtomicU8, Ordering};
        static P: AtomicU8 = AtomicU8::new(0);
        let v = P.load(Ordering::Relaxed);
        if v != 0 {
            return [Path::F32, Path::Scalar, Path::Neon, Path::Dot, Path::Mmla, Path::Wasm][v as usize - 1];
        }
        let (mmla, dot) = cpu_features();
        let p = if mmla && dot {
            Path::Mmla
        } else if dot {
            Path::Dot
        } else {
            Path::Neon
        };
        P.store(p as u8 + 1, Ordering::Relaxed);
        p
    }
    #[cfg(not(any(target_arch = "aarch64", all(target_arch = "wasm32", target_feature = "simd128"))))]
    {
        // the same integer arithmetic, portable (to check accuracy off-device): TINYASR_INT8=1
        #[cfg(feature = "std")]
        {
            static P: std::sync::OnceLock<Path> = std::sync::OnceLock::new();
            *P.get_or_init(|| if std::env::var("TINYASR_INT8").is_ok_and(|v| v == "1") { Path::Scalar } else { Path::F32 })
        }
        #[cfg(not(feature = "std"))]
        {
            Path::F32
        }
    }
}

/// (i8mm, dotprod) without std: what the kernel reports (Linux, Android: auxiliary vector), sysctl (Apple), else what
/// the build targets.
#[cfg(all(target_arch = "aarch64", not(feature = "std")))]
#[allow(unsafe_code)]
fn cpu_features() -> (bool, bool) {
    #[cfg(any(target_os = "linux", target_os = "android"))]
    {
        unsafe extern "C" {
            fn getauxval(t: core::ffi::c_ulong) -> core::ffi::c_ulong;
        }
        const AT_HWCAP: core::ffi::c_ulong = 16;
        const AT_HWCAP2: core::ffi::c_ulong = 26;
        const HWCAP_ASIMDDP: core::ffi::c_ulong = 1 << 20;
        const HWCAP2_I8MM: core::ffi::c_ulong = 1 << 13;
        // SAFETY: getauxval only reads the auxiliary vector.
        let (h1, h2) = unsafe { (getauxval(AT_HWCAP), getauxval(AT_HWCAP2)) };
        (cfg!(target_feature = "i8mm") || h2 & HWCAP2_I8MM != 0, cfg!(target_feature = "dotprod") || h1 & HWCAP_ASIMDDP != 0)
    }
    #[cfg(target_vendor = "apple")]
    {
        unsafe extern "C" {
            fn sysctlbyname(name: *const core::ffi::c_char, old: *mut core::ffi::c_void, oldlen: *mut usize, new: *const core::ffi::c_void, newlen: usize) -> i32;
        }
        let has = |name: &core::ffi::CStr| {
            let mut v: u32 = 0;
            let mut n = core::mem::size_of::<u32>();
            // SAFETY: v holds n bytes; sysctlbyname writes at most n.
            let r = unsafe { sysctlbyname(name.as_ptr(), &mut v as *mut u32 as *mut core::ffi::c_void, &mut n, core::ptr::null(), 0) };
            r == 0 && v != 0
        };
        (cfg!(target_feature = "i8mm") || has(c"hw.optional.arm.FEAT_I8MM"), cfg!(target_feature = "dotprod") || has(c"hw.optional.arm.FEAT_DotProd"))
    }
    #[cfg(not(any(target_os = "linux", target_os = "android", target_vendor = "apple")))]
    {
        (cfg!(target_feature = "i8mm"), cfg!(target_feature = "dotprod"))
    }
}

/// Integer weights (n x k, PyTorch Linear layout), packed as described above.
pub struct Quant {
    pub n: usize,
    pub k: usize,
    group: usize,
    int4: bool,
    w: Vec<u8>,   // ceil(n / 8) panels of k * 8 (int8) or k * 4 (int4) bytes
    s: Vec<f32>, // ceil(n / 8) panels x k / group groups x 8 scales
}

/// Activations quantized to int8 per row and per group, rows interleaved in pairs (see above).
struct Act {
    q: Vec<i8>,   // ceil(m / 2) pairs x 2k
    s: Vec<f32>, // pairs x k / group x 4: the scale of each lane of a 2 x 2 block (the row's part of it)
}

impl Quant {
    /// From codes (int8 values, or int4 in -8..=7) and the scale of each (row, group); `group` divides k and is a
    /// multiple of 16.
    pub fn new(n: usize, k: usize, group: usize, int4: bool, code: impl Fn(usize, usize) -> i8, scale: impl Fn(usize, usize) -> f32) -> Quant {
        assert!(n > 0 && group > 0 && group.is_multiple_of(16) && k.is_multiple_of(group));
        let pb = k * if int4 { 4 } else { 8 };
        let ng = k / group;
        let mut w = vec![0u8; n.div_ceil(8) * pb];
        let mut s = vec![0f32; n.div_ceil(8) * ng * 8];
        for j in 0..n {
            let (jp, o) = (j / 8, j % 8);
            let base = jp * pb + (o / 2) * 16 + (o % 2) * 8;
            for kk in 0..k {
                let v = code(j, kk);
                if int4 {
                    debug_assert!((-8..=7).contains(&v));
                    let nib = v as u8 & 15;
                    w[base + (kk / 16) * 64 + kk % 8] |= if (kk / 8).is_multiple_of(2) { nib } else { nib << 4 };
                } else {
                    w[base + (kk / 8) * 64 + kk % 8] = v as u8;
                }
            }
            for c in 0..ng {
                s[(jp * ng + c) * 8 + o] = scale(j, c) / if int4 { 16.0 } else { 1.0 };
            }
        }
        Quant { n, k, group, int4, w, s }
    }

    fn panel_bytes(&self) -> usize {
        self.k * if self.int4 { 4 } else { 8 }
    }

    /// Weight (j, kk) as the kernels see it (int4: times 16) and its scale.
    fn at(&self, j: usize, kk: usize) -> (i8, f32) {
        let (jp, o) = (j / 8, j % 8);
        let base = jp * self.panel_bytes() + (o / 2) * 16 + (o % 2) * 8 + kk % 8;
        let v = if self.int4 {
            let b = self.w[base + (kk / 16) * 64];
            (if (kk / 8).is_multiple_of(2) { b << 4 } else { b & 0xf0 }) as i8
        } else {
            self.w[base + (kk / 8) * 64] as i8
        };
        (v, self.s[(jp * (self.k / self.group) + kk / self.group) * 8 + o])
    }

    /// out (m x n) = a (m x k) * W^T (+ bias).
    fn gemm(&self, a: &[f32], m: usize, bias: Option<&[f32]>) -> Vec<f32> {
        assert!(a.len() >= m * self.k);
        let a = &a[..m * self.k];
        let path = path();
        let mut out = vec![0.0; m * self.n];
        if path == Path::F32 {
            blocked(a, self, bias, &mut out);
            return out;
        }
        let (n, k, g) = (self.n, self.k, self.group);
        let dot = path == Path::Dot;
        let pairs = m.div_ceil(2);
        let np = n.div_ceil(8);
        let zero = vec![0f32; if bias.is_some() { 0 } else { np * 8 }];
        let bias = bias.unwrap_or(&zero);
        let work = m * n * k;
        const MBP: usize = 16; // row pairs per cache block (32 rows: 16 KiB of activations at k = 512)
        let mbp = crate::par::block_rows(2 * pairs, 2 * MBP) / 2;
        let threads = crate::par::threads();
        if threads > 1 && pairs.div_ceil(mbp) < 2 * threads && np >= 2 * threads && work >= crate::par::MIN_WORK {
            // few rows (streaming chunks, decoding): groups of output panels instead, one task each, into their own
            // columns
            let act = pack_act(a, m, k, g, dot);
            let pg = np.div_ceil(4 * threads);
            let parts = crate::par::map(np.div_ceil(pg), work, |grp| {
                let (jp0, jp1) = (grp * pg, (grp * pg + pg).min(np));
                let w = (jp1 - jp0) * 8;
                let mut o = vec![0f32; m * w];
                self.block(path, &act, 0, pairs, m, jp0, jp1, bias, &mut o, 0, jp0 * 8, w);
                o
            });
            for (grp, o) in parts.iter().enumerate() {
                let j0 = grp * pg * 8;
                let w = o.len() / m;
                let c = w.min(n - j0);
                for r in 0..m {
                    out[r * n + j0..r * n + j0 + c].copy_from_slice(&o[r * w..r * w + c]);
                }
            }
            return out;
        }
        // blocks of row pairs are independent: one task each, which quantizes its own rows
        crate::par::chunks_mut(&mut out, 2 * mbp * n, work, |blk, out| {
            let (p0, r0) = (blk * mbp, 2 * blk * mbp); // first pair, first row of the block
            let rows = out.len() / n;
            let act = pack_act(&a[r0 * k..(r0 + rows) * k], rows, k, g, dot);
            self.block(path, &act, p0, (p0 + mbp).min(pairs), m, 0, np, bias, out, r0, 0, n);
        });
        out
    }

    /// Row pairs p0..p1 x output panels jp0..jp1 into `out` (row r, column j at (r - r0) * stride + j - c0). `act`
    /// holds the rows from pair p0 on; `m`: all rows of the product.
    #[allow(clippy::too_many_arguments)]
    fn block(&self, path: Path, act: &Act, p0: usize, p1: usize, m: usize, jp0: usize, jp1: usize, bias: &[f32], out: &mut [f32], r0: usize, c0: usize, stride: usize) {
        let (n, k, g) = (self.n, self.k, self.group);
        let ng = k / g;
        let pb = self.panel_bytes();
        for jp in jp0..jp1 {
            let w = &self.w[jp * pb..(jp + 1) * pb];
            let sw = &self.s[jp * ng * 8..(jp + 1) * ng * 8];
            let j0 = jp * 8;
            let mut p = p0;
            while p < p1 {
                let rp = if path == Path::Mmla && p1 - p >= 4 { 4 } else if p1 - p >= 2 { 2 } else { 1 };
                let (a, sa) = (&act.q[(p - p0) * 2 * k..(p - p0 + rp) * 2 * k], &act.s[(p - p0) * ng * 4..(p - p0 + rp) * ng * 4]);
                let mut t = [[[0f32; 4]; 4]; RPMAX];
                tile(path, rp, rp == 1 && 2 * p + 1 == m, self.int4, a, sa, w, sw, k, g, &mut t);
                for (r, t) in t.iter().enumerate().take(rp) {
                    let rr = 2 * (p + r);
                    if rr + 2 <= m && j0 + 8 <= n {
                        // whole block: row rr is lanes 0, 1 of every pair, row rr + 1 lanes 2, 3
                        let b = &bias[j0..j0 + 8];
                        for h in 0..2 {
                            let o0 = (rr + h - r0) * stride + j0 - c0;
                            let o = &mut out[o0..o0 + 8];
                            for pp in 0..4 {
                                o[2 * pp] = t[pp][2 * h] + b[2 * pp];
                                o[2 * pp + 1] = t[pp][2 * h + 1] + b[2 * pp + 1];
                            }
                        }
                        continue;
                    }
                    for (pp, t) in t.iter().enumerate() {
                        for (lane, &(ri, oi)) in MMLA_LANES.iter().enumerate() {
                            let (row, o) = (rr + ri, j0 + 2 * pp + oi);
                            if row < m && o < n {
                                out[(row - r0) * stride + o - c0] = t[lane] + bias[o];
                            }
                        }
                    }
                }
                p += rp;
            }
        }
    }
}

/// Rows of `a` (k each) -> int8 per `g` values, interleaved in row pairs; lane scales in the order of the kernel's
/// 2 x 2 blocks (`dot`: DOT_LANES, else MMLA_LANES).
fn pack_act(a: &[f32], m: usize, k: usize, g: usize, dot: bool) -> Act {
    let pairs = m.div_ceil(2);
    let ng = k / g;
    let mut q = vec![0i8; pairs * 2 * k];
    let mut s = vec![0f32; pairs * ng * 4];
    for r in 0..m {
        let (p, h) = (r / 2, r % 2);
        let dst = &mut q[p * 2 * k..(p + 1) * 2 * k];
        for c in 0..ng {
            let x = &a[r * k + c * g..r * k + (c + 1) * g];
            let max = absmax(x);
            let inv = if max > 0.0 { 127.0 / max } else { 0.0 };
            for st in 0..g / 8 {
                let kk = c * g + st * 8;
                let o = (kk / 8) * 16 + h * 8;
                quant8(&x[st * 8..st * 8 + 8], inv, &mut dst[o..o + 8]);
            }
            let sv = &mut s[(p * ng + c) * 4..(p * ng + c + 1) * 4];
            let lanes = if dot { &DOT_LANES } else { &MMLA_LANES };
            for (l, &(ri, _)) in lanes.iter().enumerate() {
                // `dot`: a lone last row fills the whole pair (both halves are it: see `neon::dot`, SINGLE)
                if ri == h || dot && r + 1 == m && h == 0 {
                    sv[l] = max / 127.0;
                }
            }
        }
        if dot && r + 1 == m && h == 0 {
            for st in 0..k / 8 {
                dst.copy_within(st * 16..st * 16 + 8, st * 16 + 8);
            }
        }
    }
    Act { q, s }
}

#[inline(always)]
fn absmax(x: &[f32]) -> f32 {
    // 8 independent lanes so the reduction vectorizes (max is exact in any order)
    let mut m = [0f32; 8];
    for c in x.chunks_exact(8) {
        for l in 0..8 {
            m[l] = m[l].max(c[l].abs());
        }
    }
    m.iter().fold(0f32, |a, &b| a.max(b))
}

/// 8 values times `inv`, rounded half away from zero, to int8.
#[inline(always)]
fn quant8(x: &[f32], inv: f32, out: &mut [i8]) {
    #[cfg(target_arch = "aarch64")]
    #[allow(unsafe_code)]
    // SAFETY: NEON is mandatory on aarch64; x and out hold 8 values (asserted).
    unsafe {
        use core::arch::aarch64::*;
        assert!(x.len() >= 8 && out.len() >= 8);
        let v = vdupq_n_f32(inv);
        let a = vcvtaq_s32_f32(vmulq_f32(vld1q_f32(x.as_ptr()), v));
        let b = vcvtaq_s32_f32(vmulq_f32(vld1q_f32(x.as_ptr().add(4)), v));
        vst1_s8(out.as_mut_ptr(), vqmovn_s16(vcombine_s16(vqmovn_s32(a), vqmovn_s32(b))));
    }
    #[cfg(not(target_arch = "aarch64"))]
    for (v, o) in x[..8].iter().zip(out[..8].iter_mut()) {
        let r = v * inv;
        *o = (r + if r >= 0.0 { 0.5 } else { -0.5 }) as i8; // round half away from zero
    }
}

/// One kernel call: rp (1, 2) row pairs x the 4 output pairs of one panel, 2 x 2 blocks of f32 into t[row pair][pair].
#[allow(clippy::too_many_arguments)]
#[inline(always)]
fn tile(path: Path, rp: usize, single: bool, int4: bool, a: &[i8], sa: &[f32], w: &[u8], sw: &[f32], k: usize, g: usize, t: &mut [[[f32; 4]; 4]; RPMAX]) {
    let ng = k / g;
    let _ = single; // the `sdot` path only
    assert!((1..=RPMAX).contains(&rp) && (rp <= 2 || path == Path::Mmla) && a.len() >= rp * 2 * k && sa.len() >= rp * ng * 4);
    assert!(w.len() >= k * if int4 { 4 } else { 8 } && sw.len() >= ng * 8 && g.is_multiple_of(16) && k.is_multiple_of(g));
    match path {
        #[cfg(target_arch = "aarch64")]
        Path::Mmla | Path::Dot | Path::Neon => {
            #[allow(unsafe_code)]
            // SAFETY: the path was picked for this CPU's features (`path`); every read stays inside the slices asserted
            // above (row pair r: a[r*2k + 0..2k], sa[r*ng*4 + 0..ng*4]; the panel: w[0..k*8 or k*4], sw[0..ng*8]).
            unsafe {
                macro_rules! go {
                    ($f:ident) => {
                        match (rp, int4) {
                            (2, true) => neon::$f::<2, true>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t),
                            (2, false) => neon::$f::<2, false>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t),
                            (_, true) => neon::$f::<1, true>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t),
                            (_, false) => neon::$f::<1, false>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t),
                        }
                    };
                }
                match path {
                    Path::Dot if single => {
                        // one row (packed twice): the diagonal sdot gives both of its outputs per pair
                        if int4 {
                            neon::dot::<1, true, true>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t)
                        } else {
                            neon::dot::<1, false, true>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t)
                        }
                    }
                    Path::Mmla if rp == 4 => {
                        if int4 {
                            neon::mmla::<4, true>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t)
                        } else {
                            neon::mmla::<4, false>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t)
                        }
                    }
                    Path::Mmla => go!(mmla),
                    Path::Dot => go!(dot2),
                    _ => go!(plain),
                }
            }
        }
        #[cfg(all(target_arch = "wasm32", target_feature = "simd128"))]
        Path::Wasm => {
            #[allow(unsafe_code)]
            // SAFETY: as above.
            unsafe {
                match (rp, int4) {
                    (2, true) => wasm::tile::<2, true>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t),
                    (2, false) => wasm::tile::<2, false>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t),
                    (_, true) => wasm::tile::<1, true>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t),
                    (_, false) => wasm::tile::<1, false>(a.as_ptr(), sa.as_ptr(), w.as_ptr(), sw.as_ptr(), k, g, t),
                }
            }
        }
        _ => tile_scalar(rp, int4, a, sa, w, sw, k, g, t),
    }
}

/// The integer product in plain Rust, same layout and lane order (MMLA_LANES).
#[allow(clippy::too_many_arguments)]
fn tile_scalar(rp: usize, int4: bool, a: &[i8], sa: &[f32], w: &[u8], sw: &[f32], k: usize, g: usize, t: &mut [[[f32; 4]; 4]; RPMAX]) {
    let ng = k / g;
    let wv = |o: usize, kk: usize| -> i32 {
        let base = (o / 2) * 16 + (o % 2) * 8 + kk % 8;
        if int4 {
            let b = w[base + (kk / 16) * 64];
            (if (kk / 8).is_multiple_of(2) { b << 4 } else { b & 0xf0 }) as i8 as i32
        } else {
            w[base + (kk / 8) * 64] as i8 as i32
        }
    };
    for r in 0..rp {
        for pp in 0..4 {
            for (lane, &(ri, oi)) in MMLA_LANES.iter().enumerate() {
                let o = 2 * pp + oi;
                let mut f = 0f32;
                for c in 0..ng {
                    let mut d = 0i32;
                    for kk in c * g..(c + 1) * g {
                        d += a[r * 2 * k + (kk / 8) * 16 + ri * 8 + kk % 8] as i32 * wv(o, kk);
                    }
                    f += d as f32 * (sa[(r * ng + c) * 4 + lane] * sw[c * 8 + o]);
                }
                t[r][pp][lane] = f;
            }
        }
    }
}

#[cfg(target_arch = "aarch64")]
#[allow(unsafe_code, clippy::needless_range_loop)]
mod neon {
    use core::arch::aarch64::*;

    /// [scale of output 2p, 2p + 1] twice: the weight half of a 2 x 2 block's lane scales (both lane orders).
    #[inline(always)]
    unsafe fn wscale(sw: *const f32) -> float32x4_t {
        // SAFETY: the caller's sw points to 2 floats.
        unsafe {
            let v = vld1_f32(sw);
            vcombine_f32(v, v)
        }
    }

    /// int4 vector -> (low nibbles, high nibbles), times 16.
    #[inline(always)]
    unsafe fn unpack(v: int8x16_t) -> (int8x16_t, int8x16_t) {
        // SAFETY: register-only NEON (mandatory on aarch64).
        unsafe { (vshlq_n_s8::<4>(v), vandq_s8(v, vdupq_n_s8(0xf0u8 as i8))) }
    }

    #[inline(always)]
    unsafe fn store<const RP: usize>(accf: &[[float32x4_t; 4]; RP], t: &mut [[[f32; 4]; 4]; super::RPMAX]) {
        for r in 0..RP {
            for p in 0..4 {
                // SAFETY: t[r][p] holds 4 floats.
                unsafe { vst1q_f32(t[r][p].as_mut_ptr(), accf[r][p]) };
            }
        }
    }

    macro_rules! smmla {
        ($acc:expr, $a:expr, $w:expr) => {
            // smmla (the i8mm intrinsics are still unstable in core::arch)
            core::arch::asm!("smmla {acc:v}.4s, {a:v}.16b, {w:v}.16b", acc = inout(vreg) $acc, a = in(vreg) $a, w = in(vreg) $w, options(pure, nomem, nostack))
        };
    }
    macro_rules! sdot {
        ($acc:expr, $a:expr, $w:expr) => {
            // sdot (vdotq_s32 is still unstable in core::arch)
            core::arch::asm!("sdot {acc:v}.4s, {a:v}.16b, {w:v}.16b", acc = inout(vreg) $acc, a = in(vreg) $a, w = in(vreg) $w, options(pure, nomem, nostack))
        };
    }

    /// i8mm: one smmla per (row pair, output pair) and 8 k gives the whole 2 x 2 block.
    #[target_feature(enable = "i8mm")]
    pub unsafe fn mmla<const RP: usize, const INT4: bool>(a: *const i8, sa: *const f32, w: *const u8, sw: *const f32, k: usize, g: usize, t: &mut [[[f32; 4]; 4]; super::RPMAX]) {
        let ng = k / g;
        // SAFETY: the caller (`tile`) guarantees RP row pairs of 2k activations and ng x 4 scales each, and a panel.
        unsafe {
            let mut accf = [[vdupq_n_f32(0.0); 4]; RP];
            for c in 0..ng {
                let mut acc = [[vdupq_n_s32(0); 4]; RP];
                let mut kk = c * g;
                while kk < (c + 1) * g {
                    if INT4 {
                        let wp = w.add(kk * 4) as *const i8;
                        let wv: [(int8x16_t, int8x16_t); 4] = core::array::from_fn(|p| unpack(vld1q_s8(wp.add(16 * p))));
                        for r in 0..RP {
                            let ap = a.add(r * 2 * k + kk * 2);
                            let (a0, a1) = (vld1q_s8(ap), vld1q_s8(ap.add(16)));
                            for p in 0..4 {
                                smmla!(acc[r][p], a0, wv[p].0);
                                smmla!(acc[r][p], a1, wv[p].1);
                            }
                        }
                        kk += 16;
                    } else {
                        let wp = w.add(kk * 8) as *const i8;
                        let wv: [int8x16_t; 4] = core::array::from_fn(|p| vld1q_s8(wp.add(16 * p)));
                        for r in 0..RP {
                            let a0 = vld1q_s8(a.add(r * 2 * k + kk * 2));
                            for p in 0..4 {
                                smmla!(acc[r][p], a0, wv[p]);
                            }
                        }
                        kk += 8;
                    }
                }
                for p in 0..4 {
                    let s = wscale(sw.add(c * 8 + 2 * p));
                    for r in 0..RP {
                        let sc = vmulq_f32(vld1q_f32(sa.add((r * ng + c) * 4)), s);
                        accf[r][p] = vfmaq_f32(accf[r][p], vcvtq_f32_s32(acc[r][p]), sc);
                    }
                }
            }
            store(&accf, t);
        }
    }

    /// dotprod: per (row pair, output pair) and 8 k, sdot of [r0|r1] . [o0|o1] (r0 o0, r1 o1) and of [r1|r0] . [o0|o1]
    /// (r1 o0, r0 o1), two lanes each; added pairwise at the group end: DOT_LANES. Two output pairs at a time (registers).
    /// `dot` for full row pairs (`go!` takes kernels with two const parameters).
    #[inline(always)]
    pub unsafe fn dot2<const RP: usize, const INT4: bool>(a: *const i8, sa: *const f32, w: *const u8, sw: *const f32, k: usize, g: usize, t: &mut [[[f32; 4]; 4]; super::RPMAX]) {
        // SAFETY: as `dot`.
        unsafe { dot::<RP, INT4, false>(a, sa, w, sw, k, g, t) }
    }

    /// SINGLE (RP = 1): the pair holds one row twice ([r0|r0], `pack_act`); the diagonal sdot alone gives r0 o0 and
    /// r0 o1, already in MMLA_LANES order (lanes 0, 1), with no cross products.
    #[target_feature(enable = "dotprod")]
    pub unsafe fn dot<const RP: usize, const INT4: bool, const SINGLE: bool>(a: *const i8, sa: *const f32, w: *const u8, sw: *const f32, k: usize, g: usize, t: &mut [[[f32; 4]; 4]; super::RPMAX]) {
        let ng = k / g;
        // SAFETY: as `mmla`.
        unsafe {
            let mut accf = [[vdupq_n_f32(0.0); 4]; RP];
            for half in 0..2 {
                for c in 0..ng {
                    let mut ad = [[vdupq_n_s32(0); 2]; RP];
                    let mut ax = [[vdupq_n_s32(0); 2]; RP];
                    let mut kk = c * g;
                    while kk < (c + 1) * g {
                        if INT4 {
                            let wp = w.add(kk * 4 + 32 * half) as *const i8;
                            let wv: [(int8x16_t, int8x16_t); 2] = core::array::from_fn(|p| unpack(vld1q_s8(wp.add(16 * p))));
                            for r in 0..RP {
                                let ap = a.add(r * 2 * k + kk * 2);
                                let (a0, a1) = (vld1q_s8(ap), vld1q_s8(ap.add(16)));
                                let (x0, x1) = (vextq_s8::<8>(a0, a0), vextq_s8::<8>(a1, a1));
                                for p in 0..2 {
                                    sdot!(ad[r][p], a0, wv[p].0);
                                    sdot!(ad[r][p], a1, wv[p].1);
                                    if !SINGLE {
                                        sdot!(ax[r][p], x0, wv[p].0);
                                        sdot!(ax[r][p], x1, wv[p].1);
                                    }
                                }
                            }
                            kk += 16;
                        } else {
                            let wp = w.add(kk * 8 + 32 * half) as *const i8;
                            let wv: [int8x16_t; 2] = core::array::from_fn(|p| vld1q_s8(wp.add(16 * p)));
                            for r in 0..RP {
                                let a0 = vld1q_s8(a.add(r * 2 * k + kk * 2));
                                let x0 = vextq_s8::<8>(a0, a0);
                                for p in 0..2 {
                                    sdot!(ad[r][p], a0, wv[p]);
                                    if !SINGLE {
                                        sdot!(ax[r][p], x0, wv[p]);
                                    }
                                }
                            }
                            kk += 8;
                        }
                    }
                    for p in 0..2 {
                        let pp = 2 * half + p;
                        let s = wscale(sw.add(c * 8 + 2 * pp));
                        for r in 0..RP {
                            let sc = vmulq_f32(vld1q_f32(sa.add((r * ng + c) * 4)), s);
                            let x = if SINGLE { ad[r][p] } else { ax[r][p] };
                            accf[r][pp] = vfmaq_f32(accf[r][pp], vcvtq_f32_s32(vpaddq_s32(ad[r][p], x)), sc);
                        }
                    }
                }
            }
            // DOT_LANES -> MMLA_LANES: lanes 0, 3, 2, 1 (SINGLE: already)
            let idx = vld1q_u8([0u8, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7].as_ptr());
            for r in 0..if SINGLE { 0 } else { RP } {
                for p in 0..4 {
                    accf[r][p] = vreinterpretq_f32_u8(vqtbl1q_u8(vreinterpretq_u8_f32(accf[r][p]), idx));
                }
            }
            store(&accf, t);
        }
    }

    /// Plain ARMv8.0 NEON: per lane of the 2 x 2 block, smull of 8-byte halves into int16 and smlal of the next k-step
    /// into the same int16 (|a| <= 127, |w| <= 128: two products fit), then sadalp into int32; the accumulators are
    /// reduced pairwise at the group end (MMLA_LANES). Two output pairs at a time.
    #[inline(never)]
    pub unsafe fn plain<const RP: usize, const INT4: bool>(a: *const i8, sa: *const f32, w: *const u8, sw: *const f32, k: usize, g: usize, t: &mut [[[f32; 4]; 4]; super::RPMAX]) {
        let ng = k / g;
        /// Two 8-k steps (a0, w0), (a1, w1) of one 2 x 2 block.
        #[inline(always)]
        unsafe fn mac2(acc: &mut [int32x4_t; 4], a0: int8x16_t, w0: int8x16_t, a1: int8x16_t, w1: int8x16_t) {
            // SAFETY: register-only NEON (mandatory on aarch64).
            unsafe {
                let (al, ah, wl, wh) = (vget_low_s8(a0), vget_high_s8(a0), vget_low_s8(w0), vget_high_s8(w0));
                let (bl, bh, vl, vh) = (vget_low_s8(a1), vget_high_s8(a1), vget_low_s8(w1), vget_high_s8(w1));
                acc[0] = vpadalq_s16(acc[0], vmlal_s8(vmull_s8(al, wl), bl, vl));
                acc[1] = vpadalq_s16(acc[1], vmlal_s8(vmull_s8(al, wh), bl, vh));
                acc[2] = vpadalq_s16(acc[2], vmlal_s8(vmull_s8(ah, wl), bh, vl));
                acc[3] = vpadalq_s16(acc[3], vmlal_s8(vmull_s8(ah, wh), bh, vh));
            }
        }
        // SAFETY: as `mmla`.
        unsafe {
            let mut accf = [[vdupq_n_f32(0.0); 4]; RP];
            for half in 0..2 {
                for c in 0..ng {
                    let mut acc = [[[vdupq_n_s32(0); 4]; 2]; RP];
                    let mut kk = c * g;
                    while kk < (c + 1) * g {
                        for r in 0..RP {
                            let ap = a.add(r * 2 * k + kk * 2);
                            let (a0, a1) = (vld1q_s8(ap), vld1q_s8(ap.add(16)));
                            for p in 0..2 {
                                if INT4 {
                                    let (lo, hi) = unpack(vld1q_s8(w.add(kk * 4 + 32 * half + 16 * p) as *const i8));
                                    mac2(&mut acc[r][p], a0, lo, a1, hi);
                                } else {
                                    let wp = w.add(kk * 8 + 32 * half + 16 * p) as *const i8;
                                    mac2(&mut acc[r][p], a0, vld1q_s8(wp), a1, vld1q_s8(wp.add(64)));
                                }
                            }
                        }
                        kk += 16;
                    }
                    for p in 0..2 {
                        let pp = 2 * half + p;
                        let s = wscale(sw.add(c * 8 + 2 * pp));
                        for r in 0..RP {
                            let q = &acc[r][p];
                            let sum = vpaddq_s32(vpaddq_s32(q[0], q[1]), vpaddq_s32(q[2], q[3]));
                            let sc = vmulq_f32(vld1q_f32(sa.add((r * ng + c) * 4)), s);
                            accf[r][pp] = vfmaq_f32(accf[r][pp], vcvtq_f32_s32(sum), sc);
                        }
                    }
                }
            }
            store(&accf, t);
        }
    }
}

#[cfg(all(target_arch = "wasm32", target_feature = "simd128"))]
#[allow(unsafe_code, clippy::needless_range_loop)]
mod wasm {
    use core::arch::wasm32::*;

    /// 2 x 2 block accumulators [r0 o0, r0 o1, r1 o0, r1 o1], one i32x4 each, summed across their lanes at the end.
    #[derive(Clone, Copy)]
    struct Blk(v128, v128, v128, v128);

    impl Blk {
        #[inline(always)]
        fn zero() -> Blk {
            Blk(i32x4_splat(0), i32x4_splat(0), i32x4_splat(0), i32x4_splat(0))
        }
        /// Two 8-k steps: a = [row 0 | row 1], x = the same halves swapped, w = [output 0 | output 1], per step.
        /// Widening multiplies (smull / pmullw), two steps' int16 products added (|a| <= 127, |w| <= 128: no
        /// overflow), then widened pairwise into the int32 sums.
        #[inline(always)]
        fn mac2(&mut self, a0: v128, x0: v128, w0: v128, a1: v128, x1: v128, w1: v128) {
            let d00 = i16x8_add(i16x8_extmul_low_i8x16(a0, w0), i16x8_extmul_low_i8x16(a1, w1));
            let d11 = i16x8_add(i16x8_extmul_high_i8x16(a0, w0), i16x8_extmul_high_i8x16(a1, w1));
            let d10 = i16x8_add(i16x8_extmul_low_i8x16(x0, w0), i16x8_extmul_low_i8x16(x1, w1));
            let d01 = i16x8_add(i16x8_extmul_high_i8x16(x0, w0), i16x8_extmul_high_i8x16(x1, w1));
            self.0 = i32x4_add(self.0, i32x4_extadd_pairwise_i16x8(d00));
            self.1 = i32x4_add(self.1, i32x4_extadd_pairwise_i16x8(d01));
            self.2 = i32x4_add(self.2, i32x4_extadd_pairwise_i16x8(d10));
            self.3 = i32x4_add(self.3, i32x4_extadd_pairwise_i16x8(d11));
        }
        /// [r0 o0, r0 o1, r1 o0, r1 o1] (MMLA_LANES)
        #[inline(always)]
        fn sum(&self) -> v128 {
            let u = i32x4_add(i32x4_shuffle::<0, 4, 2, 6>(self.0, self.1), i32x4_shuffle::<1, 5, 3, 7>(self.0, self.1));
            let v = i32x4_add(i32x4_shuffle::<0, 4, 2, 6>(self.2, self.3), i32x4_shuffle::<1, 5, 3, 7>(self.2, self.3));
            i32x4_add(i32x4_shuffle::<0, 1, 4, 5>(u, v), i32x4_shuffle::<2, 3, 6, 7>(u, v))
        }
    }

    #[inline(always)]
    fn swap(v: v128) -> v128 {
        i64x2_shuffle::<1, 0>(v, v)
    }

    /// wasm SIMD128 kernel: one row pair x two output pairs (8 accumulators: x86, which runs most wasm, has 16 vector
    /// registers), 16 k per iteration.
    pub unsafe fn tile<const RP: usize, const INT4: bool>(a: *const i8, sa: *const f32, w: *const u8, sw: *const f32, k: usize, g: usize, t: &mut [[[f32; 4]; 4]; super::RPMAX]) {
        let ng = k / g;
        let mask = i8x16_splat(0xf0u8 as i8);
        // SAFETY: the caller (`tile`) guarantees RP row pairs of 2k activations and ng x 4 scales each, and a panel.
        unsafe {
            for r in 0..RP {
                let (a, sa) = (a.add(r * 2 * k), sa.add(r * ng * 4));
                for half in 0..2 {
                    let w = w.add(32 * half);
                    let (mut f0, mut f1) = (f32x4_splat(0.0), f32x4_splat(0.0));
                    for c in 0..ng {
                        let (mut b0, mut b1) = (Blk::zero(), Blk::zero());
                        let mut kk = c * g;
                        while kk < (c + 1) * g {
                            let (a0, a1) = (v128_load(a.add(kk * 2) as *const v128), v128_load(a.add(kk * 2 + 16) as *const v128));
                            let (x0, x1) = (swap(a0), swap(a1));
                            if INT4 {
                                let (v0, v1) = (v128_load(w.add(kk * 4) as *const v128), v128_load(w.add(kk * 4 + 16) as *const v128));
                                b0.mac2(a0, x0, i8x16_shl(v0, 4), a1, x1, v128_and(v0, mask));
                                b1.mac2(a0, x0, i8x16_shl(v1, 4), a1, x1, v128_and(v1, mask));
                            } else {
                                let wp = w.add(kk * 8);
                                b0.mac2(a0, x0, v128_load(wp as *const v128), a1, x1, v128_load(wp.add(64) as *const v128));
                                b1.mac2(a0, x0, v128_load(wp.add(16) as *const v128), a1, x1, v128_load(wp.add(80) as *const v128));
                            }
                            kk += 16;
                        }
                        let sav = v128_load(sa.add(c * 4) as *const v128);
                        let s0 = f32x4_mul(sav, v128_load64_splat(sw.add(c * 8 + 4 * half) as *const u64));
                        let s1 = f32x4_mul(sav, v128_load64_splat(sw.add(c * 8 + 4 * half + 2) as *const u64));
                        f0 = f32x4_add(f0, f32x4_mul(f32x4_convert_i32x4(b0.sum()), s0));
                        f1 = f32x4_add(f1, f32x4_mul(f32x4_convert_i32x4(b1.sum()), s1));
                    }
                    v128_store(t[r][2 * half].as_mut_ptr() as *mut v128, f0);
                    v128_store(t[r][2 * half + 1].as_mut_ptr() as *mut v128, f1);
                }
            }
        }
    }
}

impl Panels for Quant {
    fn n(&self) -> usize {
        self.n
    }
    fn k(&self) -> usize {
        self.k
    }
    fn panel<'s>(&'s self, jp: usize, scratch: &'s mut [f32]) -> &'s [f32] {
        let (n, k) = (self.n, self.k);
        let j0 = jp * NR;
        let cols = NR.min(n - j0);
        if cols < NR {
            scratch.fill(0.0);
        }
        for l in 0..cols {
            for p in 0..k {
                let (q, s) = self.at(j0 + l, p);
                scratch[p * NR + l] = q as f32 * s;
            }
        }
        scratch
    }
}

/// int8 weights with one scale per output row, multiplied in f32: a quarter of the memory of `Packed`. Many rows: each
/// weight widened to q * scale (the dequantized f32 matrix, same results as `Packed`); few rows (decoding): the sums of
/// a * q scaled once per output (equal up to float rounding). For layers where activation
/// quantization would cost accuracy (the joiner's output, the front end's projection).
pub struct QuantF {
    pub n: usize,
    pub k: usize,
    q: Vec<i8>,  // ceil(n / NR) panels x k x NR (k-major, as `Packed`)
    s: Vec<f32>, // ceil(n / NR) * NR
}

impl QuantF {
    pub fn new(n: usize, k: usize, code: impl Fn(usize, usize) -> i8, scale: impl Fn(usize) -> f32) -> QuantF {
        let mut q = vec![0i8; n.div_ceil(NR) * k * NR];
        let mut s = vec![0f32; n.div_ceil(NR) * NR];
        for j in 0..n {
            let (panel, l) = (j / NR, j % NR);
            for p in 0..k {
                q[(panel * k + p) * NR + l] = code(j, p);
            }
            s[j] = scale(j);
        }
        QuantF { n, k, q, s }
    }

    fn gemm(&self, a: &[f32], m: usize, bias: Option<&[f32]>) -> Vec<f32> {
        assert!(a.len() >= m * self.k);
        let mut out = vec![0.0; m * self.n];
        if m <= 8 {
            // few rows (decoding): widen in registers instead of a scratch panel per call; groups of panels are
            // independent (one task each when worth it), each into its own columns
            let (n, k) = (self.n, self.k);
            let np = n.div_ceil(NR);
            const PG: usize = 8; // panels per task
            let cols = crate::par::map(np.div_ceil(PG), m * n * k, |grp| {
                let (jp0, jp1) = (grp * PG, (grp * PG + PG).min(np));
                let w = (jp1 - jp0) * NR;
                let mut o = vec![0f32; m * w];
                for jp in jp0..jp1 {
                    let j0 = jp * NR;
                    let cols = NR.min(n - j0);
                    let mut init = [0f32; NR];
                    if let Some(b) = bias {
                        init[..cols].copy_from_slice(&b[j0..j0 + cols]);
                    }
                    let panel = &self.q[jp * k * NR..(jp + 1) * k * NR];
                    let sc: &[f32; NR] = self.s[j0..j0 + NR].try_into().unwrap();
                    let mut i = 0;
                    while i < m {
                        let rows = 4.min(m - i);
                        let mut tile = [[0f32; NR]; 4];
                        let a = &a[i * k..(i + rows) * k];
                        match rows {
                            4 => kernel_q8::<4>(a, k, panel, sc, &init, &mut tile),
                            3 => kernel_q8::<3>(a, k, panel, sc, &init, (&mut tile[..3]).try_into().unwrap()),
                            2 => kernel_q8::<2>(a, k, panel, sc, &init, (&mut tile[..2]).try_into().unwrap()),
                            _ => kernel_q8::<1>(a, k, panel, sc, &init, (&mut tile[..1]).try_into().unwrap()),
                        }
                        for r in 0..rows {
                            let o0 = (i + r) * w + (jp - jp0) * NR;
                            o[o0..o0 + NR].copy_from_slice(&tile[r]);
                        }
                        i += rows;
                    }
                }
                o
            });
            for (grp, o) in cols.iter().enumerate() {
                let j0 = grp * PG * NR;
                let w = o.len() / m;
                let c = w.min(n - j0);
                for r in 0..m {
                    out[r * n + j0..r * n + j0 + c].copy_from_slice(&o[r * w..r * w + c]);
                }
            }
            return out;
        }
        blocked(&a[..m * self.k], self, bias, &mut out);
        out
    }
}

/// `kernel_q8` written for auto-vectorization (wasm SIMD128, SSE): b = q * scale per weight, then the same sums.
#[cfg(not(target_arch = "aarch64"))]
#[inline(always)]
fn kernel_q8<const R: usize>(a: &[f32], k: usize, panel: &[i8], sc: &[f32; NR], init: &[f32; NR], out: &mut [[f32; NR]; R]) {
    let rows: [&[f32]; R] = core::array::from_fn(|r| &a[r * k..(r + 1) * k]);
    let mut acc = [[0f32; NR]; R];
    for (p, q) in panel[..k * NR].chunks_exact(NR).enumerate() {
        let mut b = [0f32; NR];
        for l in 0..NR {
            b[l] = q[l] as f32;
        }
        for r in 0..R {
            let x = rows[r][p];
            for l in 0..NR {
                acc[r][l] += x * b[l];
            }
        }
    }
    // the column scales once, at the end
    for r in 0..R {
        for l in 0..NR {
            out[r][l] = init[l] + acc[r][l] * sc[l];
        }
    }
}

/// `kernel` with the panel widened in registers: b = (int8 -> f32) * scale, then the same fused multiply-adds.
#[cfg(target_arch = "aarch64")]
#[inline(always)]
#[allow(unsafe_code)]
fn kernel_q8<const R: usize>(a: &[f32], k: usize, panel: &[i8], sc: &[f32; NR], init: &[f32; NR], out: &mut [[f32; NR]; R]) {
    use core::arch::aarch64::*;
    assert!(a.len() >= R * k && panel.len() >= k * NR);
    // SAFETY: NEON is mandatory on aarch64; reads stay inside the slices checked above.
    unsafe {
        let ip = init.as_ptr();
        let start: [float32x4_t; 4] = [vld1q_f32(ip), vld1q_f32(ip.add(4)), vld1q_f32(ip.add(8)), vld1q_f32(ip.add(12))];
        let s: [float32x4_t; 4] = core::array::from_fn(|j| vld1q_f32(sc.as_ptr().add(4 * j)));
        let mut acc = [[vdupq_n_f32(0.0); 4]; R];
        let (ap, bp) = (a.as_ptr(), panel.as_ptr());
        for p in 0..k {
            let q = vld1q_s8(bp.add(p * NR));
            let (lo, hi) = (vmovl_s8(vget_low_s8(q)), vmovl_high_s8(q));
            let b = [
                vcvtq_f32_s32(vmovl_s16(vget_low_s16(lo))),
                vcvtq_f32_s32(vmovl_high_s16(lo)),
                vcvtq_f32_s32(vmovl_s16(vget_low_s16(hi))),
                vcvtq_f32_s32(vmovl_high_s16(hi)),
            ];
            for (r, acc) in acc.iter_mut().enumerate() {
                let x = vdupq_n_f32(*ap.add(r * k + p));
                for j in 0..4 {
                    acc[j] = vfmaq_f32(acc[j], b[j], x);
                }
            }
        }
        // the column scales once, at the end
        for (o, acc) in out.iter_mut().zip(&acc) {
            for (j, v) in acc.iter().enumerate() {
                vst1q_f32(o.as_mut_ptr().add(4 * j), vfmaq_f32(start[j], *v, s[j]));
            }
        }
    }
}

impl Panels for QuantF {
    fn n(&self) -> usize {
        self.n
    }
    fn k(&self) -> usize {
        self.k
    }
    fn panel<'s>(&'s self, jp: usize, scratch: &'s mut [f32]) -> &'s [f32] {
        let k = self.k;
        let q = &self.q[jp * k * NR..(jp + 1) * k * NR];
        let s = &self.s[jp * NR..(jp + 1) * NR];
        for (o, q) in scratch[..k * NR].chunks_exact_mut(NR).zip(q.chunks_exact(NR)) {
            for l in 0..NR {
                o[l] = q[l] as f32 * s[l];
            }
        }
        scratch
    }
}

pub enum Weight {
    F32(Packed),
    Int(Quant),
    I8(QuantF),
}

/// y = x W^T + b, with W in PyTorch Linear layout (n x k).
pub struct Linear {
    pub w: Weight,
    pub b: Option<Vec<f32>>,
}

impl Linear {
    pub fn n(&self) -> usize {
        match &self.w {
            Weight::F32(p) => p.n,
            Weight::Int(q) => q.n,
            Weight::I8(q) => q.n,
        }
    }

    /// x: m rows of k.
    pub fn apply(&self, x: &[f32], m: usize) -> Vec<f32> {
        match &self.w {
            Weight::F32(p) => gemm(x, m, p, self.b.as_deref()),
            Weight::Int(q) => q.gemm(x, m, self.b.as_deref()),
            Weight::I8(q) => q.gemm(x, m, self.b.as_deref()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rnd(seed: &mut u64) -> f32 {
        *seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        ((*seed >> 33) as f32 / (1u64 << 31) as f32) * 2.0 - 1.0
    }

    /// Every integer kernel this CPU has agrees with the f32 product of the dequantized weights (within int8
    /// activation rounding) and with the portable one (to float rounding).
    #[test]
    fn quant_kernels() {
        let mut seed = 1;
        for &(m, n, k, g, int4) in &[(1, 8, 64, 64, true), (5, 19, 128, 64, true), (33, 40, 512, 64, true), (3, 13, 512, 512, false), (36, 24, 96, 32, false), (2, 7, 32, 16, true)] {
            let codes: Vec<i8> = (0..n * k).map(|_| (rnd(&mut seed) * if int4 { 8.0 } else { 127.0 }).floor().clamp(if int4 { -8.0 } else { -127.0 }, if int4 { 7.0 } else { 127.0 }) as i8).collect();
            let scales: Vec<f32> = (0..n * k / g).map(|_| 0.01 + rnd(&mut seed).abs() * 0.05).collect();
            let q = Quant::new(n, k, g, int4, |j, p| codes[j * k + p], |j, c| scales[j * (k / g) + c]);
            let a: Vec<f32> = (0..m * k).map(|_| rnd(&mut seed)).collect();
            let bias: Vec<f32> = (0..n).map(|_| rnd(&mut seed)).collect();
            let dense: Vec<f32> = (0..n * k).map(|i| codes[i] as f32 * scales[(i / k) * (k / g) + (i % k) / g]).collect();
            let want_f32 = gemm(&a, m, &Packed::from_rows(&dense, n, k), Some(&bias));
            // the exact product with the activations quantized as the kernels do
            let mut want = vec![0f32; m * n];
            for r in 0..m {
                for j in 0..n {
                    let mut f = bias[j] as f64;
                    for c in 0..k / g {
                        let x = &a[r * k + c * g..r * k + (c + 1) * g];
                        let max = x.iter().fold(0f32, |m, v| m.max(v.abs()));
                        let inv = if max > 0.0 { 127.0 / max } else { 0.0 };
                        let d: i64 = (0..g).map(|p| (x[p] * inv).round() as i64 * codes[j * k + c * g + p] as i64).sum();
                        f += d as f64 * (max / 127.0) as f64 * scales[j * (k / g) + c] as f64;
                    }
                    want[r * n + j] = f as f32;
                    // and that is close to the f32 product
                    assert!((want[r * n + j] - want_f32[r * n + j]).abs() < 0.05 * (k as f32).sqrt() * if int4 { 0.1 } else { 1.0 });
                }
            }
            let mut paths = vec![Path::Scalar];
            #[cfg(target_arch = "aarch64")]
            {
                paths.push(Path::Neon);
                if std::arch::is_aarch64_feature_detected!("dotprod") {
                    paths.push(Path::Dot);
                }
                if std::arch::is_aarch64_feature_detected!("i8mm") {
                    paths.push(Path::Mmla);
                }
            }
            let mut first: Option<Vec<f32>> = None;
            for &path in &paths {
                let act = pack_act(&a, m, k, g, path == Path::Dot);
                let lanes = &MMLA_LANES;
                let mut got = vec![0f32; m * n];
                let pb = k * if int4 { 4 } else { 8 };
                let ng = k / g;
                for p in 0..m.div_ceil(2) {
                    for jp in 0..n.div_ceil(8) {
                        let mut t = [[[0f32; 4]; 4]; RPMAX];
                        tile(path, 1, 2 * p + 1 == m, int4, &act.q[p * 2 * k..], &act.s[p * ng * 4..], &q.w[jp * pb..], &q.s[jp * ng * 8..], k, g, &mut t);
                        for pp in 0..4 {
                            for (l, &(ri, oi)) in lanes.iter().enumerate() {
                                let (r, o) = (2 * p + ri, jp * 8 + 2 * pp + oi);
                                if r < m && o < n {
                                    got[r * n + o] = t[0][pp][l] + bias[o];
                                }
                            }
                        }
                    }
                }
                // the 2-row-pair kernels, through the driver
                let full = if path == super::path() { Some(q.gemm(&a, m, Some(&bias))) } else { None };
                for i in 0..m * n {
                    let tol = 1e-3 * (1.0 + want[i].abs());
                    assert!((got[i] - want[i]).abs() < tol, "{path:?} m{m} n{n} k{k} g{g} int4 {int4} [{i}]: {} vs {}", got[i], want[i]);
                    if let Some(f) = &full {
                        assert!((f[i] - got[i]).abs() < 1e-4 * (1.0 + got[i].abs()), "{path:?} driver [{i}]");
                    }
                    if let Some(f) = &first {
                        assert!((f[i] - got[i]).abs() < 1e-4 * (1.0 + got[i].abs()), "{path:?} vs scalar [{i}]: {} vs {}", got[i], f[i]);
                    }
                }
                first.get_or_insert(got);
            }
            // the f32 view of the packed weights is the dense matrix exactly
            let mut scratch = vec![0f32; k * NR];
            for jp in 0..n.div_ceil(NR) {
                let pnl = Panels::panel(&q, jp, &mut scratch).to_vec();
                for l in 0..NR.min(n - jp * NR) {
                    for p in 0..k {
                        assert_eq!(pnl[p * NR + l], dense[(jp * NR + l) * k + p]);
                    }
                }
            }
        }
    }

    #[cfg(target_arch = "aarch64")]
    #[test]
    fn swoosh_neon_matches_scalar() {
        let mut seed = 3;
        let x: Vec<f32> = (0..4096).map(|i| if i < 64 { (i as f32 - 32.0) * 3.0 } else { rnd(&mut seed) * 20.0 }).collect();
        for (shift, c) in [(4.0, 0.035), (1.0, 0.313_261_7)] {
            let mut y = x.clone();
            swoosh4(&mut y, shift, c);
            for (a, &v) in y.iter().zip(&x) {
                let sp = (v - shift).max(0.0) + ln(1.0 + exp(-(v - shift).abs()));
                let want = sp - 0.08 * v - c;
                assert!((a - want).abs() <= 2e-7 * (1.0 + want.abs()), "{v}: {a} vs {want}");
            }
        }
    }

    /// Any number of threads gives the same bits (row blocks, column groups, the few-rows paths).
    #[cfg(feature = "std")]
    #[test]
    fn threads_same_bits() {
        use crate::par::{Exec, Pool, enter};
        let ex = std::sync::Arc::new(Exec::Pool(Pool::new(3)));
        let mut seed = 11;
        for &(m, n, k, g, int4) in &[(1, 2080, 512, 512, false), (4, 2048, 512, 512, false), (16, 1536, 512, 64, true), (300, 768, 256, 64, true), (77, 384, 128, 128, false)] {
            let codes: Vec<i8> = (0..n * k).map(|_| (rnd(&mut seed) * 7.9).floor() as i8).collect();
            let sc: Vec<f32> = (0..n * k / g).map(|_| 0.01 + rnd(&mut seed).abs() * 0.05).collect();
            let q = Quant::new(n, k, g, int4, |j, p| codes[j * k + p], |j, c| sc[j * (k / g) + c]);
            let qf = QuantF::new(n, k, |j, p| codes[j * k + p], |j| sc[j * (k / g)]);
            let a: Vec<f32> = (0..m * k).map(|_| rnd(&mut seed)).collect();
            let bias: Vec<f32> = (0..n).map(|_| rnd(&mut seed)).collect();
            let one = (q.gemm(&a, m, Some(&bias)), qf.gemm(&a, m, Some(&bias)), gemm(&a, m, &Packed::from_rows(&qf_dense(&qf), n, k), None));
            let three = enter(Some(&ex), || (q.gemm(&a, m, Some(&bias)), qf.gemm(&a, m, Some(&bias)), gemm(&a, m, &Packed::from_rows(&qf_dense(&qf), n, k), None)));
            assert!(one == three, "m{m} n{n} k{k}");
        }
    }

    #[cfg(feature = "std")]
    fn qf_dense(q: &QuantF) -> Vec<f32> {
        let mut s = vec![0f32; q.k * NR];
        let mut d = vec![0f32; q.n * q.k];
        for jp in 0..q.n.div_ceil(NR) {
            let p = Panels::panel(q, jp, &mut s);
            for l in 0..NR.min(q.n - jp * NR) {
                for kk in 0..q.k {
                    d[(jp * NR + l) * q.k + kk] = p[kk * NR + l];
                }
            }
        }
        d
    }

    #[test]
    fn quantf_matches_f32() {
        let mut seed = 7;
        for &(m, n, k) in &[(1, 37, 64), (4, 16, 512), (3, 5, 8), (9, 33, 100), (70, 20, 48)] {
            let codes: Vec<i8> = (0..n * k).map(|_| (rnd(&mut seed) * 127.0) as i8).collect();
            let sc: Vec<f32> = (0..n).map(|_| 0.01 + rnd(&mut seed).abs()).collect();
            let q = QuantF::new(n, k, |j, p| codes[j * k + p], |j| sc[j]);
            let dense: Vec<f32> = (0..n * k).map(|i| codes[i] as f32 * sc[i / k]).collect();
            let a: Vec<f32> = (0..m * k).map(|_| rnd(&mut seed)).collect();
            let bias: Vec<f32> = (0..n).map(|_| rnd(&mut seed)).collect();
            let (got, want) = (q.gemm(&a, m, Some(&bias)), gemm(&a, m, &Packed::from_rows(&dense, n, k), Some(&bias)));
            for (g, w) in got.iter().zip(&want) {
                if m > 8 {
                    assert_eq!(g, w);
                } else {
                    assert!((g - w).abs() < 1e-5 * (1.0 + w.abs()) * k as f32, "{g} vs {w}");
                }
            }
        }
    }
}
