//! Zipformer2 (icefall, causal) encoder, stateless RNN-T decoder and joiner.
//!
//! Activations are row-major `t x channels`. One set of weights runs both ways:
//! - full context (`Model::full`): the second pass over a finished segment, no attention mask, each depthwise
//!   convolution sees the whole segment;
//! - streaming (`Stream`): 32-frame chunks at 50 Hz (64 feature frames, 640 ms) with 128 frames of left context,
//!   carrying the attention keys/values, the nonlinear-attention input and the convolution tails between chunks,
//!   as icefall's `streaming_forward` and the sherpa-onnx export do.

use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;

#[cfg(not(feature = "std"))]
use crate::compat::F32Ext;
use crate::fbank::{BINS, Fbank};
use crate::kernels::{Linear, Packed, Quant, QuantF, Weight, exp, gemm, ln, sigmoid, tanh};
use alloc::format;

// ---------------------------------------------------------------------------------------------- weights

pub struct Tensors<'a> {
    cfg: BTreeMap<String, i32>,
    text: BTreeMap<String, String>, // TZF3: text settings (language codes and rules)
    t: BTreeMap<String, (Vec<usize>, Data<'a>)>,
    pub pieces: Vec<String>,
}

/// A tensor as stored: f32 values, or integer codes with one scale per `group` weights of a row (TZF2).
enum Data<'a> {
    F32(Vec<f32>),
    Int(Ints<'a>),
}

/// Integer codes, still as in the file (read where needed, never unpacked whole).
struct Ints<'a> {
    bits: u8,
    group: usize,
    row: usize, // padded row length (multiple of group)
    scale: Vec<f32>,
    raw: &'a [u8], // rows x row codes: int8, or int4 offset by 8, two per byte (low nibble first)
}

impl Ints<'_> {
    /// Code (r, c): int8, or int4 in -8..=7.
    #[inline(always)]
    fn code(&self, r: usize, c: usize) -> i8 {
        let i = r * self.row + c;
        if self.bits == 8 {
            self.raw[i] as i8
        } else {
            let b = self.raw[i / 2];
            (if i.is_multiple_of(2) { b & 15 } else { b >> 4 }) as i8 - 8
        }
    }

    /// Scale of (r, c).
    #[inline(always)]
    fn scale(&self, r: usize, c: usize) -> f32 {
        self.scale[r * (self.row / self.group) + c / self.group]
    }

    /// Code (r, c) times its scale: the f32 weight.
    #[inline(always)]
    fn value(&self, r: usize, c: usize) -> f32 {
        self.code(r, c) as f32 * self.scale(r, c)
    }

    /// Whether every row has one scale (rows of k values, no padding): kept as int8 rows (`QuantF`, `Emb`).
    fn per_row(&self, k: usize) -> bool {
        self.bits == 8 && self.row == k && self.group == k
    }
}

impl Data<'_> {
    /// f32 values (rows x k, padding dropped).
    fn f32(self, dims: &[usize]) -> Vec<f32> {
        match self {
            Data::F32(v) => v,
            Data::Int(q) => {
                let n = dims[0];
                let k: usize = dims[1..].iter().product();
                let mut out = Vec::with_capacity(n * k);
                for r in 0..n {
                    for c in 0..k {
                        out.push(q.value(r, c));
                    }
                }
                out
            }
        }
    }
}

fn f16(h: u16) -> f32 {
    let (s, e, m) = ((h >> 15) as u32, ((h >> 10) & 0x1f) as u32, (h & 0x3ff) as u32);
    let bits = if e == 0 {
        if m == 0 {
            s << 31
        } else {
            // subnormal: normalize
            let mut e = 127 - 15 + 1;
            let mut m = m;
            while m & 0x400 == 0 {
                m <<= 1;
                e -= 1;
            }
            (s << 31) | (e << 23) | ((m & 0x3ff) << 13)
        }
    } else if e == 31 {
        (s << 31) | (0xff << 23) | (m << 13)
    } else {
        (s << 31) | ((e + 127 - 15) << 23) | (m << 13)
    };
    f32::from_bits(bits)
}

type R<T> = Result<T, &'static str>;

const TRUNCATED: &str = "model file: truncated";
const BAD: &str = "model file: corrupted tensor header";
const MISSING: &str = "model file: a tensor is missing (corrupted or wrong version?)";
const SHAPE: &str = "model file: a tensor has an unexpected shape (corrupted or wrong version?)";
const SETTING: &str = "model file: a setting is missing or out of range (corrupted or wrong version?)";

struct Rd<'a> {
    b: &'a [u8],
    p: usize,
}

impl<'a> Rd<'a> {
    fn take(&mut self, n: usize) -> R<&'a [u8]> {
        let end = self.p.checked_add(n).ok_or(TRUNCATED)?;
        let s = self.b.get(self.p..end).ok_or(TRUNCATED)?;
        self.p = end;
        Ok(s)
    }
    fn align4(&mut self) {
        self.p = self.p.saturating_add((4 - self.p % 4) % 4);
    }
    fn u8(&mut self) -> R<u8> {
        Ok(self.take(1)?[0])
    }
    fn u16(&mut self) -> R<usize> {
        let s = self.take(2)?;
        Ok(u16::from_le_bytes([s[0], s[1]]) as usize)
    }
    fn u32(&mut self) -> R<usize> {
        let s = self.take(4)?;
        Ok(u32::from_le_bytes([s[0], s[1], s[2], s[3]]) as usize)
    }
    fn str(&mut self) -> R<String> {
        let n = self.u16()?;
        core::str::from_utf8(self.take(n)?).map(String::from).map_err(|_| "model file: bad utf-8")
    }
}

impl<'a> Tensors<'a> {
    /// The `.tzf` file written by `export_tzf.py` (TZF1: f32) or `export_tzf2.py` (TZF2: integer weights; TZF3: as
    /// TZF2 plus text settings after the integer ones). Every size is checked against the file, so a corrupted file is
    /// an error here, never a crash.
    pub fn parse(b: &'a [u8]) -> R<Tensors<'a>> {
        let mut r = Rd { b, p: 0 };
        let (v2, v3) = match r.take(4)? {
            b"TZF1" => (false, false),
            b"TZF2" => (true, false),
            b"TZF3" => (true, true),
            _ => return Err("not a tinyasr model file (TZF1/TZF2/TZF3)"),
        };
        let mut cfg = BTreeMap::new();
        for _ in 0..r.u32()? {
            let k = r.str()?;
            let v = r.take(4)?;
            cfg.insert(k, i32::from_le_bytes([v[0], v[1], v[2], v[3]]));
        }
        let mut text = BTreeMap::new();
        if v3 {
            for _ in 0..r.u32()? {
                let k = r.str()?;
                text.insert(k, r.str()?);
            }
        }
        let mut t = BTreeMap::new();
        for _ in 0..r.u32()? {
            let k = r.str()?;
            let integer = v2 && r.u8()? == 1;
            let nd = r.u8()? as usize;
            let dims = (0..nd).map(|_| r.u32()).collect::<R<Vec<_>>>()?;
            let n = dims.iter().try_fold(1usize, |a, &d| a.checked_mul(d)).ok_or(BAD)?;
            if !integer {
                r.align4();
                let raw = r.take(n.checked_mul(4).ok_or(BAD)?)?;
                let v = raw.chunks_exact(4).map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]])).collect();
                t.insert(k, (dims, Data::F32(v)));
                continue;
            }
            let (bits, group, st) = (r.u8()?, r.u32()?, r.u8()?);
            if dims.is_empty() || n == 0 || group == 0 {
                return Err(BAD);
            }
            r.align4();
            let rows = dims[0];
            let row = (n / rows).div_ceil(group).checked_mul(group).ok_or(BAD)?;
            let total = rows.checked_mul(row).ok_or(BAD)?;
            let raw = match bits {
                8 => r.take(total)?,
                4 => r.take(total.div_ceil(2))?,
                _ => return Err("model file: unsupported integer width"),
            };
            r.align4();
            let ns = total / group;
            let scale = if st == 1 {
                r.take(ns.checked_mul(2).ok_or(BAD)?)?.chunks_exact(2).map(|c| f16(u16::from_le_bytes([c[0], c[1]]))).collect()
            } else {
                r.take(ns.checked_mul(4).ok_or(BAD)?)?.chunks_exact(4).map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]])).collect()
            };
            t.insert(k, (dims, Data::Int(Ints { bits, group, row, scale, raw })));
        }
        let pieces = (0..r.u32()?).map(|_| r.str()).collect::<R<Vec<_>>>()?;
        Ok(Tensors { cfg, text, t, pieces })
    }

    /// A setting, which must lie in lo..=hi.
    fn c(&self, k: &str, lo: usize, hi: usize) -> R<usize> {
        let v = *self.cfg.get(k).ok_or(SETTING)?;
        usize::try_from(v).ok().filter(|v| (lo..=hi).contains(v)).ok_or(SETTING)
    }

    fn get(&mut self, k: &str) -> R<(Vec<usize>, Vec<f32>)> {
        let (d, v) = self.t.remove(k).ok_or(MISSING)?;
        let v = v.f32(&d); // integer tensors always have a row dimension (parse)
        Ok((d, v))
    }

    /// A tensor of exactly n values.
    fn vec(&mut self, k: &str, n: usize) -> R<Vec<f32>> {
        let v = self.get(k)?.1;
        if v.len() == n { Ok(v) } else { Err(SHAPE) }
    }

    /// A tensor of lo..=hi values.
    fn vec_in(&mut self, k: &str, lo: usize, hi: usize) -> R<Vec<f32>> {
        let v = self.get(k)?.1;
        if (lo..=hi).contains(&v.len()) { Ok(v) } else { Err(SHAPE) }
    }

    fn scalar(&mut self, k: &str) -> R<f32> {
        Ok(self.vec(k, 1)?[0])
    }

    /// The tensor itself (f32 or integer codes), removed.
    fn take(&mut self, k: &str) -> R<(Vec<usize>, Data<'a>)> {
        self.t.remove(k).ok_or(MISSING)
    }

    fn has(&self, k: &str) -> bool {
        self.t.contains_key(k)
    }

    /// `k`.weight (n x kk; n None: any n >= 1) and, with `bias`, `k`.bias (n). Integer weights stay integer (`Quant`)
    /// when the groups tile the row exactly; otherwise f32.
    fn linear(&mut self, k: &str, bias: bool, n: Option<usize>, kk: usize) -> R<Linear> {
        let b = if bias { Some(alloc::format!("{k}.bias")) } else { None };
        self.linear_named(&alloc::format!("{k}.weight"), b.as_deref(), n, kk)
    }

    /// A 3x3 convolution `k` (cout x cin x 3 x 3, with bias) as a Linear over im2col rows with tap-major columns:
    /// column (a * 3 + b) * cin + ci is PyTorch's ci * 9 + a * 3 + b. Integer weights stay integer when one group
    /// spans the row (the reordering keeps every group's set of weights); otherwise f32.
    fn conv_linear(&mut self, k: &str, cout: usize, cin: usize) -> R<Linear> {
        let kk = 9 * cin;
        let (d, data) = self.take(&alloc::format!("{k}.weight"))?;
        if d.first() != Some(&cout) || d[1..].iter().product::<usize>() != kk {
            return Err(SHAPE);
        }
        let b = Some(self.vec(&alloc::format!("{k}.bias"), cout)?);
        let perm = |p: usize| (p % cin) * 9 + p / cin;
        let w = match data {
            Data::Int(q) if q.row == kk && q.group == kk && kk.is_multiple_of(16) => {
                Weight::Int(Quant::new(cout, kk, kk, q.bits == 4, |j, p| q.code(j, perm(p)), |j, _| q.scale(j, 0)))
            }
            data => {
                let v = data.f32(&d);
                let pv: Vec<f32> = (0..cout * kk).map(|i| v[(i / kk) * kk + perm(i % kk)]).collect();
                Weight::F32(Packed::from_rows(&pv, cout, kk))
            }
        };
        Ok(Linear { w, b })
    }

    /// As `linear`, for tensors not named X.weight / X.bias (the LSTM's weight_ih_l0 ...).
    fn linear_named(&mut self, name: &str, bias: Option<&str>, n: Option<usize>, kk: usize) -> R<Linear> {
        let (d, data) = self.t.remove(name).ok_or(MISSING)?;
        let rows = *d.first().ok_or(SHAPE)?;
        let cols: usize = d[1..].iter().product();
        if rows == 0 || n.is_some_and(|n| n != rows) || cols != kk || kk == 0 {
            return Err(SHAPE);
        }
        let b = match bias {
            Some(k) => Some(self.vec(k, rows)?),
            None => None,
        };
        let w = match data {
            // integer weights stay integer (int4 packed two per byte) when the groups tile the row in steps of 16
            Data::Int(q) if q.row == kk && q.group % 16 == 0 => {
                Weight::Int(Quant::new(rows, kk, q.group, q.bits == 4, |j, p| q.code(j, p), |j, c| q.scale(j, c * q.group)))
            }
            data => Weight::F32(Packed::from_rows(&data.f32(&d), rows, kk)),
        };
        Ok(Linear { w, b })
    }
}

