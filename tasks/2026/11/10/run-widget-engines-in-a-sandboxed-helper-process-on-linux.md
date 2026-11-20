# The Linux widget helper's system call allowlist

On Linux a helper process, the Qt-free `TelegramWidgetHelper` executable
started as `<path> 3` with the channel on descriptor 3, serves each
widget's engine and, in builds with the private Qt API, its validator and
renderer too. It lies in `TelegramHelpers/` next to the client's binary, or
in the package's libexec directory (its `TelegramDesktop/` folder) in a
package-managed build, and the lockdown is compiled only into it. Before it
serves its first widget, while it still has one thread,
`Platform::WidgetHelper::LockDownHelper()` in
`platform/linux/widget_lockdown_linux.cpp` loads the time zone, empties its
root where the system allows it, drops its capabilities, sets
no-new-privileges, applies a Landlock ruleset that allows nothing where the
kernel has Landlock, and last installs the seccomp filter that
`BuildFilter()` builds for x86_64 and aarch64. The filter kills the process
for a call of any other architecture and, on x86_64, for an x32 call, and it
answers every call it does not name with `ENOSYS`.

The allowlist follows, by kind, in the order `BuildFilter()` checks it.
"Seen" means that strace recorded the call after the filter was installed:
while the Qt-free helper served the framework's attack corpus and
conformance set on glibc 2.39, 2.28 and 2.41, and again while the helper
that renders, started by a Debug Telegram without an account through a
strace wrapper at its package-managed install's helper path, rendered the
attack corpus, the conformance automatic set and a widget reading its time
zone on the same three versions and, on 2.39, also widget-sandbox's widget
corpus in its day and night partitions. The versions are named where only
some of them make it; the other calls are fallbacks or paths no current
widget takes. Measured again on the Qt-free helper, the list lost
`clock_getres`, `time`, `nanosleep`, `clock_nanosleep`, `sched_yield`,
`rt_sigreturn`, `prctl` with `PR_GET_NAME`, the futex operations
`FUTEX_REQUEUE`, `FUTEX_CMP_REQUEUE`, `FUTEX_WAKE_OP` and
`FUTEX_WAKE_BITSET`, and the `madvise` advice `MADV_NORMAL`, `MADV_RANDOM`,
`MADV_SEQUENTIAL`, `MADV_WILLNEED` and `MADV_FREE`, because nothing recorded
them and no code linked into the helper makes them after the lockdown. The
helper that renders added nothing to the list: on glibc 2.39, 2.28 and 2.41
it made no call and no argument value that the filter did not already
allow, and the only call refused was `clone3`, on 2.39 and 2.41.

- The channel (`kChannelCalls`, then the rules for `recvfrom` and `sendto`):
  reading and writing the channel and stderr.
  - `recvfrom` only with the flags 0 or `MSG_DONTWAIT | MSG_PEEK` and
    `sendto` only with `MSG_NOSIGNAL`, both only with a null address (both
    words of the address zero; of the flags only the low word, which is what
    the kernel reads); any other flags, `MSG_OOB` among them, or any address
    get `ENOSYS`: `PosixStream::read()` and `PosixStream::write()` in
    `platform/posix/widget_channel_posix.cpp`, through `recv()` with flags 0
    and `send()` with `MSG_NOSIGNAL`, which glibc makes as these calls with a
    null address, seen; the Debug probe's descriptor listing peeks with
    `MSG_DONTWAIT | MSG_PEEK`. The kernel's out-of-band and addressed socket
    paths stay out of the helper's reach.
  - `shutdown`: `PosixStream::shutdown()`, `SHUT_WR` then `SHUT_RD`, when the
    engine end stops. Seen.
  - `write`: stderr is unbuffered, so each `fprintf(stderr, ...)` is a
    `write`: `Exit()` in `_other/widget_helper.cpp` prints a reason
    `serve()` returned, and an assertion or libstdc++'s terminate handler
    prints its message before `abort()`.
  - `writev`: glibc's fatal error reports (`__libc_message()`: malloc
    corruption, stack-protector and fortify failures) are one `writev` to
    stderr.
  - `read`: a plain read of a descriptor the helper holds. The channel is read
    with `recv()`, so only the Debug probe's status read uses it.
  - `close`: the Debug probe closes its status descriptor after the lockdown,
    before it lists its descriptors; `PosixStream`'s destructor would too, but
    the helper ends with `std::_Exit()` before it runs.
