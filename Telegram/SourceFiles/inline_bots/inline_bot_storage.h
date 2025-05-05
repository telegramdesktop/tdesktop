/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/attach/attach_bot_webview.h"

namespace Main {
class Session;
} // namespace Main

namespace InlineBots {

class Storage final {
public:
	explicit Storage(not_null<Main::Session*> session);

	bool write(
		PeerId botId,
		const QString &key,
		const std::optional<QString> &value);
	std::optional<QString> read(PeerId botId, const QString &key);
	void clear(PeerId botId);

private:
	struct Entry {
		QString key;
		QString value;
	};
	struct List {
		base::flat_map<uint64, std::vector<Entry>> data;
		int keysCount = 0;
		int totalSize = 0;
	};

	void saveToDisk(PeerId botId);
	void readFromDisk(PeerId botId);

	[[nodiscard]] static QByteArray Serialize(const List &list);
	[[nodiscard]] static List Deserialize(const QByteArray &serialized);

	const not_null<Main::Session*> _session;

	base::flat_map<PeerId, List> _lists;

};

} // namespace InlineBots