// ---------------------------------------------------------------------------------------------- pieces

/// log(1 + e^x), stable.
#[inline(always)]
fn softplus(x: f32) -> f32 {
    x.max(0.0) + ln(1.0 + exp(-x.abs()))
}

fn swoosh_l(x: &mut [f32]) {
    swoosh(x, 4.0, 0.035);
}

fn swoosh_r(x: &mut [f32]) {
    swoosh(x, 1.0, 0.313_261_7);
}

/// softplus(v - shift) - 0.08 v - c, in place.
fn swoosh(x: &mut [f32], shift: f32, c: f32) {
    #[cfg(target_arch = "aarch64")]
    let x = {
        let n = x.len() / 4 * 4;
        crate::kernels::swoosh4(&mut x[..n], shift, c);
        &mut x[n..]
    };
    x.iter_mut().for_each(|v| *v = softplus(*v - shift) - 0.08 * *v - c);
}

/// BiasNorm: x * exp(log_scale) / rms(x - bias), per row.
struct BiasNorm {
    bias: Vec<f32>,
    scale: f32,
}

impl BiasNorm {
    fn load(w: &mut Tensors, k: &str, dim: usize) -> R<BiasNorm> {
        Ok(BiasNorm { bias: w.vec(&alloc::format!("{k}.bias"), dim)?, scale: exp(w.scalar(&alloc::format!("{k}.log_scale"))?) })
    }
    fn apply(&self, x: &mut [f32]) {
        let d = self.bias.len();
        for r in x.chunks_exact_mut(d) {
            let ms = r.iter().zip(&self.bias).map(|(v, b)| (v - b) * (v - b)).sum::<f32>() / d as f32;
            let s = self.scale / ms.sqrt();
            r.iter_mut().for_each(|v| *v *= s);
        }
    }
}

/// orig + (x - orig) * scale, per channel, into x.
fn bypass(orig: &[f32], x: &mut [f32], scale: &[f32]) {
    let d = scale.len();
    for (xr, or) in x.chunks_exact_mut(d).zip(orig.chunks_exact(d)) {
        for ((v, o), s) in xr.iter_mut().zip(or).zip(scale) {
            *v = o + (*v - o) * s;
        }
    }
}

/// x (t rows) += f(x rows, first row, rows) for blocks of rows, one task each: a module's output added to the residual
/// where it is computed (no separate pass). f may read the block's rows of x (row-wise modules) or ignore them.
fn add_rows(x: &mut [f32], t: usize, work: usize, f: impl Fn(&[f32], usize, usize) -> Vec<f32> + Sync) {
    let d = x.len() / t;
    let rb = crate::par::block_rows(t, RB);
    crate::par::chunks_mut(x, rb * d, work, |b, xo| {
        let y = f(xo, b * rb, xo.len() / d);
        add(xo, &y);
    });
}

fn add(x: &mut [f32], y: &[f32]) {
    x.iter_mut().zip(y).for_each(|(a, b)| *a += b);
}

/// Compact relative positional encoding (CompactRelPositionalEncoding) of relative position v.
fn rel_pe(v: f32, dim: usize, out: &mut [f32]) {
    let cl = (dim as f32).sqrt();
    let xc = cl * v.signum() * ((v.abs() + cl).ln() - cl.ln());
    let ls = dim as f32 / (2.0 * core::f32::consts::PI);
    let a = (xc / ls).atan();
    for f in 0..dim / 2 {
        let (s, c) = (a * (f + 1) as f32).sin_cos();
        out[2 * f] = c;
        out[2 * f + 1] = s;
    }
    out[dim - 1] = 1.0;
}

struct FeedForward {
    inp: Linear,
    out: Linear,
}

impl FeedForward {
    /// x += FF(x).
    fn add_to(&self, x: &mut [f32], t: usize) {
        let k = x.len() / t;
        add_rows(x, t, 2 * t * k * self.inp.n(), |xb, _, r| {
            let mut h = self.inp.apply(xb, r);
            swoosh_l(&mut h);
            self.out.apply(&h, r)
        });
    }
}

/// Rows `keep` from the end of cat(cache, new) become the new cache; returns the concatenation.
fn cat_cache(cache: Option<&mut Vec<f32>>, new: Vec<f32>) -> Vec<f32> {
    match cache {
        None => new,
        Some(c) => {
            let keep = c.len();
            let mut all = core::mem::take(c);
            all.extend_from_slice(&new);
            *c = all[all.len() - keep..].to_vec();
            all
        }
    }
}

struct AttnWeights {
    in_proj: Linear,
    pos: Linear,
    heads: usize,
    qd: usize,
    pd: usize,
}

impl AttnWeights {
    /// Attention weights heads x t x (left + t). `keys`: cached keys (left x heads*qd), updated.
    /// `masked`: key slots to ignore (len left + t).
    /// `pe`: encodings of the relative positions -(left + t - 1) ..= t - 1 (`rel_pes`, the same for every layer).
    fn apply(&self, x: &[f32], t: usize, keys: Option<&mut Vec<f32>>, left: usize, masked: &[bool], pe: &[f32]) -> Vec<f32> {
        let (h, qd, pd) = (self.heads, self.qd, self.pd);
        let y = self.in_proj.apply(x, t);
        let w = y.len() / t;
        let qdim = h * qd;
        let mut k = vec![0.0; t * qdim];
        for i in 0..t {
            k[i * qdim..(i + 1) * qdim].copy_from_slice(&y[i * w + qdim..i * w + 2 * qdim]);
        }
        let k = cat_cache(keys, k);
        let tk = left + t;
        // projected encodings of relative positions -(left + t - 1) ..= t - 1
        let np = left + 2 * t - 1;
        let p = self.pos.apply(pe, np);
        let mut out = vec![0.0; h * t * tk];
        // heads: one task each
        crate::par::chunks_mut(&mut out, t * tk, h * t * tk * (qd + pd), |hh, out| {
            let (mut qh, mut kh, mut pt) = (vec![0.0; t * qd], vec![0.0; tk * qd], vec![0.0; pd * np]);
            // content scores: Q_h (t x qd) K_h^T, as one product
            for i in 0..t {
                qh[i * qd..(i + 1) * qd].copy_from_slice(&y[i * w + hh * qd..i * w + (hh + 1) * qd]);
            }
            for j in 0..tk {
                kh[j * qd..(j + 1) * qd].copy_from_slice(&k[j * qdim + hh * qd..j * qdim + (hh + 1) * qd]);
            }
            let sc = gemm(&qh, t, &Packed::from_rows(&kh, tk, qd), None);
            // position scores: query i, key j sees relative position row n = t - 1 - i + j; with P_h transposed
            // (pd x np) each query adds pd contiguous runs
            for n in 0..np {
                for c in 0..pd {
                    pt[c * np + n] = p[n * h * pd + hh * pd + c];
                }
            }
            for i in 0..t {
                let row = &mut out[i * tk..(i + 1) * tk];
                row.copy_from_slice(&sc[i * tk..(i + 1) * tk]);
                let n0 = t - 1 - i;
                for c in 0..pd {
                    let pq = y[i * w + 2 * qdim + hh * pd + c];
                    row.iter_mut().zip(&pt[c * np + n0..c * np + n0 + tk]).for_each(|(r, v)| *r += pq * v);
                }
                for (r, &m) in row.iter_mut().zip(masked) {
                    if m {
                        *r = -1000.0;
                    }
                }
                let m = row.iter().cloned().fold(f32::MIN, f32::max);
                let mut s = 0.0;
                row.iter_mut().for_each(|v| {
                    *v = exp(*v - m);
                    s += *v;
                });
                let inv = 1.0 / s;
                row.iter_mut().for_each(|v| *v *= inv);
            }
        });
        out
    }
}

/// y += w * x, elementwise.
#[inline(always)]
fn axpy(y: &mut [f32], w: &[f32], x: &[f32]) {
    for ((y, w), x) in y.iter_mut().zip(w).zip(x) {
        *y += w * x;
    }
}

/// Rows processed at a time by the row-wise parts of a layer (feed-forward, front end): their intermediate buffers
/// stay small (cache-resident, and under the size allocators recycle instead of mapping fresh pages each call).
const RB: usize = 64;

/// out[c] = init[c] + sum over taps of w_tap[c] * x_tap[c] (in tap order), for every channel; channels in blocks of
/// 16 held in registers across all taps (the same sums, in the same order, as one axpy per tap).
#[inline(always)]
fn taps<'a>(out: &mut [f32], init: &[f32], taps: impl Iterator<Item = (&'a [f32], &'a [f32])> + Clone) {
    let d = out.len();
    let mut c0 = 0;
    while c0 + 16 <= d {
        let mut acc: [f32; 16] = init[c0..c0 + 16].try_into().unwrap();
        for (w, x) in taps.clone() {
            let (w, x) = (&w[c0..c0 + 16], &x[c0..c0 + 16]);
            for l in 0..16 {
                acc[l] += w[l] * x[l];
            }
        }
        out[c0..c0 + 16].copy_from_slice(&acc);
        c0 += 16;
    }
    if c0 < d {
        out[c0..].copy_from_slice(&init[c0..]);
        for (w, x) in taps {
            axpy(&mut out[c0..], &w[c0..], &x[c0..]);
        }
    }
}

/// (rows x cols) -> (cols x rows).
fn transpose(x: &[f32], rows: usize, cols: usize) -> Vec<f32> {
    let mut t = vec![0.0; x.len()];
    for r in 0..rows {
        for c in 0..cols {
            t[c * rows + r] = x[r * cols + c];
        }
    }
    t
}

/// out (t x heads*d): per head, weights (t x tk) times values (tk x heads*d, head slice).
fn weighted(wts: &[f32], heads: usize, t: usize, tk: usize, v: &[f32], d: usize) -> Vec<f32> {
    let vd = v.len() / tk;
    let mut out = vec![0.0; t * heads * d];
    // heads: one task each
    let per_head = crate::par::map(heads, heads * t * tk * d, |hh| {
        // values of this head, transposed (d x tk): the right-hand matrix of W_h (t x tk) V_h
        let mut vt = vec![0.0; d * tk];
        for j in 0..tk {
            for c in 0..d {
                vt[c * tk + j] = v[j * vd + hh * d + c];
            }
        }
        gemm(&wts[hh * t * tk..(hh + 1) * t * tk], t, &Packed::from_rows(&vt, d, tk), None)
    });
    for (hh, o) in per_head.iter().enumerate() {
        for i in 0..t {
            out[i * heads * d + hh * d..i * heads * d + (hh + 1) * d].copy_from_slice(&o[i * d..(i + 1) * d]);
        }
    }
    out
}

