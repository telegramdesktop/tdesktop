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
#include "base/unixtime.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message.h"
#include "core/application.h"
#include "ui/controls/download_bar.h"

namespace Data {
namespace {

constexpr auto kClearLoadingTimeout = 5 * crl::time(1000);

constexpr auto ByItem = [](const auto &entry) {
	if constexpr (std::is_same_v<decltype(entry), const DownloadingId&>) {
		return entry.object.item;
	} else {
		const auto resolved = entry.object.get();
		return resolved ? resolved->item.get() : nullptr;
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

DownloadManager::DownloadManager()
: _clearLoadingTimer([=] { clearLoading(); }) {
}

DownloadManager::~DownloadManager() = default;

void DownloadManager::trackSession(not_null<Main::Session*> session) {
	auto &data = _sessions.emplace(session, SessionData()).first->second;

	session->data().itemRepaintRequest(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return _loading.contains(item);
	}) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		check(item);
	}, data.lifetime);

	session->data().itemLayoutChanged(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return _loading.contains(item);
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

int64 DownloadManager::computeNextStarted() {
	const auto now = base::unixtime::now();
	if (_lastStartedBase != now) {
		_lastStartedBase = now;
		_lastStartedAdded = 0;
	} else {
		++_lastStartedAdded;
	}
	return int64(_lastStartedBase) * 1000 + _lastStartedAdded;
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

	data.downloading.push_back({
		.object = object,
		.started = computeNextStarted(),
		.total = size,
	});
	_loading.emplace(item);
	_loadingProgress = DownloadProgress{
		.ready = _loadingProgress.current().ready,
		.total = _loadingProgress.current().total + size,
	};
	_loadingListChanges.fire({});
	_clearLoadingTimer.cancel();

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
		cancel(data, i);
		return;
	}
	// Load with progress only documents for now.
	Assert(document != nullptr);

	const auto path = document->filepath(true);
	if (!path.isEmpty()) {
		if (_loading.contains(item)) {
			addLoaded(entry.object, path, entry.started);
		}
	} else if (!document->loading()) {
		remove(data, i);
	} else {
		const auto totalChange = document->size - entry.total;
		const auto readyChange = document->loadOffset() - entry.ready;
		if (!readyChange && !totalChange) {
			return;
		}
		entry.ready += readyChange;
		entry.total += totalChange;
		_loadingProgress = DownloadProgress{
			.ready = _loadingProgress.current().ready + readyChange,
			.total = _loadingProgress.current().total + totalChange,
		};
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
	_loaded.emplace(item);
	_loadedAdded.fire(&data.downloaded.back());

	const auto i = ranges::find(data.downloading, item, ByItem);
	if (i != end(data.downloading)) {
		auto &entry = *i;
		const auto j = _loading.find(entry.object.item);
		if (j == end(_loading)) {
			return;
		}
		const auto document = entry.object.document;
		const auto totalChange = document->size - entry.total;
		const auto readyChange = document->size - entry.ready;
		entry.ready += readyChange;
		entry.total += totalChange;
		entry.done = true;
		_loading.erase(j);
		_loadingDone.emplace(entry.object.item);
		_loadingProgress = DownloadProgress{
			.ready = _loadingProgress.current().ready + readyChange,
			.total = _loadingProgress.current().total + totalChange,
		};
		if (_loading.empty()) {
			_clearLoadingTimer.callOnce(kClearLoadingTimeout);
		}
	}
}

auto DownloadManager::loadingList() const
-> ranges::any_view<const DownloadingId*, ranges::category::input> {
	return ranges::views::all(
		_sessions
	) | ranges::views::transform([=](const auto &pair) {
		return ranges::views::all(
			pair.second.downloading
		) | ranges::views::transform([](const DownloadingId &id) {
			return &id;
		});
	}) | ranges::views::join;
}

DownloadProgress DownloadManager::loadingProgress() const {
	return _loadingProgress.current();
}

rpl::producer<> DownloadManager::loadingListChanges() const {
	return _loadingListChanges.events();
}

auto DownloadManager::loadingProgressValue() const
-> rpl::producer<DownloadProgress> {
	return _loadingProgress.value();
}

void DownloadManager::clearLoading() {
	Expects(_loading.empty());

	for (auto &[session, data] : _sessions) {
		while (!data.downloading.empty()) {
			remove(data, data.downloading.end() - 1);
		}
	}
}

auto DownloadManager::loadedList() const
-> ranges::any_view<const DownloadedId*, ranges::category::input> {
	return ranges::views::all(
		_sessions
	) | ranges::views::transform([=](const auto &pair) {
		return ranges::views::all(
			pair.second.downloaded
		) | ranges::views::filter([](const DownloadedId &id) {
			return (id.object != nullptr);
		}) | ranges::views::transform([](const DownloadedId &id) {
			return &id;
		});
	}) | ranges::views::join;
}

auto DownloadManager::loadedAdded() const
-> rpl::producer<not_null<const DownloadedId*>> {
	return _loadedAdded.events();
}

auto DownloadManager::loadedRemoved() const
-> rpl::producer<not_null<const HistoryItem*>> {
	return _loadedRemoved.events();
}

void DownloadManager::remove(
		SessionData &data,
		std::vector<DownloadingId>::iterator i) {
	const auto now = DownloadProgress{
		.ready = _loadingProgress.current().ready - i->ready,
		.total = _loadingProgress.current().total - i->total,
	};
	_loading.remove(i->object.item);
	_loadingDone.remove(i->object.item);
	data.downloading.erase(i);
	_loadingListChanges.fire({});
	_loadingProgress = now;
	if (_loading.empty() && !_loadingDone.empty()) {
		_clearLoadingTimer.callOnce(kClearLoadingTimeout);
	}
}

void DownloadManager::cancel(
		SessionData &data,
		std::vector<DownloadingId>::iterator i) {
	const auto object = i->object;
	remove(data, i);
	if (const auto document = object.document) {
		document->cancel();
	} else if (const auto photo = object.photo) {
		photo->cancel();
	}
}

void DownloadManager::changed(not_null<const HistoryItem*> item) {
	if (_loaded.contains(item)) {
		auto &data = sessionData(item);
		const auto i = ranges::find(data.downloaded, item.get(), ByItem);
		Assert(i != end(data.downloaded));

		const auto media = item->media();
		const auto photo = media ? media->photo() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (i->object->photo != photo || i->object->document != document) {
			detach(*i);
		}
	}
	if (_loading.contains(item) || _loadingDone.contains(item)) {
		check(item);
	}
}

void DownloadManager::removed(not_null<const HistoryItem*> item) {
	if (_loaded.contains(item)) {
		auto &data = sessionData(item);
		const auto i = ranges::find(data.downloaded, item.get(), ByItem);
		Assert(i != end(data.downloaded));
		detach(*i);
	}
	if (_loading.contains(item) || _loadingDone.contains(item)) {
		auto &data = sessionData(item);
		const auto i = ranges::find(data.downloading, item, ByItem);
		Assert(i != end(data.downloading));
		auto &entry = *i;

		// We don't want to download files without messages.
		// For example, there is no way to refresh a file reference for them.
		//entry.object.item = nullptr;
		cancel(data, i);
	}
}

not_null<HistoryItem*> DownloadManager::generateItem(
		const DownloadObject &object) {
	Expects(object.document || object.photo);

	const auto session = object.document
		? &object.document->session()
		: &object.photo->session();
	const auto fromId = object.item
		? object.item->from()->id
		: session->userPeerId();
	const auto history = object.item
		? object.item->history()
		: session->data().history(session->user());
	const auto flags = MessageFlag::FakeHistoryItem;
	const auto replyTo = MsgId();
	const auto viaBotId = UserId();
	const auto groupedId = uint64();
	const auto date = base::unixtime::now();
	const auto postAuthor = QString();
	const auto caption = TextWithEntities();
	const auto make = [&](const auto media) {
		return history->makeMessage(
			history->nextNonHistoryEntryId(),
			flags,
			replyTo,
			viaBotId,
			date,
			fromId,
			QString(),
			media,
			caption,
			HistoryMessageMarkupData());
	};
	const auto result = object.document
		? make(object.document)
		: make(object.photo);
	_generated.emplace(result);
	return result;
}

void DownloadManager::detach(DownloadedId &id) {
	Expects(id.object != nullptr);
	Expects(_loaded.contains(id.object->item));
	Expects(!_generated.contains(id.object->item));

	// Maybe generate new document?
	const auto was = id.object->item;
	const auto now = generateItem(*id.object);
	_loaded.remove(was);
	_loaded.emplace(now);
	id.object->item = now;

	_loadedRemoved.fire_copy(was);
	_loadedAdded.fire_copy(&id);
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
				_loaded.remove(item);
			}
		}
	}
	while (!i->second.downloading.empty()) {
		remove(i->second, i->second.downloading.end() - 1);
	}
	_sessions.erase(i);
}

rpl::producer<Ui::DownloadBarProgress> MakeDownloadBarProgress() {
	return Core::App().downloadManager().loadingProgressValue(
	) | rpl::map([=](const DownloadProgress &progress) {
		return Ui::DownloadBarProgress{
			.ready = progress.ready,
			.total = progress.total,
		};
	});
}

rpl::producer<Ui::DownloadBarContent> MakeDownloadBarContent() {
	auto &manager = Core::App().downloadManager();
	return rpl::single(
		rpl::empty_value()
	) | rpl::then(
		manager.loadingListChanges() | rpl::to_empty
	) | rpl::map([=, &manager] {
		auto result = Ui::DownloadBarContent();
		for (const auto id : manager.loadingList()) {
			if (result.singleName.isEmpty()) {
				result.singleName = id->object.document->filename();
				result.singleThumbnail = QImage();
			}
			++result.count;
			if (id->done) {
				++result.done;
			}
		}
		return result;
	});
}

} // namespace Data
