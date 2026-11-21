//! C ABI (include/tinyasr.h). Results are JSON text owned by the object they came from, valid until the next call
//! on it.
#![allow(unsafe_code)]

use alloc::boxed::Box;
use alloc::format;
use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::{c_char, CStr};

use crate::{Event, Files, Models, Options, Session, Transcription, Word};

pub struct CSession {
    m: &'static Models,
    s: Session<'static>,
    out: Vec<u8>,
}

pub struct CFile {
    m: &'static Models,
    f: Transcription<'static>,
    out: Vec<u8>,
}

/// # Safety: p is null or points to n bytes.
unsafe fn bytes<'a>(p: *const u8, n: usize) -> Option<&'a [u8]> {
    if p.is_null() { None } else { Some(unsafe { core::slice::from_raw_parts(p, n) }) }
}

/// n samples at p; null (or n = 0): none.
/// # Safety: p is null or points to n floats.
unsafe fn samples<'a>(p: *const f32, n: usize) -> &'a [f32] {
    if p.is_null() || n == 0 { &[] } else { unsafe { core::slice::from_raw_parts(p, n) } }
}

/// A string (null or not UTF-8: empty).
/// # Safety: p is null or a NUL-terminated string.
unsafe fn text<'a>(p: *const c_char) -> &'a str {
    if p.is_null() {
        return "";
    }
    unsafe { CStr::from_ptr(p) }.to_str().unwrap_or("")
}

/// A comma-separated list of language codes ("en,ru"); null or empty: all.
/// # Safety: p is null or a NUL-terminated string.
unsafe fn langs<'a>(p: *const c_char) -> Vec<&'a str> {
    unsafe { text(p) }.split(',').map(str::trim).filter(|c| !c.is_empty()).collect()
}

/// The model's NUL-terminated copy of a language code (lives as long as the models).
fn lang_str(m: &Models, code: &str) -> *const c_char {
    m.asr.langs.iter().find(|l| l.code == code).map_or(core::ptr::null(), |l| l.code0.as_ptr() as *const c_char)
}

fn put(out: &mut Vec<u8>, s: &str) -> *const c_char {
    out.clear();
    out.extend_from_slice(s.as_bytes());
    out.push(0);
    out.as_ptr() as *const c_char
}

fn esc(s: &str, o: &mut String) {
    o.push('"');
    for c in s.chars() {
        match c {
            '"' => o.push_str("\\\""),
            '\\' => o.push_str("\\\\"),
            '\n' => o.push_str("\\n"),
            c if (c as u32) < 0x20 => o.push_str(&format!("\\u{:04x}", c as u32)),
            c => o.push(c),
        }
    }
    o.push('"');
}

fn words(ws: &[Word], o: &mut String) {
    o.push('[');
    for (i, w) in ws.iter().enumerate() {
        if i > 0 {
            o.push(',');
        }
        o.push_str("{\"text\":");
        esc(&w.text, o);
        o.push_str(&format!(",\"start\":{:.2},\"end\":{:.2}", w.start, w.end));
        o.push_str(if w.join { ",\"join\":true}" } else { "}" });
    }
    o.push(']');
}

/// Events as a JSON array: {"type": "commit", "words": [...]}, {"type": "partial", "open": [...], "tail": [...]},
/// {"type": "language", "lang": "en"}, {"type": "eou", "at": s, "p": P}; a word is {"text", "start", "end"} plus "join": true when it is written without a
/// space before it (Khmer syllables).
pub fn events_json(ev: &[Event]) -> String {
    let mut o = String::from("[");
    for (i, e) in ev.iter().enumerate() {
        if i > 0 {
            o.push(',');
        }
        match e {
            Event::Commit(w) => {
                o.push_str("{\"type\":\"commit\",\"words\":");
                words(w, &mut o);
                o.push('}');
            }
            Event::Partial { open, tail } => {
                o.push_str("{\"type\":\"partial\",\"open\":");
                words(open, &mut o);
                o.push_str(",\"tail\":");
                words(tail, &mut o);
                o.push('}');
            }
            Event::Language(l) => {
                o.push_str("{\"type\":\"language\",\"lang\":");
                esc(l, &mut o);
                o.push('}');
            }
            Event::Eou { at, p } => o.push_str(&format!("{{\"type\":\"eou\",\"at\":{at:.2},\"p\":{p:.3}}}")),
        }
    }
    o.push(']');
    o
}

/// Version of the library (its API and the model files it reads).
#[unsafe(no_mangle)]
pub extern "C" fn tinyasr_version() -> u32 {
    crate::version()
}

