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

// Three absences that read as one symptom, and one live Ui::InputField in
// one activated window is where all three meet, which is why one module
// measures them together.
//
// The window is the first. QWidget::setFocus() walks the focus_child chain
// unconditionally, but only its if (f->isActiveWindow()) branch promotes the
// target to QApplication::focusWidget(), and isActiveWindow() ends in a
// fallback to QPlatformWindow::isActive() (qwidget.cpp:6723-6725) - so it is
// the platform window not being active, on a locked or unattended console,
// that makes setFocus() "succeed" while every hasFocus() and
// isActiveWindow() branch keeps reading false - no error, no event, just
// absence. Clearing only the QPA focus window leaves that fallback answering
// true where the OS window is still active, which is why stage 1 records
// that half instead of asserting it. Test::ForceWindowActive
// (test/test_widgets.h) injects the activation through the QPA seam the
// platform plugin itself reports through; runs 2 and 7 of
// 2026/08/30/replace-wallet-with-new-or-imported paid for it.
//
// The wrapper is the second. Ui::InputField declares its own non-virtual
// bool hasFocus() const returning _inner->hasFocus(), the inner QTextEdit
// (input_field.h:377, input_field.cpp:4276-4278), and the class installs no
// setFocusProxy anywhere, so the reading taken through the QWidget* a
// generic finder hands back answers for the wrapper and is false exactly
// while the editor holds the focus. Ui::InputField::Inner declares no
// Q_OBJECT (input_field.cpp:1632), so the focused widget's className()
// resolves to its nearest Q_OBJECT ancestor and the log prints QTextEdit -
// the string three runs of that campaign were spent learning to read.
//
// The key route is the third. Ui::InputField overrides paintEvent,
// focusInEvent, mousePressEvent, contextMenuEvent and resizeEvent but not
// keyPressEvent (input_field.h:433-437), and Qt propagates an ignored key
// event up the parent chain and never down into a child - the
// QEvent::KeyPress case of QApplication::notify re-delivers it to
// w->parentWidget() until it is accepted or a window is reached. So
// Test::TypeText aimed at the wrapper reaches the wrapper's ancestors up to
// the primary window and never the inner QTextEdit that owns the text, and
// nothing is inserted while the field verifiably holds the focus.
//
// The de-activation and the re-activation happen inside one stage's single
// turn, never across two: ClearWindowActive mutates process-global Qt state
// and a timed-out stage skips every stage after it, so a split pair could
// leave the whole run without a focus window. The teardown stage, appended
// last, asserts activation once more for the same reason. The module needs
// no session, no chats list, no network and no account fixture - only a
// primary window to parent a shown field to, and a missing one is reported
// as a named fixture gate instead of crashing. It emits no deliberate
// failure: every refusal is observed through the returned WindowActivation,
// which logs nothing, and asserted as a passing Check whose details carry
// the refusal verbatim. Its before-leg, the silence this repair removes, is
// produced by reverting the helper and re-running the identical scenario,
// never by a stage that fails on purpose.
void AppendWindowActivationSelfTest(not_null<Runner*> runner);

} // namespace Test
