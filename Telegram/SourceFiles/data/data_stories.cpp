/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_stories.h"

#include "api/api_report.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_folder.h"
#include "data/data_photo.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/show.h"
#include "ui/text/text_utilities.h"

namespace Data {
namespace {

constexpr auto kMaxResolveTogether = 100;
constexpr auto kIgnorePreloadAroundIfLoaded = 15;
constexpr auto kPreloadAroundCount = 30;
constexpr auto kMarkAsReadDelay = 3 * crl::time(1000);
constexpr auto kIncrementViewsDelay = 5 * crl::time(1000);
constexpr auto kArchiveFirstPerPage = 30;
constexpr auto kArchivePerPage = 100;
constexpr auto kSavedFirstPerPage = 30;
constexpr auto kSavedPerPage = 100;
constexpr auto kMaxPreloadSources = 10;
constexpr auto kStillPreloadFromFirst = 3;
constexpr auto kMaxSegmentsCount = 180;
constexpr auto kPollingIntervalChat = 5 * TimeId(60);
constexpr auto kPollingIntervalViewer = 1 * TimeId(60);
constexpr auto kPollViewsInterval = 10 * crl::time(1000);
constexpr auto kPollingViewsPerPage = Story::kRecentViewersMax;

using UpdateFlag = StoryUpdate::Flag;

[[nodiscard]] std::optional<StoryMedia> ParseMedia(
		not_null<Session*> owner,
		const MTPMessageMedia &media) {
	return media.match([&](const MTPDmessageMediaPhoto &data)
		-> std::optional<StoryMedia> {
		if (const auto photo = data.vphoto()) {
			const auto result = owner->processPhoto(*photo);
			if (!result->isNull()) {
				return StoryMedia{ result };
			}
		}
		return {};
	}, [&](const MTPDmessageMediaDocument &data)
		-> std::optional<StoryMedia> {
		if (const auto document = data.vdocument()) {
			const auto result = owner->processDocument(*document);
			if (!result->isNull()
				&& (result->isGifv() || result->isVideoFile())) {
				result->setStoryMedia(true);
				return StoryMedia{ result };
			}
		}
		return {};
	}, [&](const MTPDmessageMediaUnsupported &data) {
		return std::make_optional(StoryMedia{ v::null });
	}, [](const auto &) { return std::optional<StoryMedia>(); });
}

} // namespace

StoriesSourceInfo StoriesSource::info() const {
	return {
		.id = peer->id,
		.last = ids.empty() ? 0 : ids.back().date,
		.count = uint32(std::min(int(ids.size()), kMaxSegmentsCount)),
		.unreadCount = uint32(std::min(unreadCount(), kMaxSegmentsCount)),
		.premium = (peer->isUser() && peer->asUser()->isPremium()) ? 1U : 0,
	};
}

int StoriesSource::unreadCount() const {
	const auto i = ids.lower_bound(StoryIdDates{ .id = readTill + 1 });
	return int(end(ids) - i);
}

StoryIdDates StoriesSource::toOpen() const {
	if (ids.empty()) {
		return {};
	}
	const auto i = ids.lower_bound(StoryIdDates{ readTill + 1 });
	return (i != end(ids)) ? *i : ids.front();
}

Stories::Stories(not_null<Session*> owner)
: _owner(owner)
, _expireTimer([=] { processExpired(); })
, _markReadTimer([=] { sendMarkAsReadRequests(); })
, _incrementViewsTimer([=] { sendIncrementViewsRequests(); })
, _pollingTimer([=] { sendPollingRequests(); })
, _pollingViewsTimer([=] { sendPollingViewsRequests(); }) {
	crl::on_main(this, [=] {
		session().changes().peerUpdates(
			Data::PeerUpdate::Flag::Rights
		) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
			const auto channel = update.peer->asChannel();
			if (!channel) {
				return;
			} else if (!channel->canEditStories()) {
				const auto peerId = channel->id;
				const auto i = _peersWithDeletedStories.find(peerId);
				if (i != end(_peersWithDeletedStories)) {
					_peersWithDeletedStories.erase(i);
					for (auto j = begin(_deleted); j != end(_deleted);) {
						if (j->peer == peerId) {
							j = _deleted.erase(j);
						} else {
							++j;
						}
					}
				}
			} else {
				clearArchive(channel);
			}
		}, _lifetime);
	});
}

Stories::~Stories() {
	Expects(_pollingSettings.empty());
	Expects(_pollingViews.empty());
}

Session &Stories::owner() const {
	return *_owner;
}

Main::Session &Stories::session() const {
	return _owner->session();
}

void Stories::apply(const MTPDupdateStory &data) {
	const auto peerId = peerFromMTP(data.vpeer());
	const auto peer = _owner->peer(peerId);
	const auto now = base::unixtime::now();
	const auto idDates = parseAndApply(peer, data.vstory(), now);
	if (!idDates) {
		return;
	}
	const auto expired = (idDates.expires <= now);
	if (expired) {
		applyExpired({ peerId, idDates.id });
		return;
	}
	const auto i = _all.find(peerId);
	if (i == end(_all)) {
		requestPeerStories(peer);
		return;
	} else if (i->second.ids.contains(idDates)) {
		return;
	}
	const auto wasInfo = i->second.info();
	i->second.ids.emplace(idDates);
	const auto nowInfo = i->second.info();
	if (peer->isSelf() && i->second.readTill < idDates.id) {
		_readTill[peerId] = i->second.readTill = idDates.id;
	}
	if (wasInfo == nowInfo) {
		return;
	}
	const auto refreshInList = [&](StorySourcesList list) {
		auto &sources = _sources[static_cast<int>(list)];
		const auto i = ranges::find(
			sources,
			peerId,
			&StoriesSourceInfo::id);
		if (i != end(sources)) {
			*i = nowInfo;
			sort(list);
		}
	};
	if (peer->hasStoriesHidden()) {
		refreshInList(StorySourcesList::Hidden);
	} else {
		refreshInList(StorySourcesList::NotHidden);
	}
	_sourceChanged.fire_copy(peerId);
	updatePeerStoriesState(peer);
}

void Stories::apply(const MTPDupdateReadStories &data) {
	bumpReadTill(peerFromMTP(data.vpeer()), data.vmax_id().v);
}

void Stories::apply(const MTPStoriesStealthMode &stealthMode) {
	const auto &data = stealthMode.data();
	_stealthMode = StealthMode{
		.enabledTill = data.vactive_until_date().value_or_empty(),
		.cooldownTill = data.vcooldown_until_date().value_or_empty(),
	};
}

void Stories::apply(not_null<PeerData*> peer, const MTPPeerStories *data) {
	if (!data) {
		applyDeletedFromSources(peer->id, StorySourcesList::NotHidden);
		applyDeletedFromSources(peer->id, StorySourcesList::Hidden);
		_all.erase(peer->id);
		_sourceChanged.fire_copy(peer->id);
		updatePeerStoriesState(peer);
	} else {
		parseAndApply(*data);
	}
}

Story *Stories::applyFromWebpage(PeerId peerId, const MTPstoryItem &story) {
	const auto idDates = parseAndApply(
		_owner->peer(peerId),
		story,
		base::unixtime::now());
	const auto value = idDates
		? lookup({ peerId, idDates.id })
		: base::make_unexpected(NoStory::Deleted);
	return value ? value->get() : nullptr;
}

void Stories::requestPeerStories(
		not_null<PeerData*> peer,
		Fn<void()> done) {
	const auto [i, ok] = _requestingPeerStories.emplace(peer);
	if (done) {
		i->second.push_back(std::move(done));
	}
	if (!ok) {
		return;
	}
	const auto finish = [=] {
		if (const auto callbacks = _requestingPeerStories.take(peer)) {
			for (const auto &callback : *callbacks) {
				callback();
			}
		}
	};
	_owner->session().api().request(MTPstories_GetPeerStories(
		peer->input
	)).done([=](const MTPstories_PeerStories &result) {
		const auto &data = result.data();
		_owner->processUsers(data.vusers());
		_owner->processChats(data.vchats());
		parseAndApply(data.vstories());
		finish();
	}).fail([=] {
		applyDeletedFromSources(peer->id, StorySourcesList::NotHidden);
		applyDeletedFromSources(peer->id, StorySourcesList::Hidden);
		finish();
	}).send();
}

