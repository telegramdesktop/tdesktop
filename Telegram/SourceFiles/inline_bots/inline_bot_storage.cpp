/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_storage.h"

#include "main/main_session.h"
#include "storage/storage_account.h"

#include <xxhash.h>

namespace InlineBots {
namespace {

constexpr auto kMaxStorageSize = (5 << 20);

[[nodiscard]] uint64 KeyHash(const QString &key) {
	return XXH64(key.data(), key.size(), 0);
}

} // namespace

Storage::Storage(not_null<Main::Session*> session)
: _session(session) {
}

bool Storage::write(
		PeerId botId,
		const QString &key,
		const std::optional<QString> &value) {
	if (value && value->size() > kMaxStorageSize) {
		return false;
	}
	readFromDisk(botId);
	auto i = _lists.find(botId);
	if (i == end(_lists)) {
		if (!value) {
			return true;
		}
		i = _lists.emplace(botId).first;
	}
	auto &list = i->second;
	const auto hash = KeyHash(key);
	auto j = list.data.find(hash);
	if (j == end(list.data)) {
		if (!value) {
			return true;
		}
		j = list.data.emplace(hash).first;
	}
	auto &bykey = j->second;
	const auto k = ranges::find(bykey, key, &Entry::key);
	if (k == end(bykey) && !value) {
		return true;
	}
	const auto size = list.totalSize
		- (k != end(bykey) ? (key.size() + k->value.size()) : 0)
		+ (value ? (key.size() + value->size()) : 0);
	if (size > kMaxStorageSize) {
		return false;
	}
	if (k == end(bykey)) {
		bykey.emplace_back(Entry{ key, *value });
		++list.keysCount;
	} else if (value) {
		k->value = *value;
	} else {
		bykey.erase(k);
		--list.keysCount;
	}
	if (bykey.empty()) {
		list.data.erase(j);
		if (list.data.empty()) {
			Assert(size == 0);
			_lists.erase(i);
		} else {
			list.totalSize = size;
		}
	} else {
		list.totalSize = size;
	}
	saveToDisk(botId);
	return true;
}

std::optional<QString> Storage::read(PeerId botId, const QString &key) {
	readFromDisk(botId);
	const auto i = _lists.find(botId);
	if (i == end(_lists)) {
		return std::nullopt;
	}
	const auto &list = i->second;
	const auto j = list.data.find(KeyHash(key));
	if (j == end(list.data)) {
		return std::nullopt;
	}
	const auto &bykey = j->second;
	const auto k = ranges::find(bykey, key, &Entry::key);
	if (k == end(bykey)) {
		return std::nullopt;
	}
	return k->value;
}

void Storage::clear(PeerId botId) {
	if (_lists.remove(botId)) {
		saveToDisk(botId);
	}
}

void Storage::saveToDisk(PeerId botId) {
	const auto i = _lists.find(botId);
	if (i != end(_lists)) {
		_session->local().writeBotStorage(botId, Serialize(i->second));
	} else {
		_session->local().writeBotStorage(botId, QByteArray());
	}
}

void Storage::readFromDisk(PeerId botId) {
	const auto serialized = _session->local().readBotStorage(botId);
	if (!serialized.isEmpty()) {
		_lists[botId] = Deserialize(serialized);
	}
}

QByteArray Storage::Serialize(const List &list) {
	auto result = QByteArray();
	const auto size = sizeof(quint32)
		+ (list.keysCount * sizeof(quint32))
		+ (list.totalSize * sizeof(ushort));
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		auto count = 0;
		stream.setVersion(QDataStream::Qt_5_1);
		stream << quint32(list.keysCount);
		for (const auto &[hash, bykey] : list.data) {
			for (const auto &entry : bykey) {
				stream << entry.key << entry.value;
				++count;
			}
		}
		Assert(count == list.keysCount);
	}
	return result;
}

Storage::List Storage::Deserialize(const QByteArray &serialized) {
	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);

	auto count = quint32();
	auto result = List();
	stream >> count;
	if (count > kMaxStorageSize) {
		return {};
	}
	for (auto i = 0; i != count; ++i) {
		auto entry = Entry();
		stream >> entry.key >> entry.value;
		const auto hash = KeyHash(entry.key);
		auto j = result.data.find(hash);
		if (j == end(result.data)) {
			j = result.data.emplace(hash).first;
		}
		auto &bykey = j->second;
		const auto k = ranges::find(bykey, entry.key, &Entry::key);
		if (k == end(bykey)) {
			bykey.push_back(entry);
			result.totalSize += entry.key.size() + entry.value.size();
			++result.keysCount;
		}
	}
	return result;
}

} // namespace InlineBots
