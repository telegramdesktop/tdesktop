//! Live recognition: the logic of the web demo's worker (asr/tinyzipf/web/demo_template.html).
//!
//! One streaming pass (320 ms chunks) over all the audio gives the live text; the "open" region (after the last
//! frozen sentence) is re-decoded with full context at every short pause and every few seconds of new audio, with
//! left context before it. Speech / silence: Silero VAD. A sentence ends when the model's end-of-turn head says the
//! speaker has finished (read at 0.24 s of silence, from a second pass over the turn; a "no" again at 0.48 s), after 3 s of
//! silence anyway, or when the re-decode writes . ? ! and the next sentence has already started; `finish` freezes
//! everything. Language: one fixed, or with several allowed each re-decode runs in each of them and keeps the one the
//! model is surest of (the stream follows).

use alloc::format;
use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;

use crate::model::Token;

#[cfg(not(feature = "std"))]
use crate::compat::F64Ext;
use crate::text::{W, balance_quotes, cap, end_mark, is_end, norm, show_quotes, spans, trim_trail, units, words};
use crate::vad::{CHUNK, VadState};
use crate::{Event, Models, Options, Word, allowed};

pub(crate) const SR: i64 = 16000;
/// 40 ms frames: the unit of all cuts and of the VAD flags.
pub(crate) const FR: i64 = 640;
/// Left context of every re-decode.
const CTX: i64 = 4 * SR;
/// Frames of quiet that count as a pause (0.32 s).
const PAUSE: i64 = 8;
/// Re-decode at least this often (2.5 s) while there is new audio.
const REFRESH: i64 = 40000;
/// Re-decoded length that forces a cut at the longest pause.
pub(crate) const CAP: i64 = 20 * SR;
/// Silence after the audio of a full pass (the model writes the last word only once it has heard some).
pub(crate) const TAIL: usize = 12800;

pub struct Session<'m> {
    m: &'m Models,
    opt: Options,
    #[cfg(all(feature = "std", not(target_arch = "wasm32")))]
    t_start: std::time::Instant,
    langs: Vec<usize>, // places among the model's languages
    lang: usize,
    stream: crate::model::Stream<'m>,
    h0: i64,
    stoks: Vec<Token>,
    pending: Vec<f32>, // input not making a whole frame yet
    aud: Vec<f32>,     // kept audio, whole frames
    vf: Vec<bool>,     // their voiced flags
    last_end: f64,     // end of the last committed word (s)
    aud0: i64,         // sample of aud[0]
    pos: i64,          // samples received (whole frames)
    open0: i64,
    open: Vec<W>,
    full_at: i64,
    full_dirty: bool,
    quiet: i64,
    vbuf: Vec<f32>,
    vst: VadState,
    v_on: bool,
    spoke: bool,
    eou: bool,
    turn0: i64,
    gap: f64,
    ctx_len: i64,
    ended: bool,
    ntok: usize,
    dec_len: i64,
    last_shown: String,
    finished: bool,
    ev: Vec<Event>,
}

