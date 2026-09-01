/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtWidgets/QWidget>

namespace Test {

// Telegram's custom widgets do not declare Q_OBJECT, so
// QObject::findChildren<T*>() cannot filter by their type and returns every
// child blindly cast to T* — using such a result crashes. Enumerate QWidget
// descendants (QWidget is a real Q_OBJECT) and dynamic_cast each instead.
template <typename T>
[[nodiscard]] std::vector<T*> FindAll(not_null<QWidget*> root) {
	auto result = std::vector<T*>();
	for (const auto widget : root->findChildren<QWidget*>()) {
		if (const auto typed = dynamic_cast<T*>(widget)) {
			result.push_back(typed);
		}
	}
	return result;
}

template <typename T>
[[nodiscard]] T *FindFirst(not_null<QWidget*> root) {
	const auto all = FindAll<T>(root);
	return all.empty() ? nullptr : all.front();
}

template <typename T>
[[nodiscard]] std::vector<T*> FindVisible(not_null<QWidget*> root) {
	auto result = FindAll<T>(root);
	result.erase(
		ranges::remove_if(result, [](T *widget) {
			return !static_cast<QWidget*>(widget)->isVisible();
		}),
		end(result));
	return result;
}

[[nodiscard]] QWidget *FindByObjectName(
	not_null<QWidget*> root,
	const QString &name);

// In-situ overlay hooks can publish the exact live object created by the
// product instead of rediscovering layer-owned/custom widgets from a window
// tree. Every replacement advances |generation|; dead QObjects resolve as
// unavailable. Publish calls are runtime no-ops unless Test::Active().
struct LiveWidgetSnapshot {
	QWidget *widget = nullptr;
	int generation = 0;
};

struct LiveActionSnapshot {
	bool available = false;
	int generation = 0;
	int invocationCount = 0;
	bool repeatable = false;
};

void PublishLiveWidget(
	const QString &key,
	not_null<QWidget*> widget);
[[nodiscard]] LiveWidgetSnapshot ReadLiveWidget(const QString &key);

void PublishLiveAction(
	const QString &key,
	not_null<QObject*> context,
	Fn<void()> action,
	bool repeatable = false);
[[nodiscard]] LiveActionSnapshot ReadLiveAction(const QString &key);
[[nodiscard]] bool InvokeLiveAction(const QString &key);

// Input delivery and settlement contract.
//
// The input helpers deliver their events synchronously with
// QApplication::sendEvent from the calling (runner-stage) context. During
// each dispatch postponed-call processing is deferred, so a product
// fix-up queued by the event can never run mid-signal-emission, where
// Qt's re-entrant document machinery would swallow its own change
// notifications. After each delivered event every pending
// Ui::PostponeCall (and any calls those queue) runs to empty in a
// top-level context — the state real user input reaches at its own
// event's unwind.
//
// Why the defer and the explicit drain: Core::Sandbox tags each
// Ui::PostponeCall with the loop-nesting level current at queue time and
// runs it only at the unwind of an event whose nesting level matches
// that tag, and only while it sits newest in the queue. Under the
// harness's synthetic nesting a fix-up either starves outright or runs
// at a matching internal unwind INSIDE the sent event's own signal
// emission — both unlike real top-level input.
//
// Guarantee: when a helper returns, postponed text fix-ups (for example
// Ui::CreateTonAmountInput's FixTonAmountInput rewrite) have run AND
// their own change handling has run, so both the widget's document and
// the field's cached text state (InputField::getLastText) show the
// product's rewritten text. The drain covers Ui::PostponeCall only:
// crl::on_main, InvokeQueued, base::Timer and network completions still
// need bounded waits. If a drained call destroys the target widget, the
// helper skips its remaining events and returns.
//
// Pointer model: the harness has no pointer, so Click and Drag end
// with a QEvent::Leave, which leaves the widget they were given
// pointerless. Give them the widget that accepts the press: an ignored
// mouse event walks up the parent chain in QApplication::notify, so a
// click aimed at a non-accepting child sets Over on the ancestor
// button that took the press while the leave reaches only the child,
// and that ancestor stays hovered. The leave is also inert while
// StateFlag::Down is still set, because leaveEventHook returns before
// setOver on that branch. Without the leave
// Ui::AbstractButton::mousePressEvent's
// checkIfOver() latches StateFlag::Over for the rest of the process,
// Ui::RoundButton::paintEvent keeps painting textBgOver, and a later
// Test::DeriveBand under the style's normal fill returns ok=0 with no
// rows — an absence reading for a widget plainly on screen. Qt does
// not re-deliver an unhandled Leave to ancestors the way it
// re-delivers an ignored mouse or key event, so the leave stays on the
// target — but on the target it is an ordinary event. It runs that
// widget's own leaveEventHook, its direct parent's
// enterFromChildEvent, every subscriber of the target's
// RpWidget::events() stream, and every installed event filter. Two
// that matter: Ui::Menu::ItemBase deselects a menu entry on Leave, so
// clicking one no longer leaves it highlighted; and Ui::Tooltip keeps
// an application-wide filter for the life of the process, so any
// synthetic click starts its hide-by-leave timer. A widget that was
// never hovered keeps its Over unchanged, because setOver returns
// early on an unchanged value.
//
// Use Test::Settle for programmatic mutations; SettlePostponedCalls
// remains the bare drain.

// Synthesizes a full mouse press + release on the widget, at its center by
// default, then a QEvent::Leave, so a completed click leaves no hover
// behind. Drives the same event path as a real click.
void Click(not_null<QWidget*> widget, std::optional<QPoint> point = {});

// Synthesizes key press + release pairs carrying one Unicode grapheme at a
// time. Surrogate pairs and joined emoji are never split between events.
void TypeText(not_null<QWidget*> widget, const QString &text);

// Delivers one input-method commit. Prefer this when the behavior under test
// is text insertion itself rather than physical key handling.
void CommitText(not_null<QWidget*> widget, const QString &text);

// Synthesizes a left-button drag in widget-local coordinates. Intermediate
// mouse moves retain Qt::LeftButton in buttons(), matching a real drag.
// Like Click, it ends with a QEvent::Leave, so the drag leaves no hover
// behind.
void Drag(
	not_null<QWidget*> widget,
	QPoint from,
	QPoint to,
	int steps = 8);

// Synthesizes a wheel event at the widget center by default. |angleDelta|
// uses Qt's native eighths-of-a-degree convention (120 is one wheel step).
void Wheel(
	not_null<QWidget*> widget,
	QPoint angleDelta,
	std::optional<QPoint> point = {});

void PressKey(
	not_null<QWidget*> widget,
	int key,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier);

// Performs the action with postponed-call processing deferred, then runs
// every pending Ui::PostponeCall to empty. The input helpers use it
// around each delivered event; wrap programmatic text mutations
// (InputField::setText) in it.
void Settle(Fn<void()> action);

// Runs every pending Ui::PostponeCall (see the contract above) — the
// bare drain, with no defer around anything.
void SettlePostponedCalls();

// Injects window activation through the QPA seam the platform plugin itself
// reports through, so a focus-routed affordance can be driven on a console
// where the window is never active in Qt's sense.
//
// QWidget::setFocus() walks the focus_child chain unconditionally, but only
// its if (f->isActiveWindow()) branch promotes the target to
// QApplication::focusWidget(), and isActiveWindow() ends in a fallback to
// QPlatformWindow::isActive() (qwidget.cpp:6723-6725) - so it takes the
// platform window not being active, on a locked or unattended console, for
// setFocus() to "succeed" while hasFocus() and isActiveWindow() keep reading
// false - no error, no event, just absence - and every affordance routed on
// them silently does nothing. Clearing only the QPA focus window does not
// reproduce that where the OS window is still active. Runs 2 and 7 of
// 2026/08/30/replace-wallet-with-new-or-imported paid for that.
// Window::Controller::activate() (window/window_controller.h:118) is not the
// answer: it asks the window manager, which on a locked console does not
// comply, which is why the injection goes through the QPA seam instead.
//
// Run 7 added the second half: the platform de-activates the window again
// asynchronously, between event-loop turns. In that one run an activation
// used in the same synchronous block as its dependent action worked, while
// a one-shot activation read back across polled turns was already gone. So
// a one-shot arrangement expected to survive across polled turns is a
// forbidden technique - assert activation in the same turn as the action,
// and re-assert it on every poll of a bounded wait.
//
// Calling ForceWindowActive from a stage's until is a deliberate, narrow
// exception to the README's stage contract, which says a readiness must be
// pure: it mutates only which window Qt considers focused, never product
// state, it is idempotent and repeatable, and it encodes no expected
// product result. Its note is capped to every kActivationNoteEvery-th call,
// and |attempts| and |notes| are process-wide counts carried in every
// reading, so the flake stays visible without flooding a 50ms-tick log.
inline constexpr auto kActivationNoteEvery = 10;

// For the values ForceWindowActive and ClearWindowActive return, |injected|
// is true exactly when |refusal| is empty, and a refusal is returned rather
// than logged: only the caller knows whether a window the helper could not
// resolve is a failure of the run or an expected reading.
// ReadWindowActivation injects nothing, so it always answers injected=false
// and fills |refusal| only when there is no window to activate through -
// never read its |injected| as "activation is in force".
struct WindowActivation {
	bool injected = false;
	bool focusWindowSet = false;
	bool activeWindow = false;
	int attempts = 0;
	int notes = 0;
	QString identity;
	QString refusal;
};

[[nodiscard]] WindowActivation ReadWindowActivation(
	QWidget *widget = nullptr);
WindowActivation ForceWindowActive(QWidget *widget = nullptr);

// Deliberately clears the application's focus window through the same seam,
// so a self-test can reach the failing shape instead of describing it. Never
// call this from a scenario, and never leave a stage with it in force.
WindowActivation ClearWindowActive();

// The one formatter both the helper's own note and every self-test Check
// detail print, so a pass and a refusal carry the same fields. It takes the
// reading rather than re-reading, because |attempts| and |notes| move on
// every call and a re-read would print numbers no verdict was made against.
[[nodiscard]] QString WindowActivationDetails(
	const WindowActivation &reading);

} // namespace Test
