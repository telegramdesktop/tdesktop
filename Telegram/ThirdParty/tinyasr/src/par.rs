//! Parallel work. One thread unless the host allows more (`crate::Models::set_threads`) or hands over its own
//! parallel-for (`crate::Models::set_parallel_for`). The model's passes split their bigger steps into independent
//! tasks (`chunks_mut`, `map`); every output value is computed by the same code whichever thread runs it, so the
//! results do not depend on the number of threads.
//!
//! Which cores run the work is the host's choice: the built-in threads start on first use, from the thread that
//! asks, and inherit its CPU affinity (Linux, Android); a host parallel-for runs tasks wherever it likes.

#[cfg(feature = "std")]
pub use imp::*;

/// Work (multiply-adds, roughly) below which a step stays on one thread (~7 us on one core): handing out tasks to
/// spinning workers costs ~1-2 us, waking sleeping ones 5-20.
pub const MIN_WORK: usize = 1 << 19;

/// Rows per task for `rows` rows: `max` on one thread; with more, about 4 tasks per thread (whole multiples of 8 rows,
/// at least 16) so they balance. How rows are grouped never changes a result.
pub fn block_rows(rows: usize, max: usize) -> usize {
    let t = threads();
    if t < 2 {
        return max;
    }
    rows.div_ceil(4 * t).div_ceil(8).saturating_mul(8).clamp(16, max)
}

#[cfg(feature = "std")]
mod imp {
    use std::cell::RefCell;
    use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
    use std::sync::{Arc, Condvar, Mutex};

    /// Spins (~tens of us) before sleeping: the next parallel step usually comes sooner, and a sleeping thread takes
    /// 5-20 us to wake on a phone.
    const SPIN: u32 = 20_000;

    fn spin_until(mut done: impl FnMut() -> bool) -> bool {
        for _ in 0..SPIN {
            if done() {
                return true;
            }
            core::hint::spin_loop();
        }
        false
    }

    /// A host's parallel-for: run task(i) for every i in 0..n, possibly at the same time, and return when all have
    /// finished.
    pub type ParallelFor = Box<dyn Fn(usize, &(dyn Fn(usize) + Sync)) + Send + Sync>;

    /// How tasks run.
    pub enum Exec {
        Pool(Pool),
        Host(ParallelFor, usize),
    }

    impl Exec {
        fn threads(&self) -> usize {
            match self {
                Exec::Pool(p) => p.threads,
                Exec::Host(_, t) => *t,
            }
        }
    }

    thread_local! {
        /// The executor of the pass running on this thread (none: one thread; also none inside a task, so nested
        /// steps run where they are).
        static CUR: RefCell<Option<Arc<Exec>>> = const { RefCell::new(None) };
    }

    fn swap(e: Option<Arc<Exec>>) -> Option<Arc<Exec>> {
        CUR.with(|c| c.replace(e))
    }

    /// Puts the previous executor back, also when unwinding.
    struct Restore(Option<Arc<Exec>>);

    impl Drop for Restore {
        fn drop(&mut self) {
            swap(self.0.take());
        }
    }

    /// Runs `body` with `exec` as this thread's executor.
    pub fn enter<R>(exec: Option<&Arc<Exec>>, body: impl FnOnce() -> R) -> R {
        let _r = Restore(swap(exec.cloned()));
        body()
    }

    /// Threads the current step may use.
    pub fn threads() -> usize {
        CUR.with(|c| c.borrow().as_ref().map_or(1, |e| e.threads()))
    }

    /// f(i) for every i in 0..n, on the current executor's threads.
    pub fn run(n: usize, f: &(dyn Fn(usize) + Sync)) {
        let ex = if n > 1 { CUR.with(|c| c.borrow().clone()) } else { None };
        let Some(ex) = ex.filter(|e| e.threads() > 1) else {
            (0..n).for_each(f);
            return;
        };
        let _r = Restore(swap(None)); // tasks run single-threaded inside
        match &*ex {
            Exec::Pool(p) => p.run(n, f),
            Exec::Host(h, _) => h(n, f),
        }
    }

    /// f(i, chunk i) for the chunks of `out` (`len` values each, the last one shorter), in parallel when the current
    /// executor has threads and the step (`work` in all) is worth it.
    pub fn chunks_mut<T: Send>(out: &mut [T], len: usize, work: usize, f: impl Fn(usize, &mut [T]) + Sync) {
        if work < super::MIN_WORK || out.len() <= len || threads() < 2 {
            out.chunks_mut(len.max(1)).enumerate().for_each(|(i, c)| f(i, c));
            return;
        }
        // one lock per chunk, each taken once: disjoint &mut access, checked by the compiler
        let parts: Vec<Mutex<&mut [T]>> = out.chunks_mut(len.max(1)).map(Mutex::new).collect();
        run(parts.len(), &|i| f(i, &mut parts[i].lock().unwrap_or_else(|e| e.into_inner())));
    }

