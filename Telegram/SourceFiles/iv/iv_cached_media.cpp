/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_cached_media.h"

#include "base/algorithm.h"
#include "base/flat_map.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "data/data_channel.h"
#include "data/data_cloud_file.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_location.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_location.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/view/media/history_view_photo.h"
#include "info/profile/info_profile_values.h"
#include "iv/markdown/iv_markdown_common.h"
#include "iv/markdown/iv_markdown_history_view_media.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/view/media_view_open_common.h"
#include "storage/file_download.h"
#include "ui/dynamic_image.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include "styles/palette.h"
#include "styles/style_chat.h"

#include <QtGui/QImage>

#include <algorithm>
#include <memory>

namespace Iv {
namespace {

constexpr auto kGeoPointScale = 1;
constexpr auto kGeoPointZoomMin = 13;

enum class CachedPagePhotoImageKind {
	Thumbnail,
	Full,
};

[[nodiscard]] QString SerializeNativeIvChannelContext(
		uint64 channelId,
		QString username) {
	auto result = QString::number(channelId);
	if (!username.isEmpty()) {
		result += u"\n"_q + username;
	}
	return result;
}

[[nodiscard]] Window::SessionController *CurrentSessionController(
		not_null<Main::Session*> session) {
	if (const auto window = Core::App().activeWindow()) {
		if (const auto current = window->sessionController();
			current && (&current->session() == session)) {
			return current;
		}
	}
	return nullptr;
}

class CachedPagePhotoDynamicImage final : public Ui::DynamicImage {
public:
	CachedPagePhotoDynamicImage(
		std::shared_ptr<::Data::PhotoMedia> media,
		not_null<PhotoData*> photo,
		::Data::FileOrigin origin,
		CachedPagePhotoImageKind kind,
		QSize requestedSize);

	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> clone() override;

	[[nodiscard]] QImage image(int size) override;

	void subscribeToUpdates(Fn<void()> callback) override;

private:
	void ensureWanted();

	[[nodiscard]] Image *resolvedImage() const;
	[[nodiscard]] QImage prepareImage(QImage image, int size) const;
	[[nodiscard]] QSize requestedSize(int size) const;