- Memory management (`kMemoryCalls`, then the rules for `mmap`, `mprotect`
  and `madvise`):
  - `brk`: malloc's heap. `mallopt(M_ARENA_MAX, 1)` in the helper's Linux
    `Adopt()` in `_other/widget_helper_linux.cpp` leaves the helper one
    arena, which grows and trims with `brk`. Seen.
  - `munmap`: QuickJS's arena slabs (`arena_slab_destroy()` in `quickjs.c`),
    malloc's large blocks and glibc's thread stacks. Seen.
  - `mremap`: glibc's `realloc()` of a block that malloc mapped on its own.
  - `mmap` and `mprotect`, with `EPERM` when the low word of `prot` has
    `PROT_EXEC`: glibc's `pthread_create()` maps a thread stack `PROT_NONE`
    and makes all of it but the guard page readable and writable with
    `mprotect`; QuickJS's 64 KiB arena slabs (`arena_slab_take()`) and
    malloc's large blocks are mapped readable and writable. Seen. The
    executable's stack is not executable, so after the start nothing honest
    maps executable memory.
  - `madvise` with `MADV_DONTNEED`, `MADV_GUARD_INSTALL` or
    `MADV_GUARD_REMOVE`, any other advice `ENOSYS`: `MADV_DONTNEED` releases
    the free pages of QuickJS's arena slabs (`arena_slab_release_pages()`)
    and, in glibc, a finished thread's stack, seen; the two guard values are
    the guard pages glibc's `pthread_create()` puts on thread stacks from
    2.42 on, not seen.
- Futexes (the rule for `futex`): with `FUTEX_PRIVATE_FLAG` and
  `FUTEX_CLOCK_REALTIME` masked off, the operation must be `FUTEX_WAIT`,
  `FUTEX_WAKE` or `FUTEX_WAIT_BITSET`; the priority-inheritance operations
  and anything else get `ENOSYS`.
  - Private `WAIT` and `WAKE`: glibc's mutexes behind `std::mutex`, and the
    wake of a `std::condition_variable`. Seen.
  - Private `WAIT_BITSET` with the realtime clock flag: `pthread_cond_wait()`
    behind `std::condition_variable::wait()`. Seen.
  - Shared `WAIT` and `WAKE`: libstdc++'s `std::promise` and `std::future`
    (`__atomic_futex_unsigned`), the handshake with which `JSThread` waits for
    its thread to start, and on 2.28 the wait of `pthread_join()`. Seen.
  - Shared `WAIT_BITSET` with the realtime clock flag: `pthread_join()` on
    2.39 and 2.41, waiting for the kernel to clear a finished thread's id.
    Seen.
- Clocks (`kClockCalls`):
  - `clock_gettime`: `CLOCK_THREAD_CPUTIME_ID` always reaches the kernel, and
    the engine's CPU budget reads it (`QuickJSContext::threadCpuTime()`, also
    from QuickJS's interrupt handler). Seen. The other clocks (`std::chrono`,
    QuickJS's `js__hrtime_ns()`) are answered by the vDSO and need the call
    only where it cannot answer.
  - `gettimeofday`: QuickJS's `js__gettimeofday_us()` behind `Date.now()`,
    `new Date()` and the seed of `Math.random()`, answered by the vDSO. Not
    seen.
