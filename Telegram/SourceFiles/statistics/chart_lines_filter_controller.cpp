/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_lines_filter_controller.h"

namespace Statistic {

LinesFilterController::LinesFilterController() = default;

void LinesFilterController::setEnabled(int id, bool enabled, crl::time now) {
	const auto it = _entries.find(id);
	if (it == end(_entries)) {
		_entries[id] = Entry{
			.enabled = enabled,
			.startedAt = now,
			.anim = anim::value(enabled ? 0. : 1., enabled ? 1. : 0.),
		};
	} else if (it->second.enabled != enabled) {
		auto &entry = it->second;
		entry.enabled = enabled;
		entry.startedAt = now;
		entry.dtCurrent = 0.;
		entry.anim.start(enabled ? 1. : 0.);
	}
	_isFinished = false;
}

bool LinesFilterController::isFinished() const {
	return _isFinished;
}

bool LinesFilterController::isEnabled(int id) const {
	const auto it = _entries.find(id);
	return (it == end(_entries)) ? true : it->second.enabled;
}

float64 LinesFilterController::alpha(int id) const {
	const auto it = _entries.find(id);
	return (it == end(_entries)) ? 1. : it->second.alpha;
}

void LinesFilterController::tick(float64 dtSpeed) {
	auto finishedCount = 0;
	auto idsToRemove = std::vector<int>();
	for (auto &[id, entry] : _entries) {
		if (!entry.startedAt) {
			continue;
		}
		entry.dtCurrent = std::min(entry.dtCurrent + dtSpeed, 1.);
		entry.anim.update(entry.dtCurrent, anim::easeInCubic);
		const auto progress = entry.anim.current();
		entry.alpha = std::clamp(progress, 0., 1.);
		if ((entry.alpha == 1.) && entry.enabled) {
			idsToRemove.push_back(id);
		}
		if (entry.anim.current() == entry.anim.to()) {
			finishedCount++;
			entry.anim.finish();
		}
	}
	_isFinished = (finishedCount == _entries.size());
	for (const auto &id : idsToRemove) {
		_entries.remove(id);
	}
}

} // namespace Statistic
