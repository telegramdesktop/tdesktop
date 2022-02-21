/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Data {

enum class DownloadType {
	Document,
	Photo,
};

// unixtime * 1000.
using DownloadDate = int64;

struct DownloadId {
	uint64 objectId = 0;
	DownloadType type = DownloadType::Document;
};

struct DownloadObject {
	HistoryItem *item = nullptr;
	DocumentData *document = nullptr;
	PhotoData *photo = nullptr;
};

struct DownloadedId {
	DownloadId download;
	DownloadDate started = 0;
	QString path;
	FullMsgId itemId;
	uint64 peerAccessHash = 0;

	std::unique_ptr<DownloadObject> object;

};

struct DownloadingId {
	DownloadObject object;
	DownloadDate started = 0;
	int ready = 0;
	int total = 0;
};

class DownloadManager final {
public:
	DownloadManager();
	~DownloadManager();

	void trackSession(not_null<Main::Session*> session);

	void addLoading(DownloadObject object);
	void addLoaded(
		DownloadObject object,
		const QString &path,
		DownloadDate started);

private:
	struct SessionData {
		std::vector<DownloadedId> downloaded;
		std::vector<DownloadingId> downloading;
		rpl::lifetime lifetime;
	};

	void check(not_null<const HistoryItem*> item);
	void changed(not_null<const HistoryItem*> item);
	void removed(not_null<const HistoryItem*> item);
	void untrack(not_null<Main::Session*> session);
	void remove(
		SessionData &data,
		std::vector<DownloadingId>::iterator i);

	[[nodiscard]] SessionData &sessionData(not_null<Main::Session*> session);
	[[nodiscard]] SessionData &sessionData(
		not_null<const HistoryItem*> item);

	base::flat_map<not_null<Main::Session*>, SessionData> _sessions;
	base::flat_set<not_null<const HistoryItem*>> _downloading;
	base::flat_set<not_null<const HistoryItem*>> _downloaded;

	int64 _loadedTotal = 0;
	int64 _loadingReady = 0;
	int64 _loadingTotal = 0;

};

} // namespace Data
