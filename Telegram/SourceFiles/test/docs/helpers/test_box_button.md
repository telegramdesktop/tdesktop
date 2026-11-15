# test_box_button

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_box_button.h](../../test_box_button.h).

## Use when

Resolve and click shell footer buttons. Read the contract below and the source header before use.

## Contract and usage

Click a box's shell footer button by label, rooted at
`Test::PaintingLayerRoot`, with its own self-test: a content-rooted
`Ui::RoundButton` search that reaches none of the footer buttons beside the
rooted search that finds and clicks one, a named refusal for a label no shell
button carries, a disabled or hidden footer readable as
`present`+`disabled`/`hidden` with `matched()` still false
(`BoxButtonDisabled` / `BoxButtonHidden` beside `BoxButtonReady`),
`box_button_shell` saved through `CaptureBoxLayer` so the frame contains the
footer row, and a same-launch hidden-then-ready shell-button reading while the
layer show animation runs. `ClickBoxButton` still Fails on an unusable button.

`ReadBoxButtons` reads each shell button through `Test::ReadButtonState`
(`test_widgets.h`), the shared reading of a button's own disabled flag
beside `QWidget::isEnabled()`
([button state](test_widgets.md#a-buttons-own-disabled-state)), so the walk
holds no second copy of the `isVisible()` / `isDisabled()` test and
`present`, `disabled`, `hidden`, `labels` and every `Box*` answer keep their
meaning: `disabled` is the footer's own `isDisabled()`, and `hidden` still
wins over it.

## Verification

- `AppendBoxButtonClickSelfTest` covers the click, the refusals, the
  busy/hidden readings and the hidden-during-show-animation reading on one
  real `Ui::GenericBox`. Its `ClickBoxButton` on the disabled
  `Harness Busy` footer is the one announced deliberate FAIL, named by the
  Note before it, so a run carrying it ends `SCENARIO_RESULT: FAIL` by
  design.
- `AppendButtonStateSelfTest` is the self-test of `ReadButtonState`: one
  stage on a real `Ui::RoundButton` parented to the primary window.
  `setDisabled(true)` and `setDisabled(false)`, called through the
  `Ui::RoundButton` type, read disabled and then enabled while
  `isEnabled()`, printed beside them as the control, stays true;
  `QWidget::setEnabled(false)` moves that control and leaves the own flag
  clear; a null widget and a widget that is not a `Ui::AbstractButton`
  refuse by name and are never `usable()`. It has no deliberate FAIL, needs
  no session, chats list, network or account fixture, reads a missing
  primary window as a named fixture gate and releases its button in the
  same stage.