void Stories::registerExpiring(TimeId expires, FullStoryId id) {
	for (auto i = _expiring.findFirst(expires)
		; (i != end(_expiring)) && (i->first == expires)
		; ++i) {
		if (i->second == id) {
			return;
		}
	}
	const auto reschedule = _expiring.empty()
		|| (_expiring.front().first > expires);
	_expiring.emplace(expires, id);
	if (reschedule) {
		scheduleExpireTimer();
	}
}

void Stories::scheduleExpireTimer() {
	if (_expireSchedulePosted) {
		return;
	}
	_expireSchedulePosted = true;
	crl::on_main(this, [=] {
		if (!_expireSchedulePosted) {
			return;
		}
		_expireSchedulePosted = false;
		if (_expiring.empty()) {
			_expireTimer.cancel();
		} else {
			const auto nearest = _expiring.front().first;
			const auto now = base::unixtime::now();
			const auto delay = (nearest > now)
				? (nearest - now)
				: 0;
			_expireTimer.callOnce(delay * crl::time(1000));
		}
	});
}

void Stories::processExpired() {
	const auto now = base::unixtime::now();
	auto expired = base::flat_set<FullStoryId>();
	auto i = begin(_expiring);
	for (; i != end(_expiring) && i->first <= now; ++i) {
		expired.emplace(i->second);
	}
	_expiring.erase(begin(_expiring), i);
	for (const auto &id : expired) {
		applyExpired(id);
	}
	if (!_expiring.empty()) {
		scheduleExpireTimer();
	}
}

Stories::Set *Stories::lookupArchive(not_null<PeerData*> peer) {
	const auto peerId = peer->id;
	if (hasArchive(peer)) {
		const auto i = _archive.find(peerId);
		return (i != end(_archive))
			? &i->second
			: &_archive.emplace(peerId, Set()).first->second;
	}
	clearArchive(peer);
	return nullptr;
}

void Stories::clearArchive(not_null<PeerData*> peer) {
	const auto peerId = peer->id;
	const auto i = _archive.find(peerId);
	if (i == end(_archive)) {
		return;
	}
	auto archive = base::take(i->second);
	_archive.erase(i);
	for (const auto &id : archive.ids.list) {
		if (const auto story = lookup({ peerId, id })) {
			if ((*story)->expired() && !(*story)->pinned()) {
				applyDeleted(peer, id);
			}
		}
	}
	_archiveChanged.fire_copy(peerId);
}

void Stories::parseAndApply(const MTPPeerStories &stories) {
	const auto &data = stories.data();
	const auto peerId = peerFromMTP(data.vpeer());
	const auto already = _readTill.find(peerId);
	const auto readTill = std::max(
		data.vmax_read_id().value_or_empty(),
		(already != end(_readTill) ? already->second : 0));
	const auto peer = _owner->peer(peerId);
	auto result = StoriesSource{
		.peer = peer,
		.readTill = readTill,
		.hidden = peer->hasStoriesHidden(),
	};
	const auto &list = data.vstories().v;
	const auto now = base::unixtime::now();
	result.ids.reserve(list.size());
	for (const auto &story : list) {
		if (const auto id = parseAndApply(result.peer, story, now)) {
			result.ids.emplace(id);
		}
	}
	if (result.ids.empty()) {
		applyDeletedFromSources(peerId, StorySourcesList::NotHidden);
		applyDeletedFromSources(peerId, StorySourcesList::Hidden);
		peer->setStoriesState(PeerData::StoriesState::None);
		return;
	} else if (peer->isSelf()) {
		result.readTill = result.ids.back().id;
	}
	_readTill[peerId] = result.readTill;
	const auto info = result.info();
	const auto i = _all.find(peerId);
	if (i != end(_all)) {
		if (i->second != result) {
			i->second = std::move(result);
		}
	} else {
		_all.emplace(peerId, std::move(result));
	}
	const auto add = [&](StorySourcesList list) {
		auto &sources = _sources[static_cast<int>(list)];
		const auto i = ranges::find(
			sources,
			peerId,
			&StoriesSourceInfo::id);
		if (i == end(sources)) {
			sources.push_back(info);
		} else if (*i == info) {
			return;
		} else {
			*i = info;
		}
		sort(list);
	};
	if (result.peer->isSelf()
		|| (result.peer->isChannel() && result.peer->asChannel()->amIn())
		|| (result.peer->isUser()
			&& (result.peer->asUser()->isBot()
				|| result.peer->asUser()->isContact()))
		|| result.peer->isServiceUser()) {
		const auto hidden = result.peer->hasStoriesHidden();
		using List = StorySourcesList;
		add(hidden ? List::Hidden : List::NotHidden);
		applyDeletedFromSources(
			peerId,
			hidden ? List::NotHidden : List::Hidden);
	} else {
		applyDeletedFromSources(peerId, StorySourcesList::NotHidden);
		applyDeletedFromSources(peerId, StorySourcesList::Hidden);
	}
	_sourceChanged.fire_copy(peerId);
	updatePeerStoriesState(result.peer);
}

Story *Stories::parseAndApply(
		not_null<PeerData*> peer,
		const MTPDstoryItem &data,
		TimeId now) {
	const auto media = ParseMedia(_owner, data.vmedia());
	if (!media) {
		return nullptr;
	}
	const auto expires = data.vexpire_date().v;
	const auto expired = (expires <= now);
	if (expired && !data.is_pinned() && !hasArchive(peer)) {
		return nullptr;
	}
	const auto id = data.vid().v;
	const auto fullId = FullStoryId{ peer->id, id };
	auto &stories = _stories[peer->id];
	const auto i = stories.find(id);
	if (i != end(stories)) {
		const auto result = i->second.get();
		const auto mediaChanged = (result->media() != *media);
		result->applyChanges(*media, data, now);
		const auto j = _pollingSettings.find(result);
		if (j != end(_pollingSettings)) {
			maybeSchedulePolling(result, j->second, now);
		}
		if (mediaChanged) {
			_preloaded.remove(fullId);
			if (_preloading && _preloading->id() == fullId) {
				_preloading = nullptr;
				rebuildPreloadSources(StorySourcesList::NotHidden);
				rebuildPreloadSources(StorySourcesList::Hidden);
				continuePreloading();
			}
			_owner->refreshStoryItemViews(fullId);
		}
		return result;
	}
	const auto wasDeleted = _deleted.remove(fullId);
	const auto result = stories.emplace(id, std::make_unique<Story>(
		id,
		peer,
		StoryMedia{ *media },
		data,
		now
	)).first->second.get();

	if (const auto archive = lookupArchive(peer)) {
		const auto added = archive->ids.list.emplace(id).second;
		if (added) {
			if (archive->total >= 0 && id > archive->lastId) {
				++archive->total;
			}
			_archiveChanged.fire_copy(peer->id);
		}
	}

	if (expired) {
		_expiring.remove(expires, fullId);
		applyExpired(fullId);
	} else {
		registerExpiring(expires, fullId);
	}

	if (wasDeleted) {
		_owner->refreshStoryItemViews(fullId);
	}

	return result;
}

StoryIdDates Stories::parseAndApply(
		not_null<PeerData*> peer,
		const MTPstoryItem &story,
		TimeId now) {
	return story.match([&](const MTPDstoryItem &data) {
		if (const auto story = parseAndApply(peer, data, now)) {
			return story->idDates();
		}
		applyDeleted(peer, data.vid().v);
		return StoryIdDates();
	}, [&](const MTPDstoryItemSkipped &data) {
		const auto expires = data.vexpire_date().v;
		const auto expired = (expires <= now);
		const auto fullId = FullStoryId{ peer->id, data.vid().v };
		if (!expired) {
			registerExpiring(expires, fullId);
		} else if (!hasArchive(peer)) {
			applyDeleted(peer, data.vid().v);
			return StoryIdDates();
		} else {
			_expiring.remove(expires, fullId);
			applyExpired(fullId);
		}
		return StoryIdDates{
			data.vid().v,
			data.vdate().v,
			data.vexpire_date().v,
		};
	}, [&](const MTPDstoryItemDeleted &data) {
		applyDeleted(peer, data.vid().v);
		return StoryIdDates();
	});
}

