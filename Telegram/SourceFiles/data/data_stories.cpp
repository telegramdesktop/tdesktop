/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_stories.h"

#include "api/api_text_entities.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "apiwrap.h"

// #TODO stories testing
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "storage/storage_shared_media.h"

namespace Data {
namespace {

} // namespace

bool StoriesList::unread() const {
	return !ids.empty() && readTill < ids.front();
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

void Story::apply(const MTPDstoryItem &data) {
	_pinned = data.is_pinned();
	_caption = TextWithEntities{
		data.vcaption().value_or_empty(),
		Api::EntitiesFromMTP(
			&owner().session(),
			data.ventities().value_or_empty()),
	};
}

Stories::Stories(not_null<Session*> owner) : _owner(owner) {
}

Stories::~Stories() {
}

Session &Stories::owner() const {
	return *_owner;
}

void Stories::apply(const MTPDupdateStories &data) {
	pushToFront(parse(data.vstories()));
	_allChanged.fire({});
}

StoriesList Stories::parse(const MTPUserStories &stories) {
	const auto &data = stories.data();
	const auto userId = UserId(data.vuser_id());
	const auto readTill = data.vmax_read_id().value_or_empty();
	const auto count = int(data.vstories().v.size());
	auto result = StoriesList{
		.user = _owner->user(userId),
		.readTill = readTill,
		.total = count,
	};
	const auto &list = data.vstories().v;
	result.ids.reserve(list.size());
	for (const auto &story : list) {
		story.match([&](const MTPDstoryItem &data) {
			if (const auto story = parse(result.user, data)) {
				result.ids.push_back(story->id());
			} else {
				--result.total;
			}
		}, [&](const MTPDstoryItemSkipped &data) {
			result.ids.push_back(data.vid().v);
		}, [&](const MTPDstoryItemDeleted &data) {
			_deleted.emplace(FullStoryId{
				.peer = peerFromUser(userId),
				.story = data.vid().v,
			});
			--result.total;
		});
	}
	result.total = std::max(result.total, int(result.ids.size()));
	return result;
}

Story *Stories::parse(not_null<PeerData*> peer, const MTPDstoryItem &data) {
	const auto id = data.vid().v;
	auto &stories = _stories[peer->id];
	const auto i = stories.find(id);
	if (i != end(stories)) {
		i->second->apply(data);
		return i->second.get();
	}
	using MaybeMedia = std::optional<
		std::variant<not_null<PhotoData*>, not_null<DocumentData*>>>;
	const auto media = data.vmedia().match([&](
			const MTPDmessageMediaPhoto &data) -> MaybeMedia {
		if (const auto photo = data.vphoto()) {
			const auto result = _owner->processPhoto(*photo);
			if (!result->isNull()) {
				return result;
			}
		}
		return {};
	}, [&](const MTPDmessageMediaDocument &data) -> MaybeMedia {
		if (const auto document = data.vdocument()) {
			const auto result = _owner->processDocument(*document);
			if (!result->isNull()
				&& (result->isGifv() || result->isVideoFile())) {
				return result;
			}
		}
		return {};
	}, [](const auto &) { return MaybeMedia(); });
	if (!media) {
		return nullptr;
	}
	const auto result = stories.emplace(id, std::make_unique<Story>(
		id,
		peer,
		StoryMedia{ *media },
		data.vdate().v)).first->second.get();
	result->apply(data);
	return result;
}

void Stories::loadMore() {
	if (_loadMoreRequestId || _allLoaded) {
		return;
	}
	const auto api = &_owner->session().api();
	using Flag = MTPstories_GetAllStories::Flag;
	_loadMoreRequestId = api->request(MTPstories_GetAllStories(
		MTP_flags(_state.isEmpty() ? Flag(0) : Flag::f_next),
		MTP_string(_state)
	)).done([=](const MTPstories_AllStories &result) {
		_loadMoreRequestId = 0;

		result.match([&](const MTPDstories_allStories &data) {
			_owner->processUsers(data.vusers());
			_state = qs(data.vstate());
			_allLoaded = !data.is_has_more();
			for (const auto &single : data.vuser_stories().v) {
				pushToBack(parse(single));
			}
			_allChanged.fire({});
		}, [](const MTPDstories_allStoriesNotModified &) {
		});
	}).fail([=] {
		_loadMoreRequestId = 0;
	}).send();
}

const std::vector<StoriesList> &Stories::all() {
	return _all;
}

bool Stories::allLoaded() const {
	return _allLoaded;
}

rpl::producer<> Stories::allChanged() const {
	return _allChanged.events();
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
}

void Stories::pushToBack(StoriesList &&list) {
	const auto i = ranges::find(_all, list.user, &StoriesList::user);
	if (i != end(_all)) {
		if (*i == list) {
			return;
		}
		*i = std::move(list);
	} else {
		_all.push_back(std::move(list));
	}
}

void Stories::pushToFront(StoriesList &&list) {
	const auto i = ranges::find(_all, list.user, &StoriesList::user);
	if (i != end(_all)) {
		*i = std::move(list);
		ranges::rotate(begin(_all), i, i + 1);
	} else {
		_all.insert(begin(_all), std::move(list));
	}
}

} // namespace Data