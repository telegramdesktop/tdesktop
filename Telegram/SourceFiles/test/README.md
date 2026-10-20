# Telegram Desktop task-test harness

Read this short guide before designing, authoring or recovering a Telegram
`-testagent` overlay. Then read only the references needed by the task and
those helpers' headers. The [helper index](docs/helpers/README.md) is a lookup
tool; neither it nor the detailed references are a required reading bundle.

The permanent harness provides the runner, fixtures, inputs and evidence
instruments. The disposable overlay supplies task-specific setup, production
entry points and checks. Search the index before recreating a shared facility.
Keep the repository's `test_scenario.cpp` a no-op outside the saved overlay.

## The stage contract

- `run` acts once on prerequisites already established by an earlier stage.
- `until` observes readiness repeatedly; expected product outcomes belong in
  `then` or `captureAndInspect`. Do not turn a mismatch into a timeout.
- `then` asserts or acts on the exact object accepted by readiness. Prefer
  `actOnWidget` and lifetime guards over resolving another instance.
- `skipReason` names an inapplicable prerequisite, never a failing result.
  `timeoutDetails` reports the last identity, geometry and state.
- Assert and log measured values on both passing and failing paths. A helper
  refusal or undecidable reading is not evidence of product success.

Read the [runner contract](docs/helpers/test_runner.md) before writing stages;
it includes the narrowly documented activation exception to a pure wait.
Startup/session/chats waits are opt-in: select only those the fixture needs.

Every scenario first passes the [session gate](docs/helpers/test_session_gate.md),
which refuses authorized non-Test accounts unless the task explicitly permits
production legs. Configure `UseTestSession` and its restore stage when the
fixture needs an account switch. `SetupScenario` only registers stages,
releases and gate options; the gate does not guard actions taken during setup.

## Exact objects and actions

Use RTTI-based harness finders, not Qt `findChildren<CustomWidget*>()`.
Visibility alone does not prove identity, ownership or unobscured geometry.
Publish exact live objects/actions when descendant searches are ambiguous;
keep lifetime guards across stages and preserve a window's concrete type.
Read [object publication and input contracts](docs/helpers/test_widgets.md).

## Resolving a chat's message list

Resolve the section that owns the exact message; do not assume every chat uses
`HistoryWidget` or that `item->mainView()` is the visible element.
Read [chat-list resolution](docs/chat-lists.md) for message-row work.

## Input helpers

Choose the real event receiver and verify its resulting state. Settlement of
postponed calls does not wait for timers, network or animations. A synthetic
click is not a lasting hover. Read [input helpers](docs/helpers/test_widgets.md#input-helpers)
for input, [launch behavior](docs/launch.md) for activation/background painting,
and the selected module's reference for clipboard, menu or other special input.

## Capture and visual assertions

Capture the real paint owner and the complete requested rectangle. Keep
readiness separate from the expected pixels; save the accepted frame before
asserting on it. Report logical versus image pixels and DPR explicitly.
Read [capture selection](docs/helpers/test_capture.md), then select only the
needed [ink](docs/helpers/test_ink.md), [post-paint](docs/helpers/test_post_paint.md),
[clocked-frame](docs/helpers/test_animation_clock.md) or
[corner](docs/helpers/test_corner_patch.md) reference.

## Helper catalog

Find modules by purpose in the [helper index](docs/helpers/README.md).
Each entry links its detailed contract and source header. Entries marked
**Self-test** validate the harness; do not add them to every product scenario.
Search [failure diagnosis](docs/diagnostics.md) by observed symptom on recovery.

## Building one reliable packed scenario

Derive checks from the task and diff; use the most direct real production
entry point. Pack compatible checks into one launch; split only for a named
incompatible process-lifetime requirement. After a test flaw, repair all
visible flaws together. Keep nondestructive states first and closing actions
last. Read [scenario design](docs/scenarios.md) and, when useful, the
[minimal overlay example](docs/overlay-example.md).

## Account fixture secrets

Run only through the workflow's marked test environment. Fixture secrets stay
in process: never put them in code, prompts, logs or evidence artifacts. Read
[account fixtures](docs/account-fixtures.md) before using account secrets or a
funded wallet, including its required reconciliation and secrecy-scan links.
For public-identity or log-coverage checks, read the separate
[publication audit contract](docs/helpers/test_publication_identity_scan.md);
neither establishes fixture secrecy.
A required missing fixture is a blocker; do not substitute fabricated proof,
reset the funded wallet or discard a possibly unrewritten rotation.

## Media fixtures and fixture gates

Metadata alone does not establish playable media. A media-dependent scenario
must observe real playback and preserve the validated fixture across stages.
Read [media fixtures](docs/media-fixtures.md) before selecting one.

## Scenario teardown before quit

Register idempotent release of subscriptions, watchers and timers with
`Runner::onFinish`; a final stage can be skipped by timeout or watchdog.
Restore asynchronous UI state in a settling stage, not during quit. Read
[teardown](docs/teardown.md) when retaining state or changing the environment.

## Failure diagnosis

Read the actual logs/captures, classify the failure, and search the
[diagnostic reference](docs/diagnostics.md) for that signature. It is a lookup
history, not a preflight reading assignment. Preserve prior positive evidence;
do not weaken an oracle to get past a refusal.

## Adding or changing a helper

Follow [the documentation format](docs/adding-helpers.md): one short index
entry, an on-demand module reference, and a precise source-header contract.
Keep this README under 8 KiB; only universal rules and routing links belong
here. Examples, platform caveats, self-test procedures and failure histories
belong in the linked references. Cite symbols and files, never source line
numbers. The full execution workflow remains in
[the test loop](../../../.agents/shared/test-loop.md).
