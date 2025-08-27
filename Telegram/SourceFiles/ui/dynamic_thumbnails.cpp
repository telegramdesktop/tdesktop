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
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
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

	std::shared_ptr<DynamicImage> clone() override;

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

class MediaThumbnail : public DynamicImage {
public:
	explicit MediaThumbnail(Data::FileOrigin origin, bool forceRound);

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

protected:
	struct Thumb {
		Image *image = nullptr;
		bool blurred = false;
	};

	[[nodiscard]] Data::FileOrigin origin() const;
	[[nodiscard]] bool forceRound() const;

	[[nodiscard]] virtual Main::Session &session() = 0;
	[[nodiscard]] virtual Thumb loaded(Data::FileOrigin origin) = 0;
	virtual void clear() = 0;

private:
	const Data::FileOrigin _origin;
	const bool _forceRound;
	QImage _full;
	rpl::lifetime _subscription;
	QImage _prepared;
	bool _blurred = false;

};

class PhotoThumbnail final : public MediaThumbnail {
public:
	PhotoThumbnail(
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		bool forceRound);

	std::shared_ptr<DynamicImage> clone() override;

private:
	Main::Session &session() override;
	Thumb loaded(Data::FileOrigin origin) override;
	void clear() override;

	const not_null<PhotoData*> _photo;
	std::shared_ptr<Data::PhotoMedia> _media;

};

class VideoThumbnail final : public MediaThumbnail {
public:
	VideoThumbnail(
		not_null<DocumentData*> video,
		Data::FileOrigin origin,
		bool forceRound);

	std::shared_ptr<DynamicImage> clone() override;

private:
	Main::Session &session() override;
	Thumb loaded(Data::FileOrigin origin) override;
	void clear() override;

	const not_null<DocumentData*> _video;
	std::shared_ptr<Data::DocumentMedia> _media;

};

class EmptyThumbnail final : public DynamicImage {
public:
	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	QImage _cached;

};

class SavedMessagesUserpic final : public DynamicImage {
public:
	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	QImage _frame;
	int _paletteVersion = 0;

};

class RepliesUserpic final : public DynamicImage {
public:
	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	QImage _frame;
	int _paletteVersion = 0;

};

class HiddenAuthorUserpic final : public DynamicImage {
public:
	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	QImage _frame;
	int _paletteVersion = 0;

};

class IconThumbnail final : public DynamicImage {
public:
	explicit IconThumbnail(const style::icon &icon);

	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	const style::icon &_icon;
	int _paletteVersion = 0;
	QImage _frame;

};

class EmojiThumbnail final : public DynamicImage {
public:
	EmojiThumbnail(
		not_null<Data::Session*> owner,
		const QString &data,
		Fn<bool()> paused,
		Fn<QColor()> textColor);

	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	const not_null<Data::Session*> _owner;
	const QString _data;
	std::unique_ptr<Ui::Text::CustomEmoji> _emoji;
	Fn<bool()> _paused;
	Fn<QColor()> _textColor;
	QImage _frame;

};

PeerUserpic::PeerUserpic(not_null<PeerData*> peer, bool forceRound)
: _peer(peer)
, _forceRound(forceRound) {
}

std::shared_ptr<DynamicImage> PeerUserpic::clone() {
	return std::make_shared<PeerUserpic>(_peer, _forceRound);
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
		_peer->paintUserpic(p, view, {
			.position = QPoint(),
			.size = size,
			.shape = (_forceRound
				? Ui::PeerUserpicShape::Circle
				: Ui::PeerUserpicShape::Auto),
		});
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
	const auto old = std::exchange(
		_subscribed,
		std::make_unique<Subscribed>(std::move(callback)));

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

MediaThumbnail::MediaThumbnail(Data::FileOrigin origin, bool forceRound)
: _origin(origin)
, _forceRound(forceRound) {
}

QImage MediaThumbnail::image(int size) {
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
		if (_forceRound) {
			_prepared = Images::Circle(std::move(_prepared));
		}
		_prepared.setDevicePixelRatio(ratio);
	}
	return _prepared;
}

