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

// Test::CaptureViaWindow measuring itself on the widget shape that paid for
// it: a real Ui::Toast. Its internal::Widget sets only
// Qt::WA_TransparentForMouseEvents (ui/toast/toast_widget.cpp:413-441), and
// while its shown level is below 1 its paintEvent draws the whole frame into
// a transparent proxy at that opacity and returns (:585-600), so a bare grab
// of it holds the harness base and nothing else, at perfectly sane geometry.
// Run 4 of 2026/08/30/replace-wallet-with-new-or-imported paid for that.
//
// The fixture has two halves. The first is that toast over a two-tone opaque
// backdrop moved onto the toast's own geometry, both children of the primary
// window: in the very turn the toast is created its shown level is still 0,
// so PreparedWidgetCapture::prepare() refuses the bare grab while the
// window-mapped crop of the same rect, taken in the same turn, is not blank.
// The failing shape and the repaired one are therefore read from one turn
// rather than from two runs, and the backdrop's two tones are what keep that
// crop non-blank at every fade level. The second half is a flat, uniformly
// filled region under a transparent child that paints nothing: a
// window-mapped crop of it is near-uniform by construction, which is how the
// blank-is-a-Note contract is exercised deterministically - the one place
// this module hands CaptureViaWindow a target it will decline - and
// Test::FailureCount() is asserted not to have moved across that call.
//
// A settled toast is deliberately not the subject of the first half. At full
// opacity it paints st::toastBg over almost its whole rect, so
// UnpaintedPermille stays far below kUnpaintedMinPermille and
// PreparedWidgetCapture accepts it, correctly: the failure exists only
// mid-fade. The toast is shown with Config::infinite, so its _hideAt is 0
// (ui/toast/toast.cpp:32-34), no expiry timer ever arms, and teardown owns
// the hide.
//
// A third subject has a fixture of its own, built and destroyed inside a
// single stage: an unparented Ui::RpWidget carrying Qt::WA_DontShowOnScreen
// with one visible child, whose ReadViaWindow reading is kept across the
// destruction of that top level. It is what proves such a reading is safe
// to retain: resolved() answers false on the very object the stage is still
// holding, while its identity, mapped rect and refusal stay intact and
// printable, and the window it named is printed from the text recorded
// while the reading was still resolved. That top level is never shown on
// the desktop - QWidgetPrivate::show_sys() takes an early return for
// Qt::WA_DontShowOnScreen (qwidget.cpp:7886-7903) and never calls
// window->setVisible(true) - so Test::ResolveActivationWindow, which only
// ever selects a top level whose QWindow::isVisible() is true, can never
// select it. The stage measures that rather than asserting it: it reads
// Test::ReadWindowActivation before, during and after, and Checks that the
// focus window, the active window and the attempt counter are all where
// they were. It calls neither ForceWindowActive nor ClearWindowActive.
//
// It needs no session, no chats list, no network and no account fixture. The
// only thing it asks of the process is a primary window to parent the fixture
// to, and a missing one is reported as a named fixture gate instead of
// crashing. It appends its own teardown as the last of its six stages, and
// it emits no deliberate failure: every structural refusal it demonstrates is
// observed through the pure ReadViaWindow / ViaWindowReady readings, which
// log nothing, and asserted as a passing Check whose details carry the
// refusal verbatim. Its before-leg, the blank frame at sane geometry this
// repair removed, is produced by reverting the helper and re-running the
// identical scenario, never by a stage that fails on purpose.
void AppendCaptureViaWindowSelfTest(not_null<Runner*> runner);

} // namespace Test
