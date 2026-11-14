# test_field_submit

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API, with its own self-test in the same module.
Source contract: [test_field_submit.h](../../test_field_submit.h).

## Use when

An overlay submits a `Ui::InputField` by key: a chat's compose field, in
`HistoryWidget` or `ComposeControls`, or a box's caption or text field. A
multi-line field fires `submits()` only when `InputField::ShouldSubmit()`
accepts the key's modifiers under its submit setting, and the compose fields
and many boxes take that setting from `Core::App().settings().sendSubmitWay()`.
Under Ctrl+Enter - the Test account's setting - a plain
`Test::PressKey(field->rawTextEdit(), Qt::Key_Return)` inserts a new line,
which then stays in the chat's draft, and the overlay reads a missed send as
a product result. `Test::SubmitField` presses Return with the modifiers the
setting requires and reports whether `submits()` fired.

Keep `PressKey` for keys that are not a submit, and for a deliberate new line
or a deliberately wrong chord.

## Preconditions and limits

- **Which setting.** `SubmitField(field)` follows `sendSubmitWay()`; it is
  application-wide, though tasks call it the account's. A field that sets
  its own - an inline bot's `None` in the compose controls - needs
  `SubmitField(field, setting)` with that setting, since `Ui::InputField` has
  no getter for it. Either way the reading's `setting` is the one followed.
  The helper never writes `sendSubmitWay()` or the field's settings: a
  campaign's subject is the account as it is set.
- **Which key.** `Enter` and `Both` press Return with no modifier,
  `CtrlEnter` Return with `Qt::ControlModifier` (`Ui::InputField` reads Meta
  as Control too, so the same chord serves macOS). `None` names no submit key
  and is refused by name before any key; `InputField::ShouldSubmit()` would
  still accept Ctrl+Shift+Return under it, but the helper never presses that
  in its place. A single-line or no-new-lines field submits on any Return.
- **The key route.** One `PressKey` pair to `field->rawTextEdit()`: its
  `keyPressEvent` hands the key to `InputField::keyPressEventInner`, where
  `submits()` fires; the wrapper does not take keys. The field need not be
  the focus widget, nor its window active or on screen (the self-test's
  field sits in a top-level shown with `Qt::WA_DontShowOnScreen`). Product
  handlers that act on focus - a box's default button, a shortcut - remain
  the caller's to establish, with `Test::ForceWindowActive` and `setFocus()`
  before the call.
- **Modifiers.** `PressKey` leaves Qt's cached keyboard modifiers at
  `Qt::NoModifier` on return, so a Ctrl+Return submit leaves no Control
  behind ([modifier state](test_widgets.md#keyboard-modifier-state)). Built
  with `DESKTOP_APP_DISABLE_PRIVATE_QT_API` that reset does not exist and
  Control stays cached; the self-test is `N/A` there.
- **What it does not establish.** It reports the field's `submits()`, not
  what the product did with it: a sent message, a closed box or a request is
  the caller's own reading, behind its own wait.

## Minimal use

```cpp
runner->add({
	.name = u"send the typed message"_q,
	.run = [=] {
		const auto field = state->field.data();
		if (!field) {
			return;
		}
		state->submit = SubmitField(field);
		Check(
			state->submit.submitted(),
			u"the compose field submitted with its own key"_q,
			FieldSubmitDetails(state->submit));
	},
});
```

`state->field` is the overlay's guarded pointer to the live field; a later
stage waits for the product's send.

## Readings and refusals

`FieldSubmit`: `setting` - the setting followed; `submits` - the
`submits()` emissions that a subscription made right before the press
observed; `refusal`. `submitted()` is true for an observed emission, even if
the field died afterwards. `FieldSubmitDetails` prints
`field submit: outcome=<...> setting=<...> submits=<n> refusal=<name>` and
never the field's text; the helper neither Notes nor Fails.

| Outcome | Meaning |
| --- | --- |
| `submitted` | `submits()` fired; the key went to a live field. |
| `not-submitted` | The live field fired nothing: its own setting differs (an inline bot's `None`, for example), or a handler took the key. Never read it as sent. |
| `refused` | `no-submit-key`: the setting is `None`; nothing was sent. |
| `unobserved` | `target-destroyed`: no emission was observed and the field died during the press. An earlier `submits()` handler that destroys the field ends the stream (`rpl::event_stream`'s destructor calls `fire_done()`) before this observer runs, so the key may have submitted. It is neither a missed submit nor grounds for a retry. |

A refusal or an unobserved outcome is a fixture reading, never a PASS.

## Lifetime and cleanup

Nothing is retained: the subscription lives for the press only, and the field
is not touched after it. What the submit does - a draft cleared, a message
sent, a box closed - is the product's and stays.

## Verification

`Test::AppendFieldSubmitSelfTest(runner)`, appended after
`runner->waitEvent(u"launch_finished"_q);`, needs no account, chats list or
network. Its fixture is a multi-line `Ui::InputField` (`st::defaultInputField`,
`st::boxWidth` wide) in a parentless top-level shown with
`Qt::WA_DontShowOnScreen`, never focused or active, holding text typed with
`Test::TypeText`, beside the fixture's own `submits()` count and a key log on
the raw editor. Its rows (`FIELD_SUBMIT:`, lengths and new-line counts, no
text):

- under `Enter`, `Both` and `CtrlEnter` the helper submits once by both
  counts with its chord, the text unchanged, and Qt's cached modifiers read
  `Qt::NoModifier` right on return;
- the control: right after the `CtrlEnter` submit, with no setting written,
  a plain `PressKey(Return)` fires nothing and inserts one new line, so the
  leg reaches the trap and the helper left the field's setting alone;
- on a field set to `None`, the caller's `CtrlEnter` reads `not-submitted`
  with one new line, and `SubmitField(field, None)` refuses `no-submit-key`
  with no key and the text unchanged;
- the default overload follows the app setting, which reads the same before
  and after every call, and submits once;
- a separate field whose earlier `submits()` handler destroys it reads
  `target-destroyed`, outcome `unobserved`, and the helper returns safely.

A recovery stage and `Runner::onFinish` destroy both windows.

## Related guidance

[Input helpers and `PressKey`](test_widgets.md#input-helpers);
[keyboard modifier state](test_widgets.md#keyboard-modifier-state);
[runner and gated groups](test_runner.md).
