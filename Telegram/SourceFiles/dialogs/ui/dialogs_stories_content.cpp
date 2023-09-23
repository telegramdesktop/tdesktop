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
#include "info/stories/info_stories_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

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
		not_null<PeerData*>,
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
		const auto ratio = style::DevicePixelRatio();
		_subscribed->key = key;
		_frame = QImage(
			QSize(size, size) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
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
		_prepared.setDevicePixelRatio(ratio);
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
		_cached.setDevicePixelRatio(ratio);
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
	auto result = Content();
	const auto &sources = _data->sources(_list);
	result.elements.reserve(sources.size());
	for (const auto &info : sources) {
		const auto source = _data->source(info.id);
		Assert(source != nullptr);

		auto userpic = std::shared_ptr<Thumbnail>();
		const auto peer = source->peer;
		if (const auto i = _userpics.find(peer); i != end(_userpics)) {
			userpic = i->second;
		} else {
			userpic = MakeUserpicThumbnail(peer);
			_userpics.emplace(peer, userpic);
		}
		result.elements.push_back({
			.id = uint64(peer->id.value),
			.name = peer->shortName(),
			.thumbnail = std::move(userpic),
			.count = info.count,
			.unreadCount = info.unreadCount,
			.skipSmall = peer->isSelf() ? 1U : 0U,
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
			if (ids.empty()) {
				consumer.put_next(Content());
				consumer.put_done();
				return lifetime;
			}

			struct State {
				Fn<void()> check;
				base::has_weak_ptr guard;
				int readTill = StoryId();
				bool pushed = false;
			};
			const auto state = lifetime.make_state<State>();
			state->readTill = readTill;
			state->check = [=] {
				if (state->pushed) {
					return;
				}
				auto done = true;
				auto resolving = false;
				auto result = Content{};
				for (const auto id : ids) {
					const auto storyId = FullStoryId{ peerId, id };
					const auto maybe = stories->lookup(storyId);
					if (maybe) {
						if (!resolving) {
							const auto unread = (id > state->readTill);
							result.elements.reserve(ids.size());
							result.elements.push_back({
								.id = uint64(id),
								.thumbnail = MakeStoryThumbnail(*maybe),
								.count = 1U,
								.unreadCount = unread ? 1U : 0U,
							});
							if (unread) {
								done = false;
							}
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
				if (done) {
					consumer.put_done();
				}
			};

			rpl::single(peerId) | rpl::then(
				stories->itemsChanged() | rpl::filter(_1 == peerId)
			) | rpl::start_with_next(state->check, lifetime);

			stories->session().changes().storyUpdates(
				Data::StoryUpdate::Flag::MarkRead
			) | rpl::start_with_next([=](const Data::StoryUpdate &update) {
				if (update.story->peer()->id == peerId) {
					if (update.story->id() > state->readTill) {
						state->readTill = update.story->id();
						if (ranges::contains(ids, state->readTill)
							|| state->readTill > ids.front()) {
							state->pushed = false;
							state->check();
						}
					}
				}
			}, lifetime);

			return lifetime;
		});
	}) | rpl::flatten_latest();
}

std::shared_ptr<Thumbnail> MakeUserpicThumbnail(not_null<PeerData*> peer) {
	return std::make_shared<PeerUserpic>(peer);
}

std::shared_ptr<Thumbnail> MakeStoryThumbnail(
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

void FillSourceMenu(
		not_null<Window::SessionController*> controller,
		const ShowMenuRequest &request) {
	const auto owner = &controller->session().data();
	const auto peer = owner->peer(PeerId(request.id));
	const auto &add = request.callback;
	if (peer->isSelf()) {
		add(tr::lng_stories_archive_button(tr::now), [=] {
			controller->showSection(Info::Stories::Make(
				peer,
				Info::Stories::Tab::Archive));
		}, &st::menuIconStoriesArchiveSection);
		add(tr::lng_stories_my_title(tr::now), [=] {
			controller->showSection(Info::Stories::Make(peer));
		}, &st::menuIconStoriesSavedSection);
	} else {
		const auto channel = peer->isChannel();
		const auto showHistoryText = channel
			? tr::lng_context_open_channel(tr::now)
			: tr::lng_profile_send_message(tr::now);
		add(showHistoryText, [=] {
			controller->showPeerHistory(peer);
		}, channel ? &st::menuIconChannel : &st::menuIconChatBubble);
		const auto viewProfileText = channel
			? tr::lng_context_view_channel(tr::now)
			: tr::lng_context_view_profile(tr::now);
		add(viewProfileText, [=] {
			controller->showPeerInfo(peer);
		}, channel ? &st::menuIconInfo : &st::menuIconProfile);
		const auto in = [&](Data::StorySourcesList list) {
			return ranges::contains(
				owner->stories().sources(list),
				peer->id,
				&Data::StoriesSourceInfo::id);
		};
		const auto toggle = [=](bool shown) {
			owner->stories().toggleHidden(
				peer->id,
				!shown,
				controller->uiShow());
		};
		if (in(Data::StorySourcesList::NotHidden)) {
			add(tr::lng_stories_archive(tr::now), [=] {
				toggle(false);
			}, &st::menuIconArchive);
		}
		if (in(Data::StorySourcesList::Hidden)) {
			add(tr::lng_stories_unarchive(tr::now), [=] {
				toggle(true);
			}, &st::menuIconUnarchive);
		}
	}
}

} // namespace Dialogs::Stories