    /// [f(0), .., f(n - 1)], in parallel when the current executor has threads and the step (`work` in all) is
    /// worth it.
    pub fn map<T: Send>(n: usize, work: usize, f: impl Fn(usize) -> T + Sync) -> Vec<T> {
        if work < super::MIN_WORK || n < 2 || threads() < 2 {
            return (0..n).map(f).collect();
        }
        let slots: Vec<Mutex<Option<T>>> = (0..n).map(|_| Mutex::new(None)).collect();
        run(n, &|i| *slots[i].lock().unwrap_or_else(|e| e.into_inner()) = Some(f(i)));
        slots.into_iter().map(|s| s.into_inner().unwrap_or_else(|e| e.into_inner()).expect("every task ran")).collect()
    }

    // ------------------------------------------------------------------------------------------ the built-in pool

    /// `threads - 1` workers (the caller is the last thread), started on first use. Workers take task indices from a
    /// shared counter: a slower or busier core just takes fewer.
    pub struct Pool {
        threads: usize,
        sh: Arc<Shared>,
        workers: Mutex<Vec<std::thread::JoinHandle<()>>>,
        busy: Mutex<()>,
    }

    struct Shared {
        st: Mutex<State>,
        wake: Condvar,
        done: Condvar,
        // copies of `State::epoch` and `State::active` to spin on (the lock decides)
        epoch: AtomicU64,
        active: AtomicUsize,
    }

    #[derive(Default)]
    struct State {
        job: Option<Job>,
        epoch: u64,
        active: usize, // workers inside the current job
        quit: bool,
        panicked: bool,
    }

    /// The task of one `run`, lent to the workers: valid while `run` waits for them (see `Pool::run`).
    #[derive(Clone, Copy)]
    struct Job {
        f: *const (dyn Fn(usize) + Sync),
        next: *const std::sync::atomic::AtomicUsize,
        n: usize,
    }

    // SAFETY: the pointees are Sync (a Sync closure, an atomic) and outlive every use (`Pool::run`).
    #[allow(unsafe_code)]
    unsafe impl Send for Job {}

    impl Pool {
        pub fn new(threads: usize) -> Pool {
            Pool {
                threads: threads.max(1),
                sh: Arc::new(Shared {
                    st: Mutex::new(State::default()),
                    wake: Condvar::new(),
                    done: Condvar::new(),
                    epoch: AtomicU64::new(0),
                    active: AtomicUsize::new(0),
                }),
                workers: Mutex::new(Vec::new()),
                busy: Mutex::new(()),
            }
        }

        fn lock(&self) -> std::sync::MutexGuard<'_, State> {
            self.sh.st.lock().unwrap_or_else(|e| e.into_inner())
        }

