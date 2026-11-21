//! Words out of tokens, and the demo's text touches: sentence ends, capitals, paired quotes.

#[cfg(not(feature = "std"))]
use crate::compat::F64Ext;
use alloc::string::String;
use alloc::vec::Vec;

use crate::model::Token;

/// A word with the times (s) of its first and last token; `tw`: of its last token with a letter or digit (the model
/// often writes trailing punctuation only once it has heard the pause after it); `lp`: log-probability of the last
/// token; `ts`: each token's byte offset in `w` and time (for units inside a word, [`units`]); `join`: no space before
/// it (a unit inside a word).
#[derive(Clone, Debug, Default)]
pub struct W {
    pub w: String,
    pub t: f64,
    pub te: f64,
    pub tw: f64,
    pub lp: f32,
    pub ts: Vec<(usize, f64)>,
    pub join: bool,
}

/// The model's end-of-utterance token (v5.6): not text.
pub const EOU_TOKEN: u32 = 12;

/// Pieces -> words; "▁" starts a word, punctuation sticks to the word before. `t0`: time of frame 0 (s).
pub fn words(pieces: &[String], tk: &[Token], t0: f64) -> Vec<W> {
    let mut out: Vec<W> = Vec::new();
    for k in tk.iter().filter(|k| k.id != EOU_TOKEN) {
        let p = pieces.get(k.id as usize).map_or("", |s| s.as_str());
        let t = t0 + k.frame as f64 * 0.04;
        match out.last_mut() {
            Some(last) if !p.starts_with('▁') => {
                last.ts.push((last.w.len(), t));
                last.w.push_str(p);
                last.te = t;
                if p.chars().any(char::is_alphanumeric) {
                    last.tw = t;
                }
                last.lp = k.logp;
            }
            _ => out.push(W { w: p.replace('▁', ""), t, te: t, tw: t, lp: k.logp, ts: alloc::vec![(0, t)], join: false }),
        }
    }
    out.retain(|w| !w.w.is_empty());
    out
}

/// Khmer writes no spaces inside a phrase, so a "word" can be a whole phrase: split each word with Khmer letters into
/// syllables, each timed by the tokens it falls in. First clusters (a consonant or independent vowel with its coeng
/// subscripts and signs; ៗ ។ and closing punctuation stay with the cluster before; a Latin or digit run is one unit),
/// then, from the right, a bare consonant cluster (no vowel sign, no subscript) becomes the final consonant of the
/// syllable before it when that one has none yet: ម|ក|ដ|ល់ -> មក|ដល់, ម្ម|ក|រ -> ម្ម|ករ. Units after the first of a
/// word have `join` (no space before). Other words pass unchanged.
pub fn units(ws: Vec<W>) -> Vec<W> {
    let khmer = |c: char| ('\u{1780}'..='\u{17FF}').contains(&c);
    let latin = |c: char| c.is_ascii_alphanumeric() || c.is_alphabetic() && !khmer(c);
    let mut out = Vec::with_capacity(ws.len());
    for w in ws {
        if !w.w.chars().any(khmer) {
            out.push(w);
            continue;
        }
        let ch: Vec<(usize, char)> = w.w.char_indices().collect();
        // clusters: [start, end) char ranges
        let mut cl: Vec<(usize, usize)> = Vec::new();
        let mut a = 0;
        for i in 1..=ch.len() {
            let new = i == ch.len() || {
                let (c, p) = (ch[i].1, ch[i - 1].1);
                p != '\u{17D2}'
                    && match c {
                        '\u{1780}'..='\u{17B3}' => true,
                        c if latin(c) => !(latin(p) || matches!(p, '.' | ',' | ':' | '\'' | '-')),
                        '«' | '(' | '"' | '“' => true,
                        _ => false,
                    }
            };
            if new {
                cl.push((a, i));
                a = i;
            }
        }
        // a bare consonant: one consonant and only non-vowel signs (់ ៍ ...), no coeng, no vowel sign
        let bare = |(x, y): (usize, usize)| {
            ('\u{1780}'..='\u{17A2}').contains(&ch[x].1)
                && ch[x + 1..y].iter().all(|&(_, c)| matches!(c, '\u{17C9}'..='\u{17D1}' | '\u{17D3}' | '\u{17DD}'))
        };
        let has = |(x, y): (usize, usize), c: char| ch[x..y].iter().any(|&(_, d)| d == c);
        // a cluster with the bantoc ់ is itself a final: it takes none
        let mut fin: Vec<bool> = cl.iter().map(|&r| has(r, '\u{17CB}')).collect();
        let mut keep = vec![true; cl.len()];
        for i in (1..cl.len()).rev() {
            // before a subscript cluster, between two vowel-less consonants, a bare consonant starts its own syllable
            // (រោង|ច|ក្រ); after a written vowel it is the final (ពឹង|ផ្អែក); with the bantoc ់ it is always a final
            let before_sub = cl.get(i + 1).is_some_and(|&r| keep[i + 1] && has(r, '\u{17D2}'));
            let own = before_sub && bare(cl[i - 1]) && !has(cl[i], '\u{17CB}');
            if keep[i] && bare(cl[i]) && !own && !fin[i - 1] && khmer(ch[cl[i - 1].1 - 1].1) && !matches!(ch[cl[i - 1].1 - 1].1, '\u{17D4}'..='\u{17DC}') {
                cl[i - 1].1 = cl[i].1; // the final consonant of the syllable before
                fin[i - 1] = true;
                keep[i] = false;
            }
        }
        let time = |bt: usize| w.ts.iter().rev().find(|(o, _)| *o <= bt).map_or(w.t, |x| x.1);
        for (k, &(x, y)) in cl.iter().enumerate().filter(|(i, _)| keep[*i]) {
            let (b0, b1) = (ch[x].0, ch[y - 1].0);
            let end = if y < ch.len() { ch[y].0 } else { w.w.len() };
            let (t, te) = (time(b0), time(b1));
            out.push(W { w: w.w[b0..end].into(), t, te, tw: te, lp: w.lp, ts: alloc::vec![(0, t)], join: k > 0 || w.join });
        }
    }
    out
}

/// Word times (s) for the output, from emission times. A transducer writes a word as its sound ends, not as it begins
/// (its first emission is ~0.2–0.4 s after the word's start): so a word ends at its last emission and begins where the
/// previous word ended (`from` for the first), with the unvoiced frames at both ends cut off — a word after a pause
/// begins where the speech does. `voiced(frame)`: the VAD flag of a 40 ms frame, None when not known (kept).
pub fn spans(ws: &[W], mut from: f64, voiced: impl Fn(i64) -> Option<bool>) -> Vec<(f32, f32)> {
    let lag = lag();
    let mut out: Vec<(i64, i64)> = Vec::with_capacity(ws.len());
    for w in ws {
        // the emissions, moved back by the model's delay; never before the previous word
        let a0 = (((w.t - lag) / 0.04).round() as i64).max((from / 0.04).round() as i64);
        let mut b = ((w.tw.max(w.t) + 0.04 - lag) / 0.04).round() as i64;
        let mut a = a0;
        b = b.max(a + 1);
        #[cfg(feature = "std")]
        if std::env::var("TINYASR_NOTRIM").is_ok() {
            out.push((a, b));
            from = b as f64 * 0.04;
            continue;
        }
        while b - a > 1 && voiced(a) == Some(false) {
            a += 1;
        }
        while b - a > 1 && voiced(b - 1) == Some(false) {
            b -= 1;
        }
        out.push((a, b));
        from = b as f64 * 0.04;
    }
    // words a short gap apart touch (no flicker between them in a highlight)
    for i in 1..out.len() {
        if out[i].0 - out[i - 1].1 <= 3 {
            out[i - 1].1 = out[i].0;
        }
    }
    out.into_iter().map(|(a, b)| ((a as f64 * 0.04) as f32, (b as f64 * 0.04) as f32)).collect()
}

#[cfg(feature = "std")]
fn lag() -> f64 {
    std::env::var("TINYASR_LAG").ok().and_then(|v| v.parse().ok()).unwrap_or(LAG)
}
#[cfg(not(feature = "std"))]
fn lag() -> f64 {
    LAG
}

/// How late the transducer writes a word after its sound (s): measured on FLEURS against the speech's own starts and ends.
const LAG: f64 = 0.32;

/// Ends a sentence: . ? ! … (Khmer ។, Persian ؟) optionally followed by a closing quote.
pub fn is_end(w: &str) -> bool {
    let w = w.strip_suffix(['"', '»']).unwrap_or(w);
    w.ends_with(['.', '?', '!', '…', '។', '؟'])
}

