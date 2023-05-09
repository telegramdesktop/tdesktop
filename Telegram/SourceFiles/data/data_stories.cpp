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
	return !items.empty() && readTill < items.front().id;
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
	result.items.reserve(list.size());
	for (const auto &story : list) {
		story.match([&](const MTPDstoryItem &data) {
			if (auto entry = parse(data)) {
				result.items.push_back(std::move(*entry));
			} else {
				--result.total;
			}
		}, [&](const MTPDstoryItemSkipped &) {
		}, [&](const MTPDstoryItemDeleted &) {
			--result.total;
		});
	}
	result.total = std::min(result.total, int(result.items.size()));
	return result;
}

std::optional<StoryItem> Stories::parse(const MTPDstoryItem &data) {
	const auto id = data.vid().v;
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
		return {};
	}
	auto caption = TextWithEntities{
		data.vcaption().value_or_empty(),
		Api::EntitiesFromMTP(
			&_owner->session(),
			data.ventities().value_or_empty()),
	};
	auto privacy = StoryPrivacy();

	const auto date = data.vdate().v;
	return StoryItem{
		.id = data.vid().v,
		.media = { *media },
		.caption = std::move(caption),
		.date = date,
		.privacy = privacy,
	};
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

// #TODO stories testing
StoryId Stories::generate(
	not_null<HistoryItem*> item,
	std::variant<
		v::null_t,
		not_null<PhotoData*>,
		not_null<DocumentData*>> media) {
	if (v::is_null(media)
		|| !item->from()->isUser()
		|| !item->isRegular()) {
		return {};
	}
	const auto document = v::is<not_null<DocumentData*>>(media)
		? v::get<not_null<DocumentData*>>(media).get()
		: nullptr;
	if (document && !document->isVideoFile()) {
		return {};
	}
	using namespace Storage;
	auto resultId = StoryId();
	const auto listType = SharedMediaType::PhotoVideo;
	const auto itemId = item->id;
	const auto peer = item->history()->peer;
	const auto session = &peer->session();
	auto full = std::vector<StoriesList>();
	const auto lifetime = session->storage().query(SharedMediaQuery(
		SharedMediaKey(peer->id, MsgId(0), listType, itemId),
		32,
		32
	)) | rpl::start_with_next([&](SharedMediaResult &&result) {
		if (!result.messageIds.contains(itemId)) {
			result.messageIds.emplace(itemId);
		}
		auto index = StoryId();
		const auto owner = &peer->owner();
		for (const auto id : result.messageIds) {
			if (const auto item = owner->message(peer, id)) {
				const auto user = item->from()->asUser();
				if (!user) {
					continue;
				}
				const auto i = ranges::find(
					full,
					not_null(user),
					&StoriesList::user);
				auto &stories = (i == end(full))
					? full.emplace_back(StoriesList{ .user = user })
					: *i;
				if (id == itemId) {
					resultId = ++index;
					stories.items.push_back({
						.id = resultId,
						.media = (document
							? StoryMedia{ not_null(document) }
							: StoryMedia{
								v::get<not_null<PhotoData*>>(media) }),
						.caption = item->originalText(),
						.date = item->date(),
					});
					++stories.total;
				} else if (const auto media = item->media()) {
					const auto photo = media->photo();
					const auto document = media->document();
					if (photo || (document && document->isVideoFile())) {
						stories.items.push_back({
							.id = ++index,
							.media = (document
								? StoryMedia{ not_null(document) }
								: StoryMedia{ not_null(photo) }),
							.caption = item->originalText(),
							.date = item->date(),
						});
						++stories.total;
					}
				}
			}
		}
		for (auto &stories : full) {
			const auto i = ranges::find(
				_all,
				stories.user,
				&StoriesList::user);
			if (i != end(_all)) {
				*i = std::move(stories);
			} else {
				_all.push_back(std::move(stories));
			}
		}
	});
	return resultId;
}

void Stories::pushToBack(StoriesList &&list) {
	const auto i = ranges::find(_all, list.user, &StoriesList::user);
	if (i != end(_all)) {
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