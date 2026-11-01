/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace Test {

class Runner;

// The self-test of Test::ClickWithPressOrigin (test_widgets.h).
//
// Why it exists. Ui::RippleButton::onStateChanged starts a press ripple at
// prepareRippleStartPosition(), which every class computes as the real
// QCursor::pos() mapped into the button plus its own offset and guard, so a
// plain Test::Click starts the ripple wherever the owner's mouse happens to
// be, or starts none. Run 2 of
// 2026/10/20/replace-sell-with-card-and-color-actions-on-the-wallet-gift-cover
// read changedPixels=[0,0] in its ~40 and ~120 ms press frames for that
// reason, and 2026/08/03/redesign-add-funds-and-buy-grams and
// 2026/08/10/correct-streamed-draft-stop-colour-and-lookups had moved the
// real cursor with QCursor::setPos instead. ClickWithPressOrigin, added by
// 2026/10/20/let-harness-presses-start-button-ripples-at-the-press-point,
// lends one press a pointer's origin; this module proves on buttons it
// builds itself that the button's own ripple then starts at the press
// point, that the class's own guard still applies, that a plain Click still
// does not do it, and that the real cursor is never touched.
//
// The fixture. An unparented Ui::RpWidget top level with
// Qt::WA_DontShowOnScreen and Qt::WA_QuitOnClose false, as
// BuildOffscreenWindow in test_via_window.cpp: it is created and its
// widgets are visible, but it never appears on the desktop, cannot be
// activated and cannot be entered by the real pointer. Its geometry is set
// before show() (afterwards Qt no longer positions such a window) at the
// corner of the cursor's screen's availableGeometry() farthest from
// QCursor::pos(), and it paints an owned #ffffff background. Its children,
// each S x S logical pixels with S = style::ConvertScale(120) and g =
// style::ConvertScale(24) between them:
// - a test-local Ui::RippleButton subclass that only fills #ffffff and
//   calls paintRipple(p, 0, 0); it overrides neither
//   prepareRippleStartPosition() nor prepareRippleMask(), so its origin is
//   RippleButton's own unguarded mapFromGlobal(QCursor::pos()) and its mask
//   the whole button; ripple #c02050;
// - a Ui::RoundButton on a copy of st::defaultActiveButton: square,
//   padding 0, empty text, textBg #ffffff and textBgOver #d8d8d8 (so a
//   hover highlight would change every pixel of it); its origin subtracts
//   the padding and its mask is the rect rounded by st::buttonRadius;
//   ripple #2050c0;
// - a Ui::IconButton on a copy of st::defaultIconButton: empty icon, no
//   hover animation, a centred ripple area A = 2S / 3; its origin subtracts
//   rippleAreaPosition and is refused outside the A x A square, and its
//   mask is the ellipse inscribed in that square; ripple #20a040;
// - a S/2 square doomed probe (the same subclass), below the first button,
//   whose press posts its own destruction through Ui::PostponeCall.
// Every colour is owned (style::owned_color), so no theme changes it, and
// every ripple keeps st::defaultRippleAnimation's durations (a 650 ms show
// eased by anim::easeOutQuint, a 200 ms linear hide). The press points, in
// button coordinates: probe (7S/20, 7S/20), RoundButton (13S/20, 7S/20),
// IconButton rippleAreaPosition + (3A/10, 7A/20), each at least S/10 from
// the button's centre. The IconButton guard point Q = rippleAreaPosition /
// 2 lies inside the button but outside the ripple-area square: the guard is
// a square containment test while the mask is an ellipse, so a corner of
// the square would still start a (clipped) ripple.
//
// The oracle, per frame, on the device pixels of GrabRect(window,
// button->geometry()), and the tolerances' origins:
// - changed: ChannelDelta against a baseline grabbed right before the press
//   exceeds 8, well above rendering noise and far below the 48 every ripple
//   colour differs from white by on every channel;
// - the mask and the button's own ripple colour come from a
//   setForceRippled(true, anim::type::instant) grab - a full-mask ripple at
//   opacity 1 in the style's colour - read once in stage 1;
// - disc edge pixels: changed pixels inside the mask eroded by 2 device
//   pixels (clear of the mask's own antialiased rim) with an unchanged
//   4-neighbour, so every edge pixel is the disc's own and never the
//   mask's. A Kasa least-squares circle fit over their centres needs at
//   least 16 points and an rms residual of at most 1.5 device pixels (a
//   thresholded antialiased circle sits near 0.3). The fit needs no whole
//   circle, which matters because every ~120 ms disc is clipped by its mask;
// - centre: within max(2, 0.05 x R(hi) x ratio) device pixels of the press
//   point x ratio - two pixels of quantisation, or 5 % of the radius for
//   the partial arcs a large clipped disc leaves - with the press pixel
//   itself changed;
// - radius: within [R(lo) - 1.5, R(hi) + 1.5] logical pixels, R(t) =
//   radiusTo x (1 - (1 - t / show)^5), radiusTo computed exactly as
//   Ui::RippleAnimation::Ripple computes it (the rounded distance from the
//   origin to the mask's farthest corner) and [lo, hi] the frame's reached
//   bounds from Test::RunClockedFrames; the slack covers the ripple's integer
//   radius and the edge threshold;
// - colour: on changed interior pixels at least 2 logical pixels inside the
//   fitted radius, k = (frame - baseline) / (mask - baseline) on every
//   channel where the mask reading differs from the baseline by at least
//   48; at least 10 such pixels, and the median k and at least 95 % of the k
//   values within [O(hi) - 0.05, O(lo) + 0.05], O(t) = 1 - t / hide, because
//   the release inside the clocked action starts the linear hide (0.05 is
//   8-bit rounding on a 48-level difference, with room);
// - nothing else: no changed pixel farther than r + 1.5 logical pixels from
//   the fitted centre;
// - growth: the ~120 ms radius exceeds the ~40 ms one by at least 4 logical
//   pixels, and more pixels changed.
// Every time is scaled by anim::SlowMultiplier().
//
// The stages, in order. Every stage after the first is N/A, by the fixture
// gate's name, when the gate failed; the frame stages are also N/A while
// anim::Disabled().
// 1. fixture: the gate - the window built, the three buttons S x S, the
//    window's QWindow not visible, animations enabled, the best corner at
//    least S/2 from the cursor, the press points off centre (the slow
//    multiplier, ratio, placement and points printed); then for each button:
//    the real cursor lies outside its global rect by at least S/2, its
//    baseline is uniformly #ffffff (a stray highlight or leftover ripple
//    fails it), and its forced ripple covers exactly the class's mask (the
//    whole button, or the IconButton's ripple-area square, within one
//    device pixel) in the style's own ripple colour (99 % of the interior
//    within 3 per channel). Saves press_origin_<subject>_baseline and
//    press_origin_<subject>_mask.
// 2. negative control: a plain Test::Click at each press point, clocked at
//    ~40 and ~120 ms: no disc centred at the press point in either taken
//    frame (the ripple starts at the real cursor, outside the button, or -
//    for the IconButton - not at all); the cursor read before and after the
//    press equals the one read at the start. Saves
//    press_origin_<subject>_control_<ms>.
// 3. press-point ripple: Test::ClickWithPressOrigin at the same points and
//    frames: the reading (refusal none, delivered, displaced, originSeen
//    equal to the point, restored, no geometry change by the press, press
//    accepted, cursor stable and equal to the start, two QEvent::Move
//    withheld, no paint while displaced, Click's global, the button back at
//    its initial geometry); the button's own events() stream records exactly
//    Click's press, release and leave - the press at the point with Click's
//    global while the button sat at its initial position plus the
//    displacement and mapped the cursor to the point, the release at the
//    same point back at the initial position, no Move; in each frame a disc
//    at the press point, its radius, its colour and nothing else changed -
//    for the RoundButton that last row is the hover caveat's proof; growth
//    between the frames. Saves press_origin_<subject>_facility_<ms>.
// 4. guard kept: ClickWithPressOrigin on the IconButton at Q, clocked as
//    in stage 3: the same reading and events() rows as there, now at Q -
//    the press reached the button while it was displaced, with the cursor
//    mapped to Q, and was accepted - and no pixel of the button changed in
//    either frame: the class saw Q and refused it, as a pointer press there
//    would. Stage 3's IconButton leg is printed beside it as the positive
//    control. Saves press_origin_icon_guard_<ms>.
// 5. nothing left behind: each button, its ripple finished, grabs
//    identical to its stage 1 baseline (no pixel differs at all), with its
//    initial geometry and its initial Qt::WA_Moved,
//    Qt::WA_NoMousePropagation and Qt::WA_PendingMoveEvent, read before the
//    grab because a render would send a pending move event; a plain Click
//    then reads like stage 2, with the cursor read before and after it
//    equal to the start. Saves press_origin_<subject>_after_<ms>.
// 6. refusals and lifetime: the window itself is top-level-target; a child
//    with Qt::WA_NativeWindow, and a plain child with such a descendant,
//    each in its own parentless never-shown container - because
//    QWidget::setAttribute(Qt::WA_NativeWindow) makes every sibling native
//    through the parent's enforceNativeChildren() - are native-target with
//    a reason naming the native widget as the target itself or as a
//    descendant, and neither container is ever created, so no platform
//    window exists; a disabled probe is disabled-target; a point outside is
//    point-outside. Each sends nothing and moves nothing: nothing
//    displaced, sent or withheld, the target's own events() stream records
//    nothing during the call, and its geometry is unchanged. The doomed
//    probe's press is sent and the probe restored, then it dies in Settle's
//    drain: its events() record holds the press alone - no release, no
//    leave -, no crash, the cursor unchanged.
// 7. real cursor untouched: QCursor::pos() equals the start, and so does
//    every clocked press's read before and after it and every facility
//    reading's cursorBefore and cursorAfter, the doomed probe's included
//    (listed).
// 8. teardown: the window, its buttons and the doomed probe are released;
//    the same idempotent release is registered with Runner::onFinish, so a
//    timeout or watchdog path destroys the window too. The styles stay in
//    the leaked state, which outlives every button.
//
// origin-unreachable is not provoked: it needs the cursor to move between
// the facility's two reads, and nothing may move the cursor. The cursor rows
// assume nobody moves the mouse during the run; if someone does, the
// per-press reads show where, and the run is repeated.
//
// It needs no session, no primary window, no chats list, no network and no
// account. It emits no deliberate FAIL. Its negative legs are disposable
// mutations of the facility: with the displacement forced to zero the
// stage 3 reading and disc rows fail while stage 2 still passes; with the
// restore skipped the stage 3 reading row and the stage 5 rows fail.
void AppendPressOriginSelfTest(not_null<Runner*> runner);

} // namespace Test
