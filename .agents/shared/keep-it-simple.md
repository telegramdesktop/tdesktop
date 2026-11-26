# Keep It Simple

**DON'T OVERENGINEER. KEEP IT SIMPLE.** This is the owner's rule for every
requirement, plan, replan, review and fix in this workflow, in every area of
the client and its tooling.

The best solution is the simplest one that fully does what was asked. It works
well and fast on the normal path, the thing a user of a working app does nearly
every time. It never reaches a never-happen outcome. Everything a design adds
beyond that has to earn its place or come out: code, layers, options,
generality, guarantees, checks, tests, steps. When two designs both work,
choose the simpler one, even if it is a little less optimal in rare cases.

## What overengineering looks like

- **Scope.** Doing more than asked: extra features, options or settings, side
  cleanups, support for callers or inputs nobody has.
- **Structure.** Abstraction before a second user: an interface with one
  implementation, a manager, registry or framework around one call, a state
  machine over a linear flow, a new layer where an existing function fits.
- **Generality.** Solving a general problem when the specific one was asked.
  Templates, plugin points, configuration and extension hooks "for later".
- **Defensiveness.** Checks for states the code cannot reach, fallbacks for
  fallbacks, re-validating trusted internal data at every layer.
- **Guarantees.** Promising more in rare cases than anyone needs (see "Rare
  cases heal themselves").
- **Reinvention.** A new mechanism where the client already has one that fits,
  or a stricter copy of an existing one.
- **Process.** Tests, fixtures, evidence, tooling, phases, documents or tasks
  beyond what proves the result.

## Never-happen outcomes

These are the only reasons to go beyond the simple design:

- a crash, undefined behavior, memory corruption, a deadlock or a frozen UI;
- a logout, or loss of the user's session or authorization;
- irreversible loss or corruption of user data: messages, drafts, settings,
  saved files, keys, money;
- a security or privacy failure, such as data reaching the wrong account, chat
  or peer, or trusting unvalidated outside input;
- wrong data presented as right, such as a message shown in the wrong chat, a
  cached item that does not match what it claims to be, or an amount in the
  wrong currency;
- a state that never recovers by itself: stuck until the user intervenes, or
  failing on every launch;
- a slower or worse normal path.

## Rare cases heal themselves

Rare cases include:
- an interrupted operation (a crash, a cancel, a logout, a closed window);
- concurrent or reentrant requests;
- a network or server failure;
- unusual input;
- a timing no user produces on purpose.

A rare case only has to end up correct again by the next use, the next retry,
the next restart or the user repeating the action. It does not need the best
possible outcome. Prefer these shapes:

- **Fail and start over** rather than recover or resume. An upload whose
  connection dropped starts again. Work interrupted by a logout is dropped,
  and the next use begins from scratch.
- **Throw away what is unfinished** rather than journal it. Whatever was not
  finalized before a crash is ignored or deleted at startup.
- **Reset rather than preserve** a rare intermediate state. A resize in the
  middle of an animation jumps to the end state. A list reloaded during a
  scroll restores a reasonable position, not an exact one.
- **Join or serialize** concurrent requests rather than coordinate them. Two
  callers wanting the same thing share one request. A shared resource has one
  owner at a time. An occasional duplicate request or an ignored late callback
  is fine.
- **Retry once or not at all** rather than build a retry state machine. The
  next action tries again.
- **Keep state on the main thread.** Only blocking or CPU-heavy work goes to
  the background and returns a result. A counter or a weak pointer drops late
  results.
- **Persist only what the normal path needs** after a restart. Timers, guards,
  counters and intermediate progress stay in memory, unless losing them reaches
  a never-happen outcome.
- **Reject unusual input cleanly** rather than interpret every variant of it,
  unless the request asks for it.

## Match the shipped precedent

The closest shipped feature of this client sets the level of structure and
guarantees. When it handles something simply, so does the new code. Going past
it needs a never-happen reason, stated in the plan. A change several times the
size of its analogue is overengineered until shown otherwise.

## Simple, not sloppy

Simplicity never excuses:
- a broken or slow normal path;
- a never-happen outcome;
- duplicating code that a shared helper already covers;
- ignoring repository conventions;
- a hack the next change has to work around.

The goal is the smallest correct change in the codebase's existing shape. A
shortcut that makes code harder to read or change is not simple.

## By role

- **Writing a task** (inbox planning, follow-up routing, consolidation,
  split):
  - Ask for the behavior: the normal path and the never-happen outcomes it can
    reach. Leave the mechanism to the plan.
  - For rare cases, ask for nothing beyond "recovers by the next use or
    restart".
  - From a guide or another client's code, take the behavior, not the
    machinery.
  - Add no criteria the request does not need.
- **Planning:**
  - Write the smallest design that does the task. Name its shipped analogue.
  - For every new file, class, option, abstraction or guarantee, name what
    requires it.
  - State the guarantee level: which never-happen outcomes the change can
    reach and how it prevents each, and how its rare cases heal.
- **Assessment:**
  - Delete what the task does not need.
  - Reject the approach (`Approach: rejected`) when its machinery is several
    times what the result needs, or when it promises more than its analogue
    without a never-happen reason.
- **Review:**
  - Blocking stays as the review rules define it. Under this rule it means a
    broken or slower normal path, a reachable never-happen outcome or a
    failing requested scenario.
  - A rare-case finding that reaches no never-happen outcome and heals by
    itself is not blocking, even when the task's code introduced it. List it
    under `## Dropped` with the way it heals.
  - Needless machinery the task added is a valid finding, and its fix is
    deletion.
- **Fix and replan:**
  - Repair with the smallest change, and remove a mechanism rather than add
    one.
  - When one mechanism keeps producing rare-case defects, replan it into a
    simpler shape and rule the self-healing cases non-blocking. Never add
    another layer to cover them.
