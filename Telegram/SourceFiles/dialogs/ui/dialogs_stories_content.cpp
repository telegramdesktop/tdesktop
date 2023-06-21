/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_stories_content.h"

#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_stories_list.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"

namespace Dialogs::Stories {
namespace {

constexpr auto kShownLastCount = 3;

class PeerUserpic final : public Thumbnail {
public:
	explicit PeerUserpic(not_null<PeerData*> peer);

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	struct Subscribed {
		explicit Subscribed(Fn<void()> callback)
		: callback(std::move(callback)) {
		}

		Ui::PeerUserpicView view;
		Fn<void()> callback;
		InMemoryKey key;
		rpl::lifetime photoLifetime;
		rpl::lifetime downloadLifetime;
	};

	[[nodiscard]] bool waitingUserpicLoad() const;
	void processNewPhoto();

	const not_null<PeerData*> _peer;
	QImage _frame;
	std::unique_ptr<Subscribed> _subscribed;

};

class StoryThumbnail : public Thumbnail {
public:
	explicit StoryThumbnail(FullStoryId id);
	virtual ~StoryThumbnail() = default;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

protected:
	struct Thumb {
		Image *image = nullptr;
		bool blurred = false;
	};
	[[nodiscard]] virtual Main::Session &session() = 0;
	[[nodiscard]] virtual Thumb loaded(FullStoryId id) = 0;
	virtual void clear() = 0;

private:
	const FullStoryId _id;
	QImage _full;
	rpl::lifetime _subscription;
	QImage _prepared;
	bool _blurred = false;

};

class PhotoThumbnail final : public StoryThumbnail {
public:
	PhotoThumbnail(not_null<PhotoData*> photo, FullStoryId id);

private:
	Main::Session &session() override;
	Thumb loaded(FullStoryId id) override;
	void clear() override;

	const not_null<PhotoData*> _photo;
	std::shared_ptr<Data::PhotoMedia> _media;

};

class VideoThumbnail final : public StoryThumbnail {
public:
	VideoThumbnail(not_null<DocumentData*> video, FullStoryId id);

private:
	Main::Session &session() override;
	Thumb loaded(FullStoryId id) override;
	void clear() override;

	const not_null<DocumentData*> _video;
	std::shared_ptr<Data::DocumentMedia> _media;

};

class EmptyThumbnail final : public Thumbnail {
public:
	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	QImage _cached;

};

class State final {
public:
	State(not_null<Data::Stories*> data, Data::StorySourcesList list);

