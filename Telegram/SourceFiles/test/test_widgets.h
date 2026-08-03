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

// Input delivery and settlement contract.
//
// Click, TypeText and PressKey deliver their events synchronously with
// QApplication::sendEvent from the calling (runner-stage) context, and run
// SettlePostponedCalls() after every delivered event.
//
// Why the explicit drain: Core::Sandbox tags each Ui::PostponeCall with
// the loop-nesting level current at queue time and runs it only at the
// unwind of an event whose nesting level matches that tag, and only while
// it sits newest in the queue. A postponed input fix-up queued under the
// harness's synthetic nesting gets a tag no later unwind matches: a
// synchronously sent keystroke's fix-up starves outright, and even a
// posted keystroke's fix-up would run only at the entry of the NEXT
// changes-firing event. Text-neutral keys never drain it. The drain runs
// every pending postponed call (and any calls those queue) to empty — the
// state real top-level input reaches at its own event's unwind.
//
// Guarantee: when a helper returns, every Ui::PostponeCall queued by its
// delivered events has already run, so postponed text fix-ups (for
// example Ui::CreateTonAmountInput's FixTonAmountInput rewrite) are
// settled and the widget shows the product's own rewritten text. The
// drain covers Ui::PostponeCall only: crl::on_main, InvokeQueued,
// base::Timer and network completions still need bounded waits. If a
// drained call destroys the target widget, the helper skips its remaining
// events and returns.

// Synthesizes a full mouse press + release on the widget, at its center by
// default. Drives the same event path as a real click.
void Click(not_null<QWidget*> widget, std::optional<QPoint> point = {});

// Synthesizes key press + release pairs carrying the text, one character at
// a time, into the widget.
void TypeText(not_null<QWidget*> widget, const QString &text);

void PressKey(
	not_null<QWidget*> widget,
	int key,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier);

// Runs every pending Ui::PostponeCall (see the contract above). The input
// helpers call this after each delivered event; call it directly after
// programmatic text mutations (InputField::setText) that queue postponed
// fix-ups of their own.
void SettlePostponedCalls();

} // namespace Test