	const std::shared_ptr<::Data::PhotoMedia> _media;
	const not_null<PhotoData*> _photo;
	const ::Data::FileOrigin _origin;
	const CachedPagePhotoImageKind _kind;
	const QSize _requestedSize;
	mutable QImage _cached;
	rpl::lifetime _subscription;

};

CachedPagePhotoDynamicImage::CachedPagePhotoDynamicImage(
		std::shared_ptr<::Data::PhotoMedia> media,
		not_null<PhotoData*> photo,
		::Data::FileOrigin origin,
		CachedPagePhotoImageKind kind,
		QSize requestedSize)
: _media(std::move(media))
, _photo(photo)
, _origin(std::move(origin))
, _kind(kind)
, _requestedSize(requestedSize) {
}

std::shared_ptr<Ui::DynamicImage> CachedPagePhotoDynamicImage::clone() {
	return std::make_shared<CachedPagePhotoDynamicImage>(
		_media,
		_photo,
		_origin,
		_kind,
		_requestedSize);
}

QImage CachedPagePhotoDynamicImage::image(int size) {
	ensureWanted();
	if (const auto image = resolvedImage()) {
		return prepareImage(image->original(), size);
	}
	return QImage();
}

void CachedPagePhotoDynamicImage::subscribeToUpdates(Fn<void()> callback) {
	if (!callback) {
		_subscription = {};
		return;
	}
	_subscription = _photo->owner().photoLoadProgress(
	) | rpl::filter([photo = _photo](not_null<PhotoData*> updated) {
		return (updated == photo);
	}) | rpl::on_next([=, callback = std::move(callback)] {
		_cached = QImage();
		callback();
	});
}

void CachedPagePhotoDynamicImage::ensureWanted() {
	switch (_kind) {
	case CachedPagePhotoImageKind::Thumbnail:
		_media->wanted(::Data::PhotoSize::Small, _origin);
		break;
	case CachedPagePhotoImageKind::Full:
		_media->wanted(::Data::PhotoSize::Large, _origin);
		break;
	}
}

Image *CachedPagePhotoDynamicImage::resolvedImage() const {
	switch (_kind) {
	case CachedPagePhotoImageKind::Full:
		if (const auto large = _media->image(::Data::PhotoSize::Large)) {
			return large;
		}
		[[fallthrough]];
	case CachedPagePhotoImageKind::Thumbnail:
		if (const auto small = _media->image(::Data::PhotoSize::Small)) {
			return small;
		} else if (const auto thumbnail = _media->image(::Data::PhotoSize::Thumbnail)) {
			return thumbnail;
		}
		return _media->thumbnailInline();
	}
	return nullptr;
}

QImage CachedPagePhotoDynamicImage::prepareImage(
		QImage image,
		int size) const {
	const auto requested = requestedSize(size);
	if (requested.isEmpty() || image.isNull()) {
		return image;
	}
	const auto ratio = style::DevicePixelRatio();
	if (!_cached.isNull()) {
		const auto cachedSize = _cached.size() / ratio;
		if (cachedSize.width() == requested.width()
			|| cachedSize.height() == requested.height()) {
			return _cached;
		}
	}
	const auto to = image.size().scaled(requested, Qt::KeepAspectRatio);
	_cached = image.scaled(
		QSize(std::max(to.width(), 1), std::max(to.height(), 1)) * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	_cached.setDevicePixelRatio(ratio);
	return _cached;
}

QSize CachedPagePhotoDynamicImage::requestedSize(int size) const {
	if (!_requestedSize.isEmpty()) {
		return _requestedSize;
	}
	return (size > 0) ? QSize(size, size) : QSize();
}

class CachedPagePhotoRuntime final : public Markdown::PhotoRuntime {
public:
	CachedPagePhotoRuntime(
		not_null<Main::Session*> session,
		not_null<PhotoData*> photo,
		::Data::FileOrigin origin,
		FullMsgId itemId = FullMsgId(),
		Fn<FullMsgId()> itemIdResolver = nullptr);

	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> thumbnail(
			QSize size) const override;

	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> full(
			QSize size) const override;

	[[nodiscard]] bool loaded() const override;

	[[nodiscard]] bool loading() const override;

	[[nodiscard]] double progress() const override;

	void open(Qt::MouseButton button) const override;

private:
	const not_null<Main::Session*> _session;
	const not_null<PhotoData*> _photo;
	const ::Data::FileOrigin _origin;
	const FullMsgId _itemId;
	const Fn<FullMsgId()> _itemIdResolver;
	const std::shared_ptr<::Data::PhotoMedia> _media;

};

CachedPagePhotoRuntime::CachedPagePhotoRuntime(
	not_null<Main::Session*> session,
	not_null<PhotoData*> photo,
	::Data::FileOrigin origin,
	FullMsgId itemId,
	Fn<FullMsgId()> itemIdResolver)
: _session(session)
, _photo(photo)
, _origin(std::move(origin))
, _itemId(itemId)
, _itemIdResolver(std::move(itemIdResolver))
, _media(photo->createMediaView()) {
}

std::shared_ptr<Ui::DynamicImage> CachedPagePhotoRuntime::thumbnail(
		QSize size) const {
	_media->wanted(::Data::PhotoSize::Small, _origin);
	return std::make_shared<CachedPagePhotoDynamicImage>(
		_media,
		_photo,
		_origin,
		CachedPagePhotoImageKind::Thumbnail,
		size);
}

std::shared_ptr<Ui::DynamicImage> CachedPagePhotoRuntime::full(
		QSize size) const {
	_media->wanted(::Data::PhotoSize::Large, _origin);
	return std::make_shared<CachedPagePhotoDynamicImage>(
		_media,
		_photo,
		_origin,
		CachedPagePhotoImageKind::Full,
		size);
}

bool CachedPagePhotoRuntime::loaded() const {
	_media->wanted(::Data::PhotoSize::Large, _origin);
	return _media->loaded();
}

bool CachedPagePhotoRuntime::loading() const {
	_media->wanted(::Data::PhotoSize::Large, _origin);
	return _photo->displayLoading();
}

double CachedPagePhotoRuntime::progress() const {
	_media->wanted(::Data::PhotoSize::Large, _origin);
	return _media->progress();
}

void CachedPagePhotoRuntime::open(Qt::MouseButton button) const {
	if (button != Qt::LeftButton && button != Qt::MiddleButton) {
		return;
	}
	if (const auto window = Core::App().activeWindow()) {
		const auto itemId = _itemIdResolver ? _itemIdResolver() : _itemId;
		const auto item = itemId ? _session->data().message(itemId) : nullptr;
		window->openInMediaView({
			CurrentSessionController(_session),
			_photo,
			item,
			item ? item->topicRootId() : MsgId(),
			item ? item->sublistPeerId() : PeerId(),
		});
	}
}

[[nodiscard]] ImageWithLocation CachedPageMapImageData(
		double latitude,
		double longitude,
		uint64 accessHash,
		QSize size,
		int zoom) {
	const auto location = GeoPointLocation{
		.lat = latitude,
		.lon = longitude,
		.access = accessHash,
		.width = std::max(size.width(), 1),
		.height = std::max(size.height(), 1),
		.zoom = std::max(zoom, kGeoPointZoomMin),
		.scale = kGeoPointScale,
	};
	return {
		.location = ImageLocation(
			{ location },
			location.width,
			location.height),
	};
}

[[nodiscard]] ::Data::LocationPoint CachedPageMapPoint(
		double latitude,
		double longitude,
		uint64 accessHash) {
	const auto point = MTP_geoPoint(
		MTP_flags(0),
		MTP_double(longitude),
		MTP_double(latitude),
		MTP_long(accessHash),
		MTP_int(0));
	return ::Data::LocationPoint(point.c_geoPoint());
}

[[nodiscard]] bool CanHostNativeIvVideoDocument(
		not_null<DocumentData*> document) {
	return !document->isVideoMessage()
		&& (document->isVideoFile() || document->isAnimation());
}

[[nodiscard]] ::Data::MediaFile::Args CachedPageVideoMediaArgs(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		bool spoiler) {
	const auto video = document->video();
	return {
		.hasQualitiesList = video && !video->qualities.empty(),
		.skipPremiumEffect = !session->premium(),
		.spoiler = spoiler,
	};
}

[[nodiscard]] bool CanHostNativeIvAudioDocument(
		not_null<DocumentData*> document) {
	return document->isAudioFile() || document->isVoiceMessage();
}

[[nodiscard]] QString CachedPageGroupedMediaCopyText(
		const Markdown::PreparedGroupedMediaBlockData &prepared) {
	auto photos = 0;
	auto videos = 0;
	for (const auto &item : prepared.items) {
		if (item.media.kind == Markdown::PreparedMediaItemKind::Photo) {
			++photos;
		} else {
			++videos;
		}
	}
	if (photos && !videos) {
		return tr::lng_media_selected_photo(tr::now, lt_count, photos);
	} else if (videos && !photos) {
		return tr::lng_media_selected_video(tr::now, lt_count, videos);
	}
	return QString();
}

[[nodiscard]] QString CachedPageAudioTitleText(
		const Markdown::PreparedAudioBlockData &audio) {
	if (!audio.title.isEmpty()) {
		return audio.title;
	}
	if (!audio.fileName.isEmpty()) {
		return audio.fileName;
	}
	return tr::lng_in_dlg_audio_file(tr::now);
}

[[nodiscard]] QString CachedPageAudioSubtitleText(
		const Markdown::PreparedAudioBlockData &audio) {
	if (!audio.performer.isEmpty()) {
		return audio.performer;
	}
	if (!audio.fileName.isEmpty()
		&& audio.fileName != CachedPageAudioTitleText(audio)) {
		return audio.fileName;
	}
	return QString();
}

[[nodiscard]] QString CachedPageAudioCopyText(
		const Markdown::PreparedAudioBlockData &audio) {
	const auto title = CachedPageAudioTitleText(audio);
	const auto subtitle = CachedPageAudioSubtitleText(audio);
	return subtitle.isEmpty() ? title : (title + u"\n"_q + subtitle);
}

class CachedPageDocumentRuntime final : public Markdown::DocumentRuntime {
public:
	CachedPageDocumentRuntime(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		::Data::FileOrigin origin,
		FullMsgId itemId = FullMsgId(),
		Fn<FullMsgId()> itemIdResolver = nullptr);

	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> thumbnail(
			QSize size) const override;

	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> full(
			QSize size) const override;

	[[nodiscard]] bool loaded() const override;

	[[nodiscard]] bool loading() const override;

	[[nodiscard]] double progress() const override;

	void open(Qt::MouseButton button) const override;

private:
	const not_null<Main::Session*> _session;
	const not_null<DocumentData*> _document;
	const ::Data::FileOrigin _origin;
	const FullMsgId _itemId;
	const Fn<FullMsgId()> _itemIdResolver;
	const std::shared_ptr<::Data::DocumentMedia> _media;

};

CachedPageDocumentRuntime::CachedPageDocumentRuntime(
	not_null<Main::Session*> session,
	not_null<DocumentData*> document,
	::Data::FileOrigin origin,
	FullMsgId itemId,
	Fn<FullMsgId()> itemIdResolver)
: _session(session)
, _document(document)
, _origin(std::move(origin))
, _itemId(itemId)
, _itemIdResolver(std::move(itemIdResolver))
, _media(document->createMediaView()) {
}

bool CachedPageDocumentRuntime::loaded() const {
	return _media->loaded();
}

bool CachedPageDocumentRuntime::loading() const {
	return _document->displayLoading();
}

double CachedPageDocumentRuntime::progress() const {
	return _document->progress();
}

void CachedPageDocumentRuntime::open(Qt::MouseButton button) const {
	if (button != Qt::LeftButton && button != Qt::MiddleButton) {
		return;
	}
	if (const auto window = Core::App().activeWindow()) {
		const auto itemId = _itemIdResolver ? _itemIdResolver() : _itemId;
		const auto item = itemId ? _session->data().message(itemId) : nullptr;
		window->openInMediaView({
			CurrentSessionController(_session),
			_document,
			item,
			item ? item->topicRootId() : MsgId(),
			item ? item->sublistPeerId() : PeerId(),
		});
	}
}

class CachedPageInlineDocumentImage final : public Ui::DynamicImage {
public:
	CachedPageInlineDocumentImage(
		not_null<DocumentData*> document,
		::Data::FileOrigin origin,
		QSize requestedSize);

	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> clone() override;

	[[nodiscard]] QImage image(int size) override;

	void subscribeToUpdates(Fn<void()> callback) override;

private:
	void ensureWanted();

	[[nodiscard]] bool fullImageLoaded() const;
	[[nodiscard]] Image *resolvedFullPhotoImage() const;
	[[nodiscard]] Image *resolvedPhotoImage() const;
	[[nodiscard]] Image *resolvedThumbnailImage() const;
	[[nodiscard]] QImage resolvedDocumentImage();
	[[nodiscard]] QImage prepareImage(
		QImage image,
		int size,
		bool full = false) const;
	[[nodiscard]] QSize requestedSize(int size) const;

	const not_null<DocumentData*> _document;
	const ::Data::FileOrigin _origin;
	const QSize _requestedSize;
	const std::shared_ptr<::Data::DocumentMedia> _media;
	const std::shared_ptr<::Data::PhotoMedia> _photoMedia;
	QImage _documentImage;
	mutable QImage _cached;
	bool _documentImageRead = false;
	mutable bool _cachedFull = false;
	rpl::lifetime _subscription;

};

[[nodiscard]] std::shared_ptr<::Data::PhotoMedia> CachedPageInlinePhotoMedia(
		not_null<DocumentData*> document) {
	const auto photo = document->goodThumbnailPhoto();
	return photo ? photo->createMediaView() : nullptr;
}

CachedPageInlineDocumentImage::CachedPageInlineDocumentImage(
	not_null<DocumentData*> document,
	::Data::FileOrigin origin,
	QSize requestedSize)
: _document(document)
, _origin(std::move(origin))
, _requestedSize(requestedSize)
, _media(document->createMediaView())
, _photoMedia(CachedPageInlinePhotoMedia(document)) {
}

std::shared_ptr<Ui::DynamicImage> CachedPageInlineDocumentImage::clone() {
	return std::make_shared<CachedPageInlineDocumentImage>(
		_document,
		_origin,
		_requestedSize);
}

QImage CachedPageInlineDocumentImage::image(int size) {
	ensureWanted();
	if (const auto image = resolvedFullPhotoImage()) {
		return prepareImage(image->original(), size, true);
	} else if (auto image = resolvedDocumentImage(); !image.isNull()) {
		return prepareImage(std::move(image), size, true);
	} else if (const auto image = resolvedPhotoImage()) {
		return prepareImage(image->original(), size);
	} else if (const auto thumbnail = resolvedThumbnailImage()) {
		return prepareImage(thumbnail->original(), size);
	}
	return QImage();
}

void CachedPageInlineDocumentImage::subscribeToUpdates(Fn<void()> callback) {
	_subscription.destroy();
	if (!callback
		|| (!_photoMedia
			&& !_document->isImage()
			&& !_document->hasThumbnail())) {
		return;
	}
	ensureWanted();
	if (fullImageLoaded()) {
		return;
	}
	_document->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return fullImageLoaded()
			|| (!_photoMedia
				&& !_document->isImage()
				&& resolvedThumbnailImage());
	}) | rpl::take(1) | rpl::on_next(std::move(callback), _subscription);
}

void CachedPageInlineDocumentImage::ensureWanted() {
	if (_photoMedia) {
		_photoMedia->wanted(::Data::PhotoSize::Large, _origin);
	}
	if (_document->isImage()) {
		_document->forceToCache(true);
		_document->save(_origin, QString(), LoadFromCloudOrLocal, true);
	} else {
		_media->thumbnailWanted(_origin);
	}
}

bool CachedPageInlineDocumentImage::fullImageLoaded() const {
	return resolvedFullPhotoImage()
		|| (_document->isImage() && _media->loaded(true));
}

Image *CachedPageInlineDocumentImage::resolvedFullPhotoImage() const {
	return _photoMedia
		? _photoMedia->image(::Data::PhotoSize::Large)
		: nullptr;
}

Image *CachedPageInlineDocumentImage::resolvedPhotoImage() const {
	if (!_photoMedia) {
		return nullptr;
	} else if (const auto small = _photoMedia->image(::Data::PhotoSize::Small)) {
		return small;
	} else if (const auto thumbnail = _photoMedia->image(
			::Data::PhotoSize::Thumbnail)) {
		return thumbnail;
	}
	return _photoMedia->thumbnailInline();
}

Image *CachedPageInlineDocumentImage::resolvedThumbnailImage() const {
	if (const auto image = _media->thumbnail()) {
		return image;
	}
	return _media->thumbnailInline();
}

QImage CachedPageInlineDocumentImage::resolvedDocumentImage() {
	if (!_document->isImage() || !_media->loaded(true)) {
		return QImage();
	} else if (_documentImageRead) {
		return _documentImage;
	}
	_documentImageRead = true;
	_document->saveFromDataSilent();
	auto &location = _document->location(true);
	if (location.accessEnable()) {
		_documentImage = Images::Read({
			.path = location.name(),
			.maxSize = requestedSize(0) * style::DevicePixelRatio(),
		}).image;
		location.accessDisable();
	} else {
		_documentImage = Images::Read({
			.content = _media->bytes(),
			.maxSize = requestedSize(0) * style::DevicePixelRatio(),
		}).image;
	}
	return _documentImage;
}

QImage CachedPageInlineDocumentImage::prepareImage(
		QImage image,
		int size,
		bool full) const {
	const auto requested = requestedSize(size);
	if (requested.isEmpty() || image.isNull()) {
		return image;
	}
	const auto ratio = style::DevicePixelRatio();
	if (!_cached.isNull() && (_cachedFull || !full)) {
		const auto cachedSize = _cached.size() / ratio;
		if (cachedSize.width() == requested.width()
			|| cachedSize.height() == requested.height()) {
			return _cached;
		}
	}
	const auto to = image.size().scaled(requested, Qt::KeepAspectRatio);
	_cached = image.scaled(
		QSize(std::max(to.width(), 1), std::max(to.height(), 1)) * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	_cached.setDevicePixelRatio(style::DevicePixelRatio());
	return _cached;
}

QSize CachedPageInlineDocumentImage::requestedSize(int size) const {
	if (!_requestedSize.isEmpty()) {
		return _requestedSize;
	}
	return (size > 0) ? QSize(size, size) : QSize();
}

std::shared_ptr<Ui::DynamicImage> CachedPageDocumentRuntime::thumbnail(
		QSize size) const {
	return std::make_shared<CachedPageInlineDocumentImage>(
		_document,
		_origin,
		size);
}

std::shared_ptr<Ui::DynamicImage> CachedPageDocumentRuntime::full(
		QSize size) const {
	return std::make_shared<CachedPageInlineDocumentImage>(
		_document,
		_origin,
		size);
}

class CachedPageMapDynamicImage final : public Ui::DynamicImage {
public:
	CachedPageMapDynamicImage(
		not_null<::Data::CloudImage*> data,
		not_null<Main::Session*> session,
		::Data::FileOrigin origin);

	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> clone() override;

	[[nodiscard]] QImage image(int size) override;

	void subscribeToUpdates(Fn<void()> callback) override;

private:
	const not_null<::Data::CloudImage*> _data;
	const not_null<Main::Session*> _session;
	const ::Data::FileOrigin _origin;
	std::shared_ptr<QImage> _view;
	QImage _prepared;
	int _paletteVersion = 0;
	rpl::lifetime _subscription;

};

CachedPageMapDynamicImage::CachedPageMapDynamicImage(
	not_null<::Data::CloudImage*> data,
	not_null<Main::Session*> session,
	::Data::FileOrigin origin)
: _data(data)
, _session(session)
, _origin(std::move(origin)) {
}

std::shared_ptr<Ui::DynamicImage> CachedPageMapDynamicImage::clone() {
	return std::make_shared<CachedPageMapDynamicImage>(
		_data,
		_session,
		_origin);
}

QImage CachedPageMapDynamicImage::image(int size) {
	const auto loaded = _view ? *_view : QImage();
	if (loaded.isNull()) {
		return QImage();
	}
	const auto paletteVersion = style::PaletteVersion();
	if (_prepared.size() == loaded.size()
		&& _prepared.devicePixelRatio() == loaded.devicePixelRatio()
		&& _paletteVersion == paletteVersion) {
		return _prepared;
	}
	_paletteVersion = paletteVersion;
	_prepared = loaded.copy();
	_prepared.setDevicePixelRatio(loaded.devicePixelRatio());
	const auto ratio = loaded.devicePixelRatio();
	const auto width = int(loaded.width() / ratio);
	const auto height = int(loaded.height() / ratio);
	const auto markerSize = std::min(width, height);
	auto p = Painter(&_prepared);
	auto hq = PainterHighQualityEnabler(p);
	const auto pinScale = std::min({
		1.0,
		width / (st::historyMapPoint.height() * 2.5),
		height / (st::historyMapPoint.height() * 2.5),
	});
	const auto center = QPointF(width / 2.0, height / 2.0);
	p.translate(center);
	p.scale(pinScale, pinScale);
	p.translate(-center);
	const auto paintMarker = [&](const style::icon &icon) {
		icon.paint(
			p,
			(width - icon.width()) / 2,
			(height / 2) - icon.height(),
			markerSize);
	};
	paintMarker(st::historyMapPoint);
	paintMarker(st::historyMapPointInner);
	return _prepared;
}

void CachedPageMapDynamicImage::subscribeToUpdates(Fn<void()> callback) {
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

class CachedPageMapRuntime final : public Markdown::MapRuntime {
public:
	CachedPageMapRuntime(
		not_null<Main::Session*> session,
		::Data::FileOrigin origin,
		double latitude,
		double longitude,
		uint64 accessHash,
		QSize size,
		int zoom);

	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> thumbnail(
			QSize size) const override;

	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> full(
			QSize size) const override;

	[[nodiscard]] bool loaded() const override;

	[[nodiscard]] bool loading() const override;

	[[nodiscard]] double progress() const override;

private:
	void ensureLoaded() const;

	const not_null<Main::Session*> _session;
	const ::Data::FileOrigin _origin;
	mutable ::Data::CloudImage _image;

};

CachedPageMapRuntime::CachedPageMapRuntime(
	not_null<Main::Session*> session,
	::Data::FileOrigin origin,
	double latitude,
	double longitude,
	uint64 accessHash,
	QSize size,
	int zoom)
: _session(session)
, _origin(std::move(origin))
, _image(session, CachedPageMapImageData(
	latitude,
	longitude,
	accessHash,
	size,
	zoom)) {
}

std::shared_ptr<Ui::DynamicImage> CachedPageMapRuntime::thumbnail(
		QSize size) const {
	ensureLoaded();
	return std::make_shared<CachedPageMapDynamicImage>(
		&_image,
		_session,
		_origin);
}

std::shared_ptr<Ui::DynamicImage> CachedPageMapRuntime::full(
		QSize size) const {	ensureLoaded();
	return std::make_shared<CachedPageMapDynamicImage>(
		&_image,
		_session,
		_origin);
}

bool CachedPageMapRuntime::loaded() const {
	ensureLoaded();
	return _image.loadedOnce();
}

bool CachedPageMapRuntime::loading() const {
	ensureLoaded();
	return _image.loading();
}

double CachedPageMapRuntime::progress() const {
	ensureLoaded();
	return _image.loadedOnce() ? 1. : 0.;
}

void CachedPageMapRuntime::ensureLoaded() const {
	_image.load(_session, _origin);
}

class CachedPageChannelRuntime final : public Markdown::ChannelRuntime {
public:
	CachedPageChannelRuntime(
		not_null<ChannelData*> channel,
		QString context,
		Fn<void(QString)> openChannel,
		Fn<void(QString)> joinChannel);

	[[nodiscard]] bool joinVisible() const override;

	void open(Qt::MouseButton button) const override;

	void join(Qt::MouseButton button) const override;

private:
	const not_null<ChannelData*> _channel;
	const QString _context;
	const Fn<void(QString)> _openChannel;
	const Fn<void(QString)> _joinChannel;

};

CachedPageChannelRuntime::CachedPageChannelRuntime(
	not_null<ChannelData*> channel,
	QString context,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel)
: _channel(channel)
, _context(std::move(context))
, _openChannel(std::move(openChannel))
, _joinChannel(std::move(joinChannel)) {
}

bool CachedPageChannelRuntime::joinVisible() const {
	return !_channel->amIn();
}

void CachedPageChannelRuntime::open(Qt::MouseButton button) const {
	if ((button == Qt::LeftButton || button == Qt::MiddleButton)
		&& _openChannel) {
		_openChannel(_context);
	}
}

void CachedPageChannelRuntime::join(Qt::MouseButton button) const {
	if ((button == Qt::LeftButton || button == Qt::MiddleButton)
		&& _joinChannel) {
		_joinChannel(_context);
	}
}

struct PendingInstantViewMediaItem {
	enum class Kind {
		Photo,
		Document,
	};

	Kind kind = Kind::Photo;
	uint64 id = 0;
	TextWithEntities caption;
};

class CachedPageMediaRuntime final
	: public Markdown::MediaRuntime
	, public std::enable_shared_from_this<CachedPageMediaRuntime> {
public:
	CachedPageMediaRuntime(
		not_null<Main::Session*> session,
		not_null<WebPageData*> page,
		Fn<void(QString)> openChannel,
		Fn<void(QString)> joinChannel);
	CachedPageMediaRuntime(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		Fn<void(QString)> openChannel,
		Fn<void(QString)> joinChannel,
		::Data::FileOrigin draftOrigin = {},
		base::weak_ptr<Window::SessionController> controller = {});
	CachedPageMediaRuntime(
		not_null<Main::Session*> session,
		not_null<HistoryView::Element*> view,
		Fn<void(QString)> openChannel,
		Fn<void(QString)> joinChannel);

	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> resolveInlineImage(
			uint64 documentId,
			QSize size) const override;

	[[nodiscard]] std::shared_ptr<Markdown::PhotoRuntime> resolvePhoto(
			uint64 photoId) const override;

	[[nodiscard]] std::shared_ptr<Markdown::DocumentRuntime> resolveDocument(
			uint64 documentId) const override;

	void registerPhoto(
		uint64 photoId,
		TextWithEntities caption) const override;
	void registerDocument(
		uint64 documentId,
		TextWithEntities caption) const override;

	[[nodiscard]] std::shared_ptr<Markdown::MapRuntime> resolveMap(
			double latitude,
			double longitude,
			uint64 accessHash,
			QSize size,
			int zoom) const override;

	[[nodiscard]] std::shared_ptr<Markdown::ChannelRuntime> resolveChannel(
			uint64 channelId,
			const QString &username) const override;

	[[nodiscard]] rpl::producer<uint64> channelJoinedChanges() const override;

	[[nodiscard]] Ui::Text::MarkedContext textContext() const override;
	[[nodiscard]] QString mentionNameEntityData(uint64 userId) const override;

	[[nodiscard]] std::shared_ptr<Markdown::IvHistoryViewMediaHost>
	hostedMediaHost(
			not_null<Window::SessionController*> controller) const;

	[[nodiscard]] std::shared_ptr<Markdown::HostedMediaBlockFactory>
	hostedMediaBlockFactory() const override;

private:
	void subscribeToChannel(
			uint64 channelId,
			not_null<ChannelData*> channel) const;

	[[nodiscard]] HistoryItem *directHostItem() const;
	[[nodiscard]] HistoryItem *openContextItem() const;
	[[nodiscard]] Window::SessionController *resolveController() const;
	void queuePendingInstantViewItem(
		PendingInstantViewMediaItem::Kind kind,
		uint64 id,
		TextWithEntities caption) const;
	void flushPendingInstantViewItems(not_null<HistoryItem*> item) const;
	[[nodiscard]] FullMsgId openContextItemId() const;
	[[nodiscard]] ::Data::FileOrigin fileOrigin() const;

	const not_null<Main::Session*> _session;
	const base::weak_ptr<Window::SessionController> _controller;
	const ::Data::FileOrigin _origin;
	const FullMsgId _itemId;
	const ::Data::FileOrigin _draftOrigin;
	const QString _pageUrl;
	const bool _useExistingView = false;
	const base::weak_ptr<HistoryView::Element> _view;
	const Fn<void(QString)> _openChannel;
	const Fn<void(QString)> _joinChannel;
	mutable std::shared_ptr<Markdown::IvHistoryViewMediaHost> _hostedMediaHost;
	mutable std::vector<PendingInstantViewMediaItem> _pendingInstantViewItems;
	mutable base::flat_map<uint64, rpl::lifetime> _channelJoinedSubscriptions;
	mutable rpl::event_stream<uint64> _channelJoinedChanges;

};

CachedPageMediaRuntime::CachedPageMediaRuntime(
	not_null<Main::Session*> session,
	not_null<WebPageData*> page,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel)
: _session(session)
, _origin(::Data::FileOriginWebPage{ page->url })
, _itemId()
, _pageUrl(page->url)
, _openChannel(std::move(openChannel))
, _joinChannel(std::move(joinChannel)) {
}

CachedPageMediaRuntime::CachedPageMediaRuntime(
	not_null<Main::Session*> session,
	FullMsgId itemId,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel,
	::Data::FileOrigin draftOrigin,
	base::weak_ptr<Window::SessionController> controller)
: _session(session)
, _controller(std::move(controller))
, _origin(itemId)
, _itemId(itemId)
, _draftOrigin(std::move(draftOrigin))
, _pageUrl()
, _openChannel(std::move(openChannel))
, _joinChannel(std::move(joinChannel)) {
}

CachedPageMediaRuntime::CachedPageMediaRuntime(
	not_null<Main::Session*> session,
	not_null<HistoryView::Element*> view,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel)
: _session(session)
, _origin(view->data()->fullId())
, _itemId(view->data()->fullId())
, _useExistingView(true)
, _view(base::make_weak(view.get()))
, _openChannel(std::move(openChannel))
, _joinChannel(std::move(joinChannel)) {
}

std::shared_ptr<Ui::DynamicImage> CachedPageMediaRuntime::resolveInlineImage(
		uint64 documentId,
		QSize size) const {
	const auto document = _session->data().document(DocumentId(documentId));
	if (document->isNull()) {
		return nullptr;
	}
	return std::make_shared<CachedPageInlineDocumentImage>(
		document,
		fileOrigin(),
		size);
}

std::shared_ptr<Markdown::PhotoRuntime> CachedPageMediaRuntime::resolvePhoto(
		uint64 photoId) const {
	const auto photo = _session->data().photo(PhotoId(photoId));
	if (photo->isNull()) {
		return nullptr;
	}
	const auto itemIdResolver = [runtime = shared_from_this()] {
		return runtime->openContextItemId();
	};
	return std::make_shared<CachedPagePhotoRuntime>(
		_session,
		photo,
		fileOrigin(),
		FullMsgId(),
		std::move(itemIdResolver));
}

std::shared_ptr<Markdown::DocumentRuntime> CachedPageMediaRuntime::resolveDocument(
		uint64 documentId) const {
	const auto document = _session->data().document(DocumentId(documentId));
	if (document->isNull()) {
		return nullptr;
	}
	const auto itemIdResolver = [runtime = shared_from_this()] {
		return runtime->openContextItemId();
	};
	return std::make_shared<CachedPageDocumentRuntime>(
		_session,
		document,
		fileOrigin(),
		FullMsgId(),
		std::move(itemIdResolver));
}

void CachedPageMediaRuntime::registerPhoto(
		uint64 photoId,
		TextWithEntities caption) const {
	const auto photo = _session->data().photo(PhotoId(photoId));
	if (photo->isNull()) {
		return;
	}
	if (const auto item = openContextItem()) {
		item->addPhotoForInstantView(photo, std::move(caption));
		return;
	}
	queuePendingInstantViewItem(
		PendingInstantViewMediaItem::Kind::Photo,
		photoId,
		std::move(caption));
}

void CachedPageMediaRuntime::registerDocument(
		uint64 documentId,
		TextWithEntities caption) const {
	const auto document = _session->data().document(DocumentId(documentId));
	if (document->isNull()) {
		return;
	}
	if (const auto item = openContextItem()) {
		item->addDocumentForInstantView(document, std::move(caption));
		return;
	}
	queuePendingInstantViewItem(
		PendingInstantViewMediaItem::Kind::Document,
		documentId,
		std::move(caption));
}

std::shared_ptr<Markdown::MapRuntime> CachedPageMediaRuntime::resolveMap(
		double latitude,
		double longitude,
		uint64 accessHash,
		QSize size,
		int zoom) const {
	return std::make_shared<CachedPageMapRuntime>(
		_session,
		fileOrigin(),
		latitude,
		longitude,
		accessHash,
		size,
		zoom);
}

std::shared_ptr<Markdown::ChannelRuntime> CachedPageMediaRuntime::resolveChannel(
		uint64 channelId,
		const QString &username) const {
	const auto channel = _session->data().channel(ChannelId(channelId));
	subscribeToChannel(channelId, channel);
	return std::make_shared<CachedPageChannelRuntime>(
		channel,
		SerializeNativeIvChannelContext(channelId, username),
		_openChannel,
		_joinChannel);
}

rpl::producer<uint64> CachedPageMediaRuntime::channelJoinedChanges() const {
	return _channelJoinedChanges.events();
}

Ui::Text::MarkedContext CachedPageMediaRuntime::textContext() const {
	return Core::TextContext({ .session = _session });
}

QString CachedPageMediaRuntime::mentionNameEntityData(uint64 userId) const {
	if (userId == 0) {
		return QString();
	}
	const auto loadedUser = _session->data().userLoaded(UserId(userId));
	return TextUtilities::MentionNameDataFromFields({
		.selfId = _session->userId().bare,
		.userId = userId,
		.accessHash = loadedUser ? loadedUser->accessHash() : 0,
	});
}

auto CachedPageMediaRuntime::hostedMediaHost(
		not_null<Window::SessionController*> controller) const
-> std::shared_ptr<Markdown::IvHistoryViewMediaHost> {
	if (_hostedMediaHost && !_hostedMediaHost->itemAlive()) {
		_hostedMediaHost = nullptr;
	}
	if (_useExistingView) {
		const auto view = _view.get();
		if (!view) {
			return nullptr;
		}
		if (!_hostedMediaHost) {
			_hostedMediaHost
				= std::make_shared<Markdown::IvHistoryViewMediaHost>(
					not_null{ view });
		}
		return _hostedMediaHost;
	}
	if (_itemId) {
		const auto item = _session->data().message(_itemId);
		if (!item) {
			return nullptr;
		}
		if (!_hostedMediaHost) {
			_hostedMediaHost
				= std::make_shared<Markdown::IvHistoryViewMediaHost>(
					controller,
					not_null{ item });
		}
		return _hostedMediaHost;
	}
	if (!_session->data().peerLoaded(PeerData::kServiceNotificationsId)) {
		return nullptr;
	}
	const auto history = _session->data().history(
		PeerData::kServiceNotificationsId);
	if (!history->peer->isUser()) {
		return nullptr;
	}
	if (!_hostedMediaHost) {
		_hostedMediaHost
			= std::make_shared<Markdown::IvHistoryViewMediaHost>(
				controller,
				history,
				_pageUrl);
		if (const auto cloudDraft = std::get_if<::Data::FileOriginCloudDraft>(
				&_draftOrigin.data)) {
			_hostedMediaHost->item()->setRichDraftOrigin(*cloudDraft);
		}
	}
	return _hostedMediaHost;
}

auto CachedPageMediaRuntime::hostedMediaBlockFactory() const
-> std::shared_ptr<Markdown::HostedMediaBlockFactory> {
	const auto controller = resolveController();
	if (!controller) {
		return nullptr;
	}
	const auto host = hostedMediaHost(not_null{ controller });
	if (!host) {
		return nullptr;
	}
	return std::make_shared<Markdown::IvHistoryViewMediaBlockFactory>(
		base::make_weak(controller),
		[session = _session, host, origin = fileOrigin()](
				Window::SessionController *controller,
				const Markdown::PreparedPhotoBlockData &prepared) {
			if (!prepared.viewerOpen
				|| !prepared.urlOverride.isEmpty()) {
				return std::shared_ptr<Markdown::MediaBlock>();
			}
			const auto photo = session->data().photo(PhotoId(prepared.photoId));
			if (photo->isNull()) {
				return std::shared_ptr<Markdown::MediaBlock>();
			}
			host->registerPhoto(photo);

			auto descriptor = Markdown::IvHistoryViewMediaDescriptor();
			descriptor.stableId = prepared.id.value;
			descriptor.kind = Markdown::IvHistoryViewMediaKind::Photo;
			descriptor.copyText = u"Photo"_q;
			descriptor.layoutHint = QSize(prepared.width, prepared.height);
			descriptor.host = host;
			descriptor.spoiler = prepared.spoiler;
			descriptor.mediaFactory = [photo, spoiler = prepared.spoiler](
					not_null<HistoryView::Element*> view) {
				return std::make_unique<HistoryView::Photo>(
					view,
					view->data(),
					photo,
					spoiler);
			};
			descriptor.photo = std::make_shared<CachedPagePhotoRuntime>(
				session,
				photo,
				origin,
				host->item()->fullId());
			return Markdown::CreateIvHistoryViewMediaBlock(
				std::move(descriptor));
		},
		[session = _session, host, origin = fileOrigin()](
				Window::SessionController *controller,
				const Markdown::PreparedVideoBlockData &prepared) {
			if (prepared.media.kind
				!= Markdown::PreparedMediaItemKind::Document) {
				return std::shared_ptr<Markdown::MediaBlock>();
			}
			const auto document = session->data().document(
				DocumentId(prepared.media.id));
			if (document->isNull()
				|| !CanHostNativeIvVideoDocument(document)) {
				return std::shared_ptr<Markdown::MediaBlock>();
			}
			host->registerDocument(document);
			auto media = std::make_shared<::Data::MediaFile>(
				host->item(),
				document,
				CachedPageVideoMediaArgs(
					session,
					document,
					prepared.media.spoiler));

			auto descriptor = Markdown::IvHistoryViewMediaDescriptor();
			descriptor.stableId = prepared.id.value;
			descriptor.kind = Markdown::IvHistoryViewMediaKind::Document;
			descriptor.copyText = tr::lng_in_dlg_video(tr::now);
			descriptor.layoutHint = QSize(
				prepared.media.width,
				prepared.media.height);
			descriptor.host = host;
			descriptor.spoiler = prepared.media.spoiler;
			descriptor.mediaFactory = [media](
					not_null<HistoryView::Element*> view) {
				return media->createView(
					view,
					view->data());
			};
			descriptor.keepAlive.push_back(base::take(media));
			descriptor.document = std::make_shared<CachedPageDocumentRuntime>(
				session,
				document,
				origin,
				host->item()->fullId());
			return Markdown::CreateIvHistoryViewMediaBlock(
				std::move(descriptor));
		},
		[session = _session, host, origin = fileOrigin()](
				Window::SessionController *controller,
				const Markdown::PreparedAudioBlockData &prepared) {
			const auto document = session->data().document(
				DocumentId(prepared.documentId));
			if (document->isNull()
				|| !CanHostNativeIvAudioDocument(document)) {
				return std::shared_ptr<Markdown::MediaBlock>();
			}
			host->registerDocument(document);
			auto media = std::make_shared<::Data::MediaFile>(
				host->item(),
				document,
				::Data::MediaFile::Args{});

			auto descriptor = Markdown::IvHistoryViewMediaDescriptor();
			descriptor.stableId = prepared.id.value;
			descriptor.kind = Markdown::IvHistoryViewMediaKind::Audio;
			descriptor.copyText = CachedPageAudioCopyText(prepared);
			descriptor.host = host;
			descriptor.mediaFactory = [media](
					not_null<HistoryView::Element*> view) {
				return media->createView(
					view,
					view->data());
			};
			descriptor.keepAlive.push_back(base::take(media));
			descriptor.document = std::make_shared<CachedPageDocumentRuntime>(
				session,
				document,
				origin,
				host->item()->fullId());
			return Markdown::CreateIvHistoryViewMediaBlock(
				std::move(descriptor));
		},
		[session = _session, host](
				Window::SessionController *controller,
				const Markdown::PreparedMapBlockData &prepared) {
			const auto mapSize = QSize(prepared.width, prepared.height);
			const auto point = CachedPageMapPoint(
				prepared.latitude,
				prepared.longitude,
				prepared.accessHash);
			const auto mapImage = std::make_shared<::Data::CloudImage>(
				session,
				CachedPageMapImageData(
					prepared.latitude,
					prepared.longitude,
					prepared.accessHash,
					mapSize,
					prepared.zoom));
			const auto mapImagePtr = mapImage.get();

			auto descriptor = Markdown::IvHistoryViewMediaDescriptor();
			descriptor.stableId = prepared.id.value;
			descriptor.kind = Markdown::IvHistoryViewMediaKind::Map;
			descriptor.copyText = tr::lng_maps_point(tr::now);
			descriptor.layoutHint = mapSize;
			descriptor.host = host;
			descriptor.mediaFactory = [mapImagePtr, point, mapSize](
					not_null<HistoryView::Element*> view) {
				return std::make_unique<HistoryView::Location>(
					view,
					not_null{ mapImagePtr },
					point,
					nullptr,
					0,
					mapSize);
			};
			descriptor.keepAlive.push_back(mapImage);
			return Markdown::CreateIvHistoryViewMediaBlock(
				std::move(descriptor));
		},
		[session = _session, host, origin = fileOrigin()](
				Window::SessionController *controller,
				const Markdown::PreparedGroupedMediaBlockData &prepared) {
			if (prepared.items.empty()
				|| (int(prepared.items.size())
					> HistoryView::GroupedMedia::kMaxSize)) {
				return std::shared_ptr<Markdown::MediaBlock>();
			}
			const auto slideshow = (prepared.intent
				== Markdown::PreparedGroupedMediaIntent::Slideshow);
			auto descriptor = Markdown::IvHistoryViewMediaDescriptor();
			auto slideFactories = std::vector<
				Markdown::IvHistoryViewMediaDescriptor::MediaFactory>();
			auto slideSizes = std::vector<QSize>();
			if (slideshow) {
				slideFactories.reserve(prepared.items.size());
				slideSizes.reserve(prepared.items.size());
			}
			const auto medias = std::make_shared<
				std::vector<std::unique_ptr<::Data::Media>>>();
			medias->reserve(prepared.items.size());
			for (auto i = 0, count = int(prepared.items.size())
					; i != count
					; ++i) {
				const auto &item = prepared.items[i];
				const auto kind = item.media.kind;
				if (kind == Markdown::PreparedMediaItemKind::Photo) {
					const auto photo = session->data().photo(
						PhotoId(item.media.id));
					if (photo->isNull()) {
						return std::shared_ptr<Markdown::MediaBlock>();
					}
					host->registerPhoto(photo);
					medias->push_back(std::make_unique<::Data::MediaPhoto>(
						host->item(),
						photo,
						item.media.spoiler));
					auto runtime = std::make_shared<CachedPagePhotoRuntime>(
						session,
						photo,
						origin,
						host->item()->fullId());
					descriptor.groupedPhotos.emplace(photo->id, runtime);
					descriptor.groupedItemIndices.emplace(photo->id, i);
					if (item.media.spoiler) {
						descriptor.groupedSpoileredIds.emplace(photo->id);
					}
					if (slideshow) {
						slideSizes.push_back(QSize(
							std::max(item.media.width, 1),
							std::max(item.media.height, 1)));
						const auto spoiler = item.media.spoiler;
						slideFactories.push_back([photo, spoiler](
								not_null<HistoryView::Element*> view) {
							return std::make_unique<HistoryView::Photo>(
								view,
								view->data(),
								photo,
								spoiler);
						});
					}
				} else {
					const auto document = session->data().document(
						DocumentId(item.media.id));
					if (document->isNull()
						|| !CanHostNativeIvVideoDocument(document)) {
						return std::shared_ptr<Markdown::MediaBlock>();
					}
					host->registerDocument(document);
					medias->push_back(std::make_unique<::Data::MediaFile>(
						host->item(),
						document,
						CachedPageVideoMediaArgs(
							session,
							document,
							item.media.spoiler)));
					auto runtime = std::make_shared<CachedPageDocumentRuntime>(
						session,
						document,
						origin,
						host->item()->fullId());
					descriptor.groupedDocuments.emplace(document->id, runtime);
					descriptor.groupedItemIndices.emplace(document->id, i);
					if (item.media.spoiler) {
						descriptor.groupedSpoileredIds.emplace(document->id);
					}
					if (slideshow) {
						const auto media = medias->back().get();
						slideSizes.push_back(QSize(
							std::max(item.media.width, 1),
							std::max(item.media.height, 1)));
						slideFactories.push_back([media](
								not_null<HistoryView::Element*> view) {
							return media->createView(view, view->data());
						});
					}
				}
				if (!medias->back()->canBeGrouped()) {
					return std::shared_ptr<Markdown::MediaBlock>();
				}
			}
			descriptor.stableId = prepared.id.value;
			descriptor.copyText = CachedPageGroupedMediaCopyText(prepared);
			descriptor.host = host;
			if (slideshow) {
				descriptor.kind = Markdown::IvHistoryViewMediaKind::Slideshow;
				descriptor.slideMediaFactories = std::move(slideFactories);
				descriptor.slideOriginalSizes = std::move(slideSizes);
			} else {
				descriptor.kind
					= Markdown::IvHistoryViewMediaKind::GroupedMedia;
				descriptor.layoutHint = QSize(st::historyGroupWidthMax, 0);
				descriptor.mediaFactory = [medias](
						not_null<HistoryView::Element*> view) {
					return std::make_unique<HistoryView::GroupedMedia>(
						view,
						*medias);
				};
			}
			descriptor.keepAlive.push_back(medias);
			return Markdown::CreateIvHistoryViewMediaBlock(
				std::move(descriptor));
		});
}

void CachedPageMediaRuntime::subscribeToChannel(
		uint64 channelId,
		not_null<ChannelData*> channel) const {
	if (_channelJoinedSubscriptions.find(channelId)
		!= end(_channelJoinedSubscriptions)) {
		return;
	}
	Info::Profile::AmInChannelValue(channel) | rpl::on_next([=](bool) {
		_channelJoinedChanges.fire_copy(channelId);
	}, _channelJoinedSubscriptions[channelId]);
}

HistoryItem *CachedPageMediaRuntime::directHostItem() const {
	if (const auto view = _view.get()) {
		return view->data().get();
	}
	return _itemId ? _session->data().message(_itemId) : nullptr;
}

Window::SessionController *CachedPageMediaRuntime::resolveController() const {
	if (const auto strong = _controller.get()) {
		return strong;
	}
	return _session->tryResolveWindow();
}

HistoryItem *CachedPageMediaRuntime::openContextItem() const {
	if (const auto item = directHostItem()) {
		flushPendingInstantViewItems(not_null{ item });
		return item;
	}
	if (const auto controller = resolveController()) {
		if (const auto host = hostedMediaHost(not_null{ controller })) {
			const auto item = host->item().get();
			flushPendingInstantViewItems(not_null{ item });
			return item;
		}
	}
	return nullptr;
}

void CachedPageMediaRuntime::queuePendingInstantViewItem(
		PendingInstantViewMediaItem::Kind kind,
		uint64 id,
		TextWithEntities caption) const {
	for (auto &pending : _pendingInstantViewItems) {
		if (pending.kind == kind && pending.id == id) {
			if (!caption.text.isEmpty() && pending.caption.text.isEmpty()) {
				pending.caption = std::move(caption);
			}
			return;
		}
	}
	_pendingInstantViewItems.push_back({
		.kind = kind,
		.id = id,
		.caption = std::move(caption),
	});
}

void CachedPageMediaRuntime::flushPendingInstantViewItems(
		not_null<HistoryItem*> item) const {
	if (_pendingInstantViewItems.empty()) {
		return;
	}
	for (auto &pending : _pendingInstantViewItems) {
		switch (pending.kind) {
		case PendingInstantViewMediaItem::Kind::Photo: {
			const auto photo = _session->data().photo(PhotoId(pending.id));
			if (!photo->isNull()) {
				item->addPhotoForInstantView(
					photo,
					std::move(pending.caption));
			}
		} break;
		case PendingInstantViewMediaItem::Kind::Document: {
			const auto document = _session->data().document(
				DocumentId(pending.id));
			if (!document->isNull()) {
				item->addDocumentForInstantView(
					document,
					std::move(pending.caption));
			}
		} break;
		}
	}
	_pendingInstantViewItems.clear();
}

FullMsgId CachedPageMediaRuntime::openContextItemId() const {
	if (const auto item = openContextItem()) {
		return item->fullId();
	}
	return FullMsgId();
}

::Data::FileOrigin CachedPageMediaRuntime::fileOrigin() const {
	return _origin;
}

} // namespace

auto CreateCachedPageMediaRuntime(
	not_null<Main::Session*> session,
	not_null<WebPageData*> page,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel)
-> std::shared_ptr<Markdown::MediaRuntime> {
	return std::make_shared<CachedPageMediaRuntime>(
		session,
		page,
		std::move(openChannel),
		std::move(joinChannel));
}

auto CreateMessageMediaRuntime(
	not_null<Main::Session*> session,
	FullMsgId itemId,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel,
	::Data::FileOrigin draftOrigin,
	base::weak_ptr<Window::SessionController> controller)
-> std::shared_ptr<Markdown::MediaRuntime> {
	return std::make_shared<CachedPageMediaRuntime>(
		session,
		itemId,
		std::move(openChannel),
		std::move(joinChannel),
		std::move(draftOrigin),
		std::move(controller));
}

auto CreateMessageMediaRuntime(
	not_null<Main::Session*> session,
	not_null<HistoryView::Element*> view,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel)
-> std::shared_ptr<Markdown::MediaRuntime> {
	return std::make_shared<CachedPageMediaRuntime>(
		session,
		view,
		std::move(openChannel),
		std::move(joinChannel));
}

} // namespace Iv
