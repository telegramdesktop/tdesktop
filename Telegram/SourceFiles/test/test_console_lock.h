/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

namespace Test {

// Whether the Windows console session this -testagent process runs in is
// locked, read at the moment of the call and never cached.
//
// Why it exists. In Attempt 1 of
// 2026/10/07/add-a-same-turn-context-menu-reader-and-a-retried-clipboard-round-trip-to-the-test-harness
// (work/test.md) two self-tests failed on a locked console, and no row of
// the run named the lock. In Run 1 the live arm of
// AppendClipboardRoundTripSelfTest was refused read-empty after ten
// attempts, and AppendPopupMenuCaptureSelfTest's "capture painted widget:
// popup_menu_open" stage timed out on "target is not visible" right after
// its open stage passed, which ended the scenario. Run 2, the popup-capture
// self-test alone on the same locked console, timed out the same way. The
// lock was found by hand, from another process. Run 3, unlocked, passed.
//
// What it reads. WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,
// <session>, WTSSessionInfoEx), with the session ProcessIdToSessionId
// answers for this process, and the answer's
// WTSINFOEX_LEVEL1_W::SessionFlags. That is the reading Qt's own clipboard
// takes before it gives up retrying OleSetClipboard
// (QWindowsContext::isSessionLocked(), qwindowscontext.cpp, called from
// QWindowsClipboard::setMimeData), except that Qt reads the active console
// session (WTSGetActiveConsoleSessionId), which the row prints beside this
// process's own. It is not Core::App().screenIsLocked(): only
// WM_WTSSESSION_CHANGE sets that (WindowsIntegration::processEvent in
// platform/win/integration_win.cpp), so a launch on a console that is
// already locked reads false there.
//
// The interpretation. WTS_SESSIONSTATE_LOCK reads locked and
// WTS_SESSIONSTATE_UNLOCK reads unlocked. Any other value, a failed call, an
// answer shorter than WTSINFOEXW and a level other than 1 read unknown.
// Before Windows 8 every reading is unknown and the flags are never
// interpreted: Windows 7 and Server 2008 R2 report the two flags reversed,
// a documented defect.
//
// Rejected sources. The input desktop's name through OpenInputDesktop: a
// locked Windows 11 console still named it Default. An OpenClipboard
// attempt: it is the very measurement the clipboard checks make, so a gate
// on it would turn any clipboard failure into an N/A. GetForegroundWindow
// and the presence of LogonUI.exe: indirect, so they stay the independent
// out-of-process control a campaign reads beside this probe.
//
// Privacy. The same struct carries the session's user, domain and window
// station names. They are never read or printed: every row is published
// with task evidence.
//
// Other platforms. Off Windows the reading is not-applicable, nothing is
// read and no gate fires. A locked macOS console does not close a shown
// popup: in 2026/10/06/add-a-post-paint-capture-sampler-to-the-harness
// (work/test.md, Test 2) a packed launch with CGSSessionScreenIsLocked true
// ran AppendPopupMenuCaptureSelfTest with 0 FAIL.
//
// Only a Locked reading ever gates (ConsoleLockGate); Unknown and
// NotApplicable never do.
enum class ConsoleLockState {
	NotApplicable,
	Unlocked,
	Locked,
	Unknown,
};

struct ConsoleLockReading {
	QString source; // the call read, "none" off Windows
	QString raw; // its raw values, space-separated name=value
	QString reason; // the interpretation, or why none was possible
	ConsoleLockState state = ConsoleLockState::NotApplicable;

	[[nodiscard]] bool locked() const {
		return (state == ConsoleLockState::Locked);
	}
};

[[nodiscard]] ConsoleLockReading ReadConsoleLock();

// The one row: "CONSOLE_LOCK: state=<locked|unlocked|unknown|not-applicable>
// source=<source> <raw> - <reason>", without " <raw>" when |raw| is empty.
[[nodiscard]] QString ConsoleLockText(const ConsoleLockReading &reading);

// "locked console: <ConsoleLockText>" for a locked() reading and empty for
// every other state, so it can be (part of) a Stage::skipReason.
[[nodiscard]] QString ConsoleLockGate(const ConsoleLockReading &reading);

} // namespace Test
