/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_stories.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
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

using UpdateFlag = StoryUpdate::Flag;

std::optional<StoryMedia> ParseMedia(
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

bool Story::applyChanges(StoryMedia media, const MTPDstoryItem &data) {
	const auto pinned = data.is_pinned();
	auto caption = TextWithEntities{
		data.vcaption().value_or_empty(),
		Api::EntitiesFromMTP(
			&owner().session(),
			data.ventities().value_or_empty()),
	};
	const auto changed = (_media != media)
		|| (_pinned != pinned)
		|| (_caption != caption);
	if (!changed) {
		return false;
	}
	_media = std::move(media);
	_pinned = pinned;
	_caption = std::move(caption);
	return true;
}

Stories::Stories(not_null<Session*> owner) : _owner(owner) {
}

Stories::~Stories() {
}

Session &Stories::owner() const {
	return *_owner;
}

Main::Session &Stories::session() const {
	return _owner->session();
}

void Stories::apply(const MTPDupdateStories &data) {
	applyChanges(parse(data.vstories()));
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
			if (const auto story = parseAndApply(result.user, data)) {
				result.ids.push_back(story->id());
			} else {
				applyDeleted({ peerFromUser(userId), data.vid().v });
				--result.total;
			}
		}, [&](const MTPDstoryItemSkipped &data) {
			result.ids.push_back(data.vid().v);
		}, [&](const MTPDstoryItemDeleted &data) {
			applyDeleted({ peerFromUser(userId), data.vid().v });
			--result.total;
		});
	}
	result.total = std::max(result.total, int(result.ids.size()));
	return result;
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

void Stories::loadMore() {
	if (_loadMoreRequestId || _allLoaded) {
		return;
	}
	const auto api = &_owner->session().api();
	using Flag = MTPstories_GetAllStories::Flag;
	_loadMoreRequestId = api->request(MTPstories_GetAllStories(
		MTP_flags(_state.isEmpty()
			? Flag(0)
			: (Flag::f_next | Flag::f_state)),
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

void Stories::sendResolveRequests() {
	if (!_resolveRequests.empty()) {
		return;
	}
	struct Prepared {
		QVector<MTPint> ids;
		std::vector<Fn<void()>> callbacks;
	};
	auto leftToSend = kMaxResolveTogether;
	auto byPeer = base::flat_map<PeerId, Prepared>();
	for (auto i = begin(_resolves); i != end(_resolves);) {
		auto &[peerId, ids] = *i;
		auto &prepared = byPeer[peerId];
		for (auto &[storyId, callbacks] : ids) {
			prepared.ids.push_back(MTP_int(storyId));
			prepared.callbacks.insert(
				end(prepared.callbacks),
				std::make_move_iterator(begin(callbacks)),
				std::make_move_iterator(end(callbacks)));
			if (!--leftToSend) {
				break;
			}
		}
		const auto sending = int(prepared.ids.size());
		if (sending == ids.size()) {
			i = _resolves.erase(i);
			if (!leftToSend) {
				break;
			}
		} else {
			ids.erase(begin(ids), begin(ids) + sending);
			break;
		}
	}
	const auto api = &_owner->session().api();
	for (auto &entry : byPeer) {
		const auto peerId = entry.first;
		auto &prepared = entry.second;
		const auto finish = [=, ids = prepared.ids](mtpRequestId id) {
			for (const auto &id : ids) {
				finalizeResolve({ peerId, id.v });
			}
			if (auto callbacks = _resolveRequests.take(id)) {
				for (const auto &callback : *callbacks) {
					callback();
				}
			}
			if (_resolveRequests.empty() && !_resolves.empty()) {
				crl::on_main(&session(), [=] { sendResolveRequests(); });
			}
		};
		const auto user = _owner->session().data().peer(peerId)->asUser();
		if (!user) {
			_resolveRequests[0] = std::move(prepared.callbacks);
			finish(0);
			continue;
		}
		const auto requestId = api->request(MTPstories_GetStoriesByID(
			user->inputUser,
			MTP_vector<MTPint>(std::move(prepared.ids))
		)).done([=](const MTPstories_Stories &result, mtpRequestId id) {
			owner().processUsers(result.data().vusers());
			processResolvedStories(user, result.data().vstories().v);
			finish(id);
		}).fail([=](const MTP::Error &error, mtpRequestId id) {
			finish(id);
		}).send();
		_resolveRequests.emplace(requestId, std::move(prepared.callbacks));
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
	const auto i = _stories.find(id.peer);
	if (i != end(_stories)) {
		const auto j = i->second.find(id.story);
		if (j != end(i->second)) {
			auto story = std::move(j->second);
			i->second.erase(j);
			session().changes().storyUpdated(
				story.get(),
				UpdateFlag::Destroyed);
			removeDependencyStory(story.get());
			if (i->second.empty()) {
				_stories.erase(i);
			}
		}
	}
	const auto j = ranges::find(_all, id.peer, [](const StoriesList &list) {
		return list.user->id;
	});
	if (j != end(_all)) {
		const auto till = ranges::remove(j->ids, id.story);
		const auto removed = int(std::distance(till, end(j->ids)));
		if (till != end(j->ids)) {
			j->ids.erase(till, end(j->ids));
			j->total = std::max(j->total - removed, 0);
			if (j->ids.empty()) {
				_all.erase(j);
			}
			_allChanged.fire({});
		}
	}
	_deleted.emplace(id);
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
	const auto already = lookup(id);
	if (already.has_value() || already.error() != NoStory::Unknown) {
		done();
		return;
	}
	auto &ids = _resolves[id.peer];
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

void Stories::applyChanges(StoriesList &&list) {
	const auto i = ranges::find(_all, list.user, &StoriesList::user);
	if (i != end(_all)) {
		auto added = false;
		for (const auto id : list.ids) {
			if (!ranges::contains(i->ids, id)) {
				i->ids.insert(begin(i->ids), id);
				++i->total;
				added = true;
			}
		}
		if (added) {
			ranges::rotate(begin(_all), i, i + 1);
		}
	} else if (!list.ids.empty()) {
		_all.insert(begin(_all), std::move(list));
	}
}

} // namespace Data