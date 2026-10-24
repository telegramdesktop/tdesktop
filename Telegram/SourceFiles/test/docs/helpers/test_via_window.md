# test_via_window

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_via_window.h](../../test_via_window.h).

## Use when

Self-test for window-mapped captures. Read the contract below and the source header before use.

## Contract and usage

The window-mapped capture's own self-test: a real `Ui::Toast` whose bare
prepared grab is refused in the turn it is created, beside the window-cropped
frame of the same rect that the harness accepts, and a flat fixture region
proving a blank frame is a `Note` and never a FAIL; a sixth stage builds an
offscreen `Qt::WA_DontShowOnScreen` top level, reads it, destroys it inside
the same `.run`, and shows the retained reading answering `resolved()` false
with its identity, mapped rect and refusal intact.