/// Load the models from memory (copied; the caller may free them after). On failure
/// returns null and writes the reason (NUL-terminated, truncated) to `err` if not null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_load(
    asr: *const u8,
    asr_len: usize,
    vad: *const u8,
    vad_len: usize,
    err: *mut c_char,
    err_len: usize,
) -> *mut Models {
    let r = (|| {
        // SAFETY: the caller passes valid (pointer, length) pairs.
        let f = unsafe {
            Files {
                asr: bytes(asr, asr_len).ok_or("no recognizer model")?,
                vad: bytes(vad, vad_len).ok_or("no VAD model")?,
            }
        };
        Models::load(f)
    })();
    match r {
        Ok(m) => Box::into_raw(Box::new(m)),
        Err(e) => {
            if !err.is_null() && err_len > 0 {
                let n = e.len().min(err_len - 1);
                // SAFETY: err has err_len bytes.
                unsafe {
                    core::ptr::copy_nonoverlapping(e.as_ptr(), err as *mut u8, n);
                    *err.add(n) = 0;
                }
            }
            core::ptr::null_mut()
        }
    }
}

/// Free models (after every session / file using them).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_free(m: *mut Models) {
    if !m.is_null() {
        // SAFETY: m came from tinyasr_load.
        drop(unsafe { Box::from_raw(m) });
    }
}

/// Run the bigger steps on up to `threads` threads (0, 1: only the calling one). See `Models::set_threads`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_set_threads(m: *mut Models, threads: u32) {
    // SAFETY: m is null or came from tinyasr_load, and no session or file uses it at the moment.
    #[cfg(feature = "std")]
    if let Some(m) = unsafe { m.as_mut() } {
        m.set_threads(threads as usize);
    }
    #[cfg(not(feature = "std"))]
    let _ = (m, threads);
}

/// A task of a parallel step: the host's parallel-for calls it with `ctx` and each index.
pub type Task = unsafe extern "C" fn(ctx: *mut core::ffi::c_void, i: usize);
/// The host's parallel-for: call `task(ctx, i)` for every i in 0..n (any threads, any order, possibly at the same time),
/// return when all have finished.
pub type ParallelFor = unsafe extern "C" fn(user: *mut core::ffi::c_void, n: usize, task: Task, ctx: *mut core::ffi::c_void);

/// The host's `user` pointer, handed back to its parallel-for from whichever thread runs a step.
#[cfg(feature = "std")]
struct User(*mut core::ffi::c_void);
// SAFETY: the host promises its parallel-for may be called from any thread (tinyasr.h).
#[cfg(feature = "std")]
unsafe impl Send for User {}
#[cfg(feature = "std")]
unsafe impl Sync for User {}

#[cfg(feature = "std")]
impl User {
    /// (a method: closures capture the whole wrapper, not its field)
    fn get(&self) -> *mut core::ffi::c_void {
        self.0
    }
}

/// Run the bigger steps through the host's parallel-for (`threads`: how many it runs at once); NULL: one thread.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_set_parallel_for(m: *mut Models, f: Option<ParallelFor>, user: *mut core::ffi::c_void, threads: u32) {
    // SAFETY: m is null or came from tinyasr_load, and no session or file uses it at the moment.
    #[cfg(feature = "std")]
    if let Some(m) = unsafe { m.as_mut() } {
        let Some(f) = f else {
            m.set_threads(1);
            return;
        };
        let user = User(user);
        m.set_parallel_for(threads as usize, move |n, task: &(dyn Fn(usize) + Sync)| {
            unsafe extern "C" fn tramp(ctx: *mut core::ffi::c_void, i: usize) {
                // SAFETY: ctx is the `&task` below, alive until the host's parallel-for returns.
                let task = unsafe { &*(ctx as *const &(dyn Fn(usize) + Sync)) };
                task(i);
            }
            let ctx = &task as *const &(dyn Fn(usize) + Sync) as *mut core::ffi::c_void;
            // SAFETY: the host's function, with its pointer; it returns only when every task has run.
            unsafe { f(user.get(), n, tramp, ctx) };
        });
    }
    #[cfg(not(feature = "std"))]
    let _ = (m, f, user, threads);
}

/// Streaming chunk of the recognizer (ms).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_chunk_ms(m: *const Models) -> u32 {
    // SAFETY: m is null or came from tinyasr_load.
    unsafe { m.as_ref() }.map_or(0, |m| m.chunk_ms())
}

/// How many languages the model has.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_lang_count(m: *const Models) -> u32 {
    // SAFETY: m is null or came from tinyasr_load.
    unsafe { m.as_ref() }.map_or(0, |m| m.asr.langs.len() as u32)
}

/// The code of the model's i-th language ("en", "uz_cyrillic", ...; valid while the models are loaded), or null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_lang(m: *const Models, i: u32) -> *const c_char {
    // SAFETY: m is null or came from tinyasr_load.
    unsafe { m.as_ref() }.and_then(|m| m.asr.langs.get(i as usize)).map_or(core::ptr::null(), |l| l.code0.as_ptr() as *const c_char)
}

