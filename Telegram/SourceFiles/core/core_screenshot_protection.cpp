/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_screenshot_protection.h"

#include "platform/platform_specific.h"

#include <QtWidgets/QApplication>

namespace Core {
namespace {

class WindowFilter final : public QObject {
public:
	explicit WindowFilter(Fn<void(not_null<QWidget*>)> appeared);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	Fn<void(not_null<QWidget*>)> _appeared;

};

WindowFilter::WindowFilter(Fn<void(not_null<QWidget*>)> appeared)
: _appeared(std::move(appeared)) {
}

bool WindowFilter::eventFilter(QObject *watched, QEvent *event) {
	const auto type = event->type();
	if (type == QEvent::WinIdChange || type == QEvent::Show) {
		if (const auto widget = qobject_cast<QWidget*>(watched)) {
			if (widget->isWindow()) {
				_appeared(widget);
			}
		}
	}
	return QObject::eventFilter(watched, event);
}

} // namespace

ScreenshotProtection::ScreenshotProtection() {
	_active.value(
	) | rpl::on_next([=](bool active) {
		apply(active);
	}, _lifetime);
}

ScreenshotProtection::~ScreenshotProtection() = default;

void ScreenshotProtection::addReason(rpl::producer<bool> active) {
	addReason(std::move(active), _lifetime);
}

void ScreenshotProtection::addReason(
		rpl::producer<bool> active,
		rpl::lifetime &lifetime) {
	const auto id = ++_lastReasonId;
	_reasons.emplace(id, false);
	std::move(
		active
	) | rpl::on_next(crl::guard(this, [=](bool value) {
		_reasons[id] = value;
		refresh();
	}), lifetime);
	lifetime.add(crl::guard(this, [=] {
		_reasons.remove(id);
		refresh();
	}));
}

bool ScreenshotProtection::active() const {
	return _active.current();
}

rpl::producer<bool> ScreenshotProtection::activeValue() const {
	return _active.value();
}

void ScreenshotProtection::refresh() {
	_active = ranges::any_of(_reasons, [](const auto &reason) {
		return reason.second;
	});
}

void ScreenshotProtection::apply(bool enabled) {
	if (enabled) {
		_filter = std::make_unique<WindowFilter>([](
				not_null<QWidget*> window) {
			Platform::SetWindowScreenshotProtection(window, true);
		});
		qApp->installEventFilter(_filter.get());
	} else {
		_filter = nullptr;
	}
	for (const auto widget : QApplication::topLevelWidgets()) {
		Platform::SetWindowScreenshotProtection(widget, enabled);
	}
}

} // namespace Core
