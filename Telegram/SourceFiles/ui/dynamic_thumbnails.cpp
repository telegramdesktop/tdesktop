/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/dynamic_thumbnails.h"

#include "data/data_changes.h"
#include "data/data_cloud_file.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_story.h"
#include "layout/layout_document_generic_preview.h"
#include "main/main_session.h"
#include "ui/empty_userpic.h"
#include "ui/dynamic_image.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/userpic_view.h"
#include "styles/style_chat.h"
#include "styles/style_overview.h"

namespace Ui {
namespace {

enum class MediaThumbnailMode {
	Crop,
	CenterCrop,
	Fit,
};

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
	explicit MediaThumbnail(
		Data::FileOrigin origin,
		bool forceRound,
		MediaThumbnailMode mode);

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

protected:
	struct Thumb {
		Image *image = nullptr;
		bool blurred = false;
	};

	[[nodiscard]] Data::FileOrigin origin() const;
	[[nodiscard]] bool forceRound() const;
	[[nodiscard]] MediaThumbnailMode mode() const;

	[[nodiscard]] virtual Main::Session &session() = 0;
	[[nodiscard]] virtual Thumb loaded(Data::FileOrigin origin) = 0;
	virtual void clear() = 0;

private:
	const Data::FileOrigin _origin;
	const bool _forceRound;
	const MediaThumbnailMode _mode;
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
		bool forceRound,
		MediaThumbnailMode mode);

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
		bool forceRound,
		MediaThumbnailMode mode);

	std::shared_ptr<DynamicImage> clone() override;

private:
	Main::Session &session() override;
	Thumb loaded(Data::FileOrigin origin) override;
	void clear() override;

	const not_null<DocumentData*> _video;
	std::shared_ptr<Data::DocumentMedia> _media;

};

class CallThumbnail final : public DynamicImage {
public:
	CallThumbnail();

	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	QImage _prepared;

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
		int loopLimit,
		Fn<bool()> paused,
		Fn<QColor()> textColor);

	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	const not_null<Data::Session*> _owner;
	const QString _data;
	const int _loopLimit = 0;
	std::unique_ptr<Ui::Text::CustomEmoji> _emoji;
	Fn<bool()> _paused;
	Fn<QColor()> _textColor;
	QImage _frame;

};

class GeoThumbnail final : public DynamicImage {
public:
	GeoThumbnail(
		not_null<Data::CloudImage*> data,
		not_null<Main::Session*> session,
		Data::FileOrigin origin,
		bool drawPin);

	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	const not_null<Data::CloudImage*> _data;
	const not_null<Main::Session*> _session;
	const Data::FileOrigin _origin;
	const bool _drawPin;
	std::shared_ptr<QImage> _view;
	QImage _prepared;
	int _paletteVersion = 0;
	rpl::lifetime _subscription;

};

class DocumentFilePreviewThumbnail final : public DynamicImage {
public:
	DocumentFilePreviewThumbnail(
		not_null<DocumentData*> document,
		Data::FileOrigin origin);

	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	[[nodiscard]] QImage prepareThumbImage(int size);
	[[nodiscard]] QImage prepareGenericImage(int size);

	const not_null<DocumentData*> _document;
	const Data::FileOrigin _origin;
	const ::Layout::DocumentGenericPreview _generic;
	std::shared_ptr<Data::DocumentMedia> _media;
	QImage _prepared;
	bool _thumbLoaded = false;
	int _paletteVersion = 0;
	rpl::lifetime _subscription;

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
	) | rpl::on_next([=] {
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
	}) | rpl::on_next([=] {
		_subscribed->callback();
		_subscribed->downloadLifetime.destroy();
	}, _subscribed->downloadLifetime);
}

MediaThumbnail::MediaThumbnail(
		Data::FileOrigin origin,
		bool forceRound,
		MediaThumbnailMode mode)
: _origin(origin)
, _forceRound(forceRound)
, _mode(mode) {
}