impl<'m> Session<'m> {
    /// `langs`: the codes of the languages to recognize in (empty: all of the model's). One fixes the language; with
    /// several the recognizer picks among them at every re-decode (that many full-context passes each time).
    pub fn new(m: &'m Models, langs: &[&str], opt: Options) -> Session<'m> {
        let langs = allowed(m, langs);
        let lang = m.asr.lang_index(&opt.start_lang).filter(|l| langs.contains(l)).unwrap_or(langs[0]);
        Session {
            m,
            #[cfg(all(feature = "std", not(target_arch = "wasm32")))]
            t_start: std::time::Instant::now(),
            opt,
            langs,
            lang,
            stream: m.asr.stream(lang),
            h0: 0,
            stoks: Vec::new(),
            pending: Vec::new(),
            aud: Vec::new(),
            vf: Vec::new(),
            last_end: 0.0,
            aud0: 0,
            pos: 0,
            open0: 0,
            open: Vec::new(),
            full_at: 0,
            full_dirty: false,
            quiet: 0,
            vbuf: Vec::new(),
            vst: VadState::new(),
            v_on: false,
            spoke: false,
            eou: false,
            turn0: 0,
            gap: 0.0,
            ctx_len: 0,
            ended: true,
            ntok: 0,
            dec_len: 0,
            last_shown: String::new(),
            finished: false,
            ev: Vec::new(),
        }
    }

    /// The current language's code (after `finish`: the one the session ended with).
    pub fn lang(&self) -> &str {
        self.m.code(self.lang)
    }

    /// Feed 16 kHz audio (any length); returns what changed.
    pub fn push(&mut self, x: &[f32]) -> Vec<Event> {
        let m = self.m;
        m.with_threads(|| self.push_here(x))
    }

    fn push_here(&mut self, x: &[f32]) -> Vec<Event> {
        if self.finished {
            return Vec::new();
        }
        self.pending.extend_from_slice(x);
        let n = self.pending.len() / FR as usize * FR as usize;
        let frames: Vec<f32> = self.pending.drain(..n).collect();
        for f in frames.chunks_exact(FR as usize) {
            self.frame(f);
        }
        core::mem::take(&mut self.ev)
    }

    /// End of audio: everything is frozen (the last `Commit`), then nothing more happens.
    pub fn finish(&mut self) -> Vec<Event> {
        let m = self.m;
        m.with_threads(|| self.finish_here())
    }

    fn finish_here(&mut self) -> Vec<Event> {
        if self.finished {
            return Vec::new();
        }
        if !self.pending.is_empty() {
            let rest = core::mem::take(&mut self.pending);
            self.frame(&rest);
        }
        self.finished = true;
        self.stream.finish();
        self.redecode();
        self.commit(true);
        self.stoks.clear();
        self.display();
        core::mem::take(&mut self.ev)
    }

    fn now(&self) -> Option<f64> {
        if let Some(c) = &self.opt.clock {
            return Some(c());
        }
        // std::time::Instant panics on wasm32-unknown-unknown: the browser passes its clock
        #[cfg(all(feature = "std", not(target_arch = "wasm32")))]
        return Some(self.t_start.elapsed().as_secs_f64());
        #[cfg(not(all(feature = "std", not(target_arch = "wasm32"))))]
        None
    }

    /// Words out with the given texts, timed by [`spans`] from `from` (s).
    fn out(&self, ws: &[W], texts: impl Iterator<Item = String>, from: f64) -> Vec<Word> {
        let f0 = self.aud0 / FR;
        // the given texts (capitals) on the words; Khmer words cut into timed syllables
        let mut ws: Vec<W> = ws.iter().zip(texts).map(|(w, text)| W { w: text, ..w.clone() }).collect();
        if self.m.asr.langs[self.lang].syllables {
            ws = units(ws);
        }
        let sp = spans(&ws, from, |f| usize::try_from(f - f0).ok().and_then(|i| self.vf.get(i).copied()));
        ws.into_iter().zip(sp).map(|(w, (start, end))| Word { text: show_quotes(w.w, &self.m.asr.langs[self.lang]), start, end, join: w.join }).collect()
    }

    fn stream_words(&self) -> Vec<W> {
        words(&self.m.asr.pieces, &self.stoks, self.h0 as f64 / SR as f64)
    }

    /// Frames [a, b) of the kept buffer, with their voiced flags. Clamped: at a sentence end the current frame's
    /// flag is not stored yet (it then counts as unvoiced).
    fn frames(&self, a: i64, b: i64) -> (&[f32], &[bool]) {
        let f0 = self.aud0 / FR;
        let (na, nv) = (self.aud.len() / FR as usize, self.vf.len());
        let (i, j) = ((a - f0) as usize, ((b - f0) as usize).min(na));
        (&self.aud[i * FR as usize..j * FR as usize], &self.vf[i.min(nv)..j.min(nv)])
    }

    /// Samples [a, b) from the kept buffer.
    fn audio(&self, a: i64, b: i64) -> &[f32] {
        let lo = (a - self.aud0).clamp(0, self.aud.len() as i64) as usize;
        let hi = (b - self.aud0).clamp(lo as i64, self.aud.len() as i64) as usize;
        &self.aud[lo..hi]
    }

    /// Keep only the audio a re-decode can still need (open region + left context).
    fn trim(&mut self) {
        let keep = (self.open0 - CTX).max(0);
        let k = ((keep - self.aud0) / FR).max(0) as usize;
        let k = k.min(self.aud.len() / FR as usize);
        self.aud.drain(..k * FR as usize);
        self.vf.drain(..k.min(self.vf.len()));
        self.aud0 += k as i64 * FR;
        // stream tokens before that too, up to a word start (the words after it come out the same)
        let first = |t: &Token| self.h0 + t.frame as i64 * FR >= keep;
        let word = |t: &Token| self.m.asr.pieces.get(t.id as usize).is_some_and(|p| p.starts_with('▁'));
        if let Some(j) = self.stoks.iter().position(first).and_then(|i| (1..=i).rev().find(|&j| word(&self.stoks[j]))) {
            self.stoks.drain(..j);
            self.ntok = self.ntok.saturating_sub(j);
        }
    }

    /// A stream in the current language over the audio from `from` on (language switch).
    fn new_stream(&mut self, from: i64) {
        self.stream = self.m.asr.stream(self.lang);
        self.h0 = from;
        self.stoks.clear();
        if self.pos > from {
            let x = self.audio(from, self.pos).to_vec();
            self.stoks = self.stream.accept(&x);
        }
    }

    /// Capital after . ? ! (the re-decode sees the frozen sentence as left context and often continues it in
    /// lowercase).
    fn cap_first(&self, ws: &[W]) -> Vec<Word> {
        self.out(ws, ws.iter().enumerate().map(|(i, w)| if i == 0 && self.ended { cap(&w.w) } else { w.w.clone() }), self.last_end)
    }

    /// Frozen words go out once (commit); the open region and the live tail on every change.
    fn display(&mut self) {
        // stream words after the last word the re-decode wrote (not after the audio it saw: if it wrote nothing for
        // the end, the stream's words stay)
        let o0 = self.open0 as f64 / SR as f64;
        let after = self.open.last().map_or(o0, |w| w.te + 0.04);
        let mut tw: Vec<W> = self.stream_words().into_iter().filter(|w| w.t >= after && w.t >= o0).collect();
        // the full pass times a word earlier than the stream: the stream's copy of the re-decode's last words can pass
        // the time filter and show twice. Stream words that repeat the last open words (within 0.8 s) go
        if let Some(lw) = self.open.last() {
            let last: Vec<String> = self.open[self.open.len().saturating_sub(3)..].iter().map(|w| norm(&w.w)).collect();
            let (mut k, mut j) = (0, 0);
            while k < tw.len() && tw[k].t < lw.te + 0.8 {
                let Some(i) = last[j.min(last.len())..].iter().position(|w| *w == norm(&tw[k].w)) else { break };
                j += i + 1;
                k += 1;
            }
            tw.drain(..k);
        }
        // the tail's first letter is capitalized as one string (the first word with a letter)
        let open = self.cap_first(&self.open);
        let from = open.last().map_or(self.last_end, |w| w.end as f64); // the tail goes on after the open words
        let mut tail: Vec<Word> = self.out(&tw, tw.iter().map(|w| w.w.clone()), from);
        if self.open.is_empty() && self.ended {
            if let Some(w) = tail.iter_mut().find(|w| w.text.chars().any(|c| c.is_alphanumeric())) {
                w.text = cap(&w.text);
            }
        }
        let mut key = String::new();
        for w in open.iter().chain([Word { text: "\0".into(), start: 0.0, end: 0.0, join: false }].iter()).chain(&tail) {
            key.push_str(&w.text);
            key.push(' ');
        }
        key.push_str(self.m.code(self.lang));
        if key != self.last_shown {
            self.last_shown = key;
            self.ev.push(Event::Partial { open, tail });
        }
    }

    fn redecode(&mut self) {
        // left context, by how the last sentence was cut: none after an end of utterance, 1 s after a dot inside a
        // re-decode, 4 s after a forced cut mid-sentence
        let a = self.aud0.max(self.open0 - self.ctx_len) / FR;
        let b = self.pos / FR; // frames (cuts and the buffer are frame aligned)
        if b * FR - self.open0 < SR / 5 {
            return;
        }
        let (x, map) = {
            let (fr, v) = self.frames(a, b);
            compress(fr, v, a)
        };
        self.dec_len = map.len() as i64 * FR;
        if map.is_empty() {
            return;
        }
        let t0 = self.now();
        let (tk, picked) = self.m.full(&x, &self.langs, self.opt.beam);
        if let Some(l) = picked.filter(|&l| l != self.lang) {
            // the model picked another language: the stream follows
            self.lang = l;
            self.new_stream(self.open0);
            self.ev.push(Event::Language(self.m.code(l).into()));
        }
        let secs = match (t0, self.now()) {
            (Some(a), Some(b)) => b - a,
            _ => x.len() as f64 / SR as f64 * self.opt.decode_rtf as f64,
        };
        self.gap = 2.0 * secs * SR as f64;
        // output frames are 40 ms = one input frame: back to the original timeline
        let tk: Vec<Token> = tk.iter().map(|t| Token { frame: map[(t.frame as usize).min(map.len() - 1)] as u32, ..*t }).collect();
        let all = words(&self.m.asr.pieces, &tk, 0.0);
        let prev = core::mem::take(&mut self.open);
        // the left context's own words are dropped
        let o0 = self.open0 as f64 / SR as f64 - 0.02;
        let mut open: Vec<W> = all.into_iter().filter(|w| w.t >= o0).collect();
        // a re-decode never takes words away: if it wrote fewer words than the last one, the earlier words before its
        // first word stay
        if open.len() < prev.len() {
            let t1 = open.first().map_or(f64::INFINITY, |w| w.t - 0.1);
            let mut keep: Vec<W> = prev.into_iter().filter(|w| w.te < t1).collect();
            keep.extend(open);
            open = keep;
        }
        self.open = open;
        self.full_at = b * FR;
        self.full_dirty = false;
        self.ntok = self.stoks.len();
        self.commit(false);
    }

    /// The first n open words are final; the open region restarts at sample `cut`, with `ctx` samples of left context.
    fn freeze(&mut self, n: usize, cut: i64, ctx: i64) {
        if n == 0 {
            return;
        }
        let mut ws = self.open[..n].to_vec();
        if self.ended {
            ws[0].w = cap(&ws[0].w);
        }
        let ws = balance_quotes(ws);
        let words = self.out(&ws, ws.iter().map(|w| w.w.clone()), self.last_end);
        if let Some(w) = words.last() {
            self.last_end = w.end as f64;
        }
        self.ev.push(Event::Commit(words));
        self.ended = is_end(&self.open[n - 1].w);
        let b = (cut as f64 / FR as f64).round() as i64 * FR;
        self.open.drain(..n);
        self.open0 = b;
        self.ctx_len = ctx;
        self.trim();
    }

    fn commit(&mut self, all: bool) {
        let ws = &self.open;
        if all {
            self.freeze(ws.len(), self.pos, 0);
            return;
        }
        let at = |j: usize| ((ws[j].te + ws[j + 1].t) / 2.0 * SR as f64).round() as i64;
        // a sentence end that the next sentence has already followed
        if let Some(j) = (0..ws.len().saturating_sub(1)).rev().find(|&j| is_end(&ws[j].w)) {
            let cut = at(j);
            self.freeze(j + 1, cut, SR);
            return;
        }
        // no dot: when the re-decoded audio passes CAP, cut at the longest pause between words
        if self.dec_len > CAP && ws.len() > 1 {
            if let Some(j) = longest_pause(ws) {
                let cut = at(j);
                self.freeze(j + 1, cut, CTX);
            }
        }
    }

    /// The sentence is over (end of utterance): freeze the open text, words the re-decode did not give taken from the
    /// stream; a dot is added if the model wrote none.
    fn end_sentence(&mut self) {
        let p0 = self.pos - self.quiet * FR; // where the pause began
        if self.full_dirty || self.full_at < self.pos - FR {
            self.redecode();
        }
        let o0 = self.open0 as f64 / SR as f64;
        let after = self.open.last().map_or(o0, |w| w.te + 0.04);
        let lim = p0 as f64 / SR as f64 + 0.5;
        let extra: Vec<W> = self.stream_words().into_iter().filter(|w| w.t >= after && w.t >= o0 && w.t < lim).collect();
        self.open.extend(extra);
        self.spoke = false;
        self.turn0 = self.pos;
        let Some(last) = self.open.last_mut() else { return };
        if !is_end(&last.w) {
            last.w = format!("{}{}", trim_trail(&last.w), end_mark(&self.m.asr.langs[self.lang]));
        }
        self.freeze(self.open.len(), self.pos - FR, 0);
    }

    /// One 40 ms frame (the last one of the audio may be shorter; it is zero padded).
    fn frame(&mut self, fr: &[f32]) {
        let mut f = vec![0.0; FR as usize];
        f[..fr.len()].copy_from_slice(fr);
        self.aud.extend_from_slice(&f);
        self.pos += FR;
        let new = self.stream.accept(fr);
        self.stoks.extend(new);
        // Silero, 32 ms chunks; speech from p > 0.5 until p < 0.35
        self.vbuf.extend_from_slice(&f);
        while self.vbuf.len() >= CHUNK {
            let p = self.m.vad.chunk(&mut self.vst, &self.vbuf[..CHUNK]);
            self.vbuf.drain(..CHUNK);
            self.v_on = p > 0.5 || (self.v_on && p > 0.35);
        }
        let voiced = self.v_on;
        // end of utterance: the end-of-turn head at 0.24 s of silence (it was trained on 0-0.5 s of silence after the
        // speech), from a second pass over the turn so far (<= 30 s); ended at P > 0.5, frozen at 0.48 s of silence so
        // the re-decode hears the word's end (speech before that cancels it); a "no" is asked again at 0.48 s; or
        // after 3 s of silence anyway (without the head: 1.6 s)
        if voiced {
            self.spoke = true;
            self.eou = false;
        } else if self.spoke {
            let q = self.quiet + 1;
            self.quiet = q; // end_sentence reads where the pause began
            let head = self.m.asr.has_head();
            if head && q == 6 {
                let from = self.aud0.max(self.turn0).max(self.pos - 30 * SR);
                if let Some(p) = self.m.eou(self.audio(from, self.pos), self.lang) {
                    self.ev.push(Event::Eou { at: self.pos as f32 / SR as f32, p });
                    if p > 0.5 {
                        self.eou = true;
                    }
                }
            } else if head && q == 12 {
                // a "not done" at 0.24 s is asked again on the longer silence (still inside the head's 0-0.5 s)
                let mut end = self.eou;
                if !end {
                    let from = self.aud0.max(self.turn0).max(self.pos - 30 * SR);
                    if let Some(p) = self.m.eou(self.audio(from, self.pos), self.lang) {
                        self.ev.push(Event::Eou { at: self.pos as f32 / SR as f32, p });
                        end = p > 0.5;
                    }
                }
                self.eou = false;
                if end {
                    self.end_sentence();
                }
            } else if q == if head { 75 } else { 40 } {
                self.end_sentence();
            }
            self.quiet = q - 1;
        }
        self.quiet = if voiced { 0 } else { self.quiet + 1 };
        self.vf.push(voiced);
        if voiced {
            self.full_dirty = true;
        }
        if self.stoks.len() > self.ntok {
            self.full_dirty = true; // the stream heard something even if the VAD did not say so
        }
        // at most ~1/3 of real time on re-decodes: the next one waits for 2x the audio the last one took to decode
        if self.full_dirty
            && (self.pos - self.full_at) as f64 >= self.gap
            && (self.quiet >= PAUSE || self.pos - self.full_at >= REFRESH)
        {
            self.redecode();
        }
        self.display();
    }
}

/// What a full pass sees of frames starting at frame `a`: silence before the first speech cut to 1 s, pauses over
/// 1.6 s to 0.8 + 0.8 s (Silero can mark speech ~0.5 s late; ~2 s of leading silence made the model write nothing for a
/// whole sentence), and 0.8 s of silence after. Returns the audio and the original frame of each kept frame.
pub(crate) fn compress(fr: &[f32], v: &[bool], a: i64) -> (Vec<f32>, Vec<i64>) {
    let n = fr.len() / FR as usize;
    let voiced = |i: usize| v.get(i).copied().unwrap_or(false);
    let first = (0..n).find(|&i| voiced(i));
    let mut keep = Vec::new();
    let mut i = 0;
    while i < n {
        if voiced(i) {
            keep.push(i);
            i += 1;
            continue;
        }
        let mut j = i;
        while j < n && !voiced(j) {
            j += 1;
        }
        let lead = first.is_none_or(|f| i < f);
        for k in i..j {
            let ok = if lead { j - k <= 25 } else { j - i <= 40 || k - i < 20 || j - k <= 20 };
            if ok || (j == n && k - i < 20) {
                keep.push(k);
            }
        }
        i = j;
    }
    let mut x = Vec::with_capacity(keep.len() * FR as usize + TAIL);
    for &k in &keep {
        x.extend_from_slice(&fr[k * FR as usize..(k + 1) * FR as usize]);
    }
    x.resize(x.len() + TAIL, 0.0);
    (x, keep.into_iter().map(|k| a + k as i64).collect())
}

/// Index j of the longest gap between word j and j + 1.
pub(crate) fn longest_pause(ws: &[W]) -> Option<usize> {
    let mut best = None;
    let mut g = 0.0;
    for j in 0..ws.len().saturating_sub(1) {
        let d = ws[j + 1].t - ws[j].te;
        if d > g {
            g = d;
            best = Some(j);
        }
    }
    best
}
