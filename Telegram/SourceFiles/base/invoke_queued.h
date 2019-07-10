/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QEvent>
#include <QtCore/QCoreApplication>

#include "base/basic_types.h"

namespace base {

class InvokeQueuedEvent : public QEvent {
public:
	static constexpr auto kType = QEvent::Type(60666);

	explicit InvokeQueuedEvent(FnMut<void()> &&method)
	: QEvent(kType)
	, _method(std::move(method)) {
	}

	void invoke() {
		_method();
	}

private:
	FnMut<void()> _method;

};

} // namespace base

template <typename Lambda>
inline void InvokeQueued(const QObject *context, Lambda &&lambda) {
	QCoreApplication::postEvent(
		const_cast<QObject*>(context),
		new base::InvokeQueuedEvent(std::forward<Lambda>(lambda)));
}