QImage MediaThumbnail::image(int size) {
	const auto ratio = style::DevicePixelRatio();
	if (_prepared.width() != size * ratio) {
		const auto full = QSize(size, size) * ratio;
		if (_full.isNull()) {
			_prepared = QImage(
				full,
				QImage::Format_ARGB32_Premultiplied);
			_prepared.setDevicePixelRatio(ratio);
			_prepared.fill(_mode == MediaThumbnailMode::Fit
				? Qt::transparent
				: Qt::black);
		} else if (_mode == MediaThumbnailMode::Fit) {
			auto scaled = _full.scaled(
				full,
				Qt::KeepAspectRatio,
				Qt::SmoothTransformation);
			const auto scaledSize = QSizeF(scaled.size()) / ratio;
			scaled.setDevicePixelRatio(ratio);
			_prepared = QImage(full, QImage::Format_ARGB32_Premultiplied);
			_prepared.setDevicePixelRatio(ratio);
			_prepared.fill(Qt::transparent);

			auto p = QPainter(&_prepared);
			p.drawImage(
				QPointF(
					(size - scaledSize.width()) / 2.,
					(size - scaledSize.height()) / 2.),
				scaled);
		} else {
			auto source = QRect();
			if (_mode == MediaThumbnailMode::CenterCrop) {
				const auto side = std::min(_full.width(), _full.height());
				const auto x = (_full.width() - side) / 2;
				const auto y = (_full.height() - side) / 2;
				source = QRect(x, y, side, side);
			} else {
				const auto width = _full.width();
				const auto skip = std::max((_full.height() - width) / 2, 0);
				source = QRect(0, skip, width, width);
			}
			_prepared = _full.copy(source).scaled(
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
		}) | rpl::take(1) | rpl::on_next(callback);
	}
}

Data::FileOrigin MediaThumbnail::origin() const {
	return _origin;
}

bool MediaThumbnail::forceRound() const {
	return _forceRound;
}

MediaThumbnailMode MediaThumbnail::mode() const {
	return _mode;
}

PhotoThumbnail::PhotoThumbnail(
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	bool forceRound,
	MediaThumbnailMode mode)
: MediaThumbnail(origin, forceRound, mode)
, _photo(photo) {
}

std::shared_ptr<DynamicImage> PhotoThumbnail::clone() {
	return std::make_shared<PhotoThumbnail>(
		_photo,
		origin(),
		forceRound(),
		mode());
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
	bool forceRound,
	MediaThumbnailMode mode)
: MediaThumbnail(origin, forceRound, mode)
, _video(video) {
}

