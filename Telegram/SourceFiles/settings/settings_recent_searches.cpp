/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_recent_searches.h"

#include "core/version.h"
#include "main/main_session.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"

namespace Settings {
namespace {

constexpr auto kLimit = 32;

} // namespace

RecentSearches::RecentSearches(not_null<Main::Session*> session)
: _session(session) {
}

RecentSearches::~RecentSearches() = default;

const std::vector<QString> &RecentSearches::list() const {
	_session->local().readSearchSuggestions();

	return _list;
}

void RecentSearches::bump(const QString &entryId) {
	if (entryId.isEmpty()) {
		return;
	}
	_session->local().readSearchSuggestions();

	if (!_list.empty() && _list.front() == entryId) {
		return;
	}
	auto i = ranges::find(_list, entryId);
	if (i == end(_list)) {
		if (int(_list.size()) >= kLimit) {
			_list.pop_back();
		}
		_list.push_back(entryId);
		i = end(_list) - 1;
	}
	ranges::rotate(begin(_list), i, i + 1);

	_session->local().writeSearchSuggestionsDelayed();
}

void RecentSearches::remove(const QString &entryId) {
	const auto i = ranges::find(_list, entryId);
	if (i != end(_list)) {
		_list.erase(i);
	}
	_session->local().writeSearchSuggestionsDelayed();
}

QByteArray RecentSearches::serialize() const {
	_session->local().readSearchSuggestions();

	if (_list.empty()) {
		return {};
	}
	const auto count = std::min(int(_list.size()), kLimit);
	auto size = 2 * int(sizeof(quint32));
	for (auto i = 0; i < count; ++i) {
		size += Serialize::stringSize(_list[i]);
	}
	auto stream = Serialize::ByteArrayWriter(size);
	stream
		<< quint32(AppVersion)
		<< quint32(count);
	for (auto i = 0; i < count; ++i) {
		stream << _list[i];
	}
	return std::move(stream).result();
}

void RecentSearches::applyLocal(QByteArray serialized) {
	_list.clear();
	if (serialized.isEmpty()) {
		return;
	}
	auto stream = Serialize::ByteArrayReader(serialized);
	auto version = quint32();
	auto count = quint32();
	stream >> version >> count;
	if (!stream.ok()) {
		return;
	}
	_list.reserve(count);
	for (auto i = quint32(0); i < count; ++i) {
		auto value = QString();
		stream >> value;
		if (!stream.ok()) {
			_list.clear();
			return;
		}
		_list.push_back(value);
	}
}

} // namespace Settings
