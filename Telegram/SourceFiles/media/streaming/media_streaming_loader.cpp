/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_loader.h"

namespace Media {
namespace Streaming {

bool LoadedPart::valid(int64 size) const {
	return (offset != kFailedOffset)
		&& ((bytes.size() == Loader::kPartSize)
			|| (offset + bytes.size() == size));
}

bool operator<(
		const PriorityQueue::Entry &a,
		const PriorityQueue::Entry &b) {
	if (a.priority > b.priority) {
		return true;
	} else if (a.priority < b.priority) {
		return false;
	} else {
		return a.value < b.value;
	}
}

bool PriorityQueue::add(int64 value) {
	const auto i = ranges::find(_data, value, &Entry::value);
	if (i == end(_data)) {
		_data.insert({ value, _priority });
		return true;
	} else if (i->priority != _priority) {
		_data.erase(i);
		_data.insert({ value, _priority });
		return true;
	}
	return false;
}

bool PriorityQueue::remove(int64 value) {
	const auto i = ranges::find(_data, value, &Entry::value);
	if (i == end(_data)) {
		return false;
	}
	_data.erase(i);
	return true;
}

bool PriorityQueue::empty() const {
	return _data.empty();
}

std::optional<int64> PriorityQueue::front() const {
	return _data.empty()
		? std::nullopt
		: std::make_optional(_data.front().value);
}

std::optional<int64> PriorityQueue::take() {
	if (_data.empty()) {
		return std::nullopt;
	}
	const auto result = _data.front().value;
	_data.erase(_data.begin());
	return result;
}

base::flat_set<int64> PriorityQueue::takeInRange(int64 from, int64 till) {
	auto result = base::flat_set<int64>();
	for (auto i = _data.begin(); i != _data.end();) {
		if (i->value >= from && i->value < till) {
			result.emplace(i->value);
			i = _data.erase(i);
		} else {
			++i;
		}
	}
	return result;
}

void PriorityQueue::clear() {
	_data.clear();
}

void PriorityQueue::resetPriorities() {
	++_priority;
}

} // namespace Streaming
} // namespace Media