std::shared_ptr<DynamicImage> VideoThumbnail::clone() {
	return std::make_shared<VideoThumbnail>(
		_video,
		origin(),
		forceRound(),
		mode());
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

CallThumbnail::CallThumbnail() = default;

std::shared_ptr<DynamicImage> CallThumbnail::clone() {
	return std::make_shared<CallThumbnail>();
}

QImage CallThumbnail::image(int size) {
	const auto ratio = style::DevicePixelRatio();
	const auto full = QSize(size, size) * ratio;
	if (_prepared.size() != full) {
		_prepared = QImage(full, QImage::Format_ARGB32_Premultiplied);
		_prepared.fill(Qt::black);
		_prepared.setDevicePixelRatio(ratio);

		_prepared = Images::Circle(std::move(_prepared));
	}
	return _prepared;
}

void CallThumbnail::subscribeToUpdates(Fn<void()> callback) {
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
	int loopLimit,
	Fn<bool()> paused,
	Fn<QColor()> textColor)
: _owner(owner)
, _data(data)
, _loopLimit(loopLimit)
, _paused(std::move(paused))
, _textColor(std::move(textColor)) {
}

void EmojiThumbnail::subscribeToUpdates(Fn<void()> callback) {
	if (!callback) {
		_emoji = nullptr;
		return;
	}
	auto emoji = _owner->customEmojiManager().create(
		_data,
		std::move(callback),
		Data::CustomEmojiSizeTag::Large);
	_emoji = (_loopLimit > 0)
		? MakeWrappedEmoji<Ui::Text::LimitedLoopsEmoji>(
			std::move(emoji),
			_loopLimit)
		: std::move(emoji);
}

std::shared_ptr<DynamicImage> EmojiThumbnail::clone() {
	return std::make_shared<EmojiThumbnail>(
		_owner,
		_data,
		_loopLimit,
		_paused,
		_textColor);
}

QImage EmojiThumbnail::image(int size) {
	if (!_emoji) {
		return QImage();
	}

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

GeoThumbnail::GeoThumbnail(
	not_null<Data::CloudImage*> data,
	not_null<Main::Session*> session,
	Data::FileOrigin origin,
	bool drawPin)
: _data(data)
, _session(session)
, _origin(origin)
, _drawPin(drawPin) {
}

std::shared_ptr<DynamicImage> GeoThumbnail::clone() {
	return std::make_shared<GeoThumbnail>(
		_data,
		_session,
		_origin,
		_drawPin);
}

QImage GeoThumbnail::image(int size) {
	const auto ratio = style::DevicePixelRatio();
	const auto full = QSize(size, size) * ratio;
	const auto paletteVersion = style::PaletteVersion();
	if (_prepared.size() == full && _paletteVersion == paletteVersion) {
		return _prepared;
	}
	_paletteVersion = paletteVersion;

	const auto loaded = _view ? *_view : QImage();
	if (loaded.isNull()) {
		_prepared = QImage(full, QImage::Format_ARGB32_Premultiplied);
		_prepared.fill(Qt::black);
	} else {
		const auto w = loaded.width();
		const auto h = loaded.height();
		const auto side = std::min(w, h);
		const auto x = (w - side) / 2;
		const auto y = (h - side) / 2;
		_prepared = loaded.copy(x, y, side, side).scaled(
			full,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	}
	_prepared = Images::Round(
		std::move(_prepared),
		ImageRoundRadius::Small);
	_prepared.setDevicePixelRatio(ratio);
	if (_drawPin && !loaded.isNull()) {
		auto p = Painter(&_prepared);
		auto hq = PainterHighQualityEnabler(p);
		const auto pinScale = std::min(
			1.0,
			size / (st::historyMapPoint.height() * 2.5));
		const auto center = QPointF(size / 2.0, size / 2.0);
		p.translate(center);
		p.scale(pinScale, pinScale);
		p.translate(-center);
		const auto paintMarker = [&](const style::icon &icon) {
			icon.paint(
				p,
				(size - icon.width()) / 2,
				(size / 2) - icon.height(),
				size);
		};
		paintMarker(st::historyMapPoint);
		paintMarker(st::historyMapPointInner);
	}
	return _prepared;
}

void GeoThumbnail::subscribeToUpdates(Fn<void()> callback) {
	_subscription.destroy();
	if (!callback) {
		_view = nullptr;
		_prepared = QImage();
		return;
	}
	_view = _data->createView();
	_data->load(_session, _origin);
	if (!_view->isNull()) {
		return;
	}
	_subscription = _session->downloaderTaskFinished(
	) | rpl::filter([=] {
		return !_view->isNull();
	}) | rpl::take(1) | rpl::on_next([=] {
		_prepared = QImage();
		callback();
	});
}

DocumentFilePreviewThumbnail::DocumentFilePreviewThumbnail(
	not_null<DocumentData*> document,
	Data::FileOrigin origin)
: _document(document)
, _origin(origin)
, _generic(::Layout::DocumentGenericPreview::Create(document)) {
}

std::shared_ptr<DynamicImage> DocumentFilePreviewThumbnail::clone() {
	return std::make_shared<DocumentFilePreviewThumbnail>(
		_document,
		_origin);
}

QImage DocumentFilePreviewThumbnail::image(int size) {
	const auto ratio = style::DevicePixelRatio();
	const auto paletteVersion = style::PaletteVersion();
	const auto good = (_prepared.width() == size * ratio);
	if (good && _paletteVersion == paletteVersion) {
		return _prepared;
	}
	_paletteVersion = paletteVersion;

	if (_media) {
		const auto thumbnail = _media->thumbnail();
		const auto blurred = _media->thumbnailInline();
		if (thumbnail || blurred) {
			_prepared = prepareThumbImage(size);
			if (!_prepared.isNull()) {
				return _prepared;
			}
		}
	}
	_prepared = prepareGenericImage(size);
	return _prepared;
}

QImage DocumentFilePreviewThumbnail::prepareThumbImage(int size) {
	const auto ratio = style::DevicePixelRatio();
	const auto thumbnail = _media->thumbnail();
	const auto blurred = _media->thumbnailInline();
	const auto image = thumbnail ? thumbnail : blurred;
	if (!image) {
		return {};
	}
	auto full = image->original();
	const auto side = std::min(full.width(), full.height());
	const auto x = (full.width() - side) / 2;
	const auto y = (full.height() - side) / 2;
	auto result = full.copy(x, y, side, side).scaled(
		QSize(size, size) * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	result = Images::Round(
		std::move(result),
		ImageRoundRadius::Small);
	result.setDevicePixelRatio(ratio);
	return result;
}

QImage DocumentFilePreviewThumbnail::prepareGenericImage(int size) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(ratio);

	const auto radius = size / 6;
	const auto foldSize = size / 4;

	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);

	// Rounded rect with top-right corner cut for fold.
	auto path = QPainterPath();
	path.moveTo(radius, 0);
	path.lineTo(size - foldSize, 0);
	path.lineTo(size, foldSize);
	path.lineTo(size, size - radius);
	path.arcTo(
		size - 2 * radius,
		size - 2 * radius,
		2 * radius,
		2 * radius,
		0,
		-90);
	path.lineTo(radius, size);
	path.arcTo(0, size - 2 * radius, 2 * radius, 2 * radius, 270, -90);
	path.lineTo(0, radius);
	path.arcTo(0, 0, 2 * radius, 2 * radius, 180, -90);
	path.closeSubpath();

	p.setBrush(_generic.color);
	p.drawPath(path);

	// Fold triangle.
	auto fold = QPainterPath();
	fold.moveTo(size - foldSize, 0);
	fold.lineTo(size, foldSize);
	fold.lineTo(size - foldSize, foldSize);
	fold.closeSubpath();
	p.setBrush(_generic.dark);
	p.drawPath(fold);

	if (!_generic.ext.isEmpty()) {
		// Reference: overview uses 18px font in a 70px square.
		const auto refSize = st::overviewFileLayout.fileThumbSize;
		const auto fontSize = std::max(
			size * st::overviewFileExtFont->f.pixelSize() / refSize,
			8);
		const auto font = style::font(
			fontSize,
			st::overviewFileExtFont->flags(),
			st::overviewFileExtFont->family());
		const auto padding = size * st::overviewFileExtPadding / refSize;
		const auto maxw = size - padding * 2;
		auto ext = _generic.ext;
		auto extw = font->width(ext);
		if (extw > maxw) {
			ext = font->elided(ext, maxw, Qt::ElideMiddle);
			extw = font->width(ext);
		}
		p.setFont(font);
		p.setPen(st::overviewFileExtFg);
		p.drawText(
			(size - extw) / 2,
			(size - font->height) / 2 + font->ascent,
			ext);
	}
	p.end();
	return result;
}

void DocumentFilePreviewThumbnail::subscribeToUpdates(Fn<void()> callback) {
	_subscription.destroy();
	if (!callback) {
		_media = nullptr;
		_prepared = QImage();
		return;
	}
	if (!_document->hasThumbnail()) {
		return;
	}
	if (!_media) {
		_media = _document->createMediaView();
		_media->thumbnailWanted(_origin);
	}
	if (_media->thumbnail()) {
		_thumbLoaded = true;
		return;
	}
	if (!_thumbLoaded) {
		_subscription = _document->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return _media && _media->thumbnail();
		}) | rpl::take(1) | rpl::on_next([=] {
			_thumbLoaded = true;
			_prepared = QImage();
			callback();
		});
	}
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
	}, [](const std::shared_ptr<Data::GroupCall> &call) -> Result {
		return std::make_shared<CallThumbnail>();
	}, [&](not_null<PhotoData*> photo) -> Result {
		return std::make_shared<PhotoThumbnail>(
			photo,
			id,
			true,
			MediaThumbnailMode::Crop);
	}, [&](not_null<DocumentData*> video) -> Result {
		return std::make_shared<VideoThumbnail>(
			video,
			id,
			true,
			MediaThumbnailMode::Crop);
	});
}

