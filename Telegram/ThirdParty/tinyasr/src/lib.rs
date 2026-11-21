//! Whole on-device speech recognition with no dependencies: microphone audio in, words with times out.
//!
//! Two ways to recognize:
//!
//! - [`Session`]: live audio. A streaming pass (Zipformer2 transducer) gives text as you speak; the open
//!   sentence is re-decoded with full context at pauses; Silero VAD tells speech from silence, and the model's
//!   end-of-turn head whether the speaker has finished, which ends the sentence and makes its words final. Language:
//!   one of the languages the host allows (see [`Session::new`]).
//! - [`Transcription`]: a finished recording. Only the full-context pass, over sentence-sized pieces cut at pauses;
//!   words come out piece by piece as they are decoded (an iterator).
//!
//! Every model is a file the caller hands over as bytes ([`Files`]): the recognizer (.tzf, int4, with its end-of-turn
//! head; uncompressed - the host unzips a .tzf.gz) and Silero VAD straight from its ONNX file. Languages are codes the
//! model file names (`"en"`, `"uz_cyrillic"`, ...; [`Models::langs`]), with their writing rules: the library knows none
//! of its own. Which ones to recognize in is the host's choice (it may run a language ID such as tinylid first). The host calls in from whatever thread it
//! likes; the work runs on that thread, or on as many as the host allows ([`Models::set_threads`], or its own
//! parallel-for: [`Models::set_parallel_for`]), with the same results.
//!
//! ```no_run
//! # let read = |p: &str| std::fs::read(p).unwrap();
//! let (asr, vad) = (read("model.tzf"), read("silero_vad.onnx"));
//! let models = tinyasr::Models::load(tinyasr::Files { asr: &asr, vad: &vad }).unwrap();
//! let mut s = tinyasr::Session::new(&models, &["en"], Default::default());
//! let mic: Vec<f32> = vec![0.0; 16000];
//! for e in s.push(&mic).into_iter().chain(s.finish()) {
//!     if let tinyasr::Event::Commit(words) = e { println!("{words:?}") }
//! }
//! ```

#![cfg_attr(all(not(feature = "std"), not(test)), no_std)]
#![deny(unsafe_code)]

#[macro_use] // vec! in every module, also without std
extern crate alloc;

mod compat;
mod fbank;
mod kernels;
mod model;
#[cfg(feature = "c-api")]
mod ffi;
mod file;
// a static library without std carries its allocator (the host's malloc) and panic handler
#[cfg(all(not(feature = "std"), not(target_arch = "wasm32"), not(test)))]
mod no_std_runtime;
mod onnx;
mod par;
mod session;
mod text;
mod vad;

use alloc::boxed::Box;
use alloc::string::String;
use alloc::vec::Vec;

pub use file::Transcription;
pub use session::Session;
pub use vad::{Vad, VadState};

/// Sample rate of all audio in and out (mono f32 in [-1, 1]).
pub const SAMPLE_RATE: u32 = 16_000;

/// Beam width of the full-context pass unless the host sets one ([`Options::beam`], [`Transcription::new`]); 1 is greedy.
pub const DEFAULT_BEAM: usize = 4;

/// Version of the library (its API and the model files it reads: TZF3, which names its languages, and the older
/// TZF1/TZF2); C: `tinyasr_version()`.
pub fn version() -> u32 {
    3
}

/// The languages a recognition may use, as the host allows them (codes; places among the model's): one fixes it;
/// with several the recognizer picks (every full-context pass runs in each of them and keeps the transcript it is most
/// sure of: that many times the cost of those passes). Only the model's own count; none of them (or empty): all of
/// the model's.
fn allowed(m: &Models, langs: &[&str]) -> Vec<usize> {
    let l: Vec<usize> = (0..m.asr.langs.len()).filter(|&k| langs.contains(&m.asr.langs[k].code.as_str())).collect();
    if l.is_empty() { (0..m.asr.langs.len()).collect() } else { l }
}

/// A recognized word: its text (with punctuation) and times (s from the start of the audio). `join`: written without a
/// space before it (Khmer: a word is cut into syllables, each with its own times).
#[derive(Clone, Debug, PartialEq)]
pub struct Word {
    pub text: String,
    pub start: f32,
    pub end: f32,
    pub join: bool,
}

/// What recognition reports, in order.
#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    /// Final words, after all earlier ones; they never change.
    Commit(Vec<Word>),
    /// The words not final yet, replacing the previous `Partial`: `open` from the full-context re-decode, `tail` from
    /// the streaming pass after it (newest, least sure).
    Partial { open: Vec<Word>, tail: Vec<Word> },
    /// With several languages: the recognizer switched to this one (its code; words from now on are in it).
    Language(String),
    /// Diagnostics: the end-of-turn head at a pause (time s, P(the speaker has finished)).
    Eou { at: f32, p: f32 },
}

