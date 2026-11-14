# test_widget_paint_policy

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API, a synchronous fixture check of a compiled dependency.
Source contract: [test_widget_paint_policy.h](../../test_widget_paint_policy.h).

## Use when

A task moves twidget or widget-sandbox, or touches the heavy-box limit, and
has to show that widget-sandbox's shared fixture
`packages/core/tests/fixtures/paint-policy.json` passes against the rule
Telegram links: TwidgetCore's `twlite::paint`
(`core/src/paint_policy.{h,cpp}`, compiled into
`lib_widget_renderer`) and the glue's `paintLimitFromCode()`,
`paintLimitCode()`, `paintLimitMessage()` and `Renderer::limitExceeded()`
(`twidget/renderer.h`). It is the desktop's run of the fixture that
widget-sandbox's `packages/core/tests/unit/paint-policy.test.ts` and
twidget's `tests/paint_policy_tests.cpp` run upstream;
Telegram builds neither of them.

Callers: the conformance pack's first stage, the retained caller
([test_widget_conformance](test_widget_conformance.md#verification)). Its
`RunPaintPolicyFixture()` of `test_widget_conformance_paint_policy.cpp`
reads the fixture at the cleaned path of
`../core/tests/fixtures/paint-policy.json` relative to the pack's
`package`, so every launch of the pack runs it on its pin's bytes, and
takes each row as one Check; a fixture it cannot open or read gives one
failed row instead. The evidence overlay of
`2026/11/02/stop-widgets-past-the-heavy-box-limit-with-a-v0-6-0-twidgetcore`,
which added it, was its first caller.

Use something else when the question is what a live widget does: whether
its renderer reported, whether its host stopped it, which picture it kept.
That needs a widget message with
[test_widget_ops](test_widget_ops.md)'s `limitReports` and
[test_widget_host](test_widget_host.md)'s diagnostics; this check runs no
renderer, host or widget.

## Preconditions and limits

- **Pure.** Synchronous, on any thread; it needs no `Test::Active()`,
  session, window or file. The caller reads the fixture bytes, normally from
  the pinned `Telegram/ThirdParty/widget-sandbox` submodule, and keeps them;
  which file and revision it read is the caller's evidence to record.
- **Harness builds only.** Defined in Debug and in
  `TDESKTOP_RELEASE_TEST_HARNESS` builds; a build without the harness leaves
  it undefined.
- **The supported fixture** is widget-sandbox `v0.6.0`'s: top-level `_`
  (prose), `constants`, `shadows`, `boxes`, `hidesSubtree`, `animations`,
  `messages`, `reports` and `cases`, with 2, 13, 31, 4, 14, 2, 6 and 20
  entries, and case steps `set`, `remove`, `start`, `end` and `check`. A
  later fixture with a new section or step kind fails the coverage row
  instead of passing without it; extend the runner then.
- **Glue functions versus the mirrored host check.** The rule functions,
  `paintLimitFromCode()`, `paintLimitCode()`, `paintLimitMessage()` and
  `Renderer::limitExceeded()` are the compiled ones. The C++ glue has no
  twin of TS `checkPaintLimit()`, so `reports` compares the fixture with a
  mirror, written in this module, of `WidgetHost::handleRendererMessage()`'s
  reading of a `limit` report: a known string `code` and a finite numeric
  `count` of at least 0, truncated by `int(count)`. A pass shows that the
  mirror agrees with the fixture, not that the host does. The host's own
  acceptance shows only in the app: a widget that stops, with its diagnostic
  in [test_widget_host](test_widget_host.md).
- **Not covered.** The renderer applies the same rules to litehtml's
  computed values (`Renderer::count_paint()`, `box_paint_of()`); the fixture
  feeds CSS text. Counting a real document is app evidence.
- **Numbers.** TwidgetCore's `length_token()` and `transparent()` read
  numbers with `std::strtod`, which follows the process `LC_NUMERIC`; the
  coverage row records it, so a fractional blur or opacity misread under a
  comma-decimal locale can be attributed. A numeric `style` value is passed
  as `Json::number()` text, as the reference readers pass it.

## Minimal use

`sandboxRoot` is the overlay's absolute path of
`Telegram/ThirdParty/widget-sandbox` in the checkout it was built from.

```cpp
runner->add({
	.name = u"paint-policy fixture"_q,
	.run = [=] {
		auto file = QFile(state->sandboxRoot
			+ u"/packages/core/tests/fixtures/paint-policy.json"_q);
		const auto bytes = file.open(QIODevice::ReadOnly)
			? file.readAll()
			: QByteArray();
		for (const auto &row : Test::RunWidgetPaintPolicyCheck(bytes)) {
			Test::Check(row.passed, row.name, row.details);
		}
	},
});
```

Unreadable bytes give one failed coverage row, never an empty pass.

## Readings and refusals

The rows come in fixture order: one per entry of each section, then the
coverage row. Every name starts `paint policy fixture: `, then
`<section> <n>: <label>` with `n` counted from 1 within its section. The
label is the constant's name, the quoted shadow value or `display`, the
`name` of a box, animation or case, a message's code and count, or a
report's `code=` and `count=` members (`missing` when absent).

| Section | Runs | Passes when |
| --- | --- | --- |
| `constants` | `twidget::kMaxHeavyBoxes` and `kMaxBackdropBoxes`, by the names `PAINT_HEAVY_BOXES` and `PAINT_BACKDROP_BOXES` | The fixture's number equals the compiled one; an unknown name fails. |
| `shadows` | `box_shadow_blurred()`, `box_shadow_without_blur()` | Both results match. |
| `boxes` | `box_paint(style, pseudo)` on owned copies of the style's CSS text, `button` `disabled` / `pressed` mapped | `null` or `{heavy, backdrop}` matches. |
| `hidesSubtree` | `hides_subtree()` | The result matches. |
| `animations` | `AnimationPaint`, each keyframe's offset (a numeric `computedOffset`, else a numeric `offset`, else none) and every member; a value that is no string is unknown, never coerced | `null` or `{heavy, backdrop}` matches. |
| `messages` | `paintLimitFromCode()`, `paintLimitMessage()` | The reason equals `expect`. |
| `reports` | The host mirror (`hostMirror=` in the details); for an accepted report also `Renderer::limitExceeded()` on a no-op stub renderer | The mirror equals `expect`, and the stub sends exactly one message of type `limit` that the mirror reads back as the same code and count (`roundTrip=`). |
| `cases` | One `PaintCounter<std::string>` per case | Every check step matches. |

A case applies its steps in order, picking each step's kind in the TS
reader's order: `set` (a key or an array of keys, `style`, `pseudo`) calls
`set_box(key, box_paint(...))`; `remove` calls `remove_box()`; `start` with
`animation` and `keyframes` calls `start_animation()` when the animation
paint is not null; `end` calls `end_animation()`; `check` calls `check()`
and compares `heavy`, `backdrop` and the whole `stop` (`null` or
`{code, count}`). Every check calls `check()`, as a renderer does before each
frame, so a later check reads whether the first verdict stuck. A mismatch is
recorded as `step <n>: got ..., expected <the check's JSON>` and the steps
after it still run. A step of an unknown kind, keys that are not strings and
a case without a check step fail the case. Each case row's details give its
steps, checks, final heavy and backdrop counts, both peaks and its failures.

An entry whose expectation is missing or has the wrong type fails its row
instead of matching a default. There is no other input hardening: the
input is the pinned file, not hostile data.

The coverage row passes when the bytes parse, every top-level key is `_` or
a section, every section is present, non-empty and of its type (`constants`
an object, the others arrays), and no step was of an unknown kind. Its
details give `bytes`, each section's count, `entryRows`, `steps`, `checks`,
`unknownSteps`, `missing`, `unknownKeys`, `LC_NUMERIC` and `decimal`.
Unparsable bytes give the coverage row alone, failed. The `v0.6.0` tag gives
92 entry rows and the coverage row; other counts mean another file. The
coverage row is no verdict on the rule: every entry row has to pass too.

## Lifetime and cleanup

None. It allocates only its rows: no lasting state, file, account or
clipboard effect.

## Verification

Compiled and linked into the macOS Debug Telegram build of
`2026/11/02/stop-widgets-past-the-heavy-box-limit-with-a-v0-6-0-twidgetcore`,
with `paint_policy.cpp` in `lib_widget_renderer`. Outside the app, this file
built against a stub row header, Qt Core and the pinned `paint_policy.cpp`
passed all 93 rows on the `v0.6.0` bytes (`LC_NUMERIC=C`), and failed only
the named rows for four mutated copies: a later check's `stop` set to `null`
in "the first verdict sticks" (that case alone), a later check's `heavy`
changed in "a theme change recomputes every box" (that case alone), an
unknown step kind with an unknown top-level key and `reports` removed (case
1 and the coverage row), and unparsable bytes (the coverage row alone). In
that task's Debug app, its overlay's stage passed all 93 rows on the
`v0.6.0` bytes (`LC_NUMERIC=en_US.UTF-8`), and a mutated copy changed only
the row of case 16, "the first verdict sticks", to failing; its
`work/test.md` (Test 4) records both. The `reports` limit above stays:
those rows test the mirror and the glue's encoding, not the host's parser,
whichever caller runs them.

The retained conformance caller compiled and linked into the macOS Debug
Telegram build of
`2026/11/02/report-the-desktop-conformance-at-widget-sandbox-v0-6-0`, and
that task's `v0.6.0` campaign on a Release build without LTO, with the
harness, ran these rows in the first stage of each of its eight launches:
all 93 passed on the `v0.6.0` bytes every time (`LC_NUMERIC=en_US.UTF-8`).
A launch on a package copy without the sibling fixture gave the one failed
read row and no other; its `work/test.md` (Test 3) records both.

## Related guidance

[test_widget_ops](test_widget_ops.md) (live `limit` reports, and
`RunWidgetConformanceProbeCheck()`, the precedent for a shared-fixture
check); [test_widget_host](test_widget_host.md) (host diagnostics and
death); [test_widget_conformance](test_widget_conformance.md) (the
retained caller, and the `auto/37` to `auto/43` widgets through the chat
list).
