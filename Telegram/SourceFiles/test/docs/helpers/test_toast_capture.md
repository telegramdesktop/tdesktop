# test_toast_capture

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_toast_capture.h](../../test_toast_capture.h).

## Use when

Isolated toast-subtree captures. Read the contract below and the source header before use.

## Contract and usage

The toast capture whose frame cannot compose the product surface beneath it:
the live-toast walk over the top level widgets (`Ui::Toast::internal::Manager`
keeps `_toastByWidget` private and enumerates nothing), a grab rooted at the
toast subtree that `Ui::GrabWidgetToImage` fills with `st::windowBg` wherever
the toast does not cover, a settled-show readiness term measured against
`st::toastBg`'s own blend over that base because `_shownLevel` is private,
named refusals for a root that is not the live toast and for a rect outside
the toast's geometry that quote both rects and never reframe, the joined-label
text oracle read through `Test::CheckTextReads`, and its own self-test over a
synthetic sentinel surface it paints, which counts sentinel pixels in both
saved frames and then hides that surface to show no pixel of the subtree frame
depending on it, and which drives both documented ambiguity branches of the
resolver — a second live toast shown and taken down inside one stage, and the
empty walk its own teardown arranges — showing `FindLiveToast()` answering
`nullptr` for each, and which measures a default-duration toast live on
`ToastSubtreeReady` inside its lifetime and gone from the walk after it,
quoting the live count, the texts and the elapsed time.
