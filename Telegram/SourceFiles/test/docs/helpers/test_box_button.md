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