void Stories::updateDependentMessages(not_null<Data::Story*> story) {
	const auto i = _dependentMessages.find(story);
	if (i != end(_dependentMessages)) {
		for (const auto &dependent : i->second) {
			dependent->updateDependencyItem();
		}
	}
	session().changes().storyUpdated(
		story,
		Data::StoryUpdate::Flag::Edited);
}

void Stories::registerDependentMessage(
		not_null<HistoryItem*> dependent,
		not_null<Data::Story*> dependency) {
	_dependentMessages[dependency].emplace(dependent);
}

void Stories::unregisterDependentMessage(
		not_null<HistoryItem*> dependent,
		not_null<Data::Story*> dependency) {
	const auto i = _dependentMessages.find(dependency);
	if (i != end(_dependentMessages)) {
		if (i->second.remove(dependent) && i->second.empty()) {
			_dependentMessages.erase(i);
		}
	}
}

void Stories::savedStateChanged(not_null<Story*> story) {
	const auto id = story->id();
	const auto peer = story->peer()->id;
	const auto pinned = story->pinned();
	if (pinned) {
		auto &saved = _saved[peer];
		const auto added = saved.ids.list.emplace(id).second;
		if (added) {
			if (saved.total >= 0 && id > saved.lastId) {
				++saved.total;
			}
			_savedChanged.fire_copy(peer);
		}
	} else if (const auto i = _saved.find(peer); i != end(_saved)) {
		auto &saved = i->second;
		if (saved.ids.list.remove(id)) {
			if (saved.total > 0) {
				--saved.total;
			}
			_savedChanged.fire_copy(peer);
		}
	}
}

void Stories::loadMore(StorySourcesList list) {
	const auto index = static_cast<int>(list);
	if (_loadMoreRequestId[index] || _sourcesLoaded[index]) {
		return;
	}
	const auto hidden = (list == StorySourcesList::Hidden);
	const auto api = &_owner->session().api();
	using Flag = MTPstories_GetAllStories::Flag;
	_loadMoreRequestId[index] = api->request(MTPstories_GetAllStories(
		MTP_flags((hidden ? Flag::f_hidden : Flag())
			| (_sourcesStates[index].isEmpty()
				? Flag(0)
				: (Flag::f_next | Flag::f_state))),
		MTP_string(_sourcesStates[index])
	)).done([=](const MTPstories_AllStories &result) {
		_loadMoreRequestId[index] = 0;

		result.match([&](const MTPDstories_allStories &data) {
			_owner->processUsers(data.vusers());
			_owner->processChats(data.vchats());
			_sourcesStates[index] = qs(data.vstate());
			_sourcesLoaded[index] = !data.is_has_more();
			for (const auto &single : data.vpeer_stories().v) {
				parseAndApply(single);
			}
		}, [](const MTPDstories_allStoriesNotModified &) {
		});

		result.match([&](const auto &data) {
			apply(data.vstealth_mode());
		});

		preloadListsMore();
	}).fail([=] {
		_loadMoreRequestId[index] = 0;
	}).send();
}

void Stories::preloadListsMore() {
	if (_loadMoreRequestId[static_cast<int>(StorySourcesList::NotHidden)]
		|| _loadMoreRequestId[static_cast<int>(StorySourcesList::Hidden)]) {
		return;
	}
	const auto loading = [&](StorySourcesList list) {
		return _loadMoreRequestId[static_cast<int>(list)] != 0;
	};
	const auto countLoaded = [&](StorySourcesList list) {
		const auto index = static_cast<int>(list);
		return _sourcesLoaded[index] || !_sourcesStates[index].isEmpty();
	};
	if (loading(StorySourcesList::NotHidden)
		|| loading(StorySourcesList::Hidden)) {
		return;
	} else if (!countLoaded(StorySourcesList::NotHidden)) {
		loadMore(StorySourcesList::NotHidden);
	} else if (!countLoaded(StorySourcesList::Hidden)) {
		loadMore(StorySourcesList::Hidden);
	} else if (!archiveCountKnown(_owner->session().userPeerId())) {
		archiveLoadMore(_owner->session().userPeerId());
	}
}

void Stories::notifySourcesChanged(StorySourcesList list) {
	_sourcesChanged[static_cast<int>(list)].fire({});
	if (list == StorySourcesList::Hidden) {
		pushHiddenCountsToFolder();
	}
}

void Stories::pushHiddenCountsToFolder() {
	const auto &list = sources(StorySourcesList::Hidden);
	if (list.empty()) {
		if (_folderForHidden) {
			_folderForHidden->updateStoriesCount(0, 0);
		}
		return;
	}
	if (!_folderForHidden) {
		_folderForHidden = _owner->folder(Folder::kId);
	}
	const auto count = int(list.size());
	const auto unread = ranges::count_if(
		list,
		[](const StoriesSourceInfo &info) { return info.unreadCount > 0; });
	_folderForHidden->updateStoriesCount(count, unread);
}

void Stories::sendResolveRequests() {
	if (!_resolveSent.empty()) {
		return;
	}
	auto leftToSend = kMaxResolveTogether;
	auto byPeer = base::flat_map<PeerId, QVector<MTPint>>();
	for (auto i = begin(_resolvePending); i != end(_resolvePending);) {
		const auto peerId = i->first;
		auto &ids = i->second;
		auto &sent = _resolveSent[peerId];
		if (ids.size() <= leftToSend) {
			sent = base::take(ids);
			i = _resolvePending.erase(i); // Invalidates `ids`.
			leftToSend -= int(sent.size());
		} else {
			sent = {
				std::make_move_iterator(begin(ids)),
				std::make_move_iterator(begin(ids) + leftToSend)
			};
			ids.erase(begin(ids), begin(ids) + leftToSend);
			leftToSend = 0;
		}
		auto &prepared = byPeer[peerId];
		for (auto &[storyId, callbacks] : sent) {
			prepared.push_back(MTP_int(storyId));
		}
		if (!leftToSend) {
			break;
		}
	}
	const auto api = &_owner->session().api();
	for (auto &entry : byPeer) {
		const auto peerId = entry.first;
		auto &prepared = entry.second;
		const auto finish = [=](PeerId peerId) {
			const auto sent = _resolveSent.take(peerId);
			Assert(sent.has_value());
			for (const auto &[storyId, list] : *sent) {
				finalizeResolve({ peerId, storyId });
				for (const auto &callback : list) {
					callback();
				}
			}
			_itemsChanged.fire_copy(peerId);
			if (_resolveSent.empty() && !_resolvePending.empty()) {
				crl::on_main(&session(), [=] { sendResolveRequests(); });
			}
		};
		const auto peer = _owner->session().data().peer(peerId);
		api->request(MTPstories_GetStoriesByID(
			peer->input,
			MTP_vector<MTPint>(prepared)
		)).done([=](const MTPstories_Stories &result) {
			owner().processUsers(result.data().vusers());
			owner().processChats(result.data().vchats());
			processResolvedStories(peer, result.data().vstories().v);
			finish(peer->id);
		}).fail([=] {
			finish(peerId);
		}).send();
	 }
}

void Stories::processResolvedStories(
		not_null<PeerData*> peer,
		const QVector<MTPStoryItem> &list) {
	const auto now = base::unixtime::now();
	for (const auto &item : list) {
		item.match([&](const MTPDstoryItem &data) {
			if (!parseAndApply(peer, data, now)) {
				applyDeleted(peer, data.vid().v);
			}
		}, [&](const MTPDstoryItemSkipped &data) {
			LOG(("API Error: Unexpected storyItemSkipped in resolve."));
		}, [&](const MTPDstoryItemDeleted &data) {
			applyDeleted(peer, data.vid().v);
		});
	}
}