std::shared_ptr<DynamicImage> MakeIconThumbnail(const style::icon &icon) {
	return std::make_shared<IconThumbnail>(icon);
}

std::shared_ptr<DynamicImage> MakeEmojiThumbnail(
		not_null<Data::Session*> owner,
		const QString &data,
		Fn<bool()> paused,
		Fn<QColor()> textColor,
		int loopLimit) {
	return std::make_shared<EmojiThumbnail>(
		owner,
		data,
		loopLimit,
		std::move(paused),
		std::move(textColor));
}

std::shared_ptr<DynamicImage> MakePhotoThumbnail(
		not_null<PhotoData*> photo,
		FullMsgId fullId) {
	return std::make_shared<PhotoThumbnail>(
		photo,
		fullId,
		false,
		MediaThumbnailMode::Crop);
}

std::shared_ptr<DynamicImage> MakePhotoThumbnailCenterCrop(
		not_null<PhotoData*> photo,
		FullMsgId fullId) {
	return std::make_shared<PhotoThumbnail>(
		photo,
		fullId,
		false,
		MediaThumbnailMode::CenterCrop);
}

std::shared_ptr<DynamicImage> MakeDocumentThumbnail(
		not_null<DocumentData*> document,
		FullMsgId fullId) {
	return std::make_shared<VideoThumbnail>(
		document,
		fullId,
		false,
		MediaThumbnailMode::Crop);
}

