/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_line_view_context.h"

namespace Statistic {
namespace {
constexpr auto kAlphaDuration = float64(350);
} // namespace

void ChartLineViewContext::setEnabled(int id, bool enabled, crl::time now) {
	const auto it = _entries.find(id);
	if (it == end(_entries)) {
		_entries[id] = Entry{ .enabled = enabled, .startedAt = now };
	} else if (it->second.enabled != enabled) {
		auto &entry = it->second;
		entry.enabled = enabled;
		entry.startedAt = now
			- kAlphaDuration * (enabled ? entry.alpha : (1. - entry.alpha));
	}
	_isFinished = false;
}

bool ChartLineViewContext::isFinished() const {
	return _isFinished;
}

bool ChartLineViewContext::isEnabled(int id) const {
	const auto it = _entries.find(id);
	return (it == end(_entries)) ? true : it->second.enabled;
}

float64 ChartLineViewContext::alpha(int id) const {
	const auto it = _entries.find(id);
	return (it == end(_entries)) ? 1. : it->second.alpha;
}

void ChartLineViewContext::setCacheImage(int id, QImage &&image) {
	(_isFooter ? _cachesFooter : _caches)[id].image = std::move(image);
}

void ChartLineViewContext::setCacheLastToken(int id, CacheToken token) {
	(_isFooter ? _cachesFooter : _caches)[id].lastToken = token;
}

void ChartLineViewContext::setCacheHQ(int id, bool value) {
	(_isFooter ? _cachesFooter : _caches)[id].hq = value;
}

const ChartLineViewContext::Cache &ChartLineViewContext::cache(int id) {
	[[maybe_unused]] auto unused = (_isFooter ? _cachesFooter : _caches)[id];
	return (_isFooter ? _cachesFooter : _caches).find(id)->second;
}

void ChartLineViewContext::tick(crl::time now) {
	auto finishedCount = 0;
	auto idsToRemove = std::vector<int>();
	for (auto &[id, entry] : _entries) {
		if (!entry.startedAt) {
			continue;
		}
		const auto progress = (now - entry.startedAt) / kAlphaDuration;
		entry.alpha = std::clamp(
			entry.enabled ? progress : (1. - progress),
			0.,
			1.);
		if (entry.alpha == 1.) {
			idsToRemove.push_back(id);
		}
		if (progress >= 1.) {
			finishedCount++;
		}
	}
	_isFinished = (finishedCount == _entries.size());
	for (const auto &id : idsToRemove) {
		_entries.remove(id);
	}
}


} // namespace Statistic