/// The model files, as bytes.
pub struct Files<'a> {
    /// The recognizer (.tzf, uncompressed). If it has no end-of-turn head (`eou_head.*`, v5.6 on), a sentence ends
    /// only after 1.6 s of silence.
    pub asr: &'a [u8],
    /// silero_vad.onnx (v5).
    pub vad: &'a [u8],
}

/// The loaded models; read-only, so one instance can serve several sessions.
pub struct Models {
    asr: model::Model,
    vad: Vad,
    #[cfg(feature = "std")]
    exec: Option<alloc::sync::Arc<par::Exec>>,
}

impl Models {
    pub fn load(f: Files) -> Result<Models, &'static str> {
        Ok(Models {
            asr: crate::model::Model::from_bytes(f.asr)?,
            vad: Vad::from_onnx(f.vad)?,
            #[cfg(feature = "std")]
            exec: None,
        })
    }

    /// Run the bigger steps on up to `threads` threads (1, the default: only the calling one). The extra threads
    /// start on first use, from the thread that calls in, and inherit its CPU affinity (Linux, Android): pin that
    /// thread to the cores you want them on. Results are the same for any number of threads.
    #[cfg(feature = "std")]
    pub fn set_threads(&mut self, threads: usize) {
        self.exec = (threads > 1).then(|| alloc::sync::Arc::new(par::Exec::Pool(par::Pool::new(threads))));
    }

    /// Run the bigger steps through the host's parallel-for instead: `f(n, task)` must call `task(i)` for every i in
    /// 0..n, possibly at the same time, and return when all have finished. `threads`: how many it runs at once (steps
    /// too small to be worth splitting stay on one thread).
    #[cfg(feature = "std")]
    pub fn set_parallel_for(&mut self, threads: usize, f: impl Fn(usize, &(dyn Fn(usize) + Sync)) + Send + Sync + 'static) {
        self.exec = (threads > 1).then(|| alloc::sync::Arc::new(par::Exec::Host(alloc::boxed::Box::new(f), threads)));
    }

    /// Runs `body` with this instance's threads.
    fn with_threads<R>(&self, body: impl FnOnce() -> R) -> R {
        #[cfg(feature = "std")]
        return par::enter(self.exec.as_ref(), body);
        #[cfg(not(feature = "std"))]
        body()
    }

    /// The languages of the recognizer: the codes its file names, in its order.
    pub fn langs(&self) -> Vec<&str> {
        self.asr.langs.iter().map(|l| l.code.as_str()).collect()
    }

    /// The code of the model's `k`-th language.
    fn code(&self, k: usize) -> &str {
        &self.asr.langs[k].code
    }

    /// Streaming chunk of the recognizer (ms).
    pub fn chunk_ms(&self) -> u32 {
        (self.asr.chunk * 20) as u32
    }

    /// The full-context pass in the one of `langs` the model is most sure of (mean token log-probability): (tokens,
    /// that language; None if it wrote nothing).
    fn full(&self, x: &[f32], langs: &[usize], beam: usize) -> (Vec<crate::model::Token>, Option<usize>) {
        if let [l] = langs {
            return (self.asr.full(x, *l, beam), Some(*l));
        }
        let mut best: Option<(f32, Vec<crate::model::Token>, usize)> = None;
        for &l in langs {
            let t = self.asr.full(x, l, beam);
            let sc = if t.is_empty() { -1e9 } else { t.iter().map(|k| k.logp).sum::<f32>() / t.len() as f32 };
            if best.as_ref().is_none_or(|b| sc > b.0) {
                best = Some((sc, t, l));
            }
        }
        let (_, t, l) = best.unwrap();
        let l = if t.is_empty() { None } else { Some(l) };
        (t, l)
    }

    /// P(the speaker has finished) at the end of `turn` (the audio since the last sentence end, with the pause); None
    /// without the head or for a turn too short to decode.
    fn eou(&self, turn: &[f32], lang: usize) -> Option<f32> {
        self.asr.full_head(turn, lang).1
    }
}

/// Session settings.
pub struct Options {
    /// With several languages: the one to assume until the first pick (its code, e.g. the last session's, or the
    /// UI's); empty or not allowed: the first allowed.
    pub start_lang: String,
    /// A monotonic clock (s). Re-decodes are spaced by how long they took, to keep them under ~1/3 of real time on a
    /// slow device; without a clock their time is estimated as `decode_rtf` x the audio.
    pub clock: Option<Box<dyn Fn() -> f64>>,
    pub decode_rtf: f32,
    /// Beam width of the full-context pass (re-decodes of the open sentence); 1: greedy (fastest). Streaming text is
    /// always greedy.
    pub beam: usize,
}

impl Default for Options {
    fn default() -> Options {
        Options { start_lang: String::new(), clock: None, decode_rtf: 0.1, beam: DEFAULT_BEAM }
    }
}
