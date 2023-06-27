/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_story.h"

#include "base/unixtime.h"
#include "api/api_text_entities.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/streaming/media_streaming_reader.h"
#include "storage/download_manager_mtproto.h"
#include "ui/text/text_utilities.h"

namespace Data {

class StoryPreload::LoadTask final : private Storage::DownloadMtprotoTask {
public:
	LoadTask(
		FullStoryId id,
		not_null<DocumentData*> document,
		Fn<void(QByteArray)> done);
	~LoadTask();

private:
	bool readyToRequest() const override;
	int64 takeNextRequestOffset() override;
	bool feedPart(int64 offset, const QByteArray &bytes) override;
	void cancelOnFail() override;
	bool setWebFileSizeHook(int64 size) override;

	base::flat_map<uint32, QByteArray> _parts;
	Fn<void(QByteArray)> _done;
	base::flat_set<int> _requestedOffsets;
	int64 _full = 0;
	int  _nextRequestOffset = 0;
	bool _finished = false;
	bool _failed = false;

};

StoryPreload::LoadTask::LoadTask(
	FullStoryId id,
	not_null<DocumentData*> document,
	Fn<void(QByteArray)> done)
: DownloadMtprotoTask(
	&document->session().downloader(),
	document->videoPreloadLocation(),
	FileOriginStory(id.peer, id.story))
, _done(std::move(done))
, _full(document->size) {
	const auto prefix = document->videoPreloadPrefix();
	Assert(prefix > 0 && prefix <= document->size);
	const auto part = Storage::kDownloadPartSize;
	const auto parts = (prefix + part - 1) / part;
	for (auto i = 0; i != parts; ++i) {
		_parts.emplace(i * part, QByteArray());
	}
	addToQueue();
}

StoryPreload::LoadTask::~LoadTask() {
	if (!_finished && !_failed) {
		cancelAllRequests();
	}
}

bool StoryPreload::LoadTask::readyToRequest() const {
	const auto part = Storage::kDownloadPartSize;
	return !_failed && (_nextRequestOffset < _parts.size() * part);
}

int64 StoryPreload::LoadTask::takeNextRequestOffset() {
	Expects(readyToRequest());

	_requestedOffsets.emplace(_nextRequestOffset);
	_nextRequestOffset += Storage::kDownloadPartSize;
	return _requestedOffsets.back();
}

bool StoryPreload::LoadTask::feedPart(
		int64 offset,
		const QByteArray &bytes) {
	Expects(offset < _parts.size() * Storage::kDownloadPartSize);
	Expects(_requestedOffsets.contains(int(offset)));
	Expects(bytes.size() <= Storage::kDownloadPartSize);

	const auto part = Storage::kDownloadPartSize;
	const auto index = offset / part;
	_requestedOffsets.remove(int(offset));
	_parts[offset] = bytes;
	if ((_nextRequestOffset + part >= _parts.size() * part)
		&& _requestedOffsets.empty()) {
		_finished = true;
		removeFromQueue();
		auto result = Media::Streaming::SerializeComplexPartsMap(_parts);
		if (result.size() == _full) {
			// Make sure it is parsed as a complex map.
			result.push_back(char(0));
		}
		_done(result);
	}
	return true;
}

void StoryPreload::LoadTask::cancelOnFail() {
	_failed = true;
	cancelAllRequests();
	_done({});
}

bool StoryPreload::LoadTask::setWebFileSizeHook(int64 size) {
	_failed = true;
	cancelAllRequests();
	_done({});
	return false;
}

Story::Story(
	StoryId id,
	not_null<PeerData*> peer,
	StoryMedia media,
	TimeId date,
	TimeId expires)
: _id(id)
, _peer(peer)
, _media(std::move(media))
, _date(date)
, _expires(expires) {
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

bool Story::mine() const {
	return _peer->isSelf();
}

StoryIdDates Story::idDates() const {
	return { _id, _date, _expires };
}

FullStoryId Story::fullId() const {
	return { _peer->id, _id };
}

TimeId Story::date() const {
	return _date;
}

TimeId Story::expires() const {
	return _expires;
}

bool Story::expired(TimeId now) const {
	return _expires <= (now ? now : base::unixtime::now());
}

bool Story::unsupported() const {
	return v::is_null(_media.data);
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
	}, [](v::null_t) {
		return false;
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
	}, [](v::null_t) {
		return (Image*)nullptr;
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

void Story::setIsPublic(bool isPublic) {
	_isPublic = isPublic;
}

bool Story::isPublic() const {
	return _isPublic;
}

void Story::setCloseFriends(bool closeFriends) {
	_closeFriends = closeFriends;
}

bool Story::closeFriends() const {
	return _closeFriends;
}

bool Story::canDownload() const {
	return _peer->isSelf();
}

bool Story::canShare() const {
	return isPublic() && (pinned() || !expired());
}

bool Story::canDelete() const {
	return _peer->isSelf();
}

bool Story::canReport() const {
	return !_peer->isSelf();
}

bool Story::hasDirectLink() const {
	if (!_isPublic || (!_pinned && expired())) {
		return false;
	}
	const auto user = _peer->asUser();
	return user && !user->username().isEmpty();
}

std::optional<QString> Story::errorTextForForward(
		not_null<Thread*> to) const {
	const auto peer = to->peer();
	const auto holdsPhoto = v::is<not_null<PhotoData*>>(_media.data);
	const auto first = holdsPhoto
		? ChatRestriction::SendPhotos
		: ChatRestriction::SendVideos;
	const auto second = holdsPhoto
		? ChatRestriction::SendVideos
		: ChatRestriction::SendPhotos;
	if (const auto error = Data::RestrictionError(peer, first)) {
		return *error;
	} else if (const auto error = Data::RestrictionError(peer, second)) {
		return *error;
	} else if (!Data::CanSend(to, first, false)
		|| !Data::CanSend(to, second, false)) {
		return tr::lng_forward_cant(tr::now);
	}
	return {};
}

void Story::setCaption(TextWithEntities &&caption) {
	_caption = std::move(caption);
}

const TextWithEntities &Story::caption() const {
	static const auto empty = TextWithEntities();
	return unsupported() ? empty : _caption;
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
	const auto isPublic = data.is_public();
	const auto closeFriends = data.is_close_friends();
	auto caption = TextWithEntities{
		data.vcaption().value_or_empty(),
		Api::EntitiesFromMTP(
			&owner().session(),
			data.ventities().value_or_empty()),
	};
	auto views = -1;
	auto recent = std::vector<not_null<PeerData*>>();
	if (!data.is_min()) {
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
	}

	const auto changed = (_media != media)
		|| (_pinned != pinned)
		|| (_isPublic != isPublic)
		|| (_closeFriends != closeFriends)
		|| (_caption != caption)
		|| (views >= 0 && _views != views)
		|| (_recentViewers != recent);
	if (!changed) {
		return false;
	}
	_media = std::move(media);
	_pinned = pinned;
	_isPublic = isPublic;
	_closeFriends = closeFriends;
	_caption = std::move(caption);
	if (views >= 0) {
		_views = views;
	}
	_recentViewers = std::move(recent);
	return true;
}

StoryPreload::StoryPreload(not_null<Story*> story, Fn<void()> done)
: _story(story)
, _done(std::move(done)) {
	start();
}

StoryPreload::~StoryPreload() {
	if (_photo) {
		base::take(_photo)->owner()->cancel();
	}
}

FullStoryId StoryPreload::id() const {
	return _story->fullId();
}

not_null<Story*> StoryPreload::story() const {
	return _story;
}

void StoryPreload::start() {
	const auto origin = FileOriginStory(
		_story->peer()->id,
		_story->id());
	if (const auto photo = _story->photo()) {
		_photo = photo->createMediaView();
		if (_photo->loaded()) {
			callDone();
		} else {
			_photo->automaticLoad(origin, _story->peer());
			photo->session().downloaderTaskFinished(
			) | rpl::filter([=] {
				return _photo->loaded();
			}) | rpl::start_with_next([=] { callDone(); }, _lifetime);
		}
	} else if (const auto video = _story->document()) {
		if (video->canBeStreamed(nullptr) && video->videoPreloadPrefix()) {
			const auto key = video->bigFileBaseCacheKey();
			if (key) {
				const auto weak = base::make_weak(this);
				video->owner().cacheBigFile().get(key, [weak](
						const QByteArray &result) {
					if (!result.isEmpty()) {
						crl::on_main([weak] {
							if (const auto strong = weak.get()) {
								strong->callDone();
							}
						});
					} else {
						crl::on_main([weak] {
							if (const auto strong = weak.get()) {
								strong->load();
							}
						});
					}
				});
			} else {
				callDone();
			}
		} else {
			callDone();
		}
	} else {
		callDone();
	}
}

void StoryPreload::load() {
	Expects(_story->document() != nullptr);

	const auto video = _story->document();
	const auto valid = video->videoPreloadLocation().valid();
	const auto prefix = video->videoPreloadPrefix();
	const auto key = video->bigFileBaseCacheKey();
	if (!valid || prefix <= 0 || prefix > video->size || !key) {
		callDone();
		return;
	}
	_task = std::make_unique<LoadTask>(id(), video, [=](QByteArray data) {
		if (!data.isEmpty()) {
			_story->owner().cacheBigFile().putIfEmpty(
				key,
				Storage::Cache::Database::TaggedValue(std::move(data), 0));
		}
		callDone();
	});
}

void StoryPreload::callDone() {
	if (const auto onstack = _done) {
		onstack();
	}
}

} // namespace Data
