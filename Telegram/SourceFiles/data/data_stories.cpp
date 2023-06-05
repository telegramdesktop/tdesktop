/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_stories.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"

// #TODO stories testing
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "storage/storage_shared_media.h"

namespace Data {
namespace {

constexpr auto kMaxResolveTogether = 100;
constexpr auto kIgnorePreloadAroundIfLoaded = 15;
constexpr auto kPreloadAroundCount = 30;
constexpr auto kMarkAsReadDelay = 3 * crl::time(1000);

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
				return StoryMedia{ result };
			}
		}
		return {};
	}, [](const auto &) { return std::optional<StoryMedia>(); });
}

} // namespace

StoriesSourceInfo StoriesSource::info() const {
	return {
		.id = user->id,
		.last = ids.empty() ? 0 : ids.back().date,
		.unread = unread(),
		.premium = user->isPremium(),
		.hidden = hidden,
	};
}

bool StoriesSource::unread() const {
	return !ids.empty() && readTill < ids.back().id;
}

Story::Story(
	StoryId id,
	not_null<PeerData*> peer,
	StoryMedia media,
	TimeId date)
: _id(id)
, _peer(peer)
, _media(std::move(media))
, _date(date) {
}

Session &Story::owner() const {
	return _peer->owner();
}

Main::Session &Story::session() const {
	return _peer->session();
}

not_null<PeerData*> Story::peer() const {
	return _peer;
}

StoryId Story::id() const {
	return _id;
}

StoryIdDate Story::idDate() const {
	return { _id, _date };
}

FullStoryId Story::fullId() const {
	return { _peer->id, _id };
}

TimeId Story::date() const {
	return _date;
}

const StoryMedia &Story::media() const {
	return _media;
}

PhotoData *Story::photo() const {
	const auto result = std::get_if<not_null<PhotoData*>>(&_media.data);
	return result ? result->get() : nullptr;
}

DocumentData *Story::document() const {
	const auto result = std::get_if<not_null<DocumentData*>>(&_media.data);
	return result ? result->get() : nullptr;
}

bool Story::hasReplyPreview() const {
	return v::match(_media.data, [](not_null<PhotoData*> photo) {
		return !photo->isNull();
	}, [](not_null<DocumentData*> document) {
		return document->hasThumbnail();
	});
}

Image *Story::replyPreview() const {
	return v::match(_media.data, [&](not_null<PhotoData*> photo) {
		return photo->getReplyPreview(
			Data::FileOriginStory(_peer->id, _id),
			_peer,
			false);
	}, [&](not_null<DocumentData*> document) {
		return document->getReplyPreview(
			Data::FileOriginStory(_peer->id, _id),
			_peer,
			false);
	});
}

TextWithEntities Story::inReplyText() const {
	const auto type = tr::lng_in_dlg_story(tr::now);
	return _caption.text.isEmpty()
		? Ui::Text::PlainLink(type)
		: tr::lng_dialogs_text_media(
			tr::now,
			lt_media_part,
			tr::lng_dialogs_text_media_wrapped(
				tr::now,
				lt_media,
				Ui::Text::PlainLink(type),
				Ui::Text::WithEntities),
			lt_caption,
			_caption,
			Ui::Text::WithEntities);
}

void Story::setPinned(bool pinned) {
	_pinned = pinned;
}

bool Story::pinned() const {
	return _pinned;
}

void Story::setCaption(TextWithEntities &&caption) {
	_caption = std::move(caption);
}

const TextWithEntities &Story::caption() const {
	return _caption;
}

void Story::setViewsData(
		std::vector<not_null<PeerData*>> recent,
		int total) {
	_recentViewers = std::move(recent);
	_views = total;
}

const std::vector<not_null<PeerData*>> &Story::recentViewers() const {
	return _recentViewers;
}

const std::vector<StoryView> &Story::viewsList() const {
	return _viewsList;
}

int Story::views() const {
	return _views;
}

void Story::applyViewsSlice(
		const std::optional<StoryView> &offset,
		const std::vector<StoryView> &slice,
		int total) {
	_views = total;
	if (!offset) {
		const auto i = _viewsList.empty()
			? end(slice)
			: ranges::find(slice, _viewsList.front());
		const auto merge = (i != end(slice))
			&& !ranges::contains(slice, _viewsList.back());
		if (merge) {
			_viewsList.insert(begin(_viewsList), begin(slice), i);
		} else {
			_viewsList = slice;
		}
	} else if (!slice.empty()) {
		const auto i = ranges::find(_viewsList, *offset);
		const auto merge = (i != end(_viewsList))
			&& !ranges::contains(_viewsList, slice.back());
		if (merge) {
			const auto after = i + 1;
			if (after == end(_viewsList)) {
				_viewsList.insert(after, begin(slice), end(slice));
			} else {
				const auto j = ranges::find(slice, _viewsList.back());
				if (j != end(slice)) {
					_viewsList.insert(end(_viewsList), j + 1, end(slice));
				}
			}
		}
	}
}

bool Story::applyChanges(StoryMedia media, const MTPDstoryItem &data) {
	const auto pinned = data.is_pinned();
	auto caption = TextWithEntities{
		data.vcaption().value_or_empty(),
		Api::EntitiesFromMTP(
			&owner().session(),
			data.ventities().value_or_empty()),
	};
	auto views = 0;
	auto recent = std::vector<not_null<PeerData*>>();
	if (const auto info = data.vviews()) {
		views = info->data().vviews_count().v;
		if (const auto list = info->data().vrecent_viewers()) {
			recent.reserve(list->v.size());
			auto &owner = _peer->owner();
			for (const auto &id : list->v) {
				recent.push_back(owner.peer(peerFromUser(id)));
			}
		}
	}

	const auto changed = (_media != media)
		|| (_pinned != pinned)
		|| (_caption != caption)
		|| (_views != views)
		|| (_recentViewers != recent);
	if (!changed) {
		return false;
	}
	_media = std::move(media);
	_pinned = pinned;
	_caption = std::move(caption);
	_views = views;
	_recentViewers = std::move(recent);
	return true;
}

Stories::Stories(not_null<Session*> owner)
: _owner(owner)
, _markReadTimer([=] { sendMarkAsReadRequests(); }) {
}

Stories::~Stories() {
}

Session &Stories::owner() const {
	return *_owner;
}

Main::Session &Stories::session() const {
	return _owner->session();
}

void Stories::apply(const MTPDupdateStory &data) {
	const auto peerId = peerFromUser(data.vuser_id());
	const auto user = not_null(_owner->peer(peerId)->asUser());
	const auto idDate = parseAndApply(user, data.vstory());
	if (!idDate) {
		return;
	}
	const auto i = _all.find(peerId);
	if (i == end(_all)) {
		requestUserStories(user);
		return;
	} else if (i->second.ids.contains(idDate)) {
		return;
	}
	const auto was = i->second.info();
	i->second.ids.emplace(idDate);
	const auto now = i->second.info();
	if (was == now) {
		return;
	}
	const auto refreshInList = [&](StorySourcesList list) {
		auto &sources = _sources[static_cast<int>(list)];
		const auto i = ranges::find(
			sources,
			peerId,
			&StoriesSourceInfo::id);
		if (i != end(sources)) {
			*i = now;
			sort(list);
		}
	};
	refreshInList(StorySourcesList::All);
	if (!user->hasStoriesHidden()) {
		refreshInList(StorySourcesList::NotHidden);
	}
}

void Stories::requestUserStories(not_null<UserData*> user) {
	if (!_requestingUserStories.emplace(user).second) {
		return;
	}
	_owner->session().api().request(MTPstories_GetUserStories(
		user->inputUser
	)).done([=](const MTPstories_UserStories &result) {
		_requestingUserStories.remove(user);
		const auto &data = result.data();
		_owner->processUsers(data.vusers());
		parseAndApply(data.vstories());
	}).fail([=] {
		_requestingUserStories.remove(user);
		applyDeletedFromSources(user->id, StorySourcesList::All);
	}).send();

}

void Stories::parseAndApply(const MTPUserStories &stories) {
	const auto &data = stories.data();
	const auto peerId = peerFromUser(data.vuser_id());
	const auto readTill = data.vmax_read_id().value_or_empty();
	const auto count = int(data.vstories().v.size());
	const auto user = _owner->peer(peerId)->asUser();
	auto result = StoriesSource{
		.user = user,
		.readTill = readTill,
		.hidden = user->hasStoriesHidden(),
	};
	const auto &list = data.vstories().v;
	result.ids.reserve(list.size());
	for (const auto &story : list) {
		if (const auto id = parseAndApply(result.user, story)) {
			result.ids.emplace(id);
		}
	}
	if (result.ids.empty()) {
		applyDeletedFromSources(peerId, StorySourcesList::All);
		return;
	}
	const auto info = result.info();
	const auto i = _all.find(peerId);
	if (i != end(_all)) {
		if (i->second != result) {
			i->second = std::move(result);
		}
	} else {
		_all.emplace(peerId, std::move(result)).first;
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
	add(StorySourcesList::All);
	if (result.user->hasStoriesHidden()) {
		applyDeletedFromSources(peerId, StorySourcesList::NotHidden);
	} else {
		add(StorySourcesList::NotHidden);
	}
}

Story *Stories::parseAndApply(
		not_null<PeerData*> peer,
		const MTPDstoryItem &data) {
	const auto media = ParseMedia(_owner, data.vmedia());
	if (!media) {
		return nullptr;
	}
	const auto id = data.vid().v;
	auto &stories = _stories[peer->id];
	const auto i = stories.find(id);
	if (i != end(stories)) {
		if (i->second->applyChanges(*media, data)) {
			session().changes().storyUpdated(
				i->second.get(),
				UpdateFlag::Edited);
		}
		return i->second.get();
	}
	const auto result = stories.emplace(id, std::make_unique<Story>(
		id,
		peer,
		StoryMedia{ *media },
		data.vdate().v)).first->second.get();
	result->applyChanges(*media, data);
	return result;
}

StoryIdDate Stories::parseAndApply(
		not_null<PeerData*> peer,
		const MTPstoryItem &story) {
	return story.match([&](const MTPDstoryItem &data) {
		if (const auto story = parseAndApply(peer, data)) {
			return story->idDate();
		}
		applyDeleted({ peer->id, data.vid().v });
		return StoryIdDate();
	}, [&](const MTPDstoryItemSkipped &data) {
		return StoryIdDate{ data.vid().v, data.vdate().v };
	}, [&](const MTPDstoryItemDeleted &data) {
		applyDeleted({ peer->id, data.vid().v });
		return StoryIdDate();
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

void Stories::loadMore(StorySourcesList list) {
	const auto index = static_cast<int>(list);
	if (_loadMoreRequestId[index] || _sourcesLoaded[index]) {
		return;
	}
	const auto all = (list == StorySourcesList::All);
	const auto api = &_owner->session().api();
	using Flag = MTPstories_GetAllStories::Flag;
	_loadMoreRequestId[index] = api->request(MTPstories_GetAllStories(
		MTP_flags((all ? Flag::f_include_hidden : Flag())
			| (_sourcesStates[index].isEmpty()
				? Flag(0)
				: (Flag::f_next | Flag::f_state))),
		MTP_string(_sourcesStates[index])
	)).done([=](const MTPstories_AllStories &result) {
		_loadMoreRequestId[index] = 0;

		result.match([&](const MTPDstories_allStories &data) {
			_owner->processUsers(data.vusers());
			_sourcesStates[index] = qs(data.vstate());
			_sourcesLoaded[index] = !data.is_has_more();
			for (const auto &single : data.vuser_stories().v) {
				parseAndApply(single);
			}
		}, [](const MTPDstories_allStoriesNotModified &) {
		});
	}).fail([=] {
		_loadMoreRequestId[index] = 0;
	}).send();
}

void Stories::sendResolveRequests() {
	if (!_resolveSent.empty()) {
		return;
	}
	auto leftToSend = kMaxResolveTogether;
	auto byPeer = base::flat_map<PeerId, QVector<MTPint>>();
	for (auto i = begin(_resolvePending); i != end(_resolvePending);) {
		auto &[peerId, ids] = *i;
		auto &sent = _resolveSent[peerId];
		if (ids.size() <= leftToSend) {
			sent = base::take(ids);
			i = _resolvePending.erase(i);
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
		const auto user = _owner->session().data().peer(peerId)->asUser();
		if (!user) {
			finish(peerId);
			continue;
		}
		const auto requestId = api->request(MTPstories_GetStoriesByID(
			user->inputUser,
			MTP_vector<MTPint>(prepared)
		)).done([=](const MTPstories_Stories &result) {
			owner().processUsers(result.data().vusers());
			processResolvedStories(user, result.data().vstories().v);
			finish(user->id);
		}).fail([=] {
			finish(peerId);
		}).send();
	 }
}

void Stories::processResolvedStories(
		not_null<PeerData*> peer,
		const QVector<MTPStoryItem> &list) {
	for (const auto &item : list) {
		item.match([&](const MTPDstoryItem &data) {
			if (!parseAndApply(peer, data)) {
				applyDeleted({ peer->id, data.vid().v });
			}
		}, [&](const MTPDstoryItemSkipped &data) {
			LOG(("API Error: Unexpected storyItemSkipped in resolve."));
		}, [&](const MTPDstoryItemDeleted &data) {
			applyDeleted({ peer->id, data.vid().v });
		});
	}
}

void Stories::finalizeResolve(FullStoryId id) {
	const auto already = lookup(id);
	if (!already.has_value() && already.error() == NoStory::Unknown) {
		LOG(("API Error: Could not resolve story %1_%2"
			).arg(id.peer.value
			).arg(id.story));
		applyDeleted(id);
	}
}

void Stories::applyDeleted(FullStoryId id) {
	const auto removeFromList = [&](StorySourcesList list) {
		const auto index = static_cast<int>(list);
		auto &sources = _sources[index];
		const auto i = ranges::find(
			sources,
			id.peer,
			&StoriesSourceInfo::id);
		if (i != end(sources)) {
			sources.erase(i);
			_sourcesChanged[index].fire({});
		}
	};
	const auto i = _all.find(id.peer);
	if (i != end(_all)) {
		const auto j = i->second.ids.lower_bound(StoryIdDate{ id.story });
		if (j != end(i->second.ids) && j->id == id.story) {
			i->second.ids.erase(j);
			if (i->second.ids.empty()) {
				_all.erase(i);
				removeFromList(StorySourcesList::NotHidden);
				removeFromList(StorySourcesList::All);
			}
		}
	}
	_deleted.emplace(id);
	const auto j = _stories.find(id.peer);
	if (j != end(_stories)) {
		const auto k = j->second.find(id.story);
		if (k != end(j->second)) {
			auto story = std::move(k->second);
			j->second.erase(k);
			session().changes().storyUpdated(
				story.get(),
				UpdateFlag::Destroyed);
			removeDependencyStory(story.get());
			if (j->second.empty()) {
				_stories.erase(j);
			}
		}
	}
}

void Stories::applyDeletedFromSources(PeerId id, StorySourcesList list) {
	const auto removeFromList = [&](StorySourcesList from) {
		auto &sources = _sources[static_cast<int>(from)];
		const auto i = ranges::find(
			sources,
			id,
			&StoriesSourceInfo::id);
		if (i != end(sources)) {
			sources.erase(i);
		}
		_sourcesChanged[static_cast<int>(from)].fire({});
	};
	removeFromList(StorySourcesList::NotHidden);
	if (list == StorySourcesList::All) {
		removeFromList(StorySourcesList::All);
	}
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
	const auto self = _owner->session().user()->id;
	const auto proj = [&](const StoriesSourceInfo &info) {
		const auto key = int64(info.last)
			+ (info.premium ? (int64(1) << 48) : 0)
			+ (info.unread ? (int64(1) << 49) : 0)
			+ ((info.id == self) ? (int64(1) << 50) : 0);
		return std::make_pair(key, info.id);
	};
	ranges::sort(sources, ranges::greater(), proj);
	_sourcesChanged[index].fire({});
}

const base::flat_map<PeerId, StoriesSource> &Stories::all() const {
	return _all;
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

void Stories::resolve(FullStoryId id, Fn<void()> done) {
	const auto already = lookup(id);
	if (already.has_value() || already.error() != NoStory::Unknown) {
		if (done) {
			done();
		}
		return;
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
	const auto j = i->second.ids.lower_bound(StoryIdDate{ id.story });
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
	const auto i = _all.find(id.peer);
	Assert(i != end(_all));
	if (i->second.readTill >= id.story) {
		return;
	} else if (!_markReadPending.contains(id.peer)) {
		sendMarkAsReadRequests();
	}
	_markReadPending.emplace(id.peer);
	const auto wasUnread = i->second.unread();
	i->second.readTill = id.story;
	const auto nowUnread = i->second.unread();
	if (wasUnread != nowUnread) {
		const auto refreshInList = [&](StorySourcesList list) {
			auto &sources = _sources[static_cast<int>(list)];
			const auto i = ranges::find(
				sources,
				id.peer,
				&StoriesSourceInfo::id);
			if (i != end(sources)) {
				i->unread = nowUnread;
				sort(list);
			}
		};
		refreshInList(StorySourcesList::All);
		refreshInList(StorySourcesList::NotHidden);
	}
	_markReadTimer.callOnce(kMarkAsReadDelay);
}

void Stories::toggleHidden(PeerId peerId, bool hidden) {
	const auto user = _owner->peer(peerId)->asUser();
	Assert(user != nullptr);
	if (user->hasStoriesHidden() != hidden) {
		user->setFlags(hidden
			? (user->flags() | UserDataFlag::StoriesHidden)
			: (user->flags() & ~UserDataFlag::StoriesHidden));
		session().api().request(MTPcontacts_ToggleStoriesHidden(
			user->inputUser,
			MTP_bool(hidden)
		)).send();
	}

	const auto i = _all.find(peerId);
	if (i == end(_all)) {
		return;
	}
	i->second.hidden = hidden;
	const auto main = static_cast<int>(StorySourcesList::NotHidden);
	const auto all = static_cast<int>(StorySourcesList::All);
	if (hidden) {
		const auto i = ranges::find(
			_sources[main],
			peerId,
			&StoriesSourceInfo::id);
		if (i != end(_sources[main])) {
			_sources[main].erase(i);
			_sourcesChanged[main].fire({});
		}
		const auto j = ranges::find(_sources[all], peerId, &StoriesSourceInfo::id);
		if (j != end(_sources[all])) {
			j->hidden = hidden;
			_sourcesChanged[all].fire({});
		}
	} else {
		const auto i = ranges::find(
			_sources[all],
			peerId,
			&StoriesSourceInfo::id);
		if (i != end(_sources[all])) {
			i->hidden = hidden;
			_sourcesChanged[all].fire({});

			auto &sources = _sources[main];
			if (!ranges::contains(sources, peerId, &StoriesSourceInfo::id)) {
				sources.push_back(*i);
				sort(StorySourcesList::NotHidden);
			}
		}
	}
}

void Stories::sendMarkAsReadRequest(
		not_null<PeerData*> peer,
		StoryId tillId) {
	Expects(peer->isUser());

	const auto peerId = peer->id;
	_markReadRequests.emplace(peerId);
	const auto finish = [=] {
		_markReadRequests.remove(peerId);
		if (!_markReadTimer.isActive()
			&& _markReadPending.contains(peerId)) {
			sendMarkAsReadRequests();
		}
		if (_markReadRequests.empty()) {
			if (Core::Quitting()) {
				LOG(("Stories doesn't prevent quit any more."));
			}
			Core::App().quitPreventFinished();
		}
	};

	const auto api = &_owner->session().api();
	api->request(MTPstories_ReadStories(
		peer->asUser()->inputUser,
		MTP_int(tillId)
	)).done(finish).fail(finish).send();
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
			sendMarkAsReadRequest(j->second.user, j->second.readTill);
		}
		i = _markReadPending.erase(i);
	}
}

void Stories::loadViewsSlice(
		StoryId id,
		std::optional<StoryView> offset,
		Fn<void(std::vector<StoryView>)> done) {
	_viewsDone = std::move(done);
	if (_viewsStoryId == id && _viewsOffset == offset) {
		return;
	}
	_viewsStoryId = id;
	_viewsOffset = offset;

	const auto api = &_owner->session().api();
	api->request(_viewsRequestId).cancel();
	_viewsRequestId = api->request(MTPstories_GetStoryViewsList(
		MTP_int(id),
		MTP_int(offset ? offset->date : 0),
		MTP_long(offset ? peerToUser(offset->peer->id).bare : 0),
		MTP_int(2)
	)).done([=](const MTPstories_StoryViewsList &result) {
		_viewsRequestId = 0;

		auto slice = std::vector<StoryView>();

		const auto &data = result.data();
		_owner->processUsers(data.vusers());
		slice.reserve(data.vviews().v.size());
		for (const auto &view : data.vviews().v) {
			slice.push_back({
				.peer = _owner->peer(peerFromUser(view.data().vuser_id())),
				.date = view.data().vdate().v,
			});
		}
		const auto fullId = FullStoryId{
			.peer = _owner->session().userPeerId(),
			.story = _viewsStoryId,
		};
		if (const auto story = lookup(fullId)) {
			(*story)->applyViewsSlice(_viewsOffset, slice, data.vcount().v);
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

bool Stories::isQuitPrevent() {
	if (!_markReadPending.empty()) {
		sendMarkAsReadRequests();
	}
	if (_markReadRequests.empty()) {
		return false;
	}
	LOG(("Stories prevents quit, marking as read..."));
	return true;
}

} // namespace Data