/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <array>

namespace Test {

class Runner;

// An opt-in application event filter that keeps console input - the
// owner's keyboard, pointer, shortcuts and window-close requests that
// reach a -testagent client from the platform - out of a campaign, while
// the harness's own input and every close or quit the product or the
// scenario causes pass. Reference: docs/helpers/test_console_shield.md.
//
// Provenance. Input delivered while a HarnessInputScope is open, at the
// event-loop level that scope was opened at, is harness input and always
// passes. Test::Settle opens one around its action, so every input
// helper's delivery passes, and so does an overlay's own delivery wrapped
// in Settle, even a spontaneous one through QWindowSystemInterface. Any
// other event of a class below is console input when it is spontaneous;
// for the three classes whose platform route is a sent event - shortcut
// (QShortcutMap::dispatchEvent), input-method (macOS IME text) and drop
// (QGuiApplicationPrivate::processDrag / processDrop) - it is console
// input whether spontaneous or not. Input processed in a nested event loop
// (QDrag::exec, QMenu::exec, a modal dialog) entered from inside a harness
// delivery is outside the scope.
//
// Classes and the Qt event types each one counts:
// - key: KeyPress, KeyRelease;
// - shortcut-override: ShortcutOverride;
// - shortcut: Shortcut;
// - input-method: InputMethod;
// - mouse-button: MouseButtonPress, MouseButtonRelease,
//   MouseButtonDblClick and their NonClientArea forms;
// - pointer: MouseMove, NonClientAreaMouseMove, Enter, Leave, TabletPress,
//   TabletMove, TabletRelease, TabletEnterProximity, TabletLeaveProximity;
// - wheel: Wheel;
// - touch: TouchBegin, TouchUpdate, TouchEnd, TouchCancel, NativeGesture;
// - context-menu: ContextMenu;
// - drop: DragEnter, DragMove, DragLeave, Drop;
// - close: Close whose receiver is a widget.
// A vetoed event is filtered out and keeps its accepted state, except a
// vetoed Close, which is ignore()d first, so QWidgetPrivate::handleClose
// keeps the widget and its platform window. Quit and every unclassified
// type always pass.
//
// Closes need no pass-through. In Qt 6 every close of a top-level window
// reaches the QWidgetWindow as a spontaneous window-level Close, because
// QWidget::close() also goes through QPlatformWindow::close() and
// QWindowSystemInterface::handleCloseEvent; that window-level Close always
// passes. QWidgetWindow::closeEvent then sends the widget's own QCloseEvent
// with sendEvent for a product or scenario close (QWidget::close(),
// QWindow::close(), quit's closeAllWindows) and spontaneously for a
// platform request (a title-bar button, the window manager), and only that
// spontaneous widget Close is vetoed.
//
// Ordering limit. Qt runs application filters newest first. The shield is
// moved to the front when armed, and again on a timer every
// kConsoleShieldRefront ms, only at the outermost event-loop level and
// never from inside the filter. An application filter the product installs
// after arming sees console input first until the next eligible tick, and
// kConsoleShieldRefront is a requested timer interval, not a bound, while
// the event loop is busy or nested.
inline constexpr auto kConsoleShieldRefront = crl::time(100);

enum class ConsoleInputClass {
	Key,
	ShortcutOverride,
	Shortcut,
	InputMethod,
	MouseButton,
	Pointer,
	Wheel,
	Touch,
	ContextMenu,
	Drop,
	Close,
};

inline constexpr auto kConsoleInputClassCount
	= int(ConsoleInputClass::Close) + 1;

// The current arm, or after its release the released arm, until the next
// arm resets it. |armedAt| is crl::now() at the arm. |counts| holds one per
// vetoed event of each class, whatever the log rate, and |vetoes| is their
// sum.
struct ConsoleShieldReading {
	QString label;
	crl::time armedAt = 0;
	int vetoes = 0;
	std::array<int, kConsoleInputClassCount> counts = {};
	bool armed = false;

