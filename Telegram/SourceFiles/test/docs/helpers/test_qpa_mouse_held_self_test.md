# test_qpa_mouse_held_self_test

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Implementation: [test_qpa_mouse_held_self_test.cpp](../../test_qpa_mouse_held_self_test.cpp).
Entry contract: [test_qpa_mouse.h](../../test_qpa_mouse.h).

## Use when

Verifying that a held [QPA mouse sequence](test_qpa_mouse.md) keeps the
pressed widget at its own pointer through lib_ui's held moves built from the
OS pointer - the [held-move filter](test_qpa_mouse.md#preconditions-and-limits) -
without forcing a click or moving the OS cursor. Product overlays do not call
it.

## Fixture

`AppendQpaMouseHeldPointerSelfTest(runner)`, appended by a disposable overlay
after `runner->waitEvent(u"launch_finished"_q);`. It needs no account, chats
list or network - an empty portable root is enough - and a desktop that can
show one small window. That window is a parentless `Ui::RpWidget` shown
without activation (`Qt::WA_ShowWithoutActivating`, never raised) with
`Qt::WA_QuitOnClose` cleared, placed against the edges of the available
geometry of the cursor's screen on the far side of the cursor in each axis;
the OS cursor is never moved. With a cell of `st::defaultActiveButton.height`
it holds a `Ui::ScrollArea` whose viewport is four by two cells, half a cell
in, its content four by four cells, and in it a small `Ui::AbstractButton`
subclass three by one cells, half a cell in, which records every move it
gets and whether it read Over right before a release; its clicked callback
counts clicks. P is the button's centre and Q a quarter cell right of the
scroll area, inside the window's margin, both in the scroll area's
coordinates: every sequence targets the area, which does not move while its
content scrolls.

The fixture stage logs `QPA_MOUSE_HELD: fixture` - Qt version, the window's
`devicePixelRatio()`, the screen, window, frame, area and button rectangles,
P and Q with their global points, the scroll range and the cursor - and its
`fixture gate:` requires the window shown inside the screen with the cursor
outside its frame, P on the button, Q off it and inside the window, room to
scroll a quarter cell from the top, and Qt holding no button, grab, popup or
modal. Every later stage is gated on it (`Runner::addGated`). Built with
`DESKTOP_APP_DISABLE_PRIVATE_QT_API` every stage is `N/A` as unsupported.

## Stages and expected readings

Each leg runs in one turn: a sequence press at P; an application event
filter installed right after it, so it runs before the sequence's filter
and records every move reaching the window as it arrived, with whether its
global point was inside the button; `scrollToY` by a quarter cell inside
`Test::Settle`; the release; `end()`; then, after `end()`, the scroll
restored inside `Settle`. Its details (`QPA_MOUSE_HELD: leg=<leg>`) print the
cursor before, at the restore and after, the scroll values, the window
arrivals and button moves with their source, buttons and points, Over at the
release, the clicks and the delivery and end readings, on PASS and FAIL.

- **held**: press at P, scroll, release at P.
  - `reached`: an application-synthesized, non-spontaneous move with the
    left button held reached the window during the hold at a point outside
    the button. Without it the leg is unreached, a FAIL, never a PASS.
  - The main row conjoins that arrival with the press and release reaching
    the button, a real scroll delta, P still inside the scrolled button,
    every application move the button got at P's button-local point (within
    0.01 logical px), Over at the release, one release and exactly one
    click.
- **control**: press at P, a held move through the sequence to Q, scroll,
  release at Q. Besides `reached`, the button gets the QPA move at Q and then
  lib_ui's move at Q, not P; it reads no Over at the release and fires no
  click.
- Each leg also requires `end()` to need no cleanup release and end clean,
  and the restoring scroll's application move after it to reach the window
  at the OS pointer with no button: unchanged.
- **cursor**: `QCursor::pos()` reads the same at the fixture, before and
  after each leg and at the end.

Without the filter the held leg's button would get the move at the OS
pointer, outside it, and fire no click; a filter that took the press point
would fail the control. Both follow from the oracles; no mutation build is
part of the self-test.

## Teardown

A recovery stage destroys the window and logs `QPA_MOUSE_HELD: teardown`;
`Runner::onFinish` destroys it too, idempotently. Each leg ends its own
sequence in its turn, so nothing stays held, grabbed or hovered.

## Related guidance

[QPA mouse delivery](test_qpa_mouse.md); [QPA mouse self-test](test_qpa_mouse_self_test.md);
[runner and gated groups](test_runner.md).