/// The full stop a sentence gets when the model wrote none (the language's, from the model file).
pub fn end_mark(lang: &crate::model::LangInfo) -> char {
    lang.full_stop
}

/// The quote marks as the language writes them: the model writes « » (two shared tokens), shown as the model file
/// says for the language (Khmer: “ ”).
pub fn show_quotes(w: String, lang: &crate::model::LangInfo) -> String {
    match lang.quotes {
        Some((a, b)) if w.contains(['«', '»']) => w.replace('«', a.encode_utf8(&mut [0; 4])).replace('»', b.encode_utf8(&mut [0; 4])),
        _ => w,
    }
}

/// Without trailing , ; : — – - (Persian ، ؛)
pub fn trim_trail(w: &str) -> &str {
    w.trim_end_matches([',', ';', ':', '—', '–', '-', '،', '؛'])
}

/// The first letter in upper case (not after a digit: "20th").
pub fn cap(w: &str) -> String {
    match w.char_indices().find(|(_, c)| c.is_alphanumeric()) {
        Some((i, c)) if c.is_alphabetic() => {
            let mut s = String::from(&w[..i]);
            s.extend(c.to_uppercase());
            s.push_str(&w[i + c.len_utf8()..]);
            s
        }
        _ => w.into(),
    }
}

/// Lower case, letters and digits only (to compare words).
pub fn norm(w: &str) -> String {
    w.to_lowercase().chars().filter(|c| c.is_alphanumeric()).collect()
}

/// A frozen sentence keeps only quotes that pair up. The model (v5.6) puts quote marks in the right places but often picks
/// the wrong direction, so the direction comes from the position first: a mark in front of a word opens, one after it
/// closes; then unmatched marks are dropped.
pub fn balance_quotes(ws: Vec<W>) -> Vec<W> {
    let edge = |s: &str, front: bool| -> usize {
        // bytes of the leading (front) or trailing run of non-letters / non-digits
        if front {
            s.char_indices().find(|(_, c)| c.is_alphanumeric()).map_or(s.len(), |(i, _)| i)
        } else {
            s.len() - s.char_indices().rev().find(|(_, c)| c.is_alphanumeric()).map_or(0, |(i, c)| i + c.len_utf8())
        }
    };
    let mut out: Vec<W> = ws
        .into_iter()
        .map(|mut w| {
            let n = edge(&w.w, true);
            w.w = alloc::format!("{}{}", w.w[..n].replace('»', "«"), &w.w[n..]);
            let n = w.w.len() - edge(&w.w, false);
            w.w = alloc::format!("{}{}", &w.w[..n], w.w[n..].replace('«', "»"));
            w
        })
        .collect();
    let mut open = Vec::new();
    for (i, w) in out.iter_mut().enumerate() {
        let mut t = String::new();
        for c in w.w.chars() {
            match c {
                '«' => {
                    open.push(i);
                    t.push(c);
                }
                '»' => {
                    if open.pop().is_some() {
                        t.push(c);
                    }
                }
                c => t.push(c),
            }
        }
        w.w = t;
    }
    for i in open {
        if let Some(k) = out[i].w.rfind('«') {
            out[i].w.replace_range(k..k + '«'.len_utf8(), "");
        }
    }
    out.retain(|w| !w.w.is_empty());
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bq(s: &str) -> String {
        let ws = s.split(' ').map(|w| W { w: w.into(), ..W::default() }).collect();
        balance_quotes(ws).iter().map(|w| w.w.as_str()).collect::<Vec<_>>().join(" ")
    }

    #[test]
    fn quotes() {
        assert_eq!(bq("Проект «Кто сверху"), "Проект Кто сверху");
        assert_eq!(bq("Фильм Одиссея» собрал"), "Фильм Одиссея собрал");
        assert_eq!(bq("фильм »Одиссея« собрал"), "фильм «Одиссея» собрал");
        assert_eq!(bq("«Да», сказал он"), "«Да», сказал он");
        assert_eq!(bq("сказал « да"), "сказал да");
    }

    #[test]
    fn caps() {
        assert_eq!(cap("20th"), "20th");
        assert_eq!(cap("«привет»"), "«Привет»");
        assert!(is_end("конец.»") && is_end("да?") && !is_end("так,"));
    }
}