	[[nodiscard]] int count(ConsoleInputClass inputClass) const {
		return counts[int(inputClass)];
	}
};

// Arms the shield and labels its rows with |label|; a no-op unless
// Test::Active(). Opt-in: a campaign whose subject is real OS input
// (Computer Use, native event injection, a test-run --activate campaign)
// does not arm it, and every armed run logs its armed row.
//
// A fresh arm resets the reading, installs the one filter, starts its
// re-front timer and, once per process, registers
// ReleaseConsoleShield(u"finish"_q) with runner->onFinish, so every finish
// path releases it. Arming while armed only moves the filter to the front
// (outside a nested loop) and logs; the label, the counts and the finish
// registration stay. One process has one runner and one shield.
void ArmConsoleShield(not_null<Runner*> runner, const QString &label);

// Removes the filter, stops its timer, keeps the reading with armed=false,
// Notes the released row and returns true. When not armed it changes
// nothing, Notes the release-skipped row and returns false. A scenario
// releases it in its first teardown stage, before any stage that closes a
// window or switches accounts.
bool ReleaseConsoleShield(const QString &by);

[[nodiscard]] ConsoleShieldReading ReadConsoleShield();

// "label=<label> armed=<0|1> vetoes=<n> counts=[key=<n>,
// shortcut-override=<n>,shortcut=<n>,input-method=<n>,mouse-button=<n>,
// pointer=<n>,wheel=<n>,touch=<n>,context-menu=<n>,drop=<n>,close=<n>]",
// in ordinary integers.
[[nodiscard]] QString ConsoleShieldSummary(
	const ConsoleShieldReading &reading);

// The rows, each through Test::Note:
// - "CONSOLE_SHIELD: armed label=<label> refrontMs=<ms>";
// - "CONSOLE_SHIELD: already armed label=<label> requested=<label>
//   moved=<0|1> vetoes=<n>";
// - "CONSOLE_SHIELD: vetoed class=<class> n=<class count> total=<arm
//   count> receiver=<typeid name> label=<label>", with
//   " sequence=<QKeySequence::PortableText>" for a shortcut only; pointer,
//   wheel and touch rows are written at n=1 and every 100th, every other
//   class's at each veto;
// - "CONSOLE_SHIELD: released by=<by> armedMs=<ms> <summary>";
// - "CONSOLE_SHIELD: release skipped by=<by> - not armed".
// No row carries a key code, key text, IME string, MIME data or position:
// the filter never reads them.

// Marks the input delivered while it lives, at the event-loop level
// current when it was created, as harness input for an armed shield.
// Scopes nest: each saves the level in force before it and restores it on
// destruction. Test::Settle holds one around its action and destroys it
// before its drain, so postponed calls the drain runs are not harness
// input. Anything delivered in the scope at that loop level passes,
// including queued platform events a QWindowSystemInterface flush or a
// processEvents call inside the action delivers.
class HarnessInputScope final {
public:
	HarnessInputScope();
	HarnessInputScope(const HarnessInputScope &other) = delete;
	HarnessInputScope &operator=(const HarnessInputScope &other) = delete;
	~HarnessInputScope();

private:
	int _previous = 0;

};

// The shield measuring itself on purpose-built parentless top-level
// windows it creates and shows without activating, with no account, chats
// list or network: foreign key, mouse, shortcut and close input delivered
// through QWindowSystemInterface reaches an unarmed control and closes it;
// armed, harness input passes with no veto while the same foreign input
// is vetoed and counted per class and QWidget::close() passes; after the
// release the foreign input reaches again and a second release changes
// nothing; a last arm left armed is released by Runner::onFinish. Append
// it last. Reference: docs/helpers/test_console_shield.md, "Verification".
void AppendConsoleShieldSelfTest(not_null<Runner*> runner);

} // namespace Test
