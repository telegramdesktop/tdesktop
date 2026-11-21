//! Silero VAD v5 (16 kHz branch of silero_vad.onnx): speech probability per 512-sample chunk.
//!
//! STFT as a convolution (256-sample window, hop 128) -> magnitude -> 4 convolutions (k=3) with ReLU -> LSTM cell
//! (128) -> ReLU -> linear -> sigmoid. The model sees 64 samples of the previous chunk before each chunk.

use alloc::vec;
use alloc::vec::Vec;

use crate::kernels::{Packed, gemm, sigmoid, tanh};

#[cfg(not(feature = "std"))]
use crate::compat::F32Ext;
use crate::onnx::Graph;

pub const CHUNK: usize = 512;
const CTX: usize = 64;
const H: usize = 128;

struct Conv {
    w: Packed, // out x (in * 3), [in channel][tap]
    b: Vec<f32>,
    stride: usize,
}

pub struct Vad {
    basis: Packed, // 258 x 256: rows 0..129 real, 129..258 imaginary
    convs: Vec<Conv>,
    w_ih: Packed,
    w_hh: Packed,
    b: Vec<f32>, // b_ih + b_hh
    out_w: Vec<f32>,
    out_b: f32,
}

/// The running state of one audio stream.
#[derive(Clone)]
pub struct VadState {
    h: Vec<f32>,
    c: Vec<f32>,
    ctx: Vec<f32>,
}

impl VadState {
    pub fn new() -> VadState {
        VadState { h: vec![0.0; H], c: vec![0.0; H], ctx: vec![0.0; CTX] }
    }
}

impl Default for VadState {
    fn default() -> VadState {
        VadState::new()
    }
}

impl Vad {
    pub fn from_onnx(b: &[u8]) -> Result<Vad, &'static str> {
        let g = Graph::parse(b)?;
        // weights of the 16 kHz branch (sr == 16000 is the `then` branch of the first If)
        let w = |name: &str| -> Result<Vec<f32>, &'static str> {
            let mut key = alloc::string::String::from("If_0_then_branch__Inline_0__");
            key.push_str(name);
            Ok(g.tensor(&key).map_err(|_| "silero_vad.onnx: weights not found (Silero VAD v5 expected)")?.f32s())
        };
        // a weight of exactly n values
        let wn = |name: &str, n: usize| -> Result<Vec<f32>, &'static str> {
            let v = w(name)?;
            if v.len() == n { Ok(v) } else { Err("silero_vad.onnx: a weight has an unexpected size (Silero VAD v5 expected)") }
        };
        let basis = wn("stft.forward_basis_buffer", 258 * 256)?;
        let mut convs = Vec::new();
        for (i, (cin, cout, stride)) in [(129, 128, 1), (128, 64, 2), (64, 64, 2), (64, 128, 1)].into_iter().enumerate() {
            let wt = wn(&alloc::format!("encoder.{i}.reparam_conv.weight"), cout * cin * 3)?;
            convs.push(Conv { w: Packed::from_rows(&wt, cout, cin * 3), b: wn(&alloc::format!("encoder.{i}.reparam_conv.bias"), cout)?, stride });
        }
        let (bi, bh) = (wn("decoder.rnn.bias_ih", 4 * H)?, wn("decoder.rnn.bias_hh", 4 * H)?);
        Ok(Vad {
            basis: Packed::from_rows(&basis, 258, 256),
            convs,
            w_ih: Packed::from_rows(&wn("decoder.rnn.weight_ih", 4 * H * H)?, 4 * H, H),
            w_hh: Packed::from_rows(&wn("decoder.rnn.weight_hh", 4 * H * H)?, 4 * H, H),
            b: bi.iter().zip(&bh).map(|(a, b)| a + b).collect(),
            out_w: wn("decoder.decoder.2.weight", H)?,
            out_b: wn("decoder.decoder.2.bias", 1)?[0],
        })
    }

    /// Speech probability of the next 512 samples.
    pub fn chunk(&self, st: &mut VadState, x: &[f32]) -> f32 {
        assert_eq!(x.len(), CHUNK);
        // context + chunk, reflect-padded by 64 on the right
        let n = CTX + CHUNK;
        let mut a = vec![0.0; n + 64];
        a[..CTX].copy_from_slice(&st.ctx);
        a[CTX..n].copy_from_slice(x);
        for i in 0..64 {
            a[n + i] = a[n - 2 - i];
        }
        st.ctx.copy_from_slice(&x[CHUNK - CTX..]);
        // STFT magnitudes: 4 frames x 129 bins (time-major)
        let t = (a.len() - 256) / 128 + 1;
        let mut frames = vec![0.0; t * 256];
        for (i, f) in frames.chunks_exact_mut(256).enumerate() {
            f.copy_from_slice(&a[i * 128..i * 128 + 256]);
        }
        let spec = gemm(&frames, t, &self.basis, None);
        let mut h: Vec<f32> = spec.chunks_exact(258).flat_map(|r| (0..129).map(move |k| (r[k] * r[k] + r[129 + k] * r[129 + k]).sqrt())).collect();
        let (mut frames, mut ch) = (t, 129);
        for c in &self.convs {
            let out_t = (frames - 1) / c.stride + 1;
            let col = im2col(&h, frames, ch, out_t, c.stride);
            h = gemm(&col, out_t, &c.w, Some(&c.b));
            h.iter_mut().for_each(|v| *v = v.max(0.0));
            (frames, ch) = (out_t, c.w.n);
        }
        // one frame of 128 left: LSTM cell (gates i, f, g, o)
        let gi = gemm(&h[..H], 1, &self.w_ih, Some(&self.b));
        let gh = gemm(&st.h, 1, &self.w_hh, None);
        for j in 0..H {
            let g = |k: usize| gi[k * H + j] + gh[k * H + j];
            let (i, f, c, o) = (sigmoid(g(0)), sigmoid(g(1)), tanh(g(2)), sigmoid(g(3)));
            st.c[j] = f * st.c[j] + i * c;
            st.h[j] = o * tanh(st.c[j]);
        }
        let z: f32 = st.h.iter().zip(&self.out_w).map(|(h, w)| h.max(0.0) * w).sum::<f32>() + self.out_b;
        sigmoid(z)
    }
}

/// Rows of [channel][tap] for a k=3, pad=1 convolution over time-major input (frames x ch).
pub fn im2col(x: &[f32], frames: usize, ch: usize, out_frames: usize, stride: usize) -> Vec<f32> {
    let mut col = vec![0.0; out_frames * ch * 3];
    for (t, row) in col.chunks_exact_mut(ch * 3).enumerate() {
        for j in 0..3 {
            let src = (t * stride + j) as isize - 1;
            if src < 0 || src as usize >= frames {
                continue;
            }
            let s = &x[src as usize * ch..(src as usize + 1) * ch];
            for (i, v) in s.iter().enumerate() {
                row[i * 3 + j] = *v;
            }
        }
    }
    col
}
