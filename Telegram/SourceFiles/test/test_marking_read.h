/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "test/test_widgets.h"

#include <crl/crl_time.h>

namespace Window {
class Controller;
} // namespace Window

namespace Test {

class Runner;

// MainWindow::markingAsRead() is true on an unlocked console when the
// session content is showing, nothing covers it, the window is neither
// hidden nor minimized, its QWindow is exposed, and either
// auto-scroll-inactive-chat is on or the window is active and the session
// is not idle. Clearing the QPA focus window leaves the option's term able
// to keep the predicate true, and QWidget::hide() makes CaptureWidget refuse
// the window. Minimizing clears the predicate without hiding the widget the
// in-process grab renders.
//
// Window::Controller::activate(), and every showHistory that is not
// anim::activation::background, clear Qt::WindowMinimized. Call this again
// after those paths. There is no standing guard: the self-test has to see
// the predicate true again after activate().
//
// |waited| is how long the caller has already been polling. Zero never
// refuses. At kNotMarkingReadBound a window that is not minimized, shown,
// and not marking returns a named refusal. A null controller refuses
// immediately and does not search for some other top-level window.
//
// The self-test needs a session main window and leaves that window shown.
// From its control until its restore stage it stacks the primary window
// above other applications' windows with Test::KeepWindowExposed, which
// activates nothing, so a window another application covers - the usual
// state after a default workspace.py test-run launch, which does not
// activate the client - still reads exposed. The hint stays set while the
// lever minimizes the window, where it exposes nothing, and is cleared at
// the restore stage and in Runner::onFinish. A locked host still skips the
// deciding half instead of passing it, as does any host where the control
// cannot read markingAsRead true. Its "activate undoes the lever" and
// "restore" stages call Window::Controller::activate(), which on macOS
// requests activation of the client (Platform::ActivateThisProcess());
// after a background workspace.py test-run launch the window server may
// grant or refuse that request, and the self-test passes either way.
struct MarkingReadReading {
	bool markingAsRead = false;
	bool isActive = false;
	bool isMinimized = false;
	bool isHidden = false;
	bool exposed = false;
	bool screenLocked = false;
	WindowActivation activation;
	QString refusal;
};

inline constexpr auto kNotMarkingReadBound = crl::time(3000);

[[nodiscard]] MarkingReadReading ReadMainWindowMarking(
	Window::Controller *controller);
[[nodiscard]] MarkingReadReading KeepMainWindowNotMarkingRead(
	Window::Controller *controller,
	crl::time waited = crl::time(0));
[[nodiscard]] QString MarkingReadDetails(const MarkingReadReading &reading);

void AppendMainWindowNotMarkingReadSelfTest(not_null<Runner*> runner);

} // namespace Test
