/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_widgets.h"

#include "base/weak_qptr.h"
#include "core/sandbox.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>

namespace Test {
namespace {

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
	for (const auto &character : text) {
		auto press = QKeyEvent(
			QEvent::KeyPress,
			0,
			Qt::NoModifier,
			QString(character));
		if (!DeliverAndSettle(alive, press)) {
			return;
		}
		auto release = QKeyEvent(
			QEvent::KeyRelease,
			0,
			Qt::NoModifier,
			QString(character));
		if (!DeliverAndSettle(alive, release)) {
			return;
		}
	}
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