void Stories::finalizeResolve(FullStoryId id) {
	const auto already = lookup(id);
	if (!already.has_value() && already.error() == NoStory::Unknown) {
		LOG(("API Error: Could not resolve story %1_%2"
			).arg(id.peer.value
			).arg(id.story));
		applyDeleted(_owner->peer(id.peer), id.story);
	}
}

void Stories::applyDeleted(not_null<PeerData*> peer, StoryId id) {
	const auto fullId = FullStoryId{ peer->id, id };
	applyRemovedFromActive(fullId);

	if (const auto channel = peer->asChannel()) {
		if (!hasArchive(channel)) {
			_peersWithDeletedStories.emplace(channel->id);
		}
	}

	_deleted.emplace(fullId);
	const auto peerId = peer->id;
	const auto i = _stories.find(peerId);
	if (i != end(_stories)) {
		const auto j = i->second.find(id);
		if (j != end(i->second)) {
			const auto &story
				= _deletingStories[fullId]
				= std::move(j->second);
			_expiring.remove(story->expires(), story->fullId());
			i->second.erase(j);

			session().changes().storyUpdated(
				story.get(),
				UpdateFlag::Destroyed);
			removeDependencyStory(story.get());
			if (hasArchive(story->peer())) {
				if (const auto k = _archive.find(peerId)
					; k != end(_archive)) {
					const auto archive = &k->second;
					if (archive->ids.list.remove(id)) {
						if (archive->total > 0) {
							--archive->total;
						}
						_archiveChanged.fire_copy(peerId);
					}
				}
			}
			if (story->pinned()) {
				if (const auto k = _saved.find(peerId); k != end(_saved)) {
					const auto saved = &k->second;
					if (saved->ids.list.remove(id)) {
						if (saved->total > 0) {
							--saved->total;
						}
						_savedChanged.fire_copy(peerId);
					}
				}
			}
			if (_preloading && _preloading->id() == fullId) {
				_preloading = nullptr;
				preloadFinished(fullId);
			}
			_owner->refreshStoryItemViews(fullId);
			Assert(!_pollingSettings.contains(story.get()));
			if (const auto j = _items.find(peerId); j != end(_items)) {
				const auto k = j->second.find(id);
				if (k != end(j->second)) {
					Assert(!k->second.lock());
					j->second.erase(k);
					if (j->second.empty()) {
						_items.erase(j);
					}
				}
			}
			if (i->second.empty()) {
				_stories.erase(i);
			}
			_deletingStories.remove(fullId);
		}
	}
}

void Stories::applyExpired(FullStoryId id) {
	if (const auto maybeStory = lookup(id)) {
		const auto story = *maybeStory;
		if (!hasArchive(story->peer()) && !story->pinned()) {
			applyDeleted(story->peer(), id.story);
			return;
		}
	}
	applyRemovedFromActive(id);
}

void Stories::applyRemovedFromActive(FullStoryId id) {
	const auto removeFromList = [&](StorySourcesList list) {
		const auto index = static_cast<int>(list);
		auto &sources = _sources[index];
		const auto i = ranges::find(
			sources,
			id.peer,
			&StoriesSourceInfo::id);
		if (i != end(sources)) {
			sources.erase(i);
			notifySourcesChanged(list);
		}
	};
	const auto i = _all.find(id.peer);
	if (i != end(_all)) {
		const auto j = i->second.ids.lower_bound(StoryIdDates{ id.story });
		if (j != end(i->second.ids) && j->id == id.story) {
			i->second.ids.erase(j);
			const auto peer = i->second.peer;
			if (i->second.ids.empty()) {
				_all.erase(i);
				removeFromList(StorySourcesList::NotHidden);
				removeFromList(StorySourcesList::Hidden);
			}
			_sourceChanged.fire_copy(id.peer);
			updatePeerStoriesState(peer);
		}
	}
}

void Stories::applyDeletedFromSources(PeerId id, StorySourcesList list) {
	auto &sources = _sources[static_cast<int>(list)];
	const auto i = ranges::find(
		sources,
		id,
		&StoriesSourceInfo::id);
	if (i != end(sources)) {
		sources.erase(i);
	}
	notifySourcesChanged(list);
}

void Stories::removeDependencyStory(not_null<Story*> story) {
	const auto i = _dependentMessages.find(story);
	if (i != end(_dependentMessages)) {
		const auto items = std::move(i->second);
		_dependentMessages.erase(i);

		for (const auto &dependent : items) {
			dependent->dependencyStoryRemoved(story);
		}
	}
}

void Stories::sort(StorySourcesList list) {
	const auto index = static_cast<int>(list);
	auto &sources = _sources[index];
	const auto self = _owner->session().userPeerId();
	const auto changelogSenderId = UserData::kServiceNotificationsId;
	const auto proj = [&](const StoriesSourceInfo &info) {
		const auto key = int64(info.last)
			+ (info.premium ? (int64(1) << 47) : 0)
			+ ((info.id == changelogSenderId) ? (int64(1) << 47) : 0)
			+ ((info.unreadCount > 0) ? (int64(1) << 49) : 0)
			+ ((info.id == self) ? (int64(1) << 50) : 0);
		return std::make_pair(key, info.id);
	};
	ranges::sort(sources, ranges::greater(), proj);
	notifySourcesChanged(list);
	preloadSourcesChanged(list);
}

std::shared_ptr<HistoryItem> Stories::lookupItem(not_null<Story*> story) {
	const auto i = _items.find(story->peer()->id);
	if (i == end(_items)) {
		return nullptr;
	}
	const auto j = i->second.find(story->id());
	if (j == end(i->second)) {
		return nullptr;
	}
	return j->second.lock();
}

StealthMode Stories::stealthMode() const {
	return _stealthMode.current();
}

rpl::producer<StealthMode> Stories::stealthModeValue() const {
	return _stealthMode.value();
}

void Stories::activateStealthMode(Fn<void()> done) {
	const auto api = &session().api();
	using Flag = MTPstories_ActivateStealthMode::Flag;
	api->request(MTPstories_ActivateStealthMode(
		MTP_flags(Flag::f_past | Flag::f_future)
	)).done([=](const MTPUpdates &result) {
		api->applyUpdates(result);
		if (done) done();
	}).fail([=] {
		if (done) done();
	}).send();
}

void Stories::sendReaction(FullStoryId id, Data::ReactionId reaction) {
	if (const auto maybeStory = lookup(id)) {
		const auto story = *maybeStory;
		story->setReactionId(reaction);

		const auto api = &session().api();
		api->request(MTPstories_SendReaction(
			MTP_flags(0),
			story->peer()->input,
			MTP_int(id.story),
			ReactionToMTP(reaction)
		)).send();
	}
}

std::shared_ptr<HistoryItem> Stories::resolveItem(not_null<Story*> story) {
	auto &items = _items[story->peer()->id];
	auto i = items.find(story->id());
	if (i == end(items)) {
		i = items.emplace(story->id()).first;
	} else if (const auto result = i->second.lock()) {
		return result;
	}
	const auto history = _owner->history(story->peer());
	auto result = std::shared_ptr<HistoryItem>(
		history->makeMessage(story).get(),
		HistoryItem::Destroyer());
	i->second = result;
	return result;
}

std::shared_ptr<HistoryItem> Stories::resolveItem(FullStoryId id) {
	const auto story = lookup(id);
	return story ? resolveItem(*story) : std::shared_ptr<HistoryItem>();
}

const StoriesSource *Stories::source(PeerId id) const {
	const auto i = _all.find(id);
	return (i != end(_all)) ? &i->second : nullptr;
}

const std::vector<StoriesSourceInfo> &Stories::sources(
		StorySourcesList list) const {
	return _sources[static_cast<int>(list)];
}

bool Stories::sourcesLoaded(StorySourcesList list) const {
	return _sourcesLoaded[static_cast<int>(list)];
}

rpl::producer<> Stories::sourcesChanged(StorySourcesList list) const {
	return _sourcesChanged[static_cast<int>(list)].events();
}

rpl::producer<PeerId> Stories::sourceChanged() const {
	return _sourceChanged.events();
}

rpl::producer<PeerId> Stories::itemsChanged() const {
	return _itemsChanged.events();
}

base::expected<not_null<Story*>, NoStory> Stories::lookup(
		FullStoryId id) const {
	const auto i = _stories.find(id.peer);
	if (i != end(_stories)) {
		const auto j = i->second.find(id.story);
		if (j != end(i->second)) {
			return j->second.get();
		}
	}
	return base::make_unexpected(
		_deleted.contains(id) ? NoStory::Deleted : NoStory::Unknown);
}

void Stories::resolve(FullStoryId id, Fn<void()> done, bool force) {
	if (!force) {
		const auto already = lookup(id);
		if (already.has_value() || already.error() != NoStory::Unknown) {
			if (done) {
				done();
			}
			return;
		}
	}
	if (const auto i = _resolveSent.find(id.peer); i != end(_resolveSent)) {
		if (const auto j = i->second.find(id.story); j != end(i->second)) {
			if (done) {
				j->second.push_back(std::move(done));
			}
			return;
		}
	}
	auto &ids = _resolvePending[id.peer];
	if (ids.empty()) {
		crl::on_main(&session(), [=] {
			sendResolveRequests();
		});
	}
	auto &callbacks = ids[id.story];
	if (done) {
		callbacks.push_back(std::move(done));
	}
}

void Stories::loadAround(FullStoryId id, StoriesContext context) {
	if (v::is<StoriesContextSingle>(context.data)) {
		return;
	} else if (v::is<StoriesContextSaved>(context.data)
		|| v::is<StoriesContextArchive>(context.data)) {
		return;
	}
	const auto i = _all.find(id.peer);
	if (i == end(_all)) {
		return;
	}
	const auto j = i->second.ids.lower_bound(StoryIdDates{ id.story });
	if (j == end(i->second.ids) || j->id != id.story) {
		return;
	}
	const auto ignore = [&] {
		const auto side = kIgnorePreloadAroundIfLoaded;
		const auto left = ranges::min(int(j - begin(i->second.ids)), side);
		const auto right = ranges::min(int(end(i->second.ids) - j), side);
		for (auto k = j - left; k != j + right; ++k) {
			const auto maybeStory = lookup({ id.peer, k->id });
			if (!maybeStory && maybeStory.error() == NoStory::Unknown) {
				return false;
			}
		}
		return true;
	}();
	if (ignore) {
		return;
	}
	const auto side = kPreloadAroundCount;
	const auto left = ranges::min(int(j - begin(i->second.ids)), side);
	const auto right = ranges::min(int(end(i->second.ids) - j), side);
	const auto from = j - left;
	const auto till = j + right;
	for (auto k = from; k != till; ++k) {
		resolve({ id.peer, k->id }, nullptr);
	}
}

void Stories::markAsRead(FullStoryId id, bool viewed) {
	if (id.peer == _owner->session().userPeerId()) {
		return;
	}
	const auto maybeStory = lookup(id);
	if (!maybeStory) {
		return;
	}
	const auto story = *maybeStory;
	if (story->expired() && story->pinned()) {
		_incrementViewsPending[id.peer].emplace(id.story);
		if (!_incrementViewsTimer.isActive()) {
			_incrementViewsTimer.callOnce(kIncrementViewsDelay);
		}
	}
	if (!bumpReadTill(id.peer, id.story)) {
		return;
	}
	if (!_markReadPending.contains(id.peer)) {
		sendMarkAsReadRequests();
	}
	_markReadPending.emplace(id.peer);
	_markReadTimer.callOnce(kMarkAsReadDelay);
}

bool Stories::bumpReadTill(PeerId peerId, StoryId maxReadTill) {
	auto &till = _readTill[peerId];
	auto refreshItems = std::vector<StoryId>();
	const auto guard = gsl::finally([&] {
		for (const auto id : refreshItems) {
			_owner->refreshStoryItemViews({ peerId, id });
		}
	});
	if (till < maxReadTill) {
		const auto from = till;
		till = maxReadTill;
		updatePeerStoriesState(_owner->peer(peerId));
		const auto i = _stories.find(peerId);
		if (i != end(_stories)) {
			refreshItems = ranges::make_subrange(
				i->second.lower_bound(from + 1),
				i->second.lower_bound(till + 1)
			) | ranges::views::transform([=](const auto &pair) {
				_owner->session().changes().storyUpdated(
					pair.second.get(),
					StoryUpdate::Flag::MarkRead);
				return pair.first;
			}) | ranges::to_vector;
		}
	}
	const auto i = _all.find(peerId);
	if (i == end(_all) || i->second.readTill >= maxReadTill) {
		return false;
	}
	const auto wasUnreadCount = i->second.unreadCount();
	i->second.readTill = maxReadTill;
	const auto nowUnreadCount = i->second.unreadCount();
	if (wasUnreadCount != nowUnreadCount) {
		const auto refreshInList = [&](StorySourcesList list) {
			auto &sources = _sources[static_cast<int>(list)];
			const auto i = ranges::find(
				sources,
				peerId,
				&StoriesSourceInfo::id);
			if (i != end(sources)) {
				i->unreadCount = nowUnreadCount;
				sort(list);
			}
		};
		refreshInList(StorySourcesList::NotHidden);
		refreshInList(StorySourcesList::Hidden);
	}
	return true;
}

void Stories::toggleHidden(
		PeerId peerId,
		bool hidden,
		std::shared_ptr<Ui::Show> show) {
	const auto peer = _owner->peer(peerId);
	if (peer->hasStoriesHidden() != hidden) {
		peer->setStoriesHidden(hidden);
		session().api().request(MTPstories_TogglePeerStoriesHidden(
			peer->input,
			MTP_bool(hidden)
		)).send();
	}

	const auto name = peer->shortName();
	const auto guard = gsl::finally([&] {
		if (show) {
			const auto phrase = hidden
				? tr::lng_stories_hidden_to_contacts
				: tr::lng_stories_shown_in_chats;
			show->showToast(phrase(
				tr::now,
				lt_user,
				Ui::Text::Bold(name),
				Ui::Text::RichLangValue));
		}
	});

	const auto i = _all.find(peerId);
	if (i == end(_all)) {
		return;
	}
	i->second.hidden = hidden;
	const auto info = i->second.info();
	const auto main = static_cast<int>(StorySourcesList::NotHidden);
	const auto other = static_cast<int>(StorySourcesList::Hidden);
	const auto proj = &StoriesSourceInfo::id;
	if (hidden) {
		const auto i = ranges::find(_sources[main], peerId, proj);
		if (i != end(_sources[main])) {
			_sources[main].erase(i);
			notifySourcesChanged(StorySourcesList::NotHidden);
			preloadSourcesChanged(StorySourcesList::NotHidden);
		}
		const auto j = ranges::find(_sources[other], peerId, proj);
		if (j == end(_sources[other])) {
			_sources[other].push_back(info);
		} else {
			*j = info;
		}
		sort(StorySourcesList::Hidden);
	} else {
		const auto i = ranges::find(_sources[other], peerId, proj);
		if (i != end(_sources[other])) {
			_sources[other].erase(i);
			notifySourcesChanged(StorySourcesList::Hidden);
			preloadSourcesChanged(StorySourcesList::Hidden);
		}
		const auto j = ranges::find(_sources[main], peerId, proj);
		if (j == end(_sources[main])) {
			_sources[main].push_back(info);
		} else {
			*j = info;
		}
		sort(StorySourcesList::NotHidden);
	}
}

void Stories::sendMarkAsReadRequest(
		not_null<PeerData*> peer,
		StoryId tillId) {
	const auto peerId = peer->id;
	_markReadRequests.emplace(peerId);
	const auto finish = [=] {
		_markReadRequests.remove(peerId);
		if (!_markReadTimer.isActive()
			&& _markReadPending.contains(peerId)) {
			sendMarkAsReadRequests();
		}
		checkQuitPreventFinished();
	};

	const auto api = &_owner->session().api();
	api->request(MTPstories_ReadStories(
		peer->input,
		MTP_int(tillId)
	)).done(finish).fail(finish).send();
}

void Stories::checkQuitPreventFinished() {
	if (_markReadRequests.empty() && _incrementViewsRequests.empty()) {
		if (Core::Quitting()) {
			LOG(("Stories doesn't prevent quit any more."));
		}
		Core::App().quitPreventFinished();
	}
}

void Stories::sendMarkAsReadRequests() {
	_markReadTimer.cancel();
	for (auto i = begin(_markReadPending); i != end(_markReadPending);) {
		const auto peerId = *i;
		if (_markReadRequests.contains(peerId)) {
			++i;
			continue;
		}
		const auto j = _all.find(peerId);
		if (j != end(_all)) {
			sendMarkAsReadRequest(j->second.peer, j->second.readTill);
		}
		i = _markReadPending.erase(i);
	}
}

void Stories::sendIncrementViewsRequests() {
	if (_incrementViewsPending.empty()) {
		return;
	}
	auto ids = QVector<MTPint>();
	struct Prepared {
		PeerId peer = 0;
		QVector<MTPint> ids;
	};
	auto prepared = std::vector<Prepared>();
	for (const auto &[peer, ids] : _incrementViewsPending) {
		if (_incrementViewsRequests.contains(peer)) {
			continue;
		}
		prepared.push_back({ .peer = peer });
		for (const auto &id : ids) {
			prepared.back().ids.push_back(MTP_int(id));
		}
	}

	const auto api = &_owner->session().api();
	for (auto &[peer, ids] : prepared) {
		_incrementViewsRequests.emplace(peer);
		const auto finish = [=, peer = peer] {
			_incrementViewsRequests.remove(peer);
			if (!_incrementViewsTimer.isActive()
				&& _incrementViewsPending.contains(peer)) {
				sendIncrementViewsRequests();
			}
			checkQuitPreventFinished();
		};
		api->request(MTPstories_IncrementStoryViews(
			_owner->peer(peer)->input,
			MTP_vector<MTPint>(std::move(ids))
		)).done(finish).fail(finish).send();
		_incrementViewsPending.remove(peer);
	}
}

void Stories::loadViewsSlice(
		not_null<PeerData*> peer,
		StoryId id,
		QString offset,
		Fn<void(StoryViews)> done) {
	Expects(peer->isSelf() || !done);

	if (_viewsStoryPeer == peer
		&& _viewsStoryId == id
		&& _viewsOffset == offset
		&& (!offset.isEmpty() || _viewsRequestId)) {
		if (_viewsRequestId) {
			_viewsDone = std::move(done);
		}
		return;
	}
	_viewsStoryPeer = peer;
	_viewsStoryId = id;
	_viewsOffset = offset;
	_viewsDone = std::move(done);

	if (peer->isSelf()) {
		sendViewsSliceRequest();
	} else {
		sendViewsCountsRequest();
	}
}

void Stories::sendViewsSliceRequest() {
	Expects(_viewsStoryPeer != nullptr);
	Expects(_viewsStoryPeer->isSelf());

	using Flag = MTPstories_GetStoryViewsList::Flag;
	const auto api = &_owner->session().api();
	_owner->session().api().request(_viewsRequestId).cancel();
	_viewsRequestId = api->request(MTPstories_GetStoryViewsList(
		MTP_flags(Flag::f_reactions_first),
		_viewsStoryPeer->input,
		MTPstring(), // q
		MTP_int(_viewsStoryId),
		MTP_string(_viewsOffset),
		MTP_int(_viewsDone ? kViewsPerPage : kPollingViewsPerPage)
	)).done([=](const MTPstories_StoryViewsList &result) {
		_viewsRequestId = 0;

		const auto &data = result.data();
		auto slice = StoryViews{
			.nextOffset = data.vnext_offset().value_or_empty(),
			.reactions = data.vreactions_count().v,
			.total = data.vcount().v,
		};
		_owner->processUsers(data.vusers());
		slice.list.reserve(data.vviews().v.size());
		for (const auto &view : data.vviews().v) {
			slice.list.push_back({
				.peer = _owner->peer(peerFromUser(view.data().vuser_id())),
				.reaction = (view.data().vreaction()
					? ReactionFromMTP(*view.data().vreaction())
					: Data::ReactionId()),
				.date = view.data().vdate().v,
			});
		}
		const auto fullId = FullStoryId{
			.peer = _owner->session().userPeerId(),
			.story = _viewsStoryId,
		};
		if (const auto story = lookup(fullId)) {
			(*story)->applyViewsSlice(_viewsOffset, slice);
		}
		if (const auto done = base::take(_viewsDone)) {
			done(std::move(slice));
		}
	}).fail([=] {
		_viewsRequestId = 0;
		if (const auto done = base::take(_viewsDone)) {
			done({});
		}
	}).send();
}

void Stories::sendViewsCountsRequest() {
	Expects(_viewsStoryPeer != nullptr);
	Expects(!_viewsDone);

	const auto api = &_owner->session().api();
	_owner->session().api().request(_viewsRequestId).cancel();
	_viewsRequestId = api->request(MTPstories_GetStoriesViews(
		_viewsStoryPeer->input,
		MTP_vector<MTPint>(1, MTP_int(_viewsStoryId))
	)).done([=](const MTPstories_StoryViews &result) {
		_viewsRequestId = 0;

		const auto &data = result.data();
		_owner->processUsers(data.vusers());
		if (data.vviews().v.size() == 1) {
			const auto fullId = FullStoryId{
				_viewsStoryPeer->id,
				_viewsStoryId,
			};
			if (const auto story = lookup(fullId)) {
				(*story)->applyViewsCounts(data.vviews().v.front().data());
			}
		}
	}).fail([=] {
		_viewsRequestId = 0;
	}).send();
}

bool Stories::hasArchive(not_null<PeerData*> peer) const {
	if (peer->isSelf()) {
		return true;
	} else if (const auto channel = peer->asChannel()) {
		return channel->canEditStories();
	}
	return false;
}

const StoriesIds &Stories::archive(PeerId peerId) const {
	static const auto empty = StoriesIds();
	const auto i = _archive.find(peerId);
	return (i != end(_archive)) ? i->second.ids : empty;
}

rpl::producer<PeerId> Stories::archiveChanged() const {
	return _archiveChanged.events();
}

int Stories::archiveCount(PeerId peerId) const {
	const auto i = _archive.find(peerId);
	return (i != end(_archive)) ? i->second.total : 0;
}

bool Stories::archiveCountKnown(PeerId peerId) const {
	const auto i = _archive.find(peerId);
	return (i != end(_archive)) && (i->second.total >= 0);
}

bool Stories::archiveLoaded(PeerId peerId) const {
	const auto i = _archive.find(peerId);
	return (i != end(_archive)) && i->second.loaded;
}

const StoriesIds &Stories::saved(PeerId peerId) const {
	static const auto empty = StoriesIds();
	const auto i = _saved.find(peerId);
	return (i != end(_saved)) ? i->second.ids : empty;
}

rpl::producer<PeerId> Stories::savedChanged() const {
	return _savedChanged.events();
}

int Stories::savedCount(PeerId peerId) const {
	const auto i = _saved.find(peerId);
	return (i != end(_saved)) ? i->second.total : 0;
}

bool Stories::savedCountKnown(PeerId peerId) const {
	const auto i = _saved.find(peerId);
	return (i != end(_saved)) && (i->second.total >= 0);
}

bool Stories::savedLoaded(PeerId peerId) const {
	const auto i = _saved.find(peerId);
	return (i != end(_saved)) && i->second.loaded;
}

