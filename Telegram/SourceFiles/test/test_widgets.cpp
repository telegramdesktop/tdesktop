/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_widgets.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>

namespace Test {

QWidget *FindByObjectName(
		not_null<QWidget*> root,
		const QString &name) {
	return root->findChild<QWidget*>(name);
}

void Click(not_null<QWidget*> widget, std::optional<QPoint> point) {
	const auto local = QPointF(point.value_or(widget->rect().center()));
	const auto global = QPointF(widget->mapToGlobal(local.toPoint()));
	auto press = QMouseEvent(
		QEvent::MouseButtonPress,
		local,
		global,
		Qt::LeftButton,
		Qt::LeftButton,
		Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	auto release = QMouseEvent(
		QEvent::MouseButtonRelease,
		local,
		global,
		Qt::LeftButton,
		Qt::NoButton,
		Qt::NoModifier);
	QApplication::sendEvent(widget, &release);
}

void TypeText(not_null<QWidget*> widget, const QString &text) {
	for (const auto &character : text) {
		auto press = QKeyEvent(
			QEvent::KeyPress,
			0,
			Qt::NoModifier,
			QString(character));
		QApplication::sendEvent(widget, &press);
		auto release = QKeyEvent(
			QEvent::KeyRelease,
			0,
			Qt::NoModifier,
			QString(character));
		QApplication::sendEvent(widget, &release);
	}
}

void PressKey(
		not_null<QWidget*> widget,
		int key,
		Qt::KeyboardModifiers modifiers) {
	auto press = QKeyEvent(QEvent::KeyPress, key, modifiers);
	QApplication::sendEvent(widget, &press);
	auto release = QKeyEvent(QEvent::KeyRelease, key, modifiers);
	QApplication::sendEvent(widget, &release);
}

} // namespace Test

#endif // _DEBUG