        #[allow(unsafe_code)]
        fn run(&self, n: usize, f: &(dyn Fn(usize) + Sync)) {
            // one parallel step at a time (several sessions on one Models): the others run where they are
            let Ok(_busy) = self.busy.try_lock() else {
                (0..n).for_each(f);
                return;
            };
            self.start();
            let next = AtomicUsize::new(0);
            // SAFETY: only the lifetime is erased. Workers use `f` and `next` only between taking the job (under the
            // lock, `active += 1`) and reporting back (`active -= 1`); `Finish` (below; it also runs when unwinding)
            // withdraws the job and waits for `active == 0` before this frame - and `f`, `next` - can go.
            let f: &'static (dyn Fn(usize) + Sync) = unsafe { core::mem::transmute(f) };
            {
                let mut st = self.lock();
                st.job = Some(Job { f, next: &next, n });
                st.epoch += 1;
                st.panicked = false;
                self.sh.epoch.store(st.epoch, Ordering::Release);
            }
            self.sh.wake.notify_all();
            struct Finish<'a>(&'a Pool);
            impl Drop for Finish<'_> {
                fn drop(&mut self) {
                    let mut st = self.0.lock();
                    st.job = None;
                    if st.active > 0 {
                        drop(st);
                        spin_until(|| self.0.sh.active.load(Ordering::Acquire) == 0);
                        st = self.0.lock();
                    }
                    while st.active > 0 {
                        st = self.0.sh.done.wait(st).unwrap_or_else(|e| e.into_inner());
                    }
                }
            }
            let fin = Finish(self);
            loop {
                let i = next.fetch_add(1, Ordering::Relaxed);
                if i >= n {
                    break;
                }
                f(i);
            }
            drop(fin);
            if self.lock().panicked {
                panic!("tinyasr: a parallel task panicked");
            }
        }

        /// The workers, spawned from the calling thread (they inherit its CPU affinity).
        fn start(&self) {
            let mut w = self.workers.lock().unwrap_or_else(|e| e.into_inner());
            while w.len() + 1 < self.threads {
                let sh = self.sh.clone();
                match std::thread::Builder::new().name("tinyasr".into()).spawn(move || worker(&sh)) {
                    Ok(h) => w.push(h),
                    Err(_) => break, // fewer threads: the work still all gets done
                }
            }
        }
    }

    #[allow(unsafe_code)]
    fn worker(sh: &Shared) {
        let mut seen = 0;
        loop {
            spin_until(|| sh.epoch.load(Ordering::Acquire) != seen);
            let job = {
                let mut st = sh.st.lock().unwrap_or_else(|e| e.into_inner());
                loop {
                    if st.quit {
                        return;
                    }
                    if let Some(j) = st.job.filter(|_| st.epoch != seen) {
                        seen = st.epoch;
                        st.active += 1;
                        sh.active.store(st.active, Ordering::Release);
                        break j;
                    }
                    st = sh.wake.wait(st).unwrap_or_else(|e| e.into_inner());
                }
            };
            let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                // SAFETY: the job is valid until this worker reports back below (`Pool::run`).
                let (f, next) = unsafe { (&*job.f, &*job.next) };
                loop {
                    let i = next.fetch_add(1, Ordering::Relaxed);
                    if i >= job.n {
                        break;
                    }
                    f(i);
                }
            }));
            let mut st = sh.st.lock().unwrap_or_else(|e| e.into_inner());
            st.panicked |= r.is_err();
            st.active -= 1;
            sh.active.store(st.active, Ordering::Release);
            if st.active == 0 {
                sh.done.notify_all();
            }
        }
    }

    impl Drop for Pool {
        fn drop(&mut self) {
            self.lock().quit = true;
            self.sh.wake.notify_all();
            for h in self.workers.get_mut().unwrap_or_else(|e| e.into_inner()).drain(..) {
                let _ = h.join();
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        #[test]
        fn pool_runs_every_task_once() {
            let ex = Arc::new(Exec::Pool(Pool::new(3)));
            enter(Some(&ex), || {
                for n in [0usize, 1, 2, 7, 1000] {
                    let mut v = vec![0u32; n * 3];
                    chunks_mut(&mut v, 3, usize::MAX, |i, c| c.iter_mut().for_each(|x| *x += i as u32 + 1));
                    assert!(v.chunks(3).enumerate().all(|(i, c)| c.iter().all(|&x| x == i as u32 + 1)));
                    assert_eq!(map(n, usize::MAX, |i| i * i), (0..n).map(|i| i * i).collect::<Vec<_>>());
                }
                // nested steps run inside the task
                let r = map(4, usize::MAX, |i| map(3, usize::MAX, |j| i * 10 + j));
                assert_eq!(r[3], vec![30, 31, 32]);
            });
        }

        #[test]
        fn host_parallel_for() {
            let calls = Arc::new(std::sync::atomic::AtomicUsize::new(0));
            let c = calls.clone();
            let host: ParallelFor = Box::new(move |n, f| {
                c.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                std::thread::scope(|s| {
                    for i in 0..n {
                        s.spawn(move || f(i));
                    }
                });
            });
            let ex = Arc::new(Exec::Host(host, 4));
            let v = enter(Some(&ex), || map(5, usize::MAX, |i| i + 1));
            assert_eq!(v, vec![1, 2, 3, 4, 5]);
            assert_eq!(calls.load(std::sync::atomic::Ordering::Relaxed), 1);
        }

        #[test]
        fn task_panic_reaches_the_caller() {
            let ex = Arc::new(Exec::Pool(Pool::new(2)));
            let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| enter(Some(&ex), || map(64, usize::MAX, |i| if i == 63 { panic!("boom") } else { i }))));
            assert!(r.is_err());
            // and the pool still works
            assert_eq!(enter(Some(&ex), || map(3, usize::MAX, |i| i)), vec![0, 1, 2]);
        }
    }
}

/// Without std: one thread.
#[cfg(not(feature = "std"))]
mod imp {
    use alloc::vec::Vec;

    pub fn threads() -> usize {
        1
    }

    pub fn chunks_mut<T>(out: &mut [T], len: usize, _work: usize, f: impl Fn(usize, &mut [T])) {
        out.chunks_mut(len.max(1)).enumerate().for_each(|(i, c)| f(i, c));
    }

    pub fn map<T>(n: usize, _work: usize, f: impl Fn(usize) -> T) -> Vec<T> {
        (0..n).map(f).collect()
    }
}

#[cfg(not(feature = "std"))]
pub use imp::*;
