# test_press_origin

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_press_origin.h](../../test_press_origin.h).

## Use when

Self-test for press-point ripple origins. Read the contract below and the source header before use.

## Contract and usage

`ClickWithPressOrigin`'s own self-test, `AppendPressOriginSelfTest`, from
`2026/10/20/let-harness-presses-start-button-ripples-at-the-press-point`. It
builds its own never-shown `Qt::WA_DontShowOnScreen` top-level window, placed
before `show()` at the corner of the cursor's screen farthest from the real
cursor, holding a test-local `Ui::RippleButton` subclass that paints only a
background and its ripple, a `Ui::RoundButton` whose `textBgOver` differs from
its `textBg`, a `Ui::IconButton` whose ripple area is two thirds of the
button, each 120 x 120 logical pixels at 100 % scale with its own owned ripple
colour and `st::defaultRippleAnimation`'s durations, and a doomed probe. At
one off-centre point per button a plain `Click` (the negative control) and the
facility are each clocked by `RunClockedFrames` at about 40 and 120 ms, and
every frame is read against a fresh baseline: a Kasa least-squares circle
fitted to the disc's own edge pixels inside the class's mask (from a
`setForceRippled` reading of the mask and the button's own ripple colour) must
be centred at the press point within `max(2, 0.05 x R)` device pixels (R the
expected radius in device pixels), with its radius in the `anim::easeOutQuint`
window at the frame's reached bounds, its interior the button's own ripple
colour at the hide opacity, no changed pixel outside it (so no hover
highlight) and growth between the frames, while the control shows no disc
centred there. The facility's reading and each button's own `events()` record
show exactly `Click`'s press, release and leave, the press delivered while the
button was displaced and mapped the cursor to the point; an `IconButton` press
outside its ripple area changes no pixel (the class's guard kept, beside the
in-area press as its positive control); afterwards every button grabs
identical to its baseline with its geometry, `Qt::WA_Moved`,
`Qt::WA_NoMousePropagation` and `Qt::WA_PendingMoveEvent` as before, and a
plain `Click` reads like the control; the window is refused
`top-level-target`, a native child and a plain child with a native descendant,
each in its own never-shown container, `native-target`, a disabled probe
`disabled-target` and a point outside `point-outside`, each sending and moving
nothing; the doomed probe, destroyed by its own press's postponed call, is put
back first and gets no release or leave; and `QCursor::pos()` reads the same
at the start, at the end and around every press. It needs no session, primary
window, network or account, and saves the baselines, mask readings and every
clocked frame.
