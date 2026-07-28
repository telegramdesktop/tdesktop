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

} // namespace Test
