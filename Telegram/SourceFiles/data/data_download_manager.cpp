/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_download_manager.h"

#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "history/history.h"
#include "history/history_item.h"

namespace Data {
namespace {

constexpr auto ByItem = [](const auto &entry) {
	if constexpr (std::is_same_v<decltype(entry), const DownloadingId&>) {
		return entry.object.item;
	} else {
		const auto resolved = entry.object.get();
		return resolved ? resolved->item : nullptr;
	}
};

[[nodiscard]] uint64 PeerAccessHash(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->accessHash();
	} else if (const auto channel = peer->asChannel()) {
		return channel->access;
	}
	return 0;
}

} // namespace

DownloadManager::DownloadManager() = default;

DownloadManager::~DownloadManager() = default;

void DownloadManager::trackSession(not_null<Main::Session*> session) {
	auto &data = _sessions.emplace(session, SessionData()).first->second;

	session->data().itemRepaintRequest(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return _downloading.contains(item);
	}) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		check(item);
	}, data.lifetime);

	session->data().itemLayoutChanged(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return _downloading.contains(item);
	}) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		check(item);
	}, data.lifetime);

	session->data().itemViewRefreshRequest(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		changed(item);
	}, data.lifetime);

	session->changes().messageUpdates(
		MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const MessageUpdate &update) {
		removed(update.item);
	}, data.lifetime);

	session->account().sessionChanges(
	) | rpl::filter(
		rpl::mappers::_1 != session
	) | rpl::take(1) | rpl::start_with_next([=] {
		untrack(session);
	}, data.lifetime);
}

void DownloadManager::addLoading(DownloadObject object) {
	Expects(object.item != nullptr);
	Expects(object.document || object.photo);

	const auto item = object.item;
	auto &data = sessionData(item);

	const auto already = ranges::find(data.downloading, item, ByItem);
	if (already != end(data.downloading)) {
		const auto document = already->object.document;
		const auto photo = already->object.photo;
		if (document == object.document && photo == object.photo) {
			check(item);
			return;
		}
		remove(data, already);
	}

	const auto size = object.document
		? object.document->size
		: object.photo->imageByteSize(PhotoSize::Large);

	data.downloading.push_back({ .object = object, .total = size });
	_downloading.emplace(item);
	_loadingTotal += size;

	check(item);
}

void DownloadManager::check(not_null<const HistoryItem*> item) {
	auto &data = sessionData(item);
	const auto i = ranges::find(data.downloading, item, ByItem);
	Assert(i != end(data.downloading));
	auto &entry = *i;

	const auto media = item->media();
	const auto photo = media ? media->photo() : nullptr;
	const auto document = media ? media->document() : nullptr;
	if (entry.object.photo != photo || entry.object.document != document) {
		if (const auto document = entry.object.document) {
			document->cancel();
		} else if (const auto photo = entry.object.photo) {
			photo->cancel();
		}
		remove(data, i);
		return;
	}
	// Load with progress only documents for now.
	Assert(document != nullptr);

	const auto path = document->filepath(true);
	if (!path.isEmpty()) {
		addLoaded(entry.object, path, entry.started);
	} else if (!document->loading()) {
		remove(data, i);
	} else {
		const auto total = document->size;
		const auto ready = document->loadOffset();
		if (total == entry.total && ready == entry.ready) {
			return;
		}
		_loadingTotal += (total - entry.total);
		_loadingReady += (ready - entry.ready);
		entry.total = total;
		entry.ready = ready;
	}
}

void DownloadManager::addLoaded(
		DownloadObject object,
		const QString &path,
		DownloadDate started) {
	Expects(object.item != nullptr);
	Expects(object.document || object.photo);

	const auto item = object.item;
	auto &data = sessionData(item);

	const auto id = object.document
		? DownloadId{ object.document->id, DownloadType::Document }
		: DownloadId{ object.photo->id, DownloadType::Photo };
	data.downloaded.push_back({
		.download = id,
		.started = started,
		.path = path,
		.itemId = item->fullId(),
		.peerAccessHash = PeerAccessHash(item->history()->peer),
		.object = std::make_unique<DownloadObject>(object),
	});
	_downloaded.emplace(item);

	const auto i = ranges::find(data.downloading, item, ByItem);
	if (i != end(data.downloading)) {
		auto &entry = *i;
		const auto size = entry.total;
		remove(data, i);
		if (_downloading.empty()) {
			Assert(_loadingTotal == 0 && _loadingReady == 0);
			_loadedTotal = 0;
		} else {
			_loadedTotal += size;
		}
	}
}

void DownloadManager::remove(
		SessionData &data,
		std::vector<DownloadingId>::iterator i) {
	_loadingTotal -= i->total;
	_loadingReady -= i->ready;
	_downloading.remove(i->object.item);
	data.downloading.erase(i);
}

void DownloadManager::changed(not_null<const HistoryItem*> item) {
	if (_downloading.contains(item)) {
		check(item);
	} else if (_downloaded.contains(item)) {
		auto &data = sessionData(item);
		const auto i = ranges::find(data.downloaded, item, ByItem);
		Assert(i != end(data.downloaded));

		const auto media = item->media();
		const auto photo = media ? media->photo() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (i->object->photo != photo || i->object->document != document) {
			*i->object = DownloadObject();

			_downloaded.remove(item);
		}
	}
}

void DownloadManager::removed(not_null<const HistoryItem*> item) {
	if (_downloading.contains(item)) {
		auto &data = sessionData(item);
		const auto i = ranges::find(data.downloading, item, ByItem);
		Assert(i != end(data.downloading));
		auto &entry = *i;

		// We don't want to download files without messages.
		// For example, there is no way to refresh a file reference for them.
		//entry.object.item = nullptr;
		if (const auto document = entry.object.document) {
			document->cancel();
		} else if (const auto photo = entry.object.photo) {
			photo->cancel();
		}
		remove(data, i);
	} else if (_downloaded.contains(item)) {
		auto &data = sessionData(item);
		const auto i = ranges::find(data.downloaded, item, ByItem);
		Assert(i != end(data.downloaded));
		*i->object = DownloadObject();

		_downloaded.remove(item);
	}
}

DownloadManager::SessionData &DownloadManager::sessionData(
		not_null<Main::Session*> session) {
	const auto i = _sessions.find(session);
	Assert(i != end(_sessions));
	return i->second;
}

DownloadManager::SessionData &DownloadManager::sessionData(
		not_null<const HistoryItem*> item) {
	return sessionData(&item->history()->session());
}

void DownloadManager::untrack(not_null<Main::Session*> session) {
	const auto i = _sessions.find(session);
	Assert(i != end(_sessions));

	for (const auto &entry : i->second.downloaded) {
		if (const auto resolved = entry.object.get()) {
			if (const auto item = resolved->item) {
				_downloaded.remove(item);
			}
		}
	}
	while (!i->second.downloading.empty()) {
		remove(i->second, i->second.downloading.end() - 1);
	}
	_sessions.erase(i);
}

} // namespace Data