	[[nodiscard]] Content next();

private:
	const not_null<Data::Stories*> _data;
	const Data::StorySourcesList _list;
	base::flat_map<
		not_null<UserData*>,
		std::shared_ptr<Thumbnail>> _userpics;

};

PeerUserpic::PeerUserpic(not_null<PeerData*> peer)
: _peer(peer) {
}

QImage PeerUserpic::image(int size) {
	Expects(_subscribed != nullptr);

	const auto good = (_frame.width() == size * _frame.devicePixelRatio());
	const auto key = _peer->userpicUniqueKey(_subscribed->view);
	if (!good || (_subscribed->key != key && !waitingUserpicLoad())) {
		_subscribed->key = key;
		_frame = QImage(
			QSize(size, size) * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(style::DevicePixelRatio());
		_frame.fill(Qt::transparent);

		auto p = Painter(&_frame);
		_peer->paintUserpic(p, _subscribed->view, 0, 0, size);
	}
	return _frame;
}

bool PeerUserpic::waitingUserpicLoad() const {
	return _peer->hasUserpic() && _peer->useEmptyUserpic(_subscribed->view);
}

void PeerUserpic::subscribeToUpdates(Fn<void()> callback) {
	if (!callback) {
		_subscribed = nullptr;
		return;
	}
	_subscribed = std::make_unique<Subscribed>(std::move(callback));

	_peer->session().changes().peerUpdates(
		_peer,
		Data::PeerUpdate::Flag::Photo
	) | rpl::start_with_next([=] {
		_subscribed->callback();
		processNewPhoto();
	}, _subscribed->photoLifetime);

	processNewPhoto();
}

void PeerUserpic::processNewPhoto() {
	Expects(_subscribed != nullptr);

	if (!waitingUserpicLoad()) {
		_subscribed->downloadLifetime.destroy();
		return;
	}
	_peer->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return !waitingUserpicLoad();
	}) | rpl::start_with_next([=] {
		_subscribed->callback();
		_subscribed->downloadLifetime.destroy();
	}, _subscribed->downloadLifetime);
}

StoryThumbnail::StoryThumbnail(FullStoryId id)
: _id(id) {
}

QImage StoryThumbnail::image(int size) {
	const auto ratio = style::DevicePixelRatio();
	if (_prepared.width() != size * ratio) {
		if (_full.isNull()) {
			_prepared = QImage(
				QSize(size, size) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_prepared.fill(Qt::black);
		} else {
			const auto width = _full.width();
			const auto skip = std::max((_full.height() - width) / 2, 0);
			_prepared = _full.copy(0, skip, width, width).scaled(
				QSize(size, size) * ratio,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		}
		_prepared = Images::Circle(std::move(_prepared));
	}
	return _prepared;
}

void StoryThumbnail::subscribeToUpdates(Fn<void()> callback) {
	_subscription.destroy();
	if (!callback) {
		clear();
		return;
	} else if (!_full.isNull() && !_blurred) {
		return;
	}
	const auto thumbnail = loaded(_id);
	if (const auto image = thumbnail.image) {
		_full = image->original();
	}
	_blurred = thumbnail.blurred;
	if (!_blurred) {
		_prepared = QImage();
	} else {
		_subscription = session().downloaderTaskFinished(
		) | rpl::filter([=] {
			const auto thumbnail = loaded(_id);
			if (!thumbnail.blurred) {
				_full = thumbnail.image->original();
				_prepared = QImage();
				_blurred = false;
				return true;
			}
			return false;
		}) | rpl::take(1) | rpl::start_with_next(callback);
	}
}

PhotoThumbnail::PhotoThumbnail(not_null<PhotoData*> photo, FullStoryId id)
: StoryThumbnail(id)
, _photo(photo) {
}

Main::Session &PhotoThumbnail::session() {
	return _photo->session();
}

StoryThumbnail::Thumb PhotoThumbnail::loaded(FullStoryId id) {
	if (!_media) {
		_media = _photo->createMediaView();
		_media->wanted(
			Data::PhotoSize::Small,
			Data::FileOriginStory(id.peer, id.story));
	}
	if (const auto small = _media->image(Data::PhotoSize::Small)) {
		return { .image = small };
	}
	return { .image = _media->thumbnailInline(), .blurred = true };
}

void PhotoThumbnail::clear() {
	_media = nullptr;
}

VideoThumbnail::VideoThumbnail(
	not_null<DocumentData*> video,
	FullStoryId id)
: StoryThumbnail(id)
, _video(video) {
}

Main::Session &VideoThumbnail::session() {
	return _video->session();
}

StoryThumbnail::Thumb VideoThumbnail::loaded(FullStoryId id) {
	if (!_media) {
		_media = _video->createMediaView();
		_media->thumbnailWanted(Data::FileOriginStory(id.peer, id.story));
	}
	if (const auto small = _media->thumbnail()) {
		return { .image = small };
	}
	return { .image = _media->thumbnailInline(), .blurred = true };
}

void VideoThumbnail::clear() {
	_media = nullptr;
}

QImage EmptyThumbnail::image(int size) {
	const auto ratio = style::DevicePixelRatio();
	if (_cached.width() != size * ratio) {
		_cached = QImage(
			QSize(size, size) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_cached.fill(Qt::black);
	}
	return _cached;
}

void EmptyThumbnail::subscribeToUpdates(Fn<void()> callback) {
}

State::State(not_null<Data::Stories*> data, Data::StorySourcesList list)
: _data(data)
, _list(list) {
}

Content State::next() {
	auto result = Content{
		.hidden = (_list == Data::StorySourcesList::Hidden)
	};
	const auto &sources = _data->sources(_list);
	result.elements.reserve(sources.size());
	for (const auto &info : sources) {
		const auto source = _data->source(info.id);
		Assert(source != nullptr);

		auto userpic = std::shared_ptr<Thumbnail>();
		const auto user = source->user;
		if (const auto i = _userpics.find(user); i != end(_userpics)) {
			userpic = i->second;
		} else {
			userpic = std::make_shared<PeerUserpic>(user);
			_userpics.emplace(user, userpic);
		}
		result.elements.push_back({
			.id = uint64(user->id.value),
			.name = (user->isSelf()
				? tr::lng_stories_my_name(tr::now)
				: user->shortName()),
			.thumbnail = std::move(userpic),
			.unread = info.unread,
			.suggestHide = !info.hidden,
			.suggestUnhide = info.hidden,
			.profile = true,
			.skipSmall = user->isSelf(),
		});
	}
	return result;
}

} // namespace

rpl::producer<Content> ContentForSession(
		not_null<Main::Session*> session,
		Data::StorySourcesList list) {
	return [=](auto consumer) {
		auto result = rpl::lifetime();
		const auto stories = &session->data().stories();
		const auto state = result.make_state<State>(stories, list);
		rpl::single(
			rpl::empty
		) | rpl::then(
			stories->sourcesChanged(list)
		) | rpl::start_with_next([=] {
			consumer.put_next(state->next());
		}, result);
		return result;
	};
}

[[nodiscard]] std::shared_ptr<Thumbnail> PrepareThumbnail(
		not_null<Data::Story*> story) {
	using Result = std::shared_ptr<Thumbnail>;
	const auto id = story->fullId();
	return v::match(story->media().data, [](v::null_t) -> Result {
		return std::make_shared<EmptyThumbnail>();
	}, [&](not_null<PhotoData*> photo) -> Result {
		return std::make_shared<PhotoThumbnail>(photo, id);
	}, [&](not_null<DocumentData*> video) -> Result {
		return std::make_shared<VideoThumbnail>(video, id);
	});
}

rpl::producer<Content> LastForPeer(not_null<PeerData*> peer) {
	using namespace rpl::mappers;

	const auto stories = &peer->owner().stories();
	const auto peerId = peer->id;

	return rpl::single(
		peerId
	) | rpl::then(
		stories->sourceChanged() | rpl::filter(_1 == peerId)
	) | rpl::map([=] {
		auto ids = std::vector<StoryId>();
		auto readTill = StoryId();
		if (const auto source = stories->source(peerId)) {
			readTill = source->readTill;
			ids = ranges::views::all(source->ids)
				| ranges::views::reverse
				| ranges::views::take(kShownLastCount)
				| ranges::views::transform(&Data::StoryIdDates::id)
				| ranges::to_vector;
		}
		return rpl::make_producer<Content>([=](auto consumer) {
			auto lifetime = rpl::lifetime();

			struct State {
				Fn<void()> check;
				base::has_weak_ptr guard;
				bool pushed = false;
			};
			const auto state = lifetime.make_state<State>();
			state->check = [=] {
				if (state->pushed) {
					return;
				}
				auto resolving = false;
				auto result = Content{};
				for (const auto id : ids) {
					const auto storyId = FullStoryId{ peerId, id };
					const auto maybe = stories->lookup(storyId);
					if (maybe) {
						if (!resolving) {
							result.elements.reserve(ids.size());
							result.elements.push_back({
								.id = uint64(id),
								.thumbnail = PrepareThumbnail(*maybe),
								.unread = (id > readTill),
							});
						}
					} else if (maybe.error() == Data::NoStory::Unknown) {
						resolving = true;
						stories->resolve(
							storyId,
							crl::guard(&state->guard, state->check));
					}
				}
				if (resolving) {
					return;
				}
				state->pushed = true;
				consumer.put_next(std::move(result));
				consumer.put_done();
			};
			rpl::single(peerId) | rpl::then(
				stories->itemsChanged() | rpl::filter(_1 == peerId)
			) | rpl::start_with_next(state->check, lifetime);

			return lifetime;
		});
	}) | rpl::flatten_latest();
}

} // namespace Dialogs::Stories