- Threads (`kThreadCalls`, then the rules for `prctl`, `clone` and `clone3`):
  starting, naming and ending the helper's `JSThread`s (`widget.sandbox` for
  each runtime, `widget.probe`, `widget.reaper` and, where the helper
  renders, each widget's `widget.validator` and its served host's
  `twidget-host`), and their signal masks.
  - `clone`, only when the low word of `flags` is exactly glibc's thread set
    `CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
    CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID |
    CLONE_CHILD_CLEARTID`, and any other flags kill the process: glibc's
    `pthread_create()`, which `JSThread` calls with an 8 MiB stack, after
    `clone3` got `ENOSYS` where glibc tries that first. Seen.
  - `clone3`, answered `ENOSYS`: glibc 2.34 and later try it first, and its
    flags sit behind a pointer the filter cannot read. glibc 2.39 and 2.41
    remember the answer and use `clone` for every later thread. Seen once per
    helper on 2.39 and 2.41.
  - `set_robust_list` and, where the kernel headers define it, `rseq`: glibc's
    start of a new thread registers its robust-mutex list and its
    restartable-sequence area. Seen, `rseq` on 2.39 and 2.41.
  - `rt_sigprocmask`: `pthread_create()` blocks every signal around the
    `clone` and the new thread restores its creator's mask; a finishing
    thread blocks every signal but `SIGSETXID`; newer glibc's first
    `pthread_create()` unblocks `SIGCANCEL` and `SIGSETXID`; `raise()` and
    `abort()` use it too. Seen on 2.39 and 2.41.
  - `prctl` with `PR_SET_NAME` only, any other option `ENOSYS`:
    `JSThread::entry()` names its thread with
    `pthread_setname_np(pthread_self(), ...)`, which glibc makes as
    `prctl(PR_SET_NAME)` for the calling thread. Seen.
  - `exit`: the end of a thread, in glibc's `start_thread()` after the thread
    function returns. Seen.
- Signals and the process's end (`kSignalCalls`, then the rule for
  `tgkill`):
  - `rt_sigaction`: newer glibc's first `pthread_create()` installs glibc's
    `SIGSETXID` handler, seen once per helper on 2.39 and 2.41; `abort()`
    resets `SIGABRT`.
  - `restart_syscall`: the kernel resumes a timed futex wait that a stop and
    continue interrupted, as a debugger or a stopped helper causes. Not
    seen.
  - `getpid`, `gettid`, and `tgkill` only when its `tgid` is the helper's own
    pid as read when the filter was built (1 inside the PID namespace), else
    `ENOSYS`: glibc's `raise()`, and so `abort()`, sends its signal with
    `tgkill(getpid(), gettid(), ...)`, and `pthread_kill()` signals a thread
    of the helper. Not seen.
  - `exit_group`: the end of the process, `std::_Exit()` in `Exit()` in
    `_other/widget_helper.cpp`. Seen once per helper.
- Random (`kRandomCalls`):
  - `getrandom`: the engine's `random` host request (`randomBytes()` in
    `sandbox_engine.cpp`) behind `crypto.getRandomValues()` and
    `crypto.randomUUID()`. Seen.
- Killed (`kKilledCalls`; the process dies with `SIGSYS`): `execve` and
  `execveat` (another program), `socket` and `connect` (a new or a connected
  endpoint), `ptrace`, `process_vm_readv` and `process_vm_writev` (another
  process), and on x86_64 `fork` and `vfork`. aarch64 has neither, and its
  `fork()` is a `clone` without the thread set, which the `clone` rule kills.
  No honest engine makes them.
- Everything else gets `ENOSYS`: opening or examining files (`openat`, the
  `stat` family), `ioctl`, `kill`, `socketpair`, `sendmsg`, `recvmsg`,
  `poll`, `mount`, `unshare`, `setns`, `bpf`, `userfaultfd`, `io_uring_*`,
  `perf_event_open`, `keyctl`, `memfd_create`, and `seccomp` and Landlock
  themselves. glibc and libstdc++ read `ENOSYS` as a missing kernel feature
  and take their fallbacks instead of crashing; in the strace passes only
  `clone3` got it, apart from the Debug probe's own opens.

A later addition goes inside one of these kinds and only for a call the
helper is measured to make there; a call that opens, creates or connects
anything is never added. The first rule whose call number matches decides,
and each kind's plain table comes before that kind's argument rules and every
table before the killed calls: a call number that has an argument rule or is
killed (`recvfrom`, `sendto`, `mmap`, `mprotect`, `madvise`, `futex`, `prctl`,
`tgkill`, `clone`, `clone3`, or a killed call) must never be added to a plain
table, or the rule it already has silently becomes an unconditional allow.
