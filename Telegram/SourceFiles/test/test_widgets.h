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
// Use Test::Settle for programmatic mutations; SettlePostponedCalls
// remains the bare drain.

// Synthesizes a full mouse press + release on the widget, at its center by
// default. Drives the same event path as a real click.
void Click(not_null<QWidget*> widget, std::optional<QPoint> point = {});

// Synthesizes key press + release pairs carrying one Unicode grapheme at a
// time. Surrogate pairs and joined emoji are never split between events.
void TypeText(not_null<QWidget*> widget, const QString &text);

// Delivers one input-method commit. Prefer this when the behavior under test
// is text insertion itself rather than physical key handling.
void CommitText(not_null<QWidget*> widget, const QString &text);

// Synthesizes a left-button drag in widget-local coordinates. Intermediate
// mouse moves retain Qt::LeftButton in buttons(), matching a real drag.
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

} // namespace Test
