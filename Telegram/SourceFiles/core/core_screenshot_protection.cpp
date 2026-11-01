/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_screenshot_protection.h"

#include "base/event_filter.h"
#include "platform/platform_specific.h"

namespace Core {

ScreenshotProtection::ScreenshotProtection() = default;

ScreenshotProtection::~ScreenshotProtection() = default;

void ScreenshotProtection::addContentReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime) {
	addReason(window, std::move(active), lifetime);
}

void ScreenshotProtection::addAmbientReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime) {
	if (Platform::AmbientScreenshotProtectionSupported()) {
		addReason(window, std::move(active), lifetime);
	}
}

void ScreenshotProtection::addReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime) {
	const auto id = ++_lastReasonId;
	_reasons.emplace(id, Reason{ window.get(), false });
	std::move(
		active
	) | rpl::on_next(crl::guard(this, [=](bool value) {
		_reasons[id].active = value;
		refresh();
	}), lifetime);
	base::install_event_filter(window, crl::guard(this, [=](
			not_null<QEvent*> event) {
		if (event->type() == QEvent::WinIdChange) {
			apply(window, true);
		}
		return base::EventFilterResult::Continue;
	}), lifetime);
	lifetime.add(crl::guard(this, [=] {
		_reasons.remove(id);
		refresh();
	}));
}

bool ScreenshotProtection::activeFor(not_null<QWidget*> window) const {
	for (const auto &[id, reason] : _reasons) {
		if (reason.active && reason.window == window.get()) {
			return true;
		}
	}
	return false;
}

void ScreenshotProtection::apply(not_null<QWidget*> window, bool force) {
	const auto enabled = activeFor(window);
	const auto i = _applied.find(window);
	if (!force && i != _applied.end() && i->second.enabled == enabled) {
		return;
	}
	Platform::SetWindowScreenshotProtection(window, enabled);
	if (i == _applied.end()) {
		_applied.emplace(window, Applied{ window.get(), enabled });
	} else {
		i->second.enabled = enabled;
	}
}

void ScreenshotProtection::refresh() {
	auto desired = base::flat_map<not_null<QWidget*>, bool>();
	for (const auto &[id, reason] : _reasons) {
		if (const auto window = reason.window.data()) {
			auto &state = desired[not_null{ window }];
			state = state || reason.active;
		}
	}
	for (auto i = _applied.begin(); i != _applied.end();) {
		if (i->second.window.data()) {
			++i;
		} else {
			i = _applied.erase(i);
		}
	}
	auto windows = desired;
	for (const auto &[widget, applied] : _applied) {
		windows.emplace(widget, false);
	}
	for (const auto &[widget, enabled] : windows) {
		apply(widget);
	}
}

} // namespace Core