std::shared_ptr<DynamicImage> MakeDocumentThumbnail(
		not_null<DocumentData*> document,
		Data::FileOrigin origin) {
	return std::make_shared<VideoThumbnail>(
		document,
		origin,
		false,
		MediaThumbnailMode::Crop);
}

std::shared_ptr<DynamicImage> MakeDocumentThumbnailFit(
		not_null<DocumentData*> document,
		Data::FileOrigin origin) {
	return std::make_shared<VideoThumbnail>(
		document,
		origin,
		false,
		MediaThumbnailMode::Fit);
}

std::shared_ptr<DynamicImage> MakeDocumentThumbnailCenterCrop(
		not_null<DocumentData*> document,
		FullMsgId fullId) {
	return std::make_shared<VideoThumbnail>(
		document,
		fullId,
		false,
		MediaThumbnailMode::CenterCrop);
}

std::shared_ptr<DynamicImage> MakeDocumentFilePreviewThumbnail(
		not_null<DocumentData*> document,
		FullMsgId fullId) {
	return std::make_shared<DocumentFilePreviewThumbnail>(
		document,
		fullId);
}

std::shared_ptr<DynamicImage> MakeGeoThumbnail(
		not_null<Data::CloudImage*> data,
		not_null<Main::Session*> session,
		Data::FileOrigin origin) {
	return std::make_shared<GeoThumbnail>(
		data,
		session,
		std::move(origin),
		false);
}

std::shared_ptr<DynamicImage> MakeGeoThumbnailWithPin(
		not_null<Data::CloudImage*> data,
		not_null<Main::Session*> session,
		Data::FileOrigin origin) {
	return std::make_shared<GeoThumbnail>(
		data,
		session,
		std::move(origin),
		true);
}

} // namespace Ui
