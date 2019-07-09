/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <list>
#include <unordered_map>

namespace base {

template <typename Entry>
class last_used_cache {
public:
	void up(Entry entry);
	void remove(Entry entry);
	void clear();

	Entry take_lowest();

private:
	std::list<Entry> _queue;
	std::unordered_map<Entry, typename std::list<Entry>::iterator> _map;

};

template <typename Entry>
void last_used_cache<Entry>::up(Entry entry) {
	if (!_queue.empty() && _queue.back() == entry) {
		return;
	}
	const auto i = _map.find(entry);
	if (i != end(_map)) {
		_queue.splice(end(_queue), _queue, i->second);
	} else {
		_map.emplace(entry, _queue.insert(end(_queue), entry));
	}
}

template <typename Entry>
void last_used_cache<Entry>::remove(Entry entry) {
	const auto i = _map.find(entry);
	if (i != end(_map)) {
		_queue.erase(i->second);
		_map.erase(i);
	}
}

template <typename Entry>
void last_used_cache<Entry>::clear() {
	_queue.clear();
	_map.clear();
}

template <typename Entry>
Entry last_used_cache<Entry>::take_lowest() {
	if (_queue.empty()) {
		return Entry();
	}
	auto result = std::move(_queue.front());
	_queue.erase(begin(_queue));
	_map.erase(result);
	return result;
}

} // namespace base
