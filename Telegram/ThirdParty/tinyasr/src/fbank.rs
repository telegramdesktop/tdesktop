//! Kaldi log-mel filterbank as sherpa-onnx computes it for Zipformer models (kaldi-native-fbank defaults):
//! 25 ms povey window every 10 ms, DC offset removed, pre-emphasis 0.97, 512-point power spectrum, 80 mel bins
//! between 20 Hz and 7600 Hz, no dither, `snip_edges = false` (frame i is centred on sample 160 i + 80, samples
//! before the start are mirrored). Streaming: frames come out as soon as their window is complete.

use alloc::vec;
use alloc::vec::Vec;

#[cfg(not(feature = "std"))]
use crate::compat::{F32Ext, F64Ext};

pub const BINS: usize = 80;
const LEN: usize = 400;
const SHIFT: usize = 160;
const FFT: usize = 512;

struct Mel {
    first: usize,
    w: Vec<f32>,
}

pub struct Fbank {
    window: Vec<f32>,
    mel: Vec<Mel>,
    tw: Vec<(f32, f32)>,
}

fn mel_scale(f: f64) -> f64 {
    1127.0 * (1.0 + f / 700.0).ln()
}

impl Fbank {
    pub fn new() -> Fbank {
        let window = (0..LEN)
            .map(|i| (0.5 - 0.5 * (2.0 * core::f64::consts::PI * i as f64 / (LEN - 1) as f64).cos()).powf(0.85) as f32)
            .collect();
        let (lo, hi) = (mel_scale(20.0), mel_scale(8000.0 - 400.0));
        let delta = (hi - lo) / (BINS + 1) as f64;
        let mel = (0..BINS)
            .map(|b| {
                let (l, c, r) = (lo + b as f64 * delta, lo + (b + 1) as f64 * delta, lo + (b + 2) as f64 * delta);
                let mut first = usize::MAX;
                let mut w = Vec::new();
                for i in 0..FFT / 2 {
                    let m = mel_scale(i as f64 * 16000.0 / FFT as f64);
                    if m > l && m < r {
                        if first == usize::MAX {
                            first = i;
                        }
                        w.push(if m <= c { (m - l) / (c - l) } else { (r - m) / (r - c) } as f32);
                    }
                }
                Mel { first, w }
            })
            .collect();
        let tw = (0..FFT / 2)
            .map(|k| {
                let a = -2.0 * core::f64::consts::PI * k as f64 / FFT as f64;
                (a.cos() as f32, a.sin() as f32)
            })
            .collect();
        Fbank { window, mel, tw }
    }

    /// Number of frames for `n` samples once the input is complete.
    pub fn num_frames(n: usize) -> usize {
        (n + SHIFT / 2) / SHIFT
    }

    /// Frames that only need samples already received (streaming: the last few wait for more audio).
    pub fn num_ready(n: usize) -> usize {
        // frame i covers [160 i - 120, 160 i + 280)
        if n < 280 { 0 } else { (n - 280) / SHIFT + 1 }
    }

    /// Frame `i` of the audio whose samples from `off` on are `x` (n = off + x.len() samples so far; mirrored past
    /// either end as Kaldi does). Reads samples from 160 i - 120 on (frame 0: from 0).
    pub fn frame(&self, x: &[f32], off: usize, i: usize, out: &mut [f32]) {
        let n = (off + x.len()) as isize;
        let start = (i * SHIFT) as isize + (SHIFT / 2) as isize - (LEN / 2) as isize;
        let mut w = [0f32; LEN];
        for (j, v) in w.iter_mut().enumerate() {
            let mut s = start + j as isize;
            while s < 0 || s >= n {
                s = if s < 0 { -s - 1 } else { 2 * n - 1 - s };
            }
            *v = x[s as usize - off];
        }
        let mean = w.iter().sum::<f32>() / LEN as f32;
        w.iter_mut().for_each(|v| *v -= mean);
        for j in (1..LEN).rev() {
            w[j] -= 0.97 * w[j - 1];
        }
        w[0] -= 0.97 * w[0];
        let mut re = [0f32; FFT];
        let mut im = [0f32; FFT];
        for j in 0..LEN {
            re[j] = w[j] * self.window[j];
        }
        self.fft(&mut re, &mut im);
        let mut pow = [0f32; FFT / 2];
        for k in 0..FFT / 2 {
            pow[k] = re[k] * re[k] + im[k] * im[k];
        }
        for (o, m) in out.iter_mut().zip(&self.mel) {
            let e: f32 = m.w.iter().zip(&pow[m.first..]).map(|(a, b)| a * b).sum();
            *o = e.max(f32::EPSILON).ln();
        }
    }

    /// Frames lo..hi of `x` (BINS each).
    pub fn frames(&self, x: &[f32], lo: usize, hi: usize) -> Vec<f32> {
        self.frames_at(x, 0, lo, hi)
    }

    /// Frames lo..hi of the audio whose samples from `off` on are `x` (see `frame`).
    pub fn frames_at(&self, x: &[f32], off: usize, lo: usize, hi: usize) -> Vec<f32> {
        let mut out = vec![0.0; (hi - lo) * BINS];
        // frames are independent: blocks of 64, one task each (~10k operations a frame)
        crate::par::chunks_mut(&mut out, 64 * BINS, (hi - lo) * 10_000, |b, o| {
            for (j, f) in o.chunks_exact_mut(BINS).enumerate() {
                self.frame(x, off, lo + b * 64 + j, f);
            }
        });
        out
    }

    /// In-place radix-2 FFT of length 512.
    fn fft(&self, re: &mut [f32; FFT], im: &mut [f32; FFT]) {
        let mut j = 0;
        for i in 1..FFT {
            let mut bit = FFT >> 1;
            while j & bit != 0 {
                j ^= bit;
                bit >>= 1;
            }
            j |= bit;
            if i < j {
                re.swap(i, j);
                im.swap(i, j);
            }
        }
        let mut len = 2;
        while len <= FFT {
            let step = FFT / len;
            for s in (0..FFT).step_by(len) {
                for k in 0..len / 2 {
                    let (c, d) = self.tw[k * step];
                    let (a, b) = (s + k, s + k + len / 2);
                    let (xr, xi) = (re[b] * c - im[b] * d, re[b] * d + im[b] * c);
                    re[b] = re[a] - xr;
                    im[b] = im[a] - xi;
                    re[a] += xr;
                    im[a] += xi;
                }
            }
            len <<= 1;
        }
    }
}
