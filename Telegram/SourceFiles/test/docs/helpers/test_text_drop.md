# test_text_drop

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_text_drop.h](../../test_text_drop.h).

## Use when

Self-test for text-drop routing. Read the contract below and the source header before use.

## Contract and usage

The text-drop helper's own self-test: a real `Ui::InputField` inside a
parentless, never-shown container that accepts drops and counts every drag
event, where the field's mime hook taking the drop receives exactly the text
and leaving it makes the field hold exactly the text, beside three refusal
controls - `acceptDrops` off and a disabled field (`viewport-refuses-drops`),
a read-only editor under the container (`enter-not-accepted`, preceded by a
raw-route premise showing the same enter and drop landing in the container)
and an editor made read-only on the drop (`drop-not-accepted`) - each leg
reading the container's counters, `QDragManager`'s current target by pointer
and the `QClipboard::changed` count within the call, never the clipboard
itself.
