# test_touch

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_touch.h](../../test_touch.h).

## Use when

Drive touch input through Qt's platform layer from the touchscreen and touchpad the harness registers. Read the contract below and the source header before use.

## Contract and usage

Registered-device touch input through Qt's platform layer (Input helpers,
`TouchSequence`): `TouchDeviceKind`, `HarnessTouchDevice`,
`IsHarnessTouchDevice`, `TouchContact`, `TouchSequence` (`send`, `cancel`,
`frames`, `owner`, `finished`, `stopped`), the `TouchFrame` reading - `sent`,
`rows` of `TouchEventRow` with receiver, device, spontaneity, points or mouse
source and the Begin `climb`, `left`, `submitted` and a named `refusal`:
`unsupported`, `stopped`, `finished`, `target-gone`, `window-gone`,
`no-window`, `invalid-frame`, `nothing-down` or `not-submitted` -
`cleanupCancel` and `cleanupRows` of the terminal cleanup - the cached
`TouchOwner` with its `TouchOwnerStatus` (`None`, `Accepted`, `Refused`,
`Ambiguous`) and `TouchFrameDetails`. Its own self-test,
`AppendTouchSelfTest`, needs no session, network or account: one parentless
harness window, plus a second small one for the leg that destroys its window,
shown without activation (`Qt::WA_ShowWithoutActivating`, `Qt::WA_QuitOnClose`
off) and released through `Runner::onFinish`, with geometry from
`st::defaultActiveButton.height` and sample points carrying .25 / .75
fractions; purpose-built probes, their destructors and an object filter on the
main window's `QWindow` keep their own journal, the oracle the helper's rows
are compared with, against literal expectations. After fixture gates (a native
`QWindow` for each window, every sample point hitting its widget, a scroll
range longer than the swipe, no popup or modal window,
`Qt::AA_SynthesizeMouseForUnhandledTouchEvents` set) and the device census
(exactly the two devices with their roles and capabilities, the primary
pointing device neither), it measures: Begin, three Updates and End within
0.01 px at the printed device pixel ratio; a climb from a child without
`Qt::WA_AcceptTouchEvents` at ancestor-local positions; the refusing-child
trap (both widgets marked, `owner()` the ancestor, the first-marked walk the
child, the owner cached past End); the unhandled control (Qt-synthesized mouse
from the screen, release included, the owner `Refused`,
`QGuiApplication::mouseButtons()` back to `Qt::NoButton`), then a clean
accepting leg again; a point-free cancel and the same-device, same-id
recovery; the seven-frame multi-contact script against a literal list, the
all-`Stationary` frame reaching the `QWindow` only and Qt's `MouseMove` from
it reaching no widget; two touchpad contacts; a probe destroyed by a
`Ui::PostponeCall` its own Begin posted (gone on return, the sequence stopped,
its readings still printable, the next send refused, a fresh sequence on a
surviving probe delivered); the terminal cleanup - a refusing probe that posts
its own deletion from Qt's synthesized press, and one deleted between two
calls while that press holds `Qt::LeftButton`, each back at `Qt::NoButton` on
return through the one point-free cancel and the release Qt sends the
`QWindow` for it, the deleted probe's next call refused `target-gone` and
every call after a stop `stopped`; then a gesture bound to a transparent
coordinate probe whose press on a `Ui::AbstractButton` an accepting probe's
contact keeps held as the gesture ends with every widget alive, so the
cleanup's cancel sends no `TouchCancel`, its release reaches the button after
the ending's `Leave`, the `Leave` follows it again, and on return the button
is neither down nor over, Qt's buttons are released, the sequence is finished
and not stopped and its next call is refused `finished` - then a clean
accepting leg as the fresh gesture; a `Leave` from the helper's ending that
destroys the target, then the second window, read dead with the sequence
stopped and finished on return, then the held gesture again with the button's
`Leave`, while it is down, destroying the coordinate probe, so the same
cleanup follows that `Leave` and the sequence is stopped as well; two contacts
on two refusing children of an ancestor that refuses its first Begin and
accepts its second, then the reverse, read `Ambiguous` beside the ancestor's
own answers and its mark after the frame (in the reverse it gets no End, and
Qt's synthesized release reaches the `QWindow` only); the devices unchanged;
and, last, a `Ui::ScrollArea` tap that clicks its `Ui::AbstractButton` exactly
once through the application-synthesized mouse and leaves no hover, and a
viewport-mapped swipe that scrolls and clicks nothing, after which the scroll
area is released before its kinetic timer runs. Every accepted purpose-built
probe leg shows no mouse event and no `Enter` at any widget, while the scroll
legs keep their `Enter` and application-synthesized rows in view.

## Related guidance

[`TouchSequence` among the input helpers](test_widgets.md#input-helpers);
[failure diagnosis](../diagnostics.md).
