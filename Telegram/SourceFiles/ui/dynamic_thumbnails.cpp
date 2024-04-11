/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/dynamic_thumbnails.h"

#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_story.h"
#include "main/main_session.h"
#include "ui/empty_userpic.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"

namespace Ui {
namespace {

class PeerUserpic final : public DynamicImage {
public:
	PeerUserpic(not_null<PeerData*> peer, bool forceRound);

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
		int paletteVersion = 0;
		rpl::lifetime photoLifetime;
		rpl::lifetime downloadLifetime;
	};

	[[nodiscard]] bool waitingUserpicLoad() const;
	void processNewPhoto();

	const not_null<PeerData*> _peer;
	QImage _frame;
	std::unique_ptr<Subscribed> _subscribed;
	bool _forceRound = false;

};

class StoryThumbnail : public DynamicImage {
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

class EmptyThumbnail final : public DynamicImage {
public:
	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	QImage _cached;

};

class SavedMessagesUserpic final : public DynamicImage {
public:
	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	QImage _frame;
	int _paletteVersion = 0;

};

PeerUserpic::PeerUserpic(not_null<PeerData*> peer, bool forceRound)
: _peer(peer)
, _forceRound(forceRound) {
}

QImage PeerUserpic::image(int size) {
	Expects(_subscribed != nullptr);

	const auto good = (_frame.width() == size * _frame.devicePixelRatio());
	const auto key = _peer->userpicUniqueKey(_subscribed->view);
	const auto paletteVersion = style::PaletteVersion();
	if (!good
		|| (_subscribed->paletteVersion != paletteVersion
			&& _peer->useEmptyUserpic(_subscribed->view))
		|| (_subscribed->key != key && !waitingUserpicLoad())) {
		_subscribed->key = key;
		_subscribed->paletteVersion = paletteVersion;

		const auto ratio = style::DevicePixelRatio();
		if (!good) {
			_frame = QImage(
				QSize(size, size) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_frame.setDevicePixelRatio(ratio);
		}
		_frame.fill(Qt::transparent);

		auto p = Painter(&_frame);
		auto &view = _subscribed->view;
		if (!_forceRound) {
			_peer->paintUserpic(p, view, 0, 0, size);
		} else if (const auto cloud = _peer->userpicCloudImage(view)) {
			const auto full = size * style::DevicePixelRatio();
			Ui::ValidateUserpicCache(view, cloud, nullptr, full, false);
			p.drawImage(QRect(0, 0, size, size), view.cached);
		} else {
			const auto full = size * style::DevicePixelRatio();
			const auto r = full / 2.;
			const auto empty = _peer->generateUserpicImage(view, full, r);
			p.drawImage(QRect(0, 0, size, size), empty);
		}
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
		_media->wanted(Data::PhotoSize::Small, id);
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
		_media->thumbnailWanted(id);
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

QImage SavedMessagesUserpic::image(int size) {
	const auto good = (_frame.width() == size * _frame.devicePixelRatio());
	const auto paletteVersion = style::PaletteVersion();
	if (!good || _paletteVersion != paletteVersion) {
		_paletteVersion = paletteVersion;

		const auto ratio = style::DevicePixelRatio();
		if (!good) {
			_frame = QImage(
				QSize(size, size) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_frame.setDevicePixelRatio(ratio);
		}
		_frame.fill(Qt::transparent);

		auto p = Painter(&_frame);
		Ui::EmptyUserpic::PaintSavedMessages(p, 0, 0, size, size);
	}
	return _frame;
}

void SavedMessagesUserpic::subscribeToUpdates(Fn<void()> callback) {
}

} // namespace

std::shared_ptr<DynamicImage> MakeUserpicThumbnail(
		not_null<PeerData*> peer,
		bool forceRound) {
	return std::make_shared<PeerUserpic>(peer, forceRound);
}

std::shared_ptr<DynamicImage> MakeSavedMessagesThumbnail() {
	return std::make_shared<SavedMessagesUserpic>();
}

std::shared_ptr<DynamicImage> MakeStoryThumbnail(
		not_null<Data::Story*> story) {
	using Result = std::shared_ptr<DynamicImage>;
	const auto id = story->fullId();
	return v::match(story->media().data, [](v::null_t) -> Result {
		return std::make_shared<EmptyThumbnail>();
	}, [&](not_null<PhotoData*> photo) -> Result {
		return std::make_shared<PhotoThumbnail>(photo, id);
	}, [&](not_null<DocumentData*> video) -> Result {
		return std::make_shared<VideoThumbnail>(video, id);
	});
}

} // namespace Ui
