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
#include "test/test_capture.h"
#include "test/test_log.h"

#include <QtCore/QPointer>
#include <QtCore/QTextBoundaryFinder>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <qpa/qwindowsysteminterface.h>

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

void DeliverPointerLeave(const base::weak_qptr<QWidget> &widget) {
	auto leave = QEvent(QEvent::Leave);
	DeliverAndSettle(widget, leave);
}

[[nodiscard]] int &ActivationAttempts() {
	static auto result = 0;
	return result;
}

[[nodiscard]] int &ActivationNotes() {
	static auto result = 0;
	return result;
}

// A non-null |widget| whose own window carries no QWindow handle is refused
// rather than silently retargeted at some other top-level: activating a
// window the caller never named would make every field of the returned
// reading a claim about a widget it was not taken on. Only the no-widget
// form may search, because it names no target to be wrong about.
[[nodiscard]] QWindow *ResolveActivationWindow(QWidget *widget) {
	if (widget) {
		const auto top = widget->window();
		return top ? top->windowHandle() : nullptr;
	} else if (const auto active = QGuiApplication::focusWindow()) {
		return active;
	}
	for (const auto window : QGuiApplication::topLevelWindows()) {
		if (window->isVisible()) {
			return window;
		}
	}
	return nullptr;
}

void InjectActivation(QWindow *window) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
	QWindowSystemInterface::handleFocusWindowChanged(
		window,
		Qt::ActiveWindowFocusReason);
#else // Qt >= 6.3.0
	QWindowSystemInterface::handleWindowActivated(
		window,
		Qt::ActiveWindowFocusReason);
#endif // Qt < 6.3.0
	QWindowSystemInterface::flushWindowSystemEvents();
}

[[nodiscard]] QString ActivationWindowIdentity(QWindow *window) {
	if (!window) {
		return u"no window"_q;
	}
	const auto name = window->objectName();
	const auto geometry = window->geometry();
	return u"%1 %2,%3 %4x%5"_q
		.arg(name.isEmpty() ? u"application focus window"_q : name)
		.arg(geometry.x())
		.arg(geometry.y())
		.arg(geometry.width())
		.arg(geometry.height());
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
	DeliverPointerLeave(alive);
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
	DeliverPointerLeave(alive);
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

WindowActivation ReadWindowActivation(QWidget *widget) {
	const auto window = ResolveActivationWindow(widget);
	auto result = WindowActivation();
	result.focusWindowSet = (QGuiApplication::focusWindow() != nullptr);
	result.activeWindow = (QApplication::activeWindow() != nullptr);
	result.attempts = ActivationAttempts();
	result.notes = ActivationNotes();
	result.identity = widget
		? WidgetDescription(widget)
		: ActivationWindowIdentity(window);
	if (window) {
		return result;
	} else if (!widget) {
		result.refusal = u"no visible top-level QWindow to activate"_q;
		return result;
	}
	result.refusal = u"the widget's own window carries no QWindow handle, "
		u"so there is nothing to activate through: %1"_q
		.arg(result.identity);
	return result;
}

WindowActivation ForceWindowActive(QWidget *widget) {
	++ActivationAttempts();
	const auto window = ResolveActivationWindow(widget);
	if (!window) {
		return ReadWindowActivation(widget);
	}
	InjectActivation(window);
	auto result = ReadWindowActivation(widget);
	result.injected = true;
	result.refusal = QString();
	if (((result.attempts - 1) % kActivationNoteEvery) == 0) {
		++ActivationNotes();
		result.notes = ActivationNotes();
		Note(WindowActivationDetails(result));
	}
	return result;
}

WindowActivation ClearWindowActive() {
	InjectActivation(nullptr);
	auto result = ReadWindowActivation(nullptr);
	result.injected = true;
	result.refusal = QString();
	result.identity = u"none - the application focus window was cleared"_q;
	Note(WindowActivationDetails(result));
	return result;
}

QString WindowActivationDetails(const WindowActivation &reading) {
	const auto line = u"window activation: injected=%1 focusWindowSet=%2 "
		u"activeWindow=%3 attempts=%4 notes=%5 target=%6"_q
		.arg(reading.injected ? 1 : 0)
		.arg(reading.focusWindowSet ? 1 : 0)
		.arg(reading.activeWindow ? 1 : 0)
		.arg(reading.attempts)
		.arg(reading.notes)
		.arg(reading.identity);
	return reading.refusal.isEmpty()
		? line
		: (line + u" - %1"_q.arg(reading.refusal));
}

} // namespace Test

#endif // _DEBUG
