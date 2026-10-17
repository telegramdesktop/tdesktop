/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

namespace Test {

// Whether the console session this -testagent process runs in is locked,
// read at the moment of the call and never cached: on Windows the WTS
// session flags, on macOS the window-server session's lock bit, elsewhere
// not-applicable.
//
// Why it exists on Windows. In Attempt 1 of
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
// Why it exists on macOS. In the smoke run of
// 2026/10/08/keep-an-occluded-harness-window-exposed-without-activating-it
// (work/review1-general.md, B1) workspace.py test-run read screen_locked:
// true before and after the run, with loginwindow frontmost, while the
// window-exposure self-test printed screenLocked=0 from
// Core::App().screenIsLocked(): its lock gate never fired and both legs
// blamed the host's occlusion. N4 of the same review found the
// marking-read and animation-clock readings blind the same way.
//
// What it reads on Windows.
// WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, <session>,
// WTSSessionInfoEx), with the session ProcessIdToSessionId answers for this
// process, and the answer's WTSINFOEX_LEVEL1_W::SessionFlags. That is the
// reading Qt's own clipboard takes before it gives up retrying
// OleSetClipboard (QWindowsContext::isSessionLocked(), qwindowscontext.cpp,
// called from QWindowsClipboard::setMimeData), except that Qt reads the
// active console session (WTSGetActiveConsoleSessionId), which the row
// prints beside this process's own.
//
// What it reads on macOS. CGSessionCopyCurrentDictionary() (CoreGraphics
// CGSession.h), a fresh copy per call, released here, and two booleans in
// it: the undocumented CGSSessionScreenIsLocked, true while the session's
// lock screen is up - measured present and true on a locked console by
// that task's native probe, and absent on an unlocked one on macOS 27 -
// and kCGSessionOnConsoleKey ("kCGSSessionOnConsoleKey"). They are the
// keys workspace.py test-run reads out of process, through ioreg's
// IOConsoleUsers, for its screen_locked: the independent control.
//
// The interpretation on Windows. WTS_SESSIONSTATE_LOCK reads locked and
// WTS_SESSIONSTATE_UNLOCK reads unlocked. Any other value, a failed call, an
// answer shorter than WTSINFOEXW and a level other than 1 read unknown.
// Before Windows 8 every reading is unknown and the flags are never
// interpreted: Windows 7 and Server 2008 R2 report the two flags reversed,
// a documented defect.
//
// The interpretation on macOS, in this order. A null dictionary - the
// process runs in no window-server session - reads unknown, with no raw
// values. CGSSessionScreenIsLocked true reads locked. A
// CGSSessionScreenIsLocked that is not a boolean reads unknown.
// kCGSessionOnConsoleKey true, with the lock key absent or false, reads
// unlocked. Anything else reads unknown: the session is not on the console
// (fast user switching), so its lock bit does not say whether its screen
// is shown. The raw values are
// "kCGSSessionOnConsoleKey=<1|0|absent|not-boolean>
// CGSSessionScreenIsLocked=<1|0|absent|not-boolean>".
//
// Rejected sources on Windows. The input desktop's name through
// OpenInputDesktop: a locked Windows 11 console still named it Default. An
// OpenClipboard attempt: it is the very measurement the clipboard checks
// make, so a gate on it would turn any clipboard failure into an N/A.
// GetForegroundWindow and the presence of LogonUI.exe: indirect, so they
// stay the independent out-of-process control a campaign reads beside this
// probe.
//
// Rejected on every platform: Core::App().screenIsLocked(), which is never
// consulted. It is a product flag set only by notifications that arrive
// after the launch - WM_WTSSESSION_CHANGE (WindowsIntegration::processEvent
// in platform/win/integration_win.cpp), the com.apple.screenIsLocked and
// com.apple.screenIsUnlocked observers (screenIsLocked: and
// screenIsUnlocked: in platform/mac/main_window_mac.mm), and the
// xdg-desktop-portal Inhibit monitor's screensaver-active state
// (LinuxIntegration::initInhibit in platform/linux/integration_linux.cpp) -
// so a launch on a console that is already locked reads false there, and
// where it would add anything it adds a second source the row could not
// name.
//
// Privacy. On Windows the same struct carries the session's user, domain
// and window station names; on macOS the dictionary carries the user's
// short and long names, UID, GID, session UUID and audit and security
// session ids. They are never read or printed: every row is published with
// task evidence.
//
// Which gates. ConsoleLockGate answers only in a Windows build. The legs it
// gates break there: no process on a locked Windows console's desktop can
// open the clipboard, and Qt closes a popup held open across turns on
// ApplicationDeactivate. A locked macOS console does not close a shown
// popup: in 2026/10/06/add-a-post-paint-capture-sampler-to-the-harness
// (work/test.md, Test 2) a packed launch with CGSSessionScreenIsLocked true
// ran AppendPopupMenuCaptureSelfTest with 0 FAIL. So on macOS a locked
// reading is printed and never gates those legs. A check that a locked
// macOS console does break gates on locked() itself
// (AppendKeepWindowExposedSelfTest); one where the lock only names a cause
// reads locked() into a printed field (ReadMainWindowMarking, the
// animation-clock window reading). Elsewhere the reading is
// not-applicable, nothing is read and no gate fires.
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
	QString source; // the call read, "none" off Windows and macOS
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

// "locked console: <ConsoleLockText>" for a locked() reading in a Windows
// build, and empty for every other reading and on every other platform, so
// it can be (part of) a Stage::skipReason.
[[nodiscard]] QString ConsoleLockGate(const ConsoleLockReading &reading);

} // namespace Test
