/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_widgets.h"

#include "base/flat_map.h"
#include "base/weak_qptr.h"
#include "core/sandbox.h"
#include "test/test_agent.h"

#include <QtCore/QPointer>
#include <QtCore/QTextBoundaryFinder>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>

namespace Test {
namespace {

struct LiveWidgetEntry {
	QPointer<QWidget> widget;
	int generation = 0;
};

struct LiveActionEntry {
	QPointer<QObject> context;
	std::shared_ptr<Fn<void()>> action;
	int generation = 0;
	int invocationCount = 0;
	bool repeatable = false;
};

[[nodiscard]] base::flat_map<QString, LiveWidgetEntry> &LiveWidgets() {
	static auto result = base::flat_map<QString, LiveWidgetEntry>();
	return result;
}

[[nodiscard]] base::flat_map<QString, LiveActionEntry> &LiveActions() {
	static auto result = base::flat_map<QString, LiveActionEntry>();
	return result;
}

bool DeliverAndSettle(
		const base::weak_qptr<QWidget> &widget,
		QEvent &event) {
	const auto strong = widget.get();
	if (!strong) {
		return false;
	}
	Settle([&] {
		QApplication::sendEvent(strong, &event);
	});
	return (widget.get() != nullptr);
}

} // namespace

QWidget *FindByObjectName(
		not_null<QWidget*> root,
		const QString &name) {
	return root->findChild<QWidget*>(name);
}

void PublishLiveWidget(
		const QString &key,
		not_null<QWidget*> widget) {
	if (!Active()) {
		return;
	}
	auto &entry = LiveWidgets()[key];
	entry = {
		.widget = widget.get(),
		.generation = entry.generation + 1,
	};
}

LiveWidgetSnapshot ReadLiveWidget(const QString &key) {
	const auto i = LiveWidgets().find(key);
	if (i == end(LiveWidgets())) {
		return {};
	}
	return {
		.widget = i->second.widget.data(),
		.generation = i->second.generation,
	};
}

void PublishLiveAction(
		const QString &key,
		not_null<QObject*> context,
		Fn<void()> action,
		bool repeatable) {
	if (!Active()) {
		return;
	}
	const auto callback = action
		? std::make_shared<Fn<void()>>(std::move(action))
		: std::shared_ptr<Fn<void()>>();
	auto &entry = LiveActions()[key];
	entry = {
		.context = context.get(),
		.action = callback,
		.generation = entry.generation + 1,
		.invocationCount = 0,
		.repeatable = repeatable,
	};
}

LiveActionSnapshot ReadLiveAction(const QString &key) {
	const auto i = LiveActions().find(key);
	if (i == end(LiveActions())) {
		return {};
	}
	const auto &entry = i->second;
	return {
		.available = entry.context
			&& entry.action
			&& (entry.repeatable || !entry.invocationCount),
		.generation = entry.generation,
		.invocationCount = entry.invocationCount,
		.repeatable = entry.repeatable,
	};
}

bool InvokeLiveAction(const QString &key) {
	auto i = LiveActions().find(key);
	if (i == end(LiveActions())) {
		return false;
	}
	auto &entry = i->second;
	if (!entry.context
		|| !entry.action
		|| (!entry.repeatable && entry.invocationCount)) {
		return false;
	}
	++entry.invocationCount;
	const auto action = entry.action;
	(*action)();
	return true;
}

void Click(not_null<QWidget*> widget, std::optional<QPoint> point) {
	const auto alive = base::make_weak(widget);
	const auto local = QPointF(point.value_or(widget->rect().center()));
	const auto global = QPointF(widget->mapToGlobal(local.toPoint()));
	auto press = QMouseEvent(
		QEvent::MouseButtonPress,
		local,
		global,
		Qt::LeftButton,
		Qt::LeftButton,
		Qt::NoModifier);
	if (!DeliverAndSettle(alive, press)) {
		return;
	}
	auto release = QMouseEvent(
		QEvent::MouseButtonRelease,
		local,
		global,
		Qt::LeftButton,
		Qt::NoButton,
		Qt::NoModifier);
	DeliverAndSettle(alive, release);
}

void TypeText(not_null<QWidget*> widget, const QString &text) {
	const auto alive = base::make_weak(widget);
	auto finder = QTextBoundaryFinder(QTextBoundaryFinder::Grapheme, text);
	finder.toStart();
	auto from = 0;
	while (from < text.size()) {
		const auto till = finder.toNextBoundary();
		if (till < 0) {
			break;
		}
		const auto grapheme = text.mid(from, till - from);
		auto press = QKeyEvent(
			QEvent::KeyPress,
			0,
			Qt::NoModifier,
			grapheme);
		if (!DeliverAndSettle(alive, press)) {
			return;
		}
		auto release = QKeyEvent(
			QEvent::KeyRelease,
			0,
			Qt::NoModifier,
			grapheme);
		if (!DeliverAndSettle(alive, release)) {
			return;
		}
		from = till;
	}
}

void CommitText(not_null<QWidget*> widget, const QString &text) {
	const auto alive = base::make_weak(widget);
	auto event = QInputMethodEvent();
	event.setCommitString(text);
	DeliverAndSettle(alive, event);
}

void Drag(
		not_null<QWidget*> widget,
		QPoint from,
		QPoint to,
		int steps) {
	const auto alive = base::make_weak(widget);
	const auto makeEvent = [&](QEvent::Type type, QPoint local, auto button) {
		return QMouseEvent(
			type,
			QPointF(local),
			QPointF(widget->mapToGlobal(local)),
			button,
			(type == QEvent::MouseButtonRelease)
				? Qt::NoButton
				: Qt::LeftButton,
			Qt::NoModifier);
	};
	auto press = makeEvent(QEvent::MouseButtonPress, from, Qt::LeftButton);
	if (!DeliverAndSettle(alive, press)) {
		return;
	}
	steps = std::max(steps, 1);
	for (auto step = 1; step <= steps; ++step) {
		const auto local = from + ((to - from) * step) / steps;
		auto move = makeEvent(QEvent::MouseMove, local, Qt::NoButton);
		if (!DeliverAndSettle(alive, move)) {
			return;
		}
	}
	auto release = makeEvent(QEvent::MouseButtonRelease, to, Qt::LeftButton);
	DeliverAndSettle(alive, release);
}

void Wheel(
		not_null<QWidget*> widget,
		QPoint angleDelta,
		std::optional<QPoint> point) {
	const auto alive = base::make_weak(widget);
	const auto local = QPointF(point.value_or(widget->rect().center()));
	const auto global = QPointF(widget->mapToGlobal(local.toPoint()));
	auto event = QWheelEvent(
		local,
		global,
		QPoint(),
		angleDelta,
		Qt::NoButton,
		Qt::NoModifier,
		Qt::NoScrollPhase,
		false,
		Qt::MouseEventSynthesizedByApplication);
	DeliverAndSettle(alive, event);
}

void PressKey(
		not_null<QWidget*> widget,
		int key,
		Qt::KeyboardModifiers modifiers) {
	const auto alive = base::make_weak(widget);
	auto press = QKeyEvent(QEvent::KeyPress, key, modifiers);
	if (!DeliverAndSettle(alive, press)) {
		return;
	}
	auto release = QKeyEvent(QEvent::KeyRelease, key, modifiers);
	DeliverAndSettle(alive, release);
}

void Settle(Fn<void()> action) {
	auto &sandbox = Core::Sandbox::Instance();
	sandbox.setPostponedCallsDeferred(true);
	{
		const auto guard = gsl::finally([&] {
			sandbox.setPostponedCallsDeferred(false);
		});
		action();
	}
	SettlePostponedCalls();
}

void SettlePostponedCalls() {
	Core::Sandbox::Instance().drainPostponedCalls();
}

} // namespace Test

#endif // _DEBUG