void Stories::archiveLoadMore(PeerId peerId) {
	const auto peer = _owner->peer(peerId);
	const auto archive = lookupArchive(peer);
	if (!archive || archive->requestId || archive->loaded) {
		return;
	}
	const auto api = &_owner->session().api();
	archive->requestId = api->request(MTPstories_GetStoriesArchive(
		peer->input,
		MTP_int(archive->lastId),
		MTP_int(archive->lastId ? kArchivePerPage : kArchiveFirstPerPage)
	)).done([=](const MTPstories_Stories &result) {
		const auto archive = lookupArchive(peer);
		if (!archive) {
			return;
		}
		archive->requestId = 0;

		const auto &data = result.data();
		const auto now = base::unixtime::now();
		archive->total = data.vcount().v;
		for (const auto &story : data.vstories().v) {
			const auto id = story.match([&](const auto &id) {
				return id.vid().v;
			});
			archive->ids.list.emplace(id);
			archive->lastId = id;
			if (!parseAndApply(peer, story, now)) {
				archive->ids.list.remove(id);
				if (archive->total > 0) {
					--archive->total;
				}
			}
		}
		const auto ids = int(archive->ids.list.size());
		archive->loaded = data.vstories().v.empty();
		archive->total = archive->loaded ? ids : std::max(archive->total, ids);
		_archiveChanged.fire_copy(peerId);
	}).fail([=] {
		const auto archive = lookupArchive(peer);
		if (!archive) {
			return;
		}
		archive->requestId = 0;
		archive->loaded = true;
		archive->total = int(archive->ids.list.size());
		_archiveChanged.fire_copy(peerId);
	}).send();
}

void Stories::savedLoadMore(PeerId peerId) {
	auto &saved = _saved[peerId];
	if (saved.requestId || saved.loaded) {
		return;
	}
	const auto api = &_owner->session().api();
	const auto peer = _owner->peer(peerId);
	saved.requestId = api->request(MTPstories_GetPinnedStories(
		peer->input,
		MTP_int(saved.lastId),
		MTP_int(saved.lastId ? kSavedPerPage : kSavedFirstPerPage)
	)).done([=](const MTPstories_Stories &result) {
		auto &saved = _saved[peerId];
		saved.requestId = 0;

		const auto &data = result.data();
		const auto now = base::unixtime::now();
		saved.total = data.vcount().v;
		for (const auto &story : data.vstories().v) {
			const auto id = story.match([&](const auto &id) {
				return id.vid().v;
			});
			saved.ids.list.emplace(id);
			saved.lastId = id;
			if (!parseAndApply(peer, story, now)) {
				saved.ids.list.remove(id);
				if (saved.total > 0) {
					--saved.total;
				}
			}
		}
		const auto ids = int(saved.ids.list.size());
		saved.loaded = data.vstories().v.empty();
		saved.total = saved.loaded ? ids : std::max(saved.total, ids);
		_savedChanged.fire_copy(peerId);
	}).fail([=] {
		auto &saved = _saved[peerId];
		saved.requestId = 0;
		saved.loaded = true;
		saved.total = int(saved.ids.list.size());
		_savedChanged.fire_copy(peerId);
	}).send();
}

void Stories::deleteList(const std::vector<FullStoryId> &ids) {
	if (ids.empty()) {
		return;
	}
	const auto peer = session().data().peer(ids.front().peer);
	auto list = QVector<MTPint>();
	list.reserve(ids.size());
	for (const auto &id : ids) {
		if (id.peer == peer->id) {
			list.push_back(MTP_int(id.story));
		}
	}
	const auto api = &_owner->session().api();
	api->request(MTPstories_DeleteStories(
		peer->input,
		MTP_vector<MTPint>(list)
	)).done([=](const MTPVector<MTPint> &result) {
		for (const auto &id : result.v) {
			applyDeleted(peer, id.v);
		}
	}).send();
}

void Stories::togglePinnedList(
		const std::vector<FullStoryId> &ids,
		bool pinned) {
	if (ids.empty()) {
		return;
	}
	const auto peer = session().data().peer(ids.front().peer);
	auto list = QVector<MTPint>();
	list.reserve(ids.size());
	for (const auto &id : ids) {
		if (id.peer == peer->id) {
			list.push_back(MTP_int(id.story));
		}
	}
	if (list.empty()) {
		return;
	}
	const auto api = &_owner->session().api();
	api->request(MTPstories_TogglePinned(
		peer->input,
		MTP_vector<MTPint>(list),
		MTP_bool(pinned)
	)).done([=](const MTPVector<MTPint> &result) {
		const auto peerId = peer->id;
		auto &saved = _saved[peerId];
		const auto loaded = saved.loaded;
		const auto lastId = !saved.ids.list.empty()
			? saved.ids.list.back()
			: saved.lastId
			? saved.lastId
			: std::numeric_limits<StoryId>::max();
		auto dirty = false;
		for (const auto &id : result.v) {
			if (const auto maybeStory = lookup({ peerId, id.v })) {
				const auto story = *maybeStory;
				story->setPinned(pinned);
				if (pinned) {
					const auto add = loaded || (id.v >= lastId);
					if (!add) {
						dirty = true;
					} else if (saved.ids.list.emplace(id.v).second) {
						if (saved.total >= 0) {
							++saved.total;
						}
					}
				} else if (saved.ids.list.remove(id.v)) {
					if (saved.total > 0) {
						--saved.total;
					}
				} else if (!loaded) {
					dirty = true;
				}
			} else if (!loaded) {
				dirty = true;
			}
		}
		if (dirty) {
			savedLoadMore(peerId);
		} else {
			_savedChanged.fire_copy(peerId);
		}
	}).send();
}

void Stories::report(
		std::shared_ptr<Ui::Show> show,
		FullStoryId id,
		Ui::ReportReason reason,
		QString text) {
	if (const auto maybeStory = lookup(id)) {
		const auto story = *maybeStory;
		Api::SendReport(show, story->peer(), reason, text, story->id());
	}
}

bool Stories::isQuitPrevent() {
	if (!_markReadPending.empty()) {
		sendMarkAsReadRequests();
	}
	if (!_incrementViewsPending.empty()) {
		sendIncrementViewsRequests();
	}
	if (_markReadRequests.empty() && _incrementViewsRequests.empty()) {
		return false;
	}
	LOG(("Stories prevents quit, marking as read..."));
	return true;
}

void Stories::incrementPreloadingMainSources() {
	Expects(_preloadingMainSourcesCounter >= 0);

	if (++_preloadingMainSourcesCounter == 1
		&& rebuildPreloadSources(StorySourcesList::NotHidden)) {
		continuePreloading();
	}
}

void Stories::decrementPreloadingMainSources() {
	Expects(_preloadingMainSourcesCounter > 0);

	if (!--_preloadingMainSourcesCounter
		&& rebuildPreloadSources(StorySourcesList::NotHidden)) {
		continuePreloading();
	}
}

void Stories::incrementPreloadingHiddenSources() {
	Expects(_preloadingHiddenSourcesCounter >= 0);

	if (++_preloadingHiddenSourcesCounter == 1
		&& rebuildPreloadSources(StorySourcesList::Hidden)) {
		continuePreloading();
	}
}

void Stories::decrementPreloadingHiddenSources() {
	Expects(_preloadingHiddenSourcesCounter > 0);

	if (!--_preloadingHiddenSourcesCounter
		&& rebuildPreloadSources(StorySourcesList::Hidden)) {
		continuePreloading();
	}
}

void Stories::setPreloadingInViewer(std::vector<FullStoryId> ids) {
	ids.erase(ranges::remove_if(ids, [&](FullStoryId id) {
		return _preloaded.contains(id);
	}), end(ids));
	if (_toPreloadViewer != ids) {
		_toPreloadViewer = std::move(ids);
		continuePreloading();
	}
}

std::optional<Stories::PeerSourceState> Stories::peerSourceState(
		not_null<PeerData*> peer,
		StoryId storyMaxId) {
	const auto i = _readTill.find(peer->id);
	if (_readTillReceived || (i != end(_readTill))) {
		return PeerSourceState{
			.maxId = storyMaxId,
			.readTill = std::min(
				storyMaxId,
				(i != end(_readTill)) ? i->second : 0),
		};
	}
	requestReadTills();
	_pendingPeerStateMaxId[peer] = storyMaxId;
	return std::nullopt;
}

void Stories::requestReadTills() {
	if (_readTillReceived || _readTillsRequestId) {
		return;
	}
	const auto api = &_owner->session().api();
	_readTillsRequestId = api->request(MTPstories_GetAllReadPeerStories(
	)).done([=](const MTPUpdates &result) {
		_readTillReceived = true;
		api->applyUpdates(result);
		for (auto &[peer, maxId] : base::take(_pendingPeerStateMaxId)) {
			updatePeerStoriesState(peer);
		}
		for (const auto &storyId : base::take(_pendingReadTillItems)) {
			_owner->refreshStoryItemViews(storyId);
		}
	}).send();
}