/// A live session. `langs`: the languages to recognize in, comma-separated ("en,ru"; null or "": all). One fixes the
/// language; with several the recognizer picks among them, starting from `start_lang` (may be null). `clock`: a monotonic clock in seconds, or null (re-decode time is then estimated).
/// `beam`: beam width of the full-context passes (0: the default, 4; 1: greedy).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_session_new(
    m: *const Models,
    lang_list: *const c_char,
    start_lang: *const c_char,
    clock: Option<extern "C" fn() -> f64>,
    beam: u32,
) -> *mut CSession {
    // SAFETY: m is null or came from tinyasr_load and outlives the session (the caller's contract).
    let Some(m): Option<&'static Models> = (unsafe { m.as_ref() }) else {
        return core::ptr::null_mut();
    };
    let mut opt = Options::default();
    if beam > 0 {
        opt.beam = beam as usize;
    }
    // SAFETY: null or NUL-terminated strings.
    opt.start_lang = unsafe { text(start_lang) }.into();
    if let Some(c) = clock {
        opt.clock = Some(Box::new(move || c()));
    }
    let s = Session::new(m, &unsafe { langs(lang_list) }, opt);
    Box::into_raw(Box::new(CSession { m, s, out: Vec::new() }))
}

/// Feed n samples (16 kHz); returns the events (JSON array).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_session_push(s: *mut CSession, pcm: *const f32, n: usize) -> *const c_char {
    // SAFETY: s is null or came from tinyasr_session_new; pcm is null or points to n floats.
    let Some(s) = (unsafe { s.as_mut() }) else {
        return core::ptr::null();
    };
    let e = events_json(&s.s.push(unsafe { samples(pcm, n) }));
    put(&mut s.out, &e)
}

/// End of audio: the last events (everything final).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_session_finish(s: *mut CSession) -> *const c_char {
    // SAFETY: s is null or came from tinyasr_session_new.
    let Some(s) = (unsafe { s.as_mut() }) else {
        return core::ptr::null();
    };
    let e = events_json(&s.s.finish());
    put(&mut s.out, &e)
}

/// The session's language code (valid while the models are loaded).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_session_lang(s: *const CSession) -> *const c_char {
    // SAFETY: s is null or came from tinyasr_session_new.
    unsafe { s.as_ref() }.map_or(core::ptr::null(), |s| lang_str(s.m, s.s.lang()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_session_free(s: *mut CSession) {
    if !s.is_null() {
        // SAFETY: s came from tinyasr_session_new.
        drop(unsafe { Box::from_raw(s) });
    }
}

/// A recorded file (n samples, 16 kHz, copied): VAD runs here. `langs` and `beam` as for sessions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_file_new(m: *const Models, pcm: *const f32, n: usize, lang_list: *const c_char, beam: u32) -> *mut CFile {
    // SAFETY: m is null or came from tinyasr_load and outlives the file; pcm is null or points to n floats; lang is
    // null or a C string.
    let Some(m): Option<&'static Models> = (unsafe { m.as_ref() }) else {
        return core::ptr::null_mut();
    };
    let x = unsafe { samples(pcm, n) };
    let beam = if beam > 0 { beam as usize } else { crate::DEFAULT_BEAM };
    let f = Transcription::new(m, x, &unsafe { langs(lang_list) }, beam);
    Box::into_raw(Box::new(CFile { m, f, out: Vec::new() }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_file_lang(f: *const CFile) -> *const c_char {
    // SAFETY: f is null or came from tinyasr_file_new.
    unsafe { f.as_ref() }.map_or(core::ptr::null(), |f| lang_str(f.m, f.f.lang()))
}

/// The next piece's final words (JSON array of {"text", "start", "end"}), or null when the file is done.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_file_next(f: *mut CFile) -> *const c_char {
    // SAFETY: f is null or came from tinyasr_file_new.
    let Some(f) = (unsafe { f.as_mut() }) else {
        return core::ptr::null();
    };
    match f.f.next() {
        Some(ws) => {
            let mut o = String::new();
            words(&ws, &mut o);
            put(&mut f.out, &o)
        }
        None => core::ptr::null(),
    }
}

/// How far the file is recognized (0..1); -1 for null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_file_progress(f: *const CFile) -> f32 {
    // SAFETY: f is null or came from tinyasr_file_new.
    let Some(f) = (unsafe { f.as_ref() }) else {
        return -1.0;
    };
    let f = &f.f;
    if f.duration() > 0.0 { (f.position() / f.duration()).min(1.0) } else { 1.0 }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tinyasr_file_free(f: *mut CFile) {
    if !f.is_null() {
        // SAFETY: f came from tinyasr_file_new.
        drop(unsafe { Box::from_raw(f) });
    }
}