struct SelfAttn {
    inp: Linear,
    out: Linear,
    heads: usize,
}

impl SelfAttn {
    /// x += SelfAttn(x).
    fn add_to(&self, x: &mut [f32], t: usize, wts: &[f32], tk: usize, cache: Option<&mut Vec<f32>>) {
        let v = cat_cache(cache, self.inp.apply(x, t));
        let d = self.inp.n() / self.heads;
        let o = weighted(wts, self.heads, t, tk, &v, d);
        let k = o.len() / t;
        add_rows(x, t, t * k * self.out.n(), |_, r0, r| self.out.apply(&o[r0 * k..(r0 + r) * k], r));
    }
}

struct NonlinAttn {
    inp: Linear,
    out: Linear,
    hid: usize,
}

impl NonlinAttn {
    /// x += NonlinAttn(x). Uses head 0 of the attention weights.
    fn add_to(&self, x: &mut [f32], t: usize, wts: &[f32], tk: usize, cache: Option<&mut Vec<f32>>) {
        let hid = self.hid;
        let z = self.inp.apply(x, t);
        let mut xs = vec![0.0; t * hid];
        crate::par::chunks_mut(&mut xs, 64 * hid, t * hid * 16, |b, o| {
            for (i, row) in o.chunks_exact_mut(hid).enumerate() {
                let r = &z[(b * 64 + i) * 3 * hid..(b * 64 + i + 1) * 3 * hid];
                for c in 0..hid {
                    row[c] = r[hid + c] * tanh(r[c]);
                }
            }
        });
        let xs = cat_cache(cache, xs);
        let o = weighted(&wts[..t * tk], 1, t, tk, &xs, hid);
        add_rows(x, t, t * hid * self.out.n(), |_, r0, r| {
            let mut ob = o[r0 * hid..(r0 + r) * hid].to_vec();
            for (i, row) in ob.chunks_exact_mut(hid).enumerate() {
                let zr = &z[(r0 + i) * 3 * hid + 2 * hid..(r0 + i + 1) * 3 * hid];
                row.iter_mut().zip(zr).for_each(|(v, z)| *v *= z);
            }
            self.out.apply(&ob, r)
        });
    }
}

struct Conv {
    inp: Linear,
    // weights tap-major (tap x dim), so the inner loops run over contiguous channels
    causal_w: Vec<f32>, // half x dim
    causal_b: Vec<f32>,
    chunk_w: Vec<f32>, // kernel x dim
    chunk_b: Vec<f32>,
    scale: Vec<f32>, // 2 x kernel x dim
    out: Linear,
    dim: usize,
    kernel: usize,
}

impl Conv {
    /// x += Conv(x). `cache`: the last kernel/2 rows of gated input before this chunk (streaming), updated.
    fn add_to(&self, x: &mut [f32], t: usize, cache: Option<&mut Vec<f32>>) {
        let (d, k) = (self.dim, self.kernel);
        let lp = k / 2;
        // input projection and gate, row blocks: one task each
        let mut g = vec![0.0; t * d];
        let rb = crate::par::block_rows(t, RB);
        crate::par::chunks_mut(&mut g, rb * d, 2 * t * d * d, |b, o| {
            let r = o.len() / d;
            let z = self.inp.apply(&x[b * rb * d..(b * rb + r) * d], r);
            for (row, z) in o.chunks_exact_mut(d).zip(z.chunks_exact(2 * d)) {
                for c in 0..d {
                    row[c] = z[c] * sigmoid(z[d + c]);
                }
            }
        });
        // causal part over (left pad or cache) + this input
        let mut pad = match &cache {
            Some(c) => (*c).clone(),
            None => vec![0.0; lp * d],
        };
        pad.extend_from_slice(&g);
        if let Some(c) = cache {
            *c = pad[pad.len() - lp * d..].to_vec();
        }
        let half = lp + 1;
        // depthwise, output projection and the residual, row blocks: one task each
        add_rows(x, t, t * d * (half + k + d), |_, r0, rows| {
            let mut yo = vec![0.0; rows * d];
            let mut b = vec![0.0; d];
            for i in r0..r0 + rows {
                let a = &mut yo[(i - r0) * d..(i - r0 + 1) * d];
                taps(a, &self.causal_b, (0..half).map(|u| (&self.causal_w[u * d..(u + 1) * d], &pad[(i + u) * d..(i + u + 1) * d])));
                // chunkwise part: centred kernel inside this chunk, zero outside, edge-scaled
                let (u0, u1) = (lp.saturating_sub(i), k.min(t + lp - i));
                taps(&mut b, &self.chunk_b, (u0..u1).map(|u| (&self.chunk_w[u * d..(u + 1) * d], &g[(i + u - lp) * d..(i + u - lp + 1) * d])));
                let (l, r) = if t < k { (Some(i), Some(k - t + i)) } else { ((i < k).then_some(i), (i >= t - k).then(|| i - (t - k))) };
                let row = |j: Option<usize>, half: usize| j.map(|j| &self.scale[(half * k + j) * d..(half * k + j + 1) * d]);
                let (le, re) = (row(l, 0), row(r, 1));
                for c in 0..d {
                    let (le, re) = (le.map_or(0.0, |v| v[c]), re.map_or(0.0, |v| v[c]));
                    a[c] += b[c] * (1.0 + le + re);
                }
            }
            swoosh_r(&mut yo);
            self.out.apply(&yo, rows)
        });
    }
}

/// Streaming state of one layer.
#[derive(Clone)]
pub struct LayerState {
    key: Vec<f32>,
    nonlin: Vec<f32>,
    val1: Vec<f32>,
    val2: Vec<f32>,
    conv1: Vec<f32>,
    conv2: Vec<f32>,
}

struct Layer {
    attn: AttnWeights,
    ff1: FeedForward,
    ff2: FeedForward,
    ff3: FeedForward,
    nonlin: NonlinAttn,
    sa1: SelfAttn,
    sa2: SelfAttn,
    conv1: Conv,
    conv2: Conv,
    norm: BiasNorm,
    bypass: Vec<f32>,
    bypass_mid: Vec<f32>,
}

impl Layer {
    fn forward(&self, x: &mut Vec<f32>, t: usize, st: Option<&mut LayerState>, left: usize, masked: &[bool], pe: &[f32]) {
        let orig = x.clone();
        let tk = left + t;
        let (mut key, mut nl, mut v1, mut v2, mut c1, mut c2) = (None, None, None, None, None, None);
        if let Some(s) = st {
            key = Some(&mut s.key);
            nl = Some(&mut s.nonlin);
            v1 = Some(&mut s.val1);
            v2 = Some(&mut s.val2);
            c1 = Some(&mut s.conv1);
            c2 = Some(&mut s.conv2);
        }
        let w = self.attn.apply(x, t, key, left, masked, pe);
        self.ff1.add_to(x, t);
        self.nonlin.add_to(x, t, &w, tk, nl);
        self.sa1.add_to(x, t, &w, tk, v1);
        self.conv1.add_to(x, t, c1);
        self.ff2.add_to(x, t);
        let d = x.len() / t;
        let rb = crate::par::block_rows(t, RB);
        crate::par::chunks_mut(x, rb * d, 8 * t * d, |b, xo| bypass(&orig[b * rb * d..b * rb * d + xo.len()], xo, &self.bypass_mid));
        self.sa2.add_to(x, t, &w, tk, v2);
        self.conv2.add_to(x, t, c2);
        self.ff3.add_to(x, t);
        crate::par::chunks_mut(x, rb * d, 8 * t * d, |b, xo| {
            self.norm.apply(xo);
            bypass(&orig[b * rb * d..b * rb * d + xo.len()], xo, &self.bypass);
        });
    }
}

struct Stack {
    layers: Vec<Layer>,
    dim: usize,
    ds: usize,
    down: Vec<f32>, // softmax of the downsample bias
    combine: Vec<f32>,
}

fn softmax(b: &[f32]) -> Vec<f32> {
    let m = b.iter().cloned().fold(f32::MIN, f32::max);
    let e: Vec<f32> = b.iter().map(|v| exp(v - m)).collect();
    let s: f32 = e.iter().sum();
    e.iter().map(|v| v / s).collect()
}

/// SimpleDownsample: weighted sum of `w.len()` consecutive rows (the last row repeated as padding).
fn downsample(x: &[f32], t: usize, d: usize, w: &[f32]) -> (Vec<f32>, usize) {
    let ds = w.len();
    let dt = t.div_ceil(ds);
    let mut out = vec![0.0; dt * d];
    for i in 0..dt {
        for (k, wk) in w.iter().enumerate() {
            let s = (i * ds + k).min(t - 1);
            out[i * d..(i + 1) * d].iter_mut().zip(&x[s * d..(s + 1) * d]).for_each(|(o, v)| *o += wk * v);
        }
    }
    (out, dt)
}

impl Stack {
    /// x: t x dim. `masked`: left-context slots at 50 Hz (len left_full) that hold no audio yet.
    fn forward(&self, x: &mut Vec<f32>, t: usize, st: Option<&mut [LayerState]>, left_full: usize, masked_full: &[bool], pos_dim: usize) {
        let d = self.dim;
        let left = left_full / self.ds;
        let mut masked: Vec<bool> = (0..left).map(|j| masked_full.get(j * self.ds).copied().unwrap_or(false)).collect();
        if self.ds == 1 {
            masked.resize(left + t, false);
            run(&self.layers, x, t, st, left, &masked, pos_dim);
            return;
        }
        let orig = x.clone();
        let (mut y, dt) = downsample(x, t, d, &self.down);
        masked.resize(left + dt, false);
        run(&self.layers, &mut y, dt, st, left, &masked, pos_dim);
        let mut up = vec![0.0; t * d];
        for i in 0..t {
            up[i * d..(i + 1) * d].copy_from_slice(&y[(i / self.ds) * d..(i / self.ds + 1) * d]);
        }
        bypass(&orig, &mut up, &self.combine);
        *x = up;
    }
}

fn run(layers: &[Layer], x: &mut Vec<f32>, t: usize, st: Option<&mut [LayerState]>, left: usize, masked: &[bool], pos_dim: usize) {
    // the relative-position encodings depend on (left, t) alone: once for all the layers of the stack
    let np = left + 2 * t - 1;
    let mut pe = vec![0.0; np * pos_dim];
    crate::par::chunks_mut(&mut pe, 64 * pos_dim, np * pos_dim * 16, |b, o| {
        for (i, row) in o.chunks_exact_mut(pos_dim).enumerate() {
            rel_pe((b * 64 + i) as f32 - (left + t - 1) as f32, pos_dim, row);
        }
    });
    match st {
        Some(s) => layers.iter().zip(s.iter_mut()).for_each(|(l, s)| l.forward(x, t, Some(s), left, masked, &pe)),
        None => layers.iter().for_each(|l| l.forward(x, t, None, left, masked, &pe)),
    }
}

// ---------------------------------------------------------------------------------------------- front end

/// Conv2dSubsampling + ConvNeXt, channels-last images (t x f x c).
struct Embed {
    c0: Linear, // 1 -> 8, 3x3 (im2col)
    c1: Linear, // 8 -> 32, 3x3 stride 2
    c2: Linear, // 32 -> 128, 3x3 stride (1, 2)
    dw_w: Vec<f32>, // 7 x 7 x 128 (tap-major)
    dw_b: Vec<f32>,
    pw1: Linear,
    pw2: Linear,
    out: Linear, // input order (f, c) (permuted at load from PyTorch's (c, f))
    norm: BiasNorm,
}