bool Stories::isUnread(not_null<Story*> story) {
	const auto till = _readTill.find(story->peer()->id);
	if (till == end(_readTill) && !_readTillReceived) {
		requestReadTills();
		_pendingReadTillItems.emplace(story->fullId());
		return false;
	}
	const auto readTill = (till != end(_readTill)) ? till->second : 0;
	return (story->id() > readTill);
}

void Stories::registerPolling(not_null<Story*> story, Polling polling) {
	auto &settings = _pollingSettings[story];
	switch (polling) {
	case Polling::Chat: ++settings.chat; break;
	case Polling::Viewer:
		++settings.viewer;
		if ((story->peer()->isSelf() || story->peer()->isChannel())
			&& _pollingViews.emplace(story).second) {
			sendPollingViewsRequests();
		}
		break;
	}
	maybeSchedulePolling(story, settings, base::unixtime::now());
}

void Stories::unregisterPolling(not_null<Story*> story, Polling polling) {
	const auto i = _pollingSettings.find(story);
	Assert(i != end(_pollingSettings));

	switch (polling) {
	case Polling::Chat:
		Assert(i->second.chat > 0);
		--i->second.chat;
		break;
	case Polling::Viewer:
		Assert(i->second.viewer > 0);
		if (!--i->second.viewer) {
			_pollingViews.remove(story);
			if (_pollingViews.empty()) {
				_pollingViewsTimer.cancel();
			}
		}
		break;
	}
	if (!i->second.chat && !i->second.viewer) {
		_pollingSettings.erase(i);
	}
}

bool Stories::registerPolling(FullStoryId id, Polling polling) {
	if (const auto maybeStory = lookup(id)) {
		registerPolling(*maybeStory, polling);
		return true;
	}
	return false;
}

void Stories::unregisterPolling(FullStoryId id, Polling polling) {
	if (const auto maybeStory = lookup(id)) {
		unregisterPolling(*maybeStory, polling);
	} else if (const auto i = _deletingStories.find(id)
		; i != end(_deletingStories)) {
		unregisterPolling(i->second.get(), polling);
	} else {
		Unexpected("Couldn't find story for unregistering polling.");
	}
}

int Stories::pollingInterval(const PollingSettings &settings) const {
	return settings.viewer ? kPollingIntervalViewer : kPollingIntervalChat;
}

void Stories::maybeSchedulePolling(
		not_null<Story*> story,
		const PollingSettings &settings,
		TimeId now) {
	const auto last = story->lastUpdateTime();
	const auto next = last + pollingInterval(settings);
	const auto left = std::max(next - now, 0) * crl::time(1000) + 1;
	if (!_pollingTimer.isActive() || _pollingTimer.remainingTime() > left) {
		_pollingTimer.callOnce(left);
	}
}

void Stories::sendPollingRequests() {
	auto min = 0;
	const auto now = base::unixtime::now();
	for (const auto &[story, settings] : _pollingSettings) {
		const auto last = story->lastUpdateTime();
		const auto next = last + pollingInterval(settings);
		if (now >= next) {
			resolve(story->fullId(), nullptr, true);
		} else {
			const auto left = (next - now) * crl::time(1000) + 1;
			if (!min || left < min) {
				min = left;
			}
		}
	}
	if (min > 0) {
		_pollingTimer.callOnce(min);
	}
}

void Stories::sendPollingViewsRequests() {
	if (_pollingViews.empty()) {
		return;
	} else if (!_viewsRequestId) {
		Assert(_viewsDone == nullptr);
		const auto story = _pollingViews.front();
		loadViewsSlice(story->peer(), story->id(), QString(), nullptr);
	}
	_pollingViewsTimer.callOnce(kPollViewsInterval);
}

void Stories::updatePeerStoriesState(not_null<PeerData*> peer) {
	const auto till = _readTill.find(peer->id);
	const auto readTill = (till != end(_readTill)) ? till->second : 0;
	const auto pendingMaxId = [&] {
		const auto j = _pendingPeerStateMaxId.find(peer);
		return (j != end(_pendingPeerStateMaxId)) ? j->second : 0;
	};
	const auto i = _all.find(peer->id);
	const auto max = (i != end(_all))
		? (i->second.ids.empty() ? 0 : i->second.ids.back().id)
		: pendingMaxId();
	peer->setStoriesState(!max
		? PeerData::StoriesState::None
		: (max <= readTill)
		? PeerData::StoriesState::HasRead
		: PeerData::StoriesState::HasUnread);
}

void Stories::preloadSourcesChanged(StorySourcesList list) {
	if (rebuildPreloadSources(list)) {
		continuePreloading();
	}
}

bool Stories::rebuildPreloadSources(StorySourcesList list) {
	const auto index = static_cast<int>(list);
	const auto &counter = (list == StorySourcesList::Hidden)
		? _preloadingHiddenSourcesCounter
		: _preloadingMainSourcesCounter;
	if (!counter) {
		return !base::take(_toPreloadSources[index]).empty();
	}
	auto now = std::vector<FullStoryId>();
	auto processed = 0;
	for (const auto &source : _sources[index]) {
		const auto i = _all.find(source.id);
		if (i != end(_all)) {
			if (const auto id = i->second.toOpen().id) {
				const auto fullId = FullStoryId{ source.id, id };
				if (!_preloaded.contains(fullId)) {
					now.push_back(fullId);
				}
			}
		}
		if (++processed >= kMaxPreloadSources) {
			break;
		}
	}
	if (now != _toPreloadSources[index]) {
		_toPreloadSources[index] = std::move(now);
		return true;
	}
	return false;
}

void Stories::continuePreloading() {
	const auto now = _preloading ? _preloading->id() : FullStoryId();
	if (now) {
		if (shouldContinuePreload(now)) {
			return;
		}
		_preloading = nullptr;
	}
	const auto id = nextPreloadId();
	if (!id) {
		return;
	} else if (const auto maybeStory = lookup(id)) {
		startPreloading(*maybeStory);
	}
}

bool Stories::shouldContinuePreload(FullStoryId id) const {
	const auto first = ranges::views::concat(
		_toPreloadViewer,
		_toPreloadSources[static_cast<int>(StorySourcesList::Hidden)],
		_toPreloadSources[static_cast<int>(StorySourcesList::NotHidden)]
	) | ranges::views::take(kStillPreloadFromFirst);
	return ranges::contains(first, id);
}

FullStoryId Stories::nextPreloadId() const {
	const auto hidden = static_cast<int>(StorySourcesList::Hidden);
	const auto main = static_cast<int>(StorySourcesList::NotHidden);
	const auto result = !_toPreloadViewer.empty()
		? _toPreloadViewer.front()
		: !_toPreloadSources[hidden].empty()
		? _toPreloadSources[hidden].front()
		: !_toPreloadSources[main].empty()
		? _toPreloadSources[main].front()
		: FullStoryId();

	Ensures(!_preloaded.contains(result));
	return result;
}

void Stories::startPreloading(not_null<Story*> story) {
	Expects(!_preloaded.contains(story->fullId()));

	const auto id = story->fullId();
	auto preloading = std::make_unique<StoryPreload>(story, [=] {
		_preloading = nullptr;
		preloadFinished(id, true);
	});
	if (!_preloaded.contains(id)) {
		_preloading = std::move(preloading);
	}
}

void Stories::preloadFinished(FullStoryId id, bool markAsPreloaded) {
	for (auto &sources : _toPreloadSources) {
		sources.erase(ranges::remove(sources, id), end(sources));
	}
	_toPreloadViewer.erase(
		ranges::remove(_toPreloadViewer, id),
		end(_toPreloadViewer));
	if (markAsPreloaded) {
		_preloaded.emplace(id);
	}
	crl::on_main(this, [=] {
		continuePreloading();
	});
}

} // namespace Data
