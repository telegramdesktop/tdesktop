# Computer Use testing adapter

Read this during `$implement` TEST when selecting or using the UI driver. This is a Codex-only
adapter over `.agents/shared/test-loop.md`; the shared task-derived scenarios, oracles, overlay,
`-testagent` launch, portable account, watchdog, crash handling, and artifact verdicts remain
authoritative.

## Contents

- [Driver policy](#driver-policy)
- [Capability gate](#capability-gate)
- [Exact-app gate](#exact-app-gate)
- [Hybrid handshake](#hybrid-handshake)
- [Safety envelope](#safety-envelope)
- [Unavailable result mapping](#unavailable-result-mapping)
- [Durable evidence](#durable-evidence)

## Driver policy

Resolve one policy from the user's request and pass it to the task-runner:

- `auto` (default) — let the test-author choose hybrid driving only where real pointer, keyboard,
  focus, scrolling, dragging, menus, windowing, or native UI materially improves coverage.
- `overlay-only` — never use Computer Use.
- `required` — use hybrid driving for the named flow; if it cannot run safely, return
  `BLOCKED(test)` with the exact missing interaction rather than weakening the oracle.

For each check, select `Driver: overlay` or `Driver: hybrid`. Keep overlay-only for internal state,
data, exact text, and geometry that the in-app harness can exercise deterministically. Select hybrid
when the user-input path itself matters. Do not approve a runnable code task from a Computer Use
narrative or an uninstrumented click-through. Even a hybrid test retains the overlay for fixture
setup, semantic assertions or geometry capture, watchdog, terminal markers, and tight screenshots.

Have the test-author add these fields to every hybrid check in `test.md` before the run:

```
- Driver: hybrid
- UI action: <one ordered, bounded action or gesture>
- Ready marker: CU_READY: <scenario-id>:<step-id>
- Target: <stable AX role/name/value or exact visual target>
- Fallback: <equivalent overlay action, or test-blocked>
- Safety envelope: local-read-only | disposable-local-test-data
```

The test-author designs this contract but never operates Computer Use. The stateful task-runner is
the sole desktop owner and performs every Computer Use action itself, even in NESTED mode. Never let
two agents or two driver runtime sessions control the test app concurrently.

## Capability gate

Treat Computer Use as available for a run only when all of these are true:

1. The parent passed the active Computer Use `SKILL.md` path from its surfaced Skills catalog.
2. That skill supports the current desktop host and its prescribed runtime tool is callable here.
3. Required OS permissions and target-app approval are already satisfied without a fresh prompt.
4. The running test build can be distinguished unambiguously from every real Telegram instance.

Read the passed Computer Use skill completely before the first action and follow its current
bootstrap, API, state-refresh, screenshot, and confirmation rules. Do not hardcode a plugin cache
version, wrapper path, node-repl API, or macOS behavior into this workflow. Skill instructions do
not arrive through `fork_turns: "none"`; the explicit path is the handoff.

Do not infer capability from an installed directory or config entry. An installed plugin may be
disabled by policy, lack its runtime tool, support a different platform, or still need app/OS
approval. In `auto`, fall back to the prewritten overlay action. In `required`, or when no equivalent
overlay can exercise the physical interaction, return `BLOCKED(test)` with the canonical mapping below.
Missing capability or permission is never an implementation bug.

Computer Use runs in the foreground on Windows. Use it only on an unlocked, reserved active desktop
or isolated VM. Treat local pointer or keyboard interference, a focus steal, or a window switch as a
contaminated run and restart within `MAX_TEST_RUNS`. Treat WSL/Linux/headless as unavailable unless
the current host explicitly exposes a supported desktop adapter for the exact test app.

## Exact-app gate

Keep process and account ownership in the ordinary runner:

1. Perform portable-account SETUP and launch the resolved `EXE` with `-testagent` through the
   existing shell adapter.
2. Verify exactly one process PID maps to the resolved full `EXE`; record its start time too. On macOS,
   `COMPUTER_USE_APP_TARGET` is the `.app` bundle containing that executable; on Windows it is the
   full EXE path. Record PID, start time, EXE, app target, and test marker in the run's `driver.md`.
3. Call Computer Use only while that exact PID/start-time/resolved-EXE tuple is alive. Revalidate the
   same tuple immediately after every state or action call; reject a disappearance or replacement.
4. Pass `COMPUTER_USE_APP_TARGET` to the active API. If it cannot accept that full target, treat the
   driver as unavailable unless the API exposes evidence that another identifier resolves to the
   recorded process. Never target the ambiguous display name `Telegram` or a user's release client.
5. If exact identity cannot be proven, perform no state/action call and mark Computer Use unavailable.
   In `auto`, run the prewritten overlay fallback; in `required`, return `BLOCKED(test)`. Never risk
   the user's real account.

A state call can implicitly relaunch an app between checks. Before the first action, require its fresh
state to contain the predeclared AX/visual target for the current `CU_READY` step, then revalidate the
process tuple. On any mismatch, perform no further action, discard that state/screenshot as evidence,
and use the `auto` fallback or `required` blocker mapping.

Computer Use must not operate terminals, ChatGPT, Git, build tools, process cleanup, portable-data
folders, or overlay patch/reset mechanics. Those stay with their existing shell and repository
protocols.

## Hybrid handshake

For every hybrid step:

1. Have the overlay create deterministic local/injected/mock state, install its observers and
   watchdog, then flush `CU_READY: <scenario-id>:<step-id>` and wait on a condition.
2. Have the runner observe that marker, recheck the process tuple, fetch fresh app state, recheck the
   tuple, and require the predeclared target before acting.
3. Prefer a semantic accessibility target. With APIs that expose ephemeral element indices, derive
   the index from the latest state and never reuse it after an action. Use a coordinate only when no
   accessibility action exists, based on the latest screenshot, and record the window size and point.
4. Perform only the predeclared action, immediately recheck the tuple, then fetch fresh state and
   recheck again before deciding the next action.
5. Let the overlay observe the resulting application state, log actual values and PASS/FAIL markers,
   and capture the tight target or geometry. Persist supplemental Computer Use AX and screen evidence.
6. Finish through the ordinary `TEST_COMPLETE`, process/crash, watchdog, cleanup, patch-save, and
   source-restore path.

Use Computer Use for exploration only to discover a reproducible flow. Before assigning an
implementation verdict, encode the discovery as a planned overlay/hybrid check and rerun it from a
fresh test account. An exploratory impression cannot approve or fail the implementation.

## Safety envelope

Use only the prepared throwaway account and prefer `inject` or `mock-api` fixtures. Unattended
Computer Use must not:

- log in, enter credentials or secrets, approve permissions, or change account/privacy/network/OS
  settings;
- send, edit, forward, react to, or delete messages or other server-visible content;
- upload files, open external links, join/leave chats, log out, terminate sessions, delete accounts,
  make payments, or perform any other confirmation-gated action;
- approve a system dialog, CAPTCHA, security warning, or newly requested app permission.

If a planned action reaches a confirmation boundary, redesign it with local injection/mock APIs. If
that cannot preserve the behavior under test, return `BLOCKED(test)` with a precise manual follow-up;
do not pause an unattended pipeline waiting for approval and do not treat the invocation as blanket
permission.

Screenshots and AX text can contain everything visible in Telegram. Keep unrelated sensitive apps
closed, capture only the test window/target, and never expose a real account merely to obtain context.

## Unavailable result mapping

Whenever a planned hybrid check is unavailable before a run, write
`<TASK_DIR>/computer-use-capability.md` with the policy, host, active skill path or `none`, runtime
tool status, OS/app-approval status, exact-app identity result, fallback decision, and reason.

- `auto` with an equivalent fallback continues overlay-only. Record `UI-Driver: overlay` and cite the
  capability report plus overlay evidence; capability failure is not a blocker.
- `required`, or `auto` without an equivalent fallback, records `STATUS: BLOCKED`,
  `Verdict: computer-use-unavailable: <exact reason>`, `Blocker-Type: test`,
  `UI-Driver: hybrid-unavailable`, and cites the capability report in `Evidence`.
- A hybrid run that began but exhausted recoverable driver/evidence repairs records
  `UI-Driver: hybrid`; preserve its per-run artifacts and name the exact unverified interaction.

## Durable evidence

Create `<RUN_DIR>/computer-use/` for every hybrid run and persist:

- `driver.md` — policy, capability result, active skill path, host, display scale/theme/locale when
  known, exact EXE/app-target/PID/identifier, scenario id, safety envelope, and action outcomes;
- `ax-<step>-before.txt` and `ax-<step>-after.txt` — full accessibility snapshots at decisive
  checkpoints;
- `screen-<step>-before.png` and `screen-<step>-after.png` — copied immediately from the tool's
  temporary screenshot URL.

Use diff accessibility state while navigating when the current API supports it, but request and save
a fresh full tree at evidence checkpoints. Emitting or viewing an image is not durable storage. Copy
the screenshot into `RUN_DIR` and reference the exact AX/screenshot and overlay-log paths from
`test.md` and `result.md`.

Apply this evidence order when signals disagree:

1. Fresh `-testagent` assertion/crash report.
2. Overlay semantic assertions, logged state, and measured geometry.
3. Persisted full AX role/name/value/state evidence.
4. Persisted tight screenshots judged against the task oracle and old/new references.
5. Computer Use narration, which is explanatory only and never proof.

Classify an overlay assertion failure after the planned action demonstrably occurred as `IMPL_BUG`.
Classify stale targets, wrong-window control, permission/capability failure, missing durable evidence,
or ambiguous screenshots as `TEST_FLAW` or `BLOCKED(test)` according to recoverability. Treat an AX
mismatch as `IMPL_BUG` only when accessibility is an explicit task contract; otherwise repair the
evidence path.