const FW: usize = 19; // frequency width after the convolutions
const C3: usize = 128;

/// 3x3 convolution of a channels-last image (t x f x cin), no time padding, `pf` frequency padding.
/// The front end's 3 convolutions have 1, 8 and 32 input channels: a constant, so the copies of each tap unroll.
fn conv3(x: &[f32], t: usize, f: usize, cin: usize, lin: &Linear, st: usize, sf: usize, pf: usize) -> (Vec<f32>, usize, usize) {
    match cin {
        1 => conv3c::<1>(x, t, f, lin, st, sf, pf),
        8 => conv3c::<8>(x, t, f, lin, st, sf, pf),
        32 => conv3c::<32>(x, t, f, lin, st, sf, pf),
        _ => unreachable!("front-end convolution with {cin} input channels"),
    }
}

fn conv3c<const CIN: usize>(x: &[f32], t: usize, f: usize, lin: &Linear, st: usize, sf: usize, pf: usize) -> (Vec<f32>, usize, usize) {
    let cin = CIN;
    let to = (t - 3) / st + 1;
    let fo = (f + 2 * pf - 3) / sf + 1;
    // output time rows per block: about 256 Ki values of im2col at a time
    let tb = ((1 << 18) / (fo * 9 * cin)).max(1);
    let n = lin.n();
    let mut y = vec![0.0; to * fo * n];
    // blocks of tb output time rows: one task each
    crate::par::chunks_mut(&mut y, tb * fo * n, to * fo * 9 * cin * n, |blk, yo| {
        let (i0, nb) = (blk * tb, yo.len() / (fo * n));
        let mut col = vec![0.0; nb * fo * 9 * cin];
        for i in i0..i0 + nb {
            for j in 0..fo {
                let row = &mut col[((i - i0) * fo + j) * 9 * cin..((i - i0) * fo + j + 1) * 9 * cin];
                // tap-major columns (a * 3 + b) * cin + ci (`Tensors::conv_linear`): each tap is cin contiguous inputs
                for a in 0..3 {
                    for b in 0..3 {
                        let (tt, ff) = (i * st + a, (j * sf + b) as isize - pf as isize);
                        let d: &mut [f32; CIN] = (&mut row[(a * 3 + b) * cin..(a * 3 + b + 1) * cin]).try_into().unwrap();
                        *d = if ff >= 0 && (ff as usize) < f {
                            let s0 = (tt * f + ff as usize) * cin;
                            x[s0..s0 + cin].try_into().unwrap()
                        } else {
                            [0.0; CIN]
                        };
                    }
                }
            }
        }
        let mut yb = lin.apply(&col, nb * fo);
        swoosh_r(&mut yb);
        yo.copy_from_slice(&yb);
    });
    (y, to, fo)
}

impl Embed {
    /// Convolutions up to (not including) ConvNeXt: features (t x 80) -> (t' x 19 x 128), t' = (t - 7) / 2 + 1.
    fn convs(&self, feats: &[f32], t: usize) -> (Vec<f32>, usize) {
        let (x, t, f) = conv3(feats, t, 80, 1, &self.c0, 1, 1, 1);
        let (x, t, f) = conv3(&x, t, f, 8, &self.c1, 2, 2, 0);
        let (x, t, _) = conv3(&x, t, f, 32, &self.c2, 1, 2, 0);
        (x, t)
    }

    /// ConvNeXt over `rows` (t_in x 19 x 128) giving rows `[3, t_in - 3)` of the time-padded result: the caller
    /// supplies 3 rows of context (zeros or cache) on each side.
    fn convnext(&self, rows: &[f32], t_in: usize) -> Vec<f32> {
        let t = t_in - 6;
        let row = FW * C3;
        let tb = (RB / FW).max(1); // time rows per block (x 19 frequency rows each): one task each
        let mut z = vec![0.0; t * row];
        let work = t * FW * (2 * C3 * self.pw1.n() + 49 * C3);
        crate::par::chunks_mut(&mut z, tb * row, work, |blk, zo| {
            let (i0, nb) = (blk * tb, zo.len() / row);
            let mut y = vec![0.0; nb * row];
            for i in i0..i0 + nb {
                for f in 0..FW {
                    let a = &mut y[(i - i0) * row + f * C3..(i - i0) * row + (f + 1) * C3];
                    let (f0, f1) = (3usize.saturating_sub(f), 7.min(FW + 3 - f));
                    let tap = |dt: usize, df: usize| {
                        let ff = f + df - 3;
                        (&self.dw_w[(dt * 7 + df) * C3..(dt * 7 + df + 1) * C3], &rows[(i + dt) * row + ff * C3..(i + dt) * row + (ff + 1) * C3])
                    };
                    taps(a, &self.dw_b, (0..7).flat_map(|dt| (f0..f1).map(move |df| (dt, df))).map(|(dt, df)| tap(dt, df)));
                }
            }
            let mut h = self.pw1.apply(&y, nb * FW);
            swoosh_l(&mut h);
            let mut zb = self.pw2.apply(&h, nb * FW);
            add(&mut zb, &rows[(3 + i0) * row..(3 + i0 + nb) * row]);
            zo.copy_from_slice(&zb);
        });
        z
    }

    fn finish(&self, x: &[f32], t: usize) -> Vec<f32> {
        let mut y = self.out.apply(x, t);
        self.norm.apply(&mut y);
        y
    }
}

// ---------------------------------------------------------------------------------------------- model

/// A recognized token: vocabulary id, output frame (40 ms) and log-probability.
#[derive(Clone, Copy, Debug)]
pub struct Token {
    pub id: u32,
    pub frame: u32,
    pub logp: f32,
}


struct Joiner {
    enc: Linear,
    dec: Linear,
    out: Vec<(Linear, Vec<u32>)>, // per language: output rows of the allowed ids (shared + English + own), the ids
}

/// End-of-turn head (eou_head.py): P(the speaker has finished) from the second pass's last `HEAD_K` encoder frames
/// (attention pooled + the last one) and the decoder output after the last text tokens. Optional in the file.
pub const HEAD_K: usize = 50;
struct Head {
    e: Linear,
    pos: Vec<f32>, // HEAD_K x hidden
    att: Linear,
    d: Linear,
    out0: Linear,
    out2: Linear,
}

/// erf (Abramowitz & Stegun 7.1.26, |error| < 1.5e-7), for the exact GELU the head was trained with.
fn erf(x: f32) -> f32 {
    let t = 1.0 / (1.0 + 0.327_591_1 * x.abs());
    let y = 1.0 - (((((1.061_405_4 * t - 1.453_152_1) * t) + 1.421_413_7) * t - 0.284_496_74) * t + 0.254_829_6) * t * exp(-x * x);
    if x < 0.0 { -y } else { y }
}

fn gelu(x: &mut [f32]) {
    x.iter_mut().for_each(|v| *v = 0.5 * *v * (1.0 + erf(*v * core::f32::consts::FRAC_1_SQRT_2)));
}

impl Head {
    /// `enc_dim`: encoder output width, `dec_dim`: decoder output width (before the joiner).
    fn load(w: &mut Tensors, enc_dim: usize, dec_dim: usize) -> R<Option<Head>> {
        if !w.has("eou_head.e.0.weight") {
            return Ok(None);
        }
        let e = w.linear("eou_head.e.0", true, None, enc_dim)?;
        let hd = e.n();
        let d = w.linear("eou_head.d.0", true, None, dec_dim)?;
        let out0 = w.linear("eou_head.out.0", true, None, 2 * hd + d.n())?;
        Ok(Some(Head {
            pos: w.vec("eou_head.pos", HEAD_K * hd)?,
            att: w.linear("eou_head.att", true, Some(1), hd)?,
            out2: w.linear("eou_head.out.2", true, None, out0.n())?,
            e,
            d,
            out0,
        }))
    }

    /// enc: HEAD_K x 512 right-aligned (the first HEAD_K - valid rows unused), dec: decoder output before the joiner.
    fn prob(&self, enc: &[f32], valid: usize, dec: &[f32]) -> f32 {
        let k = HEAD_K;
        let valid = valid.clamp(1, k);
        let d = enc.len() / k;
        let mut h = self.e.apply(&enc[(k - valid) * d..], valid);
        gelu(&mut h);
        let hd = h.len() / valid;
        for (i, row) in h.chunks_exact_mut(hd).enumerate() {
            let p = &self.pos[(k - valid + i) * hd..(k - valid + i + 1) * hd];
            row.iter_mut().zip(p).for_each(|(a, b)| *a += b);
        }
        let sc = self.att.apply(&h, valid);
        let m = sc.iter().cloned().fold(f32::MIN, f32::max);
        let wts: Vec<f32> = sc.iter().map(|v| exp(v - m)).collect();
        let z: f32 = wts.iter().sum();
        let mut x = vec![0.0; 2 * hd];
        for (i, row) in h.chunks_exact(hd).enumerate() {
            row.iter().enumerate().for_each(|(j, v)| x[j] += wts[i] / z * v);
        }
        x[hd..].copy_from_slice(&h[(valid - 1) * hd..]);
        let mut dd = self.d.apply(dec, 1);
        gelu(&mut dd);
        x.extend_from_slice(&dd);
        let mut y = self.out0.apply(&x, 1);
        gelu(&mut y);
        sigmoid(self.out2.apply(&y, 1)[0])
    }
}

/// v5.9t hybrid decoder: an LSTM over the whole token history, added to the stateless output through a scalar gate
/// (decoder.py: embedding_out + gate * lstm_proj(lstm(embedding(y)))). One step per emitted token.
struct Lstm {
    ih: Linear, // 4H x D, bias = b_ih + b_hh
    hh: Linear, // 4H x H
    proj: Linear, // D x H
    gate: f32,
    hidden: usize,
}

impl Lstm {
    /// One step of torch.nn.LSTM (gate order i, f, g, o) on input x; updates h, c.
    fn step(&self, x: &[f32], h: &mut [f32], c: &mut [f32]) {
        let n = self.hidden;
        let mut g = self.ih.apply(x, 1);
        let r = self.hh.apply(h, 1);
        g.iter_mut().zip(&r).for_each(|(a, b)| *a += b);
        for j in 0..n {
            let (i, f, gg, o) = (sigmoid(g[j]), sigmoid(g[n + j]), tanh(g[2 * n + j]), sigmoid(g[3 * n + j]));
            c[j] = f * c[j] + i * gg;
            h[j] = o * tanh(c[j]);
        }
    }
}

/// The decoder's token embeddings (vocabulary x d): f32, or int8 rows with one scale each (as in the file).
enum Emb {
    F32(Vec<f32>, usize),
    I8 { q: Vec<i8>, s: Vec<f32>, d: usize },
}

impl Emb {
    /// The embedding of token `id` (f32).
    fn row(&self, id: u32) -> Vec<f32> {
        let id = id as usize;
        match self {
            Emb::F32(v, d) => v[id * d..(id + 1) * d].to_vec(),
            Emb::I8 { q, s, d } => q[id * d..(id + 1) * d].iter().map(|&c| c as f32 * s[id]).collect(),
        }
    }
}

/// LSTM input products (W_ih x + b) by token, for one beam-search pass (a few hundred KiB at most).
#[derive(Default)]
struct IhCache {
    ids: Vec<u32>,
    rows: Vec<Vec<f32>>,
}

impl IhCache {
    const MAX: usize = 64;

    fn has(&self, id: u32) -> bool {
        self.ids.contains(&id)
    }

    fn get(&self, id: u32) -> &[f32] {
        &self.rows[self.ids.iter().position(|&i| i == id).expect("cached")]
    }

    /// Keeps the newest MAX (the oldest goes: a beam search moves on through the text).
    fn put(&mut self, id: u32, row: &[f32]) {
        if self.ids.len() == Self::MAX {
            self.ids.remove(0);
            self.rows.remove(0);
        }
        self.ids.push(id);
        self.rows.push(row.to_vec());
    }
}

/// Most zero-duration tokens on one frame in TDT greedy before it is forced on (the server scorer's max_sym).
const TDT_MAX_SYM: usize = 5;

/// One beam-search hypothesis: decoder state, total log-probability, tokens.
#[derive(Clone)]
pub struct Hyp {
    dec: Decoding,
    logp: f32,
    toks: Vec<Token>,
    pending: bool, // TDT beam: the last token is not yet in `dec` (advanced only if the hypothesis survives pruning)
}

pub struct Model {
    head: Option<Head>,
    embed: Embed,
    stacks: Vec<Stack>,
    out_down: Vec<f32>,
    lang_vec: Vec<Vec<f32>>,
    dec_emb: Emb,
    dec_conv: Vec<f32>, // dim x 4 x context (groups of 4 channels)
    dec_dim: usize,
    context: usize,
    joiner: Joiner,
    lstm: Option<Lstm>,
    dur: Option<Linear>, // TDT duration head (durations 0..n-1) on the joiner hidden; None = RNN-T
    pos_dim: usize,
    pub left: usize,
    pub chunk: usize,
    pub pieces: Vec<String>,
    /// The languages, in the model's order (its lang_embed rows and output blocks).
    pub langs: Vec<LangInfo>,
}

/// A language of the model: its code and how its text is written, all from the model file (TZF3: "lang.K" = code,
/// "lang.K.full_stop", "lang.K.quotes", "lang.K.own", "lang.K.syllables"; older files: [`legacy_langs`]).
pub struct LangInfo {
    pub code: String,
    /// `code` NUL-terminated (the C API hands it out).
    pub code0: String,
    /// The full stop a sentence gets when the model wrote none.
    pub full_stop: char,
    /// What the model's « » are shown as (None: as written).
    pub quotes: Option<(char, char)>,
    /// Letters of the shared token block only this language may write (another language's tokens with them are left out).
    pub own: String,
    /// Words with Khmer letters come out as timed syllables (Khmer writes no spaces inside a phrase).
    pub syllables: bool,
}

impl LangInfo {
    fn new(code: &str) -> LangInfo {
        LangInfo { code: code.into(), code0: format!("{code}\0"), full_stop: '.', quotes: None, own: String::new(), syllables: false }
    }
}

/// The languages of a TZF1/TZF2 file (library version 2): "lang.K" = index into the codes of that version (files from
/// en-ru-uz on), older files by their count; the writing rules of those models.
fn legacy_langs(w: &Tensors, n: usize) -> R<Vec<LangInfo>> {
    const V2: [&str; 9] = ["en", "ru", "uk", "es", "it", "pt", "uz", "fa", "km"];
    let codes: Vec<&str> = if w.cfg.contains_key("lang.0") {
        (0..n).map(|k| w.c(&format!("lang.{k}"), 0, V2.len() - 1).map(|i| V2[i])).collect::<R<_>>()?
    } else {
        match n {
            3 => vec!["en", "ru", "uk"],
            4 => vec!["en", "es", "it", "pt"],
            _ => return Err("model file: unknown language set"),
        }
    };
    Ok(codes
        .into_iter()
        .map(|c| {
            let mut l = LangInfo::new(c);
            match c {
                "uk" => l.own = "ґҐ".into(),
                "km" => (l.full_stop, l.quotes, l.syllables) = ('។', Some(('“', '”')), true),
                _ => {}
            }
            l
        })
        .collect())
}

/// The languages of a TZF3 file: every rule from its text settings.
fn file_langs(w: &Tensors, n: usize) -> R<Vec<LangInfo>> {
    let one = |k: usize, key: &str| w.text.get(&format!("lang.{k}.{key}")).and_then(|s| s.chars().next());
    (0..n)
        .map(|k| {
            let code = w.text.get(&format!("lang.{k}")).filter(|c| !c.is_empty()).ok_or(SETTING)?;
            let mut l = LangInfo::new(code);
            l.full_stop = one(k, "full_stop").unwrap_or('.');
            l.quotes = w.text.get(&format!("lang.{k}.quotes")).and_then(|q| {
                let mut c = q.chars();
                Some((c.next()?, c.next()?))
            });
            l.own = w.text.get(&format!("lang.{k}.own")).cloned().unwrap_or_default();
            l.syllables = w.cfg.get(&format!("lang.{k}.syllables")).is_some_and(|&v| v != 0);
            Ok(l)
        })
        .collect()
}

impl Model {
    /// The place of the language with this code among the model's (None: the model has no such language).
    pub fn lang_index(&self, code: &str) -> Option<usize> {
        self.langs.iter().position(|l| l.code == code)
    }
}

/// Most tokens emitted on one encoder frame in greedy search (from the MAX_SYMBOLS env var in std builds).
#[cfg(feature = "std")]
static MAX_SYMBOLS_ENV: std::sync::OnceLock<usize> = std::sync::OnceLock::new();
#[cfg(feature = "std")]
fn max_symbols() -> usize {
    *MAX_SYMBOLS_ENV.get_or_init(|| std::env::var("MAX_SYMBOLS").ok().and_then(|v| v.parse().ok()).unwrap_or(3))
}
#[cfg(not(feature = "std"))]
fn max_symbols() -> usize {
    3
}

impl Model {
    /// The recognizer from its .tzf file. Every shape the model runs with is checked here against the settings and
    /// the other tensors, so a corrupted file or one for another version of the library is an error, never a crash
    /// later.
    pub fn from_bytes(b: &[u8]) -> Result<Model, &'static str> {
        let mut w = Tensors::parse(b)?;
        let ns = w.c("n_stacks", 1, 16)?;
        let pos_dim = w.c("pos_dim", 2, 1024)?;
        let left = w.c("left_context", 0, 1024)?;
        let chunk = w.c("chunk", 1, 512)?;
        let context = w.c("context", 1, 16)?;
        let mut stacks: Vec<Stack> = Vec::new();
        for s in 0..ns {
            let g = |w: &Tensors, k: &str, lo, hi| w.c(&format!("{k}.{s}"), lo, hi);
            let dim = g(&w, "encoder_dim", 4, 4096)?;
            let nl = g(&w, "num_encoder_layers", 1, 64)?;
            let ds = g(&w, "downsampling_factor", 1, 64)?;
            let heads = g(&w, "num_heads", 1, 64)?;
            let qd = g(&w, "query_head_dim", 1, 1024)?;
            let pd = g(&w, "pos_head_dim", 1, 1024)?;
            let kernel = g(&w, "cnn_module_kernel", 1, 255)?;
            let half = kernel / 2 + 1;
            let hid = 3 * dim / 4;
            let base = if ds == 1 { format!("encoder.encoders.{s}") } else { format!("encoder.encoders.{s}.encoder") };
            let mut layers: Vec<Layer> = Vec::new();
            for l in 0..nl {
                let p = format!("{base}.layers.{l}");
                let ff = |w: &mut Tensors, n: &str| -> R<FeedForward> {
                    let inp = w.linear(&format!("{p}.{n}.in_proj"), true, None, dim)?;
                    let out = w.linear(&format!("{p}.{n}.out_proj"), true, Some(dim), inp.n())?;
                    Ok(FeedForward { inp, out })
                };
                let conv = |w: &mut Tensors, n: &str| -> R<Conv> {
                    let q = |x: &str| format!("{p}.{n}.{x}");
                    let sc = w.vec(&q("depthwise_conv.chunkwise_conv_scale"), 2 * dim * kernel)?;
                    let (l, r) = sc.split_at(dim * kernel);
                    let mut scale = transpose(l, dim, kernel);
                    scale.extend(transpose(r, dim, kernel));
                    Ok(Conv {
                        inp: w.linear(&q("in_proj"), true, Some(2 * dim), dim)?,
                        causal_w: transpose(&w.vec(&q("depthwise_conv.causal_conv.weight"), dim * half)?, dim, half),
                        causal_b: w.vec(&q("depthwise_conv.causal_conv.bias"), dim)?,
                        chunk_w: transpose(&w.vec(&q("depthwise_conv.chunkwise_conv.weight"), dim * kernel)?, dim, kernel),
                        chunk_b: w.vec(&q("depthwise_conv.chunkwise_conv.bias"), dim)?,
                        scale,
                        out: w.linear(&q("out_proj"), true, Some(dim), dim)?,
                        dim,
                        kernel,
                    })
                };
                let sa = |w: &mut Tensors, n: &str| -> R<SelfAttn> {
                    let inp = w.linear(&format!("{p}.{n}.in_proj"), true, None, dim)?;
                    if inp.n() % heads != 0 {
                        return Err(SHAPE);
                    }
                    let out = w.linear(&format!("{p}.{n}.out_proj"), true, Some(dim), inp.n())?;
                    Ok(SelfAttn { inp, out, heads })
                };
                let layer = Layer {
                    attn: AttnWeights {
                        in_proj: w.linear(&format!("{p}.self_attn_weights.in_proj"), true, Some(2 * heads * qd + heads * pd), dim)?,
                        pos: w.linear(&format!("{p}.self_attn_weights.linear_pos"), false, Some(heads * pd), pos_dim)?,
                        heads,
                        qd,
                        pd,
                    },
                    ff1: ff(&mut w, "feed_forward1")?,
                    ff2: ff(&mut w, "feed_forward2")?,
                    ff3: ff(&mut w, "feed_forward3")?,
                    nonlin: NonlinAttn {
                        inp: w.linear(&format!("{p}.nonlin_attention.in_proj"), true, Some(3 * hid), dim)?,
                        out: w.linear(&format!("{p}.nonlin_attention.out_proj"), true, Some(dim), hid)?,
                        hid,
                    },
                    sa1: sa(&mut w, "self_attn1")?,
                    sa2: sa(&mut w, "self_attn2")?,
                    conv1: conv(&mut w, "conv_module1")?,
                    conv2: conv(&mut w, "conv_module2")?,
                    norm: BiasNorm::load(&mut w, &format!("{p}.norm"), dim)?,
                    bypass: w.vec(&format!("{p}.bypass.bypass_scale"), dim)?,
                    bypass_mid: w.vec(&format!("{p}.bypass_mid.bypass_scale"), dim)?,
                };
                // the streaming state of every layer is laid out as layer 0's
                if let Some(l0) = layers.first() {
                    if layer.sa1.inp.n() != l0.sa1.inp.n() || layer.sa2.inp.n() != l0.sa2.inp.n() {
                        return Err(SHAPE);
                    }
                }
                layers.push(layer);
            }
            let (down, combine) = if ds == 1 {
                (vec![], vec![])
            } else {
                (softmax(&w.vec(&format!("encoder.encoders.{s}.downsample.bias"), ds)?), w.vec(&format!("encoder.encoders.{s}.out_combiner.bypass_scale"), dim)?)
            };
            stacks.push(Stack { layers, dim, ds, down, combine });
        }
        let d0 = stacks[0].dim;
        let enc_dim = stacks.iter().map(|s| s.dim).max().unwrap_or(d0); // the full-dim output (`encode`)
        let out_down = softmax(&w.vec_in("encoder.downsample_output.bias", 1, 64)?);
        // front end: im2col weights stay PyTorch (cout, cin*9); `out` takes (f, c) instead of (c, f)
        let (od, odata) = w.take("encoder_embed.out.weight")?;
        let ok = FW * C3;
        if od.first() != Some(&d0) || od[1..].iter().product::<usize>() != ok {
            return Err(SHAPE);
        }
        // column f * C3 + c reads PyTorch's c * FW + f
        let perm = |p: usize| (p % C3) * FW + p / C3;
        let out_w = match odata {
            Data::Int(q) if q.per_row(ok) => Weight::I8(QuantF::new(d0, ok, |j, p| q.code(j, perm(p)), |j| q.scale(j, 0))),
            data => {
                let ow = data.f32(&od);
                let ow2: Vec<f32> = (0..d0 * ok).map(|i| ow[(i / ok) * ok + perm(i % ok)]).collect();
                Weight::F32(Packed::from_rows(&ow2, d0, ok))
            }
        };
        let pw1 = w.linear("encoder_embed.convnext.pointwise_conv1", true, None, C3)?;
        let embed = Embed {
            c0: w.conv_linear("encoder_embed.conv.0", 8, 1)?,
            c1: w.conv_linear("encoder_embed.conv.4", 32, 8)?,
            c2: w.conv_linear("encoder_embed.conv.7", C3, 32)?,
            dw_w: transpose(&w.vec("encoder_embed.convnext.depthwise_conv.weight", C3 * 49)?, C3, 49),
            dw_b: w.vec("encoder_embed.convnext.depthwise_conv.bias", C3)?,
            pw2: w.linear("encoder_embed.convnext.pointwise_conv2", true, Some(C3), pw1.n())?,
            pw1,
            out: Linear { w: out_w, b: Some(w.vec("encoder_embed.out.bias", d0)?) },
            norm: BiasNorm::load(&mut w, "encoder_embed.out_norm", d0)?,
        };
        // the languages: the file's own codes and rules (TZF3), or those of the version 2 codes
        let n = w.c("n_langs", 1, 256)?;
        let langs = if w.text.is_empty() { legacy_langs(&w, n)? } else { file_langs(&w, n)? };
        let le = w.get("lang_embed.weight")?.1;
        if le.len() < n * d0 {
            return Err(SHAPE);
        }
        let lang_vec = (0..n).map(|k| le[k * d0..(k + 1) * d0].to_vec()).collect();
        // decoder: embedding (vocabulary x dec_dim, dec_dim in groups of 4) and its convolution over `context` tokens
        let (dd, demb) = w.take("decoder.embedding.weight")?;
        let dec_dim: usize = dd.get(1..).map_or(0, |r| r.iter().product());
        if dd.len() < 2 || dec_dim == 0 || dec_dim % 4 != 0 || dd[0] == 0 {
            return Err(SHAPE);
        }
        let dec_emb = match demb {
            Data::Int(q) if q.per_row(dec_dim) => Emb::I8 {
                q: (0..dd[0] * dec_dim).map(|i| q.code(i / dec_dim, i % dec_dim)).collect(),
                s: (0..dd[0]).map(|r| q.scale(r, 0)).collect(),
                d: dec_dim,
            },
            data => Emb::F32(data.f32(&dd), dec_dim),
        };
        let dec_conv = w.vec("decoder.conv.weight", dec_dim * 4 * context)?;
        // joiner: every id a language can emit (< shared + n * lang_size) has an output row and a decoder embedding
        let shared = w.c("shared", 0, 1 << 16)?;
        let size = w.c("lang_size", 0, 1 << 16)?;
        let enc = w.linear("joiner.encoder_proj", true, None, enc_dim)?;
        let jdim = enc.n();
        let (jd, jdata) = w.take("joiner.output_linear.weight")?;
        let vocab = jd.first().copied().unwrap_or(0);
        if vocab == 0 || jd[1..].iter().product::<usize>() != jdim || shared + size == 0 || shared + n * size > vocab || vocab > dd[0] {
            return Err(SHAPE);
        }
        // int8 rows stay int8 (multiplied in f32: `QuantF`)
        let jw = match jdata {
            Data::Int(q) if q.per_row(jdim) => Ok(q),
            data => Err(data.f32(&jd)),
        };
        let jb = w.vec("joiner.output_linear.bias", vocab)?;
        let (shared, size) = (shared as u32, size as u32);
        let out = (0..n as u32)
            .map(|k| {
                // shared block + English; a shared piece with a letter some language owns (ґ: Ukrainian; reserved ids,
                // tok_dir/extra.json) only for the languages that own it
                let own = &langs[k as usize].own;
                let other = |c: char| !own.contains(c) && langs.iter().any(|l| l.own.contains(c));
                let mut ids: Vec<u32> =
                    (0..shared + size).filter(|&i| i >= shared || !w.pieces.get(i as usize).is_some_and(|p| p.chars().any(other))).collect();
                if k != 0 {
                    ids.extend(shared + k * size..shared + (k + 1) * size);
                }
                let lw = match &jw {
                    Ok(q) => Weight::I8(QuantF::new(ids.len(), jdim, |j, p| q.code(ids[j] as usize, p), |j| q.scale(ids[j] as usize, 0))),
                    Err(jw) => {
                        let mut rows = Vec::with_capacity(ids.len() * jdim);
                        for &i in &ids {
                            rows.extend_from_slice(&jw[i as usize * jdim..(i as usize + 1) * jdim]);
                        }
                        Weight::F32(Packed::from_rows(&rows, ids.len(), jdim))
                    }
                };
                let b = ids.iter().map(|&i| jb[i as usize]).collect();
                (Linear { w: lw, b: Some(b) }, ids)
            })
            .collect::<Vec<_>>();
        if out.iter().any(|(_, ids)| ids.first() != Some(&0)) {
            return Err(SHAPE); // the blank (0) is every language's first output
        }
        let joiner = Joiner { enc, dec: w.linear("joiner.decoder_proj", true, Some(jdim), dec_dim)?, out };
        let dur = if w.has("joiner.duration_linear.weight") { Some(w.linear("joiner.duration_linear", true, None, jdim)?) } else { None };
        if dur.as_ref().is_some_and(|d| d.n() < 2) {
            return Err(SHAPE);
        }
        let lstm = if w.has("decoder.lstm.weight_ih_l0") {
            let b2 = w.vec_in("decoder.lstm.bias_hh_l0", 4, 1 << 16)?;
            let hidden = b2.len() / 4;
            if b2.len() % 4 != 0 {
                return Err(SHAPE);
            }
            let b1 = w.vec("decoder.lstm.bias_ih_l0", 4 * hidden)?;
            let mut ih = w.linear_named("decoder.lstm.weight_ih_l0", None, Some(4 * hidden), dec_dim)?;
            ih.b = Some(b1.iter().zip(&b2).map(|(a, b)| a + b).collect());
            let hh = w.linear_named("decoder.lstm.weight_hh_l0", None, Some(4 * hidden), hidden)?;
            let proj = w.linear_named("decoder.lstm_proj.weight", None, Some(dec_dim), hidden)?;
            Some(Lstm { ih, hh, proj, gate: w.scalar("decoder.lstm_gate_m.g")?, hidden })
        } else {
            None
        };
        let head = Head::load(&mut w, enc_dim, dec_dim)?;
        Ok(Model {
            head,
            embed,
            stacks,
            out_down,
            lang_vec,
            dec_emb,
            dec_conv,
            dec_dim,
            context,
            joiner,
            lstm,
            dur,
            pos_dim,
            left,
            chunk,
            pieces: core::mem::take(&mut w.pieces),
            langs,
        })
    }

    /// Stacks + output combination: x (t x dim0) at 50 Hz -> (ceil(t / 2) x 512) at 25 Hz.
    fn encode(&self, x: Vec<f32>, t: usize, mut states: Option<&mut [Vec<LayerState>]>, masked: &[bool]) -> (Vec<f32>, usize) {
        let mut outs: Vec<Vec<f32>> = Vec::new();
        let mut cur = x;
        let mut cd = self.stacks[0].dim;
        let left = if states.is_some() { self.left } else { 0 };
        for (s, st) in self.stacks.iter().enumerate() {
            if st.dim != cd {
                // convert_num_channels: truncate or zero-pad to this stack's width
                let mut y = vec![0.0; t * st.dim];
                let m = st.dim.min(cd);
                for i in 0..t {
                    y[i * st.dim..i * st.dim + m].copy_from_slice(&cur[i * cd..i * cd + m]);
                }
                cur = y;
                cd = st.dim;
            }
            let ls = states.as_deref_mut().map(|all| &mut all[s][..]);
            st.forward(&mut cur, t, ls, left, masked, self.pos_dim);
            outs.push(cur.clone());
        }
        // full-dim output: the last stack, then the extra channels of earlier, wider stacks
        let last = self.stacks.len() - 1;
        let mut pieces = vec![(last, 0, self.stacks[last].dim)];
        let mut od = self.stacks[last].dim;
        for i in (0..last).rev() {
            let d = self.stacks[i].dim;
            if d > od {
                pieces.push((i, od, d));
                od = d;
            }
        }
        let mut full = vec![0.0; t * od];
        for i in 0..t {
            let mut o = 0;
            for &(s, a, b) in &pieces {
                let sd = self.stacks[s].dim;
                full[i * od + o..i * od + o + b - a].copy_from_slice(&outs[s][i * sd + a..i * sd + b]);
                o += b - a;
            }
        }
        downsample(&full, t, od, &self.out_down)
    }

    /// Full-context encoder output (frames at 25 Hz x 512) for fbank features (t x 80).
    pub fn encode_full(&self, feats: &[f32], t: usize, lang: usize) -> (Vec<f32>, usize) {
        let (x, tc) = self.embed.convs(feats, t);
        let row = FW * C3;
        let mut rows = vec![0.0; (tc + 6) * row];
        rows[3 * row..(3 + tc) * row].copy_from_slice(&x);
        let x = self.embed.convnext(&rows, tc + 6);
        let mut e = self.embed.finish(&x, tc);
        let lv = &self.lang_vec[lang];
        e.chunks_exact_mut(lv.len()).for_each(|r| add(r, lv));
        self.encode(e, tc, None, &[])
    }

    fn decoder_out(&self, ctx: &[u32], h: &[f32]) -> Vec<f32> {
        let mut y = self.decoder_y(ctx);
        if let Some(l) = &self.lstm {
            let p = l.proj.apply(h, 1);
            y.iter_mut().zip(&p).for_each(|(a, b)| *a += l.gate * b);
        }
        self.joiner.dec.apply(&y, 1)
    }

    /// `advance` for several hypotheses at once: the LSTM, its projection and the joiner's decoder projection run as
    /// one batch.
    fn advance_batch(&self, decs: &mut [&mut Decoding], ids: &[u32], ihc: &mut IhCache) {
        let n = decs.len();
        if n == 0 {
            return;
        }
        let d = self.dec_dim;
        let mut ys: Vec<f32> = Vec::with_capacity(n * d);
        for (dec, &id) in decs.iter_mut().zip(ids) {
            dec.ctx.push(id);
            ys.extend(self.decoder_y(&dec.ctx));
        }
        if let Some(l) = &self.lstm {
            let hn = l.hidden;
            let mut hp = Vec::with_capacity(n * hn);
            for dec in decs.iter() {
                hp.extend_from_slice(&dec.h);
            }
            // the input part depends on the token alone: computed once per token in this pass (row-wise, so the
            // same values as in a batch)
            let mut new: Vec<u32> = Vec::new();
            for &id in ids {
                if !ihc.has(id) && !new.contains(&id) {
                    new.push(id);
                }
            }
            let gi = if new.is_empty() {
                Vec::new()
            } else {
                let mut x = Vec::with_capacity(new.len() * d);
                new.iter().for_each(|&id| x.extend_from_slice(&self.dec_emb.row(id)));
                l.ih.apply(&x, new.len())
            };
            let mut g = Vec::with_capacity(n * 4 * hn);
            for &id in ids {
                match new.iter().position(|&j| j == id) {
                    Some(j) => g.extend_from_slice(&gi[j * 4 * hn..(j + 1) * 4 * hn]),
                    None => g.extend_from_slice(ihc.get(id)),
                }
            }
            for (j, &id) in new.iter().enumerate() {
                ihc.put(id, &gi[j * 4 * hn..(j + 1) * 4 * hn]);
            }
            let r = l.hh.apply(&hp, n);
            g.iter_mut().zip(&r).for_each(|(a, b)| *a += b);
            for (b, dec) in decs.iter_mut().enumerate() {
                let g = &g[b * 4 * hn..(b + 1) * 4 * hn];
                for j in 0..hn {
                    let (i, f, gg, o) = (sigmoid(g[j]), sigmoid(g[hn + j]), tanh(g[2 * hn + j]), sigmoid(g[3 * hn + j]));
                    dec.c[j] = f * dec.c[j] + i * gg;
                    dec.h[j] = o * tanh(dec.c[j]);
                }
            }
            let mut hs = Vec::with_capacity(n * hn);
            decs.iter().for_each(|dec| hs.extend_from_slice(&dec.h));
            let p = l.proj.apply(&hs, n);
            ys.iter_mut().zip(&p).for_each(|(a, b)| *a += l.gate * b);
        }
        let o = self.joiner.dec.apply(&ys, n);
        let jd = o.len() / n;
        for (b, dec) in decs.iter_mut().enumerate() {
            dec.out = o[b * jd..(b + 1) * jd].to_vec();
        }
    }

    /// Joiner hidden of every hypothesis at encoder frame i (rows), from the projected encoder output `ep`.
    fn joint_rows(&self, ep: &[f32], i: usize, decs: &[&Decoding]) -> Vec<f32> {
        let jd = self.joiner.enc.n();
        let mut h = Vec::with_capacity(decs.len() * jd);
        for dec in decs {
            h.extend((0..jd).map(|k| tanh(ep[i * jd + k] + dec.out[k])));
        }
        h
    }

    /// Emit `id`: extend the history, advance the LSTM, recompute the decoder output.
    fn advance(&self, dec: &mut Decoding, id: u32) {
        dec.ctx.push(id);
        if let Some(l) = &self.lstm {
            l.step(&self.dec_emb.row(id), &mut dec.h, &mut dec.c);
        }
        dec.out = self.decoder_out(&dec.ctx, &dec.h);
    }


    /// Decoder output before the joiner's projection (dec_dim), for the last `context` tokens of `ctx`.
    fn decoder_y(&self, ctx: &[u32]) -> Vec<f32> {
        let d = self.dec_dim;
        let c = self.context;
        let mut y = vec![0.0; d];
        let rows: Vec<Vec<f32>> = ctx[ctx.len() - c..].iter().map(|&tok| self.dec_emb.row(tok)).collect();
        for o in 0..d {
            let g = o / 4;
            let mut a = 0.0;
            for i in 0..4 {
                for (k, row) in rows.iter().enumerate() {
                    a += self.dec_conv[(o * 4 + i) * c + k] * row[g * 4 + i];
                }
            }
            y[o] = a.max(0.0);
        }
        y
    }

    /// Greedy RNN-T over encoder frames, continuing from `dec`.
    fn greedy(&self, enc: &[f32], t: usize, lang: usize, dec: &mut Decoding, frame0: u32) -> Vec<Token> {
        if let Some(dur) = &self.dur {
            return self.greedy_tdt(dur, enc, t, lang, dec, frame0);
        }
        let ep = self.joiner.enc.apply(enc, t);
        let jd = self.joiner.enc.n();
        let (out, ids) = &self.joiner.out[lang];
        let mut toks = Vec::new();
        let mut h = vec![0.0; jd];
        for i in 0..t {
            // up to max_symbols() tokens per 40 ms frame (fast speech needs more than one)
            for _ in 0..max_symbols() {
                for (k, v) in h.iter_mut().enumerate() {
                    *v = tanh(ep[i * jd + k] + dec.out[k]);
                }
                let logit = out.apply(&h, 1);
                let (best, &m) = logit.iter().enumerate().fold((0, &f32::MIN), |a, b| if b.1 > a.1 { b } else { a });
                let id = ids[best];
                if id == 0 {
                    break;
                }
                let lse = m + logit.iter().map(|v| exp(v - m)).sum::<f32>().ln();
                toks.push(Token { id, frame: frame0 + i as u32, logp: m - lse });
                self.advance(dec, id);
            }
        }
        toks
    }

    /// modified_beam_search (icefall beam_search.py): per frame, every hypothesis is extended by its log-softmax over the
    /// language's tokens; the `beam` best (hypothesis, token) pairs survive, blank keeping the text; at most one token
    /// per frame; hypotheses with the same tokens are merged (log-add). Continues from `hyps`.
    fn beam_rnnt(&self, enc: &[f32], t: usize, lang: usize, hyps: &mut Vec<Hyp>, frame0: u32, beam: usize) {
        let ep = self.joiner.enc.apply(enc, t);
        let mut ihc = IhCache::default();
        let (out, ids) = &self.joiner.out[lang];
        for i in 0..t {
            let mut cand: Vec<(f32, usize, usize, f32)> = Vec::new(); // (score, hyp, output index, token logp)
            let nh = hyps.len();
            let hrows = self.joint_rows(&ep, i, &hyps.iter().map(|h| &h.dec).collect::<Vec<_>>());
            let lg = out.apply(&hrows, nh);
            let vn = lg.len() / nh;
            for (hi, hy) in hyps.iter().enumerate() {
                let logit = &lg[hi * vn..(hi + 1) * vn];
                let m = logit.iter().cloned().fold(f32::MIN, f32::max);
                let lse = m + logit.iter().map(|v| exp(v - m)).sum::<f32>().ln();
                for (k, &v) in logit.iter().enumerate() {
                    cand.push((hy.logp + v - lse, hi, k, v - lse));
                }
            }
            let n = beam.min(cand.len());
            cand.select_nth_unstable_by(n - 1, |a, b| b.0.total_cmp(&a.0));
            cand.truncate(n);
            let mut next: Vec<Hyp> = Vec::with_capacity(n);
            for &(score, hi, k, lp) in &cand {
                let id = ids[k];
                let mut hy = hyps[hi].clone();
                hy.logp = score;
                if id != 0 {
                    hy.toks.push(Token { id, frame: frame0 + i as u32, logp: lp });
                    hy.pending = true;
                }
                if let Some(o) = next.iter_mut().find(|o| o.toks.len() == hy.toks.len() && o.toks.iter().zip(&hy.toks).all(|(a, b)| a.id == b.id)) {
                    let (a, b) = (o.logp.max(hy.logp), o.logp.min(hy.logp));
                    o.logp = a + (1.0 + exp(b - a)).ln();
                } else {
                    next.push(hy);
                }
            }
            let pend: Vec<u32> = next.iter().filter(|h| h.pending).map(|h| h.toks.last().unwrap().id).collect();
            let mut decs: Vec<&mut Decoding> = next.iter_mut().filter(|h| h.pending).map(|h| { h.pending = false; &mut h.dec }).collect();
            self.advance_batch(&mut decs, &pend, &mut ihc);
            *hyps = next;
        }
    }

    fn beam_tdt(&self, dur: &Linear, enc: &[f32], t: usize, lang: usize, beam: usize) -> Vec<Token> {
        let ep = self.joiner.enc.apply(enc, t);
        let mut ihc = IhCache::default();
        let (out, ids) = &self.joiner.out[lang];
        let lsm = |v: &[f32]| {
            let m = v.iter().cloned().fold(f32::MIN, f32::max);
            let lse = m + v.iter().map(|x| exp(x - m)).sum::<f32>().ln();
            v.iter().map(|x| x - lse).collect::<Vec<f32>>()
        };
        let same = |a: &Hyp, b: &Hyp| a.toks.len() == b.toks.len() && a.toks.iter().zip(&b.toks).all(|(x, y)| x.id == y.id);
        // merge `hy` into `list` (log-add on equal text)
        let push = |list: &mut Vec<Hyp>, hy: Hyp| {
            if let Some(o) = list.iter_mut().find(|o| same(o, &hy)) {
                let (a, b) = (o.logp.max(hy.logp), o.logp.min(hy.logp));
                o.logp = a + (1.0 + exp(b - a)).ln();
            } else {
                list.push(hy);
            }
        };
        let mut buckets: Vec<Vec<Hyp>> = (0..t).map(|_| Vec::new()).collect();
        let mut done: Vec<Hyp> = Vec::new();
        if t == 0 {
            return Vec::new();
        }
        buckets[0] = self.start_beam();
        for i in 0..t {
            let mut cur = core::mem::take(&mut buckets[i]);
            for _round in 0..=TDT_MAX_SYM {
                if cur.is_empty() {
                    break;
                }
                cur.sort_by(|a, b| b.logp.total_cmp(&a.logp));
                cur.truncate(beam);
                {  // decoder step for the survivors only, as one batch
                    let pend: Vec<u32> = cur.iter().filter(|h| h.pending).map(|h| h.toks.last().unwrap().id).collect();
                    let mut decs: Vec<&mut Decoding> = cur.iter_mut().filter(|h| h.pending).map(|h| { h.pending = false; &mut h.dec }).collect();
                    self.advance_batch(&mut decs, &pend, &mut ihc);
                }
                let mut stay: Vec<Hyp> = Vec::new(); // zero-duration tokens: same frame, next round
                let nh = cur.len();
                let hrows = self.joint_rows(&ep, i, &cur.iter().map(|h| &h.dec).collect::<Vec<_>>());
                let lgall = out.apply(&hrows, nh);
                let ldall = dur.apply(&hrows, nh);
                let (vn, dn) = (lgall.len() / nh, ldall.len() / nh);
                for (hi, hy) in cur.iter().enumerate() {
                    let lt = lsm(&lgall[hi * vn..(hi + 1) * vn]);
                    let ld = lsm(&ldall[hi * dn..(hi + 1) * dn]);
                    // this hypothesis' best `beam` (token, duration) pairs
                    let mut cand: Vec<(f32, usize, usize)> = Vec::new();
                    let mut order: Vec<usize> = (0..lt.len()).collect();
                    let kk = beam.min(order.len());
                    order.select_nth_unstable_by(kk - 1, |a, b| lt[*b].total_cmp(&lt[*a]));
                    for &k in &order[..kk] {
                        for (d, &l) in ld.iter().enumerate() {
                            if ids[k] == 0 && d == 0 {
                                continue;
                            }
                            cand.push((hy.logp + lt[k] + l, k, d));
                        }
                    }
                    let n = beam.min(cand.len());
                    cand.select_nth_unstable_by(n - 1, |a, b| b.0.total_cmp(&a.0));
                    for &(score, k, d) in &cand[..n] {
                        let id = ids[k];
                        let mut nh = hy.clone();
                        nh.logp = score;
                        if id != 0 {
                            nh.toks.push(Token { id, frame: i as u32, logp: lt[k] });
                            nh.pending = true;
                        }
                        if d == 0 {
                            push(&mut stay, nh);
                        } else if i + d >= t {
                            push(&mut done, nh);
                        } else {
                            push(&mut buckets[i + d], nh);
                        }
                    }
                }
                cur = stay;
            }
            for mut hy in cur {  // still on this frame after TDT_MAX_SYM zero-duration rounds: forced on by one frame
                if hy.pending {
                    let id = hy.toks.last().map(|x| x.id).unwrap_or(0);
                    self.advance(&mut hy.dec, id);
                    hy.pending = false;
                }
                if i + 1 >= t { push(&mut done, hy) } else { push(&mut buckets[i + 1], hy) }
            }
        }
        Model::best(&done)
    }

    fn start_beam(&self) -> Vec<Hyp> {
        vec![Hyp { dec: Decoding::new(self), logp: 0.0, toks: Vec::new(), pending: false }]
    }

    fn best(hyps: &[Hyp]) -> Vec<Token> {
        hyps.iter().max_by(|a, b| a.logp.total_cmp(&b.logp)).map(|h| h.toks.clone()).unwrap_or_default()
    }

    /// TDT greedy (tdt_decode.py): after a token or blank the frame pointer moves on by the predicted duration (a blank
    /// by at least 1; after TDT_MAX_SYM zero-duration tokens in a row by 1). A jump past this chunk's last frame is
    /// carried into the next chunk (`dec.skip`).
    fn greedy_tdt(&self, dur: &Linear, enc: &[f32], t: usize, lang: usize, dec: &mut Decoding, frame0: u32) -> Vec<Token> {
        let ep = self.joiner.enc.apply(enc, t);
        let jd = self.joiner.enc.n();
        let (out, ids) = &self.joiner.out[lang];
        let mut toks = Vec::new();
        let mut h = vec![0.0; jd];
        let argmax = |v: &[f32], from: usize| (from..v.len()).fold(from, |a, j| if v[j] > v[a] { j } else { a });
        let mut i = dec.skip;
        while i < t {
            for (k, v) in h.iter_mut().enumerate() {
                *v = tanh(ep[i * jd + k] + dec.out[k]);
            }
            let logit = out.apply(&h, 1);
            let dl = dur.apply(&h, 1);
            let best = argmax(&logit, 0);
            let id = ids[best];
            let mut d;
            if id == 0 {
                d = argmax(&dl, 1); // a blank always moves on
                dec.run = 0;
            } else {
                d = argmax(&dl, 0);
                let m = logit[best];
                let lse = m + logit.iter().map(|v| exp(v - m)).sum::<f32>().ln();
                toks.push(Token { id, frame: frame0 + i as u32, logp: m - lse });
                self.advance(dec, id);
                dec.run = if d == 0 { dec.run + 1 } else { 0 };
                if dec.run >= TDT_MAX_SYM {
                    d = 1;
                    dec.run = 0;
                }
            }
            i += d;
        }
        dec.skip = i - t;
        toks
    }


    /// Second pass: full-context transcription of a finished segment (16 kHz); beam > 1: beam search of that width
    /// (RNN-T: modified_beam_search; TDT: over tokens and durations), else greedy.
    pub fn full(&self, audio: &[f32], lang: usize, beam: usize) -> Vec<Token> {
        let fb = Fbank::new();
        let t = Fbank::num_frames(audio.len());
        if t < 16 {
            return Vec::new();
        }
        let feats = fb.frames(audio, 0, t);
        let (enc, te) = self.encode_full(&feats, t, lang);
        if beam > 1 {
            if let Some(dur) = &self.dur {
                return self.beam_tdt(dur, &enc, te, lang, beam);
            }
            let mut hyps = self.start_beam();
            self.beam_rnnt(&enc, te, lang, &mut hyps, 0, beam);
            return Model::best(&hyps);
        }
        let mut dec = Decoding::new(self);
        self.greedy(&enc, te, lang, &mut dec, 0)
    }

    /// Second pass + P(the speaker has finished) from the end-of-turn head (None if the file has no head): the last
    /// HEAD_K encoder frames and the decoder output after the last text tokens (<eou>, id 12, skipped).
    pub fn full_head(&self, audio: &[f32], lang: usize) -> (Vec<Token>, Option<f32>) {
        let fb = Fbank::new();
        let t = Fbank::num_frames(audio.len());
        if t < 16 {
            return (Vec::new(), None);
        }
        let feats = fb.frames(audio, 0, t);
        let (enc, te) = self.encode_full(&feats, t, lang);
        let mut dec = Decoding::new(self);
        let toks = self.greedy(&enc, te, lang, &mut dec, 0);
        let p = self.head.as_ref().map(|h| {
            let d = enc.len() / te.max(1);
            let n = te.min(HEAD_K);
            let mut last = vec![0.0; HEAD_K * d];
            last[(HEAD_K - n) * d..].copy_from_slice(&enc[(te - n) * d..te * d]);
            let ids: Vec<u32> = toks.iter().map(|t| t.id).filter(|&i| i != 12).collect();
            let mut ctx = vec![0u32; self.context];
            ctx.extend(&ids);
            let mut y = self.decoder_y(&ctx);
            if let Some(l) = &self.lstm {  // hybrid decoder: + the LSTM output after [blank] + the text (eou_head.py)
                let (mut hs, mut cs) = (vec![0.0; l.hidden], vec![0.0; l.hidden]);
                for &i in [0].iter().chain(&ids) {
                    l.step(&self.dec_emb.row(i), &mut hs, &mut cs);
                }
                let p = l.proj.apply(&hs, 1);
                y.iter_mut().zip(&p).for_each(|(a, b)| *a += l.gate * b);
            }
            h.prob(&last, n, &y)
        });
        (toks, p)
    }

    pub fn has_head(&self) -> bool {
        self.head.is_some()
    }

    pub fn stream(&self, lang: usize) -> Stream<'_> {
        Stream::new(self, lang)
    }

}

/// Decoder context (last `context` tokens) and its projected output.
#[derive(Clone)]
pub struct Decoding {
    ctx: Vec<u32>,
    out: Vec<f32>,
    h: Vec<f32>, // LSTM state (empty without the LSTM)
    c: Vec<f32>,
    skip: usize, // TDT: frames of the next chunk already jumped over
    run: usize,  // TDT: zero-duration tokens in a row on the current frame
}

impl Decoding {
    fn new(m: &Model) -> Decoding {
        let ctx = vec![0; m.context];
        let n = m.lstm.as_ref().map_or(0, |l| l.hidden);
        let (mut h, mut c) = (vec![0.0; n], vec![0.0; n]);
        if let Some(l) = &m.lstm {  // training runs the LSTM over [blank] + labels: the start state has seen blank
            l.step(&m.dec_emb.row(0), &mut h, &mut c);
        }
        let out = m.decoder_out(&ctx, &h);
        Decoding { ctx, out, h, c, skip: 0, run: 0 }
    }
}

// ---------------------------------------------------------------------------------------------- streaming

/// Feature frames per chunk window and per step: 2 * chunk + 13 (7 for the convolutions, 6 for ConvNeXt), and
/// 2 * chunk.
pub struct Stream<'m> {
    m: &'m Model,
    lang: usize,
    fb: Fbank,
    audio: Vec<f32>, // samples from `audio0` on (what the next feature frames read)
    audio0: usize,
    feats: Vec<f32>, // feature frames from `feat0` on (what the next chunks read)
    feat0: usize,
    nfeat: usize, // feature frames computed so far
    chunks: usize,
    embed_cache: Vec<f32>, // 3 x 19 x 128
    states: Vec<Vec<LayerState>>,
    processed: usize, // 50 Hz frames through the encoder
    dec: Decoding,
    finished: bool,
    /// Encoder output of every chunk so far (frames x 512), kept when `keep_enc` is set (tests).
    pub keep_enc: bool,
    pub enc: Vec<f32>,
}

impl<'m> Stream<'m> {
    fn new(m: &'m Model, lang: usize) -> Stream<'m> {
        let states = m
            .stacks
            .iter()
            .map(|st| {
                let left = m.left / st.ds;
                let d = st.dim;
                let l0 = &st.layers[0];
                let s = LayerState {
                    key: vec![0.0; left * l0.attn.heads * l0.attn.qd],
                    nonlin: vec![0.0; left * l0.nonlin.hid],
                    val1: vec![0.0; left * l0.sa1.inp.n()],
                    val2: vec![0.0; left * l0.sa2.inp.n()],
                    conv1: vec![0.0; (l0.conv1.kernel / 2) * d],
                    conv2: vec![0.0; (l0.conv2.kernel / 2) * d],
                };
                vec![s; st.layers.len()]
            })
            .collect();
        Stream {
            m,
            lang,
            fb: Fbank::new(),
            audio: Vec::new(),
            audio0: 0,
            feats: Vec::new(),
            feat0: 0,
            nfeat: 0,
            chunks: 0,
            embed_cache: vec![0.0; 3 * FW * C3],
            states,
            processed: 0,
            dec: Decoding::new(m),
            finished: false,
            keep_enc: false,
            enc: Vec::new(),
        }
    }

    pub fn window(&self) -> usize {
        2 * self.m.chunk + 13
    }

    /// Feed 16 kHz audio; returns the tokens decoded from the chunks it completed.
    pub fn accept(&mut self, x: &[f32]) -> Vec<Token> {
        self.audio.extend_from_slice(x);
        let n = self.audio0 + self.audio.len();
        let ready = if self.finished { Fbank::num_frames(n) } else { Fbank::num_ready(n) };
        if ready > self.nfeat {
            let f = self.fb.frames_at(&self.audio, self.audio0, self.nfeat, ready);
            self.feats.extend_from_slice(&f);
            self.nfeat = ready;
        }
        let mut out = Vec::new();
        let (win, step) = (self.window(), 2 * self.m.chunk);
        while self.chunks * step + win <= self.nfeat {
            let s = self.chunks * step - self.feat0;
            let t = self.chunk(&self.feats[s * BINS..(s + win) * BINS].to_vec());
            out.extend(t);
            self.chunks += 1;
        }
        // keep only what the next frames and chunks read: memory stays flat however long the stream runs
        let f0 = self.chunks * step;
        if f0 > self.feat0 {
            self.feats.drain(..(f0 - self.feat0) * BINS);
            self.feat0 = f0;
        }
        let a0 = (self.nfeat * 160).saturating_sub(120).max(self.audio0);
        if self.nfeat > 0 && a0 > self.audio0 {
            self.audio.drain(..a0 - self.audio0);
            self.audio0 = a0;
        }
        out
    }

    /// End of input: pad with silence so the last audio goes through (like sherpa-onnx's tail padding).
    pub fn finish(&mut self) -> Vec<Token> {
        let pad = vec![0.0; (self.window() * 160).max(16000)];
        let mut out = self.accept(&pad);
        self.finished = true;
        out.extend(self.accept(&[]));
        out
    }

    fn chunk(&mut self, feats: &[f32]) -> Vec<Token> {
        let m = self.m;
        let win = self.window();
        let (x, tc) = m.embed.convs(feats, win);
        let row = FW * C3;
        let mut rows = core::mem::take(&mut self.embed_cache);
        rows.extend_from_slice(&x);
        let t = tc - 3; // = chunk
        // cache (3 rows) + 35 new rows: the 7x7 time context comes from the window itself (13 frames overlap)
        let y = m.embed.convnext(&rows, tc + 3);
        self.embed_cache = rows[t * row..(t + 3) * row].to_vec();
        let mut e = m.embed.finish(&y, t);
        let lv = &m.lang_vec[self.lang];
        e.chunks_exact_mut(lv.len()).for_each(|r| add(r, lv));
        let left = m.left;
        let masked: Vec<bool> = (0..left).map(|j| j + self.processed < left).collect();
        self.processed += t;
        let (enc, te) = m.encode(e, t, Some(&mut self.states), &masked);
        if self.keep_enc {
            self.enc.extend_from_slice(&enc);
        }
        let frame0 = (self.chunks * t / 2) as u32;
        m.greedy(&enc, te, self.lang, &mut self.dec, frame0)
    }
}