void MediaThumbnail::subscribeToUpdates(Fn<void()> callback) {
	_subscription.destroy();
	if (!callback) {
		clear();
		return;
	} else if (!_full.isNull() && !_blurred) {
		return;
	}
	const auto thumbnail = loaded(_origin);
	if (const auto image = thumbnail.image) {
		_full = image->original();
	}
	_blurred = thumbnail.blurred;
	if (!_blurred) {
		_prepared = QImage();
	} else {
		_subscription = session().downloaderTaskFinished(
		) | rpl::filter([=] {
			const auto thumbnail = loaded(_origin);
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

Data::FileOrigin MediaThumbnail::origin() const {
	return _origin;
}

bool MediaThumbnail::forceRound() const {
	return _forceRound;
}

PhotoThumbnail::PhotoThumbnail(
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	bool forceRound)
: MediaThumbnail(origin, forceRound)
, _photo(photo) {
}

std::shared_ptr<DynamicImage> PhotoThumbnail::clone() {
	return std::make_shared<PhotoThumbnail>(_photo, origin(), forceRound());
}

Main::Session &PhotoThumbnail::session() {
	return _photo->session();
}

MediaThumbnail::Thumb PhotoThumbnail::loaded(Data::FileOrigin origin) {
	if (!_media) {
		_media = _photo->createMediaView();
		_media->wanted(Data::PhotoSize::Small, origin);
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
	Data::FileOrigin origin,
	bool forceRound)
: MediaThumbnail(origin, forceRound)
, _video(video) {
}

std::shared_ptr<DynamicImage> VideoThumbnail::clone() {
	return std::make_shared<VideoThumbnail>(_video, origin(), forceRound());
}

Main::Session &VideoThumbnail::session() {
	return _video->session();
}

MediaThumbnail::Thumb VideoThumbnail::loaded(Data::FileOrigin origin) {
	if (!_media) {
		_media = _video->createMediaView();
		_media->thumbnailWanted(origin);
	}
	if (const auto small = _media->thumbnail()) {
		return { .image = small };
	}
	return { .image = _media->thumbnailInline(), .blurred = true };
}

void VideoThumbnail::clear() {
	_media = nullptr;
}

std::shared_ptr<DynamicImage> EmptyThumbnail::clone() {
	return std::make_shared<EmptyThumbnail>();
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

std::shared_ptr<DynamicImage> SavedMessagesUserpic::clone() {
	return std::make_shared<SavedMessagesUserpic>();
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
	if (!callback) {
		_frame = {};
	}
}

std::shared_ptr<DynamicImage> RepliesUserpic::clone() {
	return std::make_shared<RepliesUserpic>();
}

QImage RepliesUserpic::image(int size) {
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
		Ui::EmptyUserpic::PaintRepliesMessages(p, 0, 0, size, size);
	}
	return _frame;
}

void RepliesUserpic::subscribeToUpdates(Fn<void()> callback) {
	if (!callback) {
		_frame = {};
	}
}

std::shared_ptr<DynamicImage> HiddenAuthorUserpic::clone() {
	return std::make_shared<HiddenAuthorUserpic>();
}

QImage HiddenAuthorUserpic::image(int size) {
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
		Ui::EmptyUserpic::PaintHiddenAuthor(p, 0, 0, size, size);
	}
	return _frame;
}

void HiddenAuthorUserpic::subscribeToUpdates(Fn<void()> callback) {
	if (!callback) {
		_frame = {};
	}
}

IconThumbnail::IconThumbnail(const style::icon &icon) : _icon(icon) {
}

std::shared_ptr<DynamicImage> IconThumbnail::clone() {
	return std::make_shared<IconThumbnail>(_icon);
}

QImage IconThumbnail::image(int size) {
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
		_icon.paintInCenter(p, QRect(0, 0, size, size));
	}
	return _frame;
}

void IconThumbnail::subscribeToUpdates(Fn<void()> callback) {
	if (!callback) {
		_frame = {};
	}
}

EmojiThumbnail::EmojiThumbnail(
	not_null<Data::Session*> owner,
	const QString &data,
	Fn<bool()> paused,
	Fn<QColor()> textColor)
: _owner(owner)
, _data(data)
, _paused(std::move(paused))
, _textColor(std::move(textColor)) {
}

void EmojiThumbnail::subscribeToUpdates(Fn<void()> callback) {
	if (!callback) {
		_emoji = nullptr;
		return;
	}
	_emoji = _owner->customEmojiManager().create(
		_data,
		std::move(callback),
		Data::CustomEmojiSizeTag::Large);
}

std::shared_ptr<DynamicImage> EmojiThumbnail::clone() {
	return std::make_shared<EmojiThumbnail>(
		_owner,
		_data,
		_paused,
		_textColor);
}

QImage EmojiThumbnail::image(int size) {
	Expects(_emoji != nullptr);

	const auto ratio = style::DevicePixelRatio();
	const auto good = (_frame.width() == size * _frame.devicePixelRatio());
	if (!good) {
		_frame = QImage(
			QSize(size, size) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
	}
	_frame.fill(Qt::transparent);

	const auto esize = Text::AdjustCustomEmojiSize(
		Emoji::GetSizeLarge() / style::DevicePixelRatio());
	const auto eskip = (size - esize) / 2;

	auto p = Painter(&_frame);
	_emoji->paint(p, {
		.textColor = _textColor ? _textColor() : st::windowBoldFg->c,
		.now = crl::now(),
		.position = QPoint(eskip, eskip),
		.paused = _paused && _paused(),
	});
	p.end();

	return _frame;
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

std::shared_ptr<DynamicImage> MakeRepliesThumbnail() {
	return std::make_shared<RepliesUserpic>();
}

std::shared_ptr<DynamicImage> MakeHiddenAuthorThumbnail() {
	return std::make_shared<HiddenAuthorUserpic>();
}

std::shared_ptr<DynamicImage> MakeStoryThumbnail(
		not_null<Data::Story*> story) {
	using Result = std::shared_ptr<DynamicImage>;
	const auto id = story->fullId();
	return v::match(story->media().data, [](v::null_t) -> Result {
		return std::make_shared<EmptyThumbnail>();
	}, [&](not_null<PhotoData*> photo) -> Result {
		return std::make_shared<PhotoThumbnail>(photo, id, true);
	}, [&](not_null<DocumentData*> video) -> Result {
		return std::make_shared<VideoThumbnail>(video, id, true);
	});
}

std::shared_ptr<DynamicImage> MakeIconThumbnail(const style::icon &icon) {
	return std::make_shared<IconThumbnail>(icon);
}

std::shared_ptr<DynamicImage> MakeEmojiThumbnail(
		not_null<Data::Session*> owner,
		const QString &data,
		Fn<bool()> paused,
		Fn<QColor()> textColor) {
	return std::make_shared<EmojiThumbnail>(
		owner,
		data,
		std::move(paused),
		std::move(textColor));
}

std::shared_ptr<DynamicImage> MakePhotoThumbnail(
		not_null<PhotoData*> photo,
		FullMsgId fullId) {
	return std::make_shared<PhotoThumbnail>(photo, fullId, false);
}

std::shared_ptr<DynamicImage> MakeDocumentThumbnail(
		not_null<DocumentData*> document,
		FullMsgId fullId) {
	return std::make_shared<VideoThumbnail>(document, fullId, false);
}

} // namespace Ui
