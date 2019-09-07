/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/send_files_box.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "storage/storage_media_prepare.h"
#include "mainwidget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "history/view/history_view_schedule_box.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "core/event_filter.h"
#include "ui/effects/animations.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/grouped_layout.h"
#include "ui/text_options.h"
#include "ui/special_buttons.h"
#include "lottie/lottie_single_player.h"
#include "data/data_document.h"
#include "media/clip/media_clip_reader.h"
#include "api/api_common.h"
#include "window/window_session_controller.h"
#include "layout.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

namespace {

constexpr auto kMinPreviewWidth = 20;
constexpr auto kShrinkDuration = crl::time(150);
constexpr auto kDragDuration = crl::time(200);
const auto kStickerMimeString = qstr("image/webp");
const auto kAnimatedStickerMimeString = qstr("application/x-tgsticker");

class SingleMediaPreview : public Ui::RpWidget {
public:
	static SingleMediaPreview *Create(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const Storage::PreparedFile &file);

	SingleMediaPreview(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		QImage preview,
		bool animated,
		bool sticker,
		const QString &animatedPreviewPath);

	bool canSendAsPhoto() const {
		return _canSendAsPhoto;
	}

	rpl::producer<int> desiredHeightValue() const override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void preparePreview(
		QImage preview,
		const QString &animatedPreviewPath);
	void prepareAnimatedPreview(const QString &animatedPreviewPath);
	void clipCallback(Media::Clip::Notification notification);

	not_null<Window::SessionController*> _controller;
	bool _animated = false;
	bool _sticker = false;
	bool _canSendAsPhoto = false;
	QPixmap _preview;
	int _previewLeft = 0;
	int _previewWidth = 0;
	int _previewHeight = 0;
	Media::Clip::ReaderPointer _gifPreview;
	std::unique_ptr<Lottie::SinglePlayer> _lottiePreview;

};

class SingleFilePreview : public Ui::RpWidget {
public:
	SingleFilePreview(
		QWidget *parent,
		const Storage::PreparedFile &file);

	rpl::producer<int> desiredHeightValue() const override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void preparePreview(const Storage::PreparedFile &file);
	void prepareThumb(const QImage &preview);

	QPixmap _fileThumb;
	Ui::Text::String _nameText;
	bool _fileIsAudio = false;
	bool _fileIsImage = false;
	QString _statusText;
	int _statusWidth = 0;

};

class AlbumThumb {
public:
	AlbumThumb(
		const Storage::PreparedFile &file,
		const Ui::GroupMediaLayout &layout);

	void moveToLayout(const Ui::GroupMediaLayout &layout);
	void animateLayoutToInitial();
	void resetLayoutAnimation();

	int photoHeight() const;

	void paintInAlbum(
		Painter &p,
		int left,
		int top,
		float64 shrinkProgress,
		float64 moveProgress);
	void paintPhoto(Painter &p, int left, int top, int outerWidth);
	void paintFile(Painter &p, int left, int top, int outerWidth);

	bool containsPoint(QPoint position) const;
	int distanceTo(QPoint position) const;
	bool isPointAfter(QPoint position) const;
	void moveInAlbum(QPoint to);
	QPoint center() const;
	void suggestMove(float64 delta, Fn<void()> callback);
	void finishAnimations();

private:
	QRect countRealGeometry() const;
	QRect countCurrentGeometry(float64 progress) const;
	void prepareCache(QSize size, int shrink);
	void drawSimpleFrame(Painter &p, QRect to, QSize size) const;

	Ui::GroupMediaLayout _layout;
	std::optional<QRect> _animateFromGeometry;
	const QImage _fullPreview;
	const int _shrinkSize = 0;
	QPixmap _albumImage;
	QImage _albumCache;
	QPoint _albumPosition;
	RectParts _albumCorners = RectPart::None;
	QPixmap _photo;
	QPixmap _fileThumb;
	QString _name;
	QString _status;
	int _nameWidth = 0;
	int _statusWidth = 0;
	bool _isVideo = false;
	float64 _suggestedMove = 0.;
	Ui::Animations::Simple _suggestedMoveAnimation;
	int _lastShrinkValue = 0;

};

AlbumThumb::AlbumThumb(
	const Storage::PreparedFile &file,
	const Ui::GroupMediaLayout &layout)
: _layout(layout)
, _fullPreview(file.preview)
, _shrinkSize(int(std::ceil(st::historyMessageRadius / 1.4)))
, _isVideo(file.type == Storage::PreparedFile::AlbumType::Video) {
	Expects(!_fullPreview.isNull());

	moveToLayout(layout);

	using Option = Images::Option;
	const auto previewWidth = _fullPreview.width();
	const auto previewHeight = _fullPreview.height();
	const auto imageWidth = std::max(
		previewWidth / cIntRetinaFactor(),
		st::minPhotoSize);
	const auto imageHeight = std::max(
		previewHeight / cIntRetinaFactor(),
		st::minPhotoSize);
	_photo = App::pixmapFromImageInPlace(Images::prepare(
		_fullPreview,
		previewWidth,
		previewHeight,
		Option::RoundedLarge | Option::RoundedAll,
		imageWidth,
		imageHeight));

	const auto idealSize = st::sendMediaFileThumbSize * cIntRetinaFactor();
	const auto fileThumbSize = (previewWidth > previewHeight)
		? QSize(previewWidth * idealSize / previewHeight, idealSize)
		: QSize(idealSize, previewHeight * idealSize / previewWidth);
	_fileThumb = App::pixmapFromImageInPlace(Images::prepare(
		_fullPreview,
		fileThumbSize.width(),
		fileThumbSize.height(),
		Option::RoundedSmall | Option::RoundedAll,
		st::sendMediaFileThumbSize,
		st::sendMediaFileThumbSize
	));

	const auto availableFileWidth = st::sendMediaPreviewSize
		- st::sendMediaFileThumbSkip
		- st::sendMediaFileThumbSize;
	const auto filepath = file.path;
	if (filepath.isEmpty()) {
		_name = filedialogDefaultName(
			qsl("image"),
			qsl(".png"),
			QString(),
			true);
		_status = qsl("%1x%2").arg(
			_fullPreview.width()
		).arg(
			_fullPreview.height()
		);
	} else {
		auto fileinfo = QFileInfo(filepath);
		_name = fileinfo.fileName();
		_status = formatSizeText(fileinfo.size());
	}
	_nameWidth = st::semiboldFont->width(_name);
	if (_nameWidth > availableFileWidth) {
		_name = st::semiboldFont->elided(
			_name,
			availableFileWidth,
			Qt::ElideMiddle);
		_nameWidth = st::semiboldFont->width(_name);
	}
	_statusWidth = st::normalFont->width(_status);
}

void AlbumThumb::resetLayoutAnimation() {
	_animateFromGeometry = std::nullopt;
}

void AlbumThumb::animateLayoutToInitial() {
	_animateFromGeometry = countRealGeometry();
	_suggestedMove = 0.;
	_albumPosition = QPoint(0, 0);
}

void AlbumThumb::moveToLayout(const Ui::GroupMediaLayout &layout) {
	animateLayoutToInitial();
	_layout = layout;

	const auto width = _layout.geometry.width();
	const auto height = _layout.geometry.height();
	_albumCorners = Ui::GetCornersFromSides(_layout.sides);
	using Option = Images::Option;
	const auto options = Option::Smooth
		| Option::RoundedLarge
		| ((_albumCorners & RectPart::TopLeft)
			? Option::RoundedTopLeft
			: Option::None)
		| ((_albumCorners & RectPart::TopRight)
			? Option::RoundedTopRight
			: Option::None)
		| ((_albumCorners & RectPart::BottomLeft)
			? Option::RoundedBottomLeft
			: Option::None)
		| ((_albumCorners & RectPart::BottomRight)
			? Option::RoundedBottomRight
			: Option::None);
	const auto pixSize = Ui::GetImageScaleSizeForGeometry(
		{ _fullPreview.width(), _fullPreview.height() },
		{ width, height });
	const auto pixWidth = pixSize.width() * cIntRetinaFactor();
	const auto pixHeight = pixSize.height() * cIntRetinaFactor();

	_albumImage = App::pixmapFromImageInPlace(Images::prepare(
		_fullPreview,
		pixWidth,
		pixHeight,
		options,
		width,
		height));
}

int AlbumThumb::photoHeight() const {
	return _photo.height() / cIntRetinaFactor();
}

void AlbumThumb::paintInAlbum(
		Painter &p,
		int left,
		int top,
		float64 shrinkProgress,
		float64 moveProgress) {
	const auto shrink = anim::interpolate(0, _shrinkSize, shrinkProgress);
	_lastShrinkValue = shrink;
	const auto geometry = countCurrentGeometry(moveProgress);
	const auto x = left + geometry.x();
	const auto y = top + geometry.y();
	if (shrink > 0 || moveProgress < 1.) {
		const auto size = geometry.size();
		if (shrinkProgress < 1 && _albumCorners != RectPart::None) {
			prepareCache(size, shrink);
			p.drawImage(x, y, _albumCache);
		} else {
			const auto to = QRect({ x, y }, size).marginsRemoved(
				{ shrink, shrink, shrink, shrink }
			);
			drawSimpleFrame(p, to, size);
		}
	} else {
		p.drawPixmap(x, y, _albumImage);
	}
	if (_isVideo) {
		const auto inner = QRect(
			x + (geometry.width() - st::msgFileSize) / 2,
			y + (geometry.height() - st::msgFileSize) / 2,
			st::msgFileSize,
			st::msgFileSize);
		{
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgDateImgBg);
			p.drawEllipse(inner);
		}
		st::historyFileThumbPlay.paintInCenter(p, inner);
	}
}

void AlbumThumb::prepareCache(QSize size, int shrink) {
	const auto width = std::max(
		_layout.geometry.width(),
		_animateFromGeometry ? _animateFromGeometry->width() : 0);
	const auto height = std::max(
		_layout.geometry.height(),
		_animateFromGeometry ? _animateFromGeometry->height() : 0);
	const auto cacheSize = QSize(width, height) * cIntRetinaFactor();

	if (_albumCache.width() < cacheSize.width()
		|| _albumCache.height() < cacheSize.height()) {
		_albumCache = QImage(cacheSize, QImage::Format_ARGB32_Premultiplied);
	}
	_albumCache.fill(Qt::transparent);
	{
		Painter p(&_albumCache);
		const auto to = QRect(QPoint(), size).marginsRemoved(
			{ shrink, shrink, shrink, shrink }
		);
		drawSimpleFrame(p, to, size);
	}
	Images::prepareRound(
		_albumCache,
		ImageRoundRadius::Large,
		_albumCorners,
		QRect(QPoint(), size * cIntRetinaFactor()));
}

void AlbumThumb::drawSimpleFrame(Painter &p, QRect to, QSize size) const {
	const auto fullWidth = _fullPreview.width();
	const auto fullHeight = _fullPreview.height();
	const auto previewSize = Ui::GetImageScaleSizeForGeometry(
		{ fullWidth, fullHeight },
		{ size.width(), size.height() });
	const auto previewWidth = previewSize.width() * cIntRetinaFactor();
	const auto previewHeight = previewSize.height() * cIntRetinaFactor();
	const auto width = size.width() * cIntRetinaFactor();
	const auto height = size.height() * cIntRetinaFactor();
	const auto scaleWidth = to.width() / float64(width);
	const auto scaleHeight = to.height() / float64(height);
	const auto Round = [](float64 value) {
		return int(std::round(value));
	};
	const auto [from, fillBlack] = [&] {
		if (previewWidth < width && previewHeight < height) {
			const auto toWidth = Round(previewWidth * scaleWidth);
			const auto toHeight = Round(previewHeight * scaleHeight);
			return std::make_pair(
				QRect(0, 0, fullWidth, fullHeight),
				QMargins(
					(to.width() - toWidth) / 2,
					(to.height() - toHeight) / 2,
					to.width() - toWidth - (to.width() - toWidth) / 2,
					to.height() - toHeight - (to.height() - toHeight) / 2));
		} else if (previewWidth * height > previewHeight * width) {
			if (previewHeight >= height) {
				const auto takeWidth = previewWidth * height / previewHeight;
				const auto useWidth = fullWidth * width / takeWidth;
				return std::make_pair(
					QRect(
						(fullWidth - useWidth) / 2,
						0,
						useWidth,
						fullHeight),
					QMargins(0, 0, 0, 0));
			} else {
				const auto takeWidth = previewWidth;
				const auto useWidth = fullWidth * width / takeWidth;
				const auto toHeight = Round(previewHeight * scaleHeight);
				const auto toSkip = (to.height() - toHeight) / 2;
				return std::make_pair(
					QRect(
						(fullWidth - useWidth) / 2,
						0,
						useWidth,
						fullHeight),
					QMargins(
						0,
						toSkip,
						0,
						to.height() - toHeight - toSkip));
			}
		} else {
			if (previewWidth >= width) {
				const auto takeHeight = previewHeight * width / previewWidth;
				const auto useHeight = fullHeight * height / takeHeight;
				return std::make_pair(
					QRect(
						0,
						(fullHeight - useHeight) / 2,
						fullWidth,
						useHeight),
					QMargins(0, 0, 0, 0));
			} else {
				const auto takeHeight = previewHeight;
				const auto useHeight = fullHeight * height / takeHeight;
				const auto toWidth = Round(previewWidth * scaleWidth);
				const auto toSkip = (to.width() - toWidth) / 2;
				return std::make_pair(
					QRect(
						0,
						(fullHeight - useHeight) / 2,
						fullWidth,
						useHeight),
					QMargins(
						toSkip,
						0,
						to.width() - toWidth - toSkip,
						0));
			}
		}
	}();

	p.drawImage(to.marginsRemoved(fillBlack), _fullPreview, from);
	if (fillBlack.top() > 0) {
		p.fillRect(to.x(), to.y(), to.width(), fillBlack.top(), st::imageBg);
	}
	if (fillBlack.bottom() > 0) {
		p.fillRect(
			to.x(),
			to.y() + to.height() - fillBlack.bottom(),
			to.width(),
			fillBlack.bottom(),
			st::imageBg);
	}
	if (fillBlack.left() > 0) {
		p.fillRect(
			to.x(),
			to.y() + fillBlack.top(),
			fillBlack.left(),
			to.height() - fillBlack.top() - fillBlack.bottom(),
			st::imageBg);
	}
	if (fillBlack.right() > 0) {
		p.fillRect(
			to.x() + to.width() - fillBlack.right(),
			to.y() + fillBlack.top(),
			fillBlack.right(),
			to.height() - fillBlack.top() - fillBlack.bottom(),
			st::imageBg);
	}
}

void AlbumThumb::paintPhoto(Painter &p, int left, int top, int outerWidth) {
	const auto width = _photo.width() / cIntRetinaFactor();
	p.drawPixmapLeft(
		left + (st::sendMediaPreviewSize - width) / 2,
		top,
		outerWidth,
		_photo);
}

void AlbumThumb::paintFile(Painter &p, int left, int top, int outerWidth) {
	const auto textLeft = left
		+ st::sendMediaFileThumbSize
		+ st::sendMediaFileThumbSkip;

	p.drawPixmap(left, top, _fileThumb);
	p.setFont(st::semiboldFont);
	p.setPen(st::historyFileNameInFg);
	p.drawTextLeft(
		textLeft,
		top + st::sendMediaFileNameTop,
		outerWidth,
		_name,
		_nameWidth);
	p.setFont(st::normalFont);
	p.setPen(st::mediaInFg);
	p.drawTextLeft(
		textLeft,
		top + st::sendMediaFileStatusTop,
		outerWidth,
		_status,
		_statusWidth);
}

bool AlbumThumb::containsPoint(QPoint position) const {
	return _layout.geometry.contains(position);
}

int AlbumThumb::distanceTo(QPoint position) const {
	const auto delta = (_layout.geometry.center() - position);
	return QPoint::dotProduct(delta, delta);
}

bool AlbumThumb::isPointAfter(QPoint position) const {
	return position.x() > _layout.geometry.center().x();
}

void AlbumThumb::moveInAlbum(QPoint to) {
	_albumPosition = to;
}

QPoint AlbumThumb::center() const {
	auto realGeometry = _layout.geometry;
	realGeometry.moveTopLeft(realGeometry.topLeft() + _albumPosition);
	return realGeometry.center();
}

void AlbumThumb::suggestMove(float64 delta, Fn<void()> callback) {
	if (_suggestedMove != delta) {
		_suggestedMoveAnimation.start(
			std::move(callback),
			_suggestedMove,
			delta,
			kShrinkDuration);
		_suggestedMove = delta;
	}
}

QRect AlbumThumb::countRealGeometry() const {
	const auto addLeft = int(std::round(
		_suggestedMoveAnimation.value(_suggestedMove) * _lastShrinkValue));
	const auto current = _layout.geometry;
	const auto realTopLeft = current.topLeft()
		+ _albumPosition
		+ QPoint(addLeft, 0);
	return { realTopLeft, current.size() };
}

QRect AlbumThumb::countCurrentGeometry(float64 progress) const {
	const auto now = countRealGeometry();
	if (_animateFromGeometry && progress < 1.) {
		return {
			anim::interpolate(_animateFromGeometry->x(), now.x(), progress),
			anim::interpolate(_animateFromGeometry->y(), now.y(), progress),
			anim::interpolate(_animateFromGeometry->width(), now.width(), progress),
			anim::interpolate(_animateFromGeometry->height(), now.height(), progress)
		};
	}
	return now;
}

void AlbumThumb::finishAnimations() {
	_suggestedMoveAnimation.stop();
}

SingleMediaPreview *SingleMediaPreview::Create(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const Storage::PreparedFile &file) {
	auto preview = QImage();
	bool animated = false;
	bool animationPreview = false;
	if (const auto image = base::get_if<FileMediaInformation::Image>(
			&file.information->media)) {
		preview = image->data;
		animated = animationPreview = image->animated;
	} else if (const auto video = base::get_if<FileMediaInformation::Video>(
			&file.information->media)) {
		preview = video->thumbnail;
		animated = true;
		animationPreview = video->isGifv;
	}
	if (preview.isNull()) {
		return nullptr;
	} else if (!animated && !Storage::ValidateThumbDimensions(
			preview.width(),
			preview.height())) {
		return nullptr;
	}
	const auto sticker = (file.information->filemime == kStickerMimeString)
		|| (file.information->filemime == kAnimatedStickerMimeString);
	return Ui::CreateChild<SingleMediaPreview>(
		parent,
		controller,
		preview,
		animated,
		sticker,
		animationPreview ? file.path : QString());
}

SingleMediaPreview::SingleMediaPreview(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	QImage preview,
	bool animated,
	bool sticker,
	const QString &animatedPreviewPath)
: RpWidget(parent)
, _controller(controller)
, _animated(animated)
, _sticker(sticker) {
	Expects(!preview.isNull());

	_canSendAsPhoto = !_animated
		&& !_sticker
		&& Storage::ValidateThumbDimensions(
			preview.width(),
			preview.height());

	preparePreview(preview, animatedPreviewPath);
}

void SingleMediaPreview::preparePreview(
		QImage preview,
		const QString &animatedPreviewPath) {
	auto maxW = 0;
	auto maxH = 0;
	if (_animated && !_sticker) {
		auto limitW = st::sendMediaPreviewSize;
		auto limitH = st::confirmMaxHeight;
		maxW = qMax(preview.width(), 1);
		maxH = qMax(preview.height(), 1);
		if (maxW * limitH > maxH * limitW) {
			if (maxW < limitW) {
				maxH = maxH * limitW / maxW;
				maxW = limitW;
			}
		} else {
			if (maxH < limitH) {
				maxW = maxW * limitH / maxH;
				maxH = limitH;
			}
		}
		preview = Images::prepare(
			preview,
			maxW * cIntRetinaFactor(),
			maxH * cIntRetinaFactor(),
			Images::Option::Smooth | Images::Option::Blurred,
			maxW,
			maxH);
	}
	auto originalWidth = preview.width();
	auto originalHeight = preview.height();
	if (!originalWidth || !originalHeight) {
		originalWidth = originalHeight = 1;
	}
	_previewWidth = st::sendMediaPreviewSize;
	if (preview.width() < _previewWidth) {
		_previewWidth = qMax(preview.width(), kMinPreviewWidth);
	}
	auto maxthumbh = qMin(qRound(1.5 * _previewWidth), st::confirmMaxHeight);
	_previewHeight = qRound(originalHeight * float64(_previewWidth) / originalWidth);
	if (_previewHeight > maxthumbh) {
		_previewWidth = qRound(_previewWidth * float64(maxthumbh) / _previewHeight);
		accumulate_max(_previewWidth, kMinPreviewWidth);
		_previewHeight = maxthumbh;
	}
	_previewLeft = (st::boxWideWidth - _previewWidth) / 2;

	preview = std::move(preview).scaled(
		_previewWidth * cIntRetinaFactor(),
		_previewHeight * cIntRetinaFactor(),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	preview = Images::prepareOpaque(std::move(preview));
	_preview = App::pixmapFromImageInPlace(std::move(preview));
	_preview.setDevicePixelRatio(cRetinaFactor());

	prepareAnimatedPreview(animatedPreviewPath);
}

void SingleMediaPreview::prepareAnimatedPreview(
		const QString &animatedPreviewPath) {
	if (_sticker && _animated) {
		const auto box = QSize(_previewWidth, _previewHeight)
			* cIntRetinaFactor();
		_lottiePreview = std::make_unique<Lottie::SinglePlayer>(
			Lottie::ReadContent(QByteArray(), animatedPreviewPath),
			Lottie::FrameRequest{ box });
		_lottiePreview->updates(
		) | rpl::start_with_next([=] {
			update();
		}, lifetime());
	} else if (!animatedPreviewPath.isEmpty()) {
		auto callback = [=](Media::Clip::Notification notification) {
			clipCallback(notification);
		};
		_gifPreview = Media::Clip::MakeReader(
			animatedPreviewPath,
			std::move(callback));
		if (_gifPreview) _gifPreview->setAutoplay();
	}
}

void SingleMediaPreview::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gifPreview && _gifPreview->state() == State::Error) {
			_gifPreview.setBad();
		}

		if (_gifPreview && _gifPreview->ready() && !_gifPreview->started()) {
			auto s = QSize(_previewWidth, _previewHeight);
			_gifPreview->start(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None);
		}

		update();
	} break;

	case NotificationRepaint: {
		if (_gifPreview && !_gifPreview->currentDisplayed()) {
			update();
		}
	} break;
	}
}

void SingleMediaPreview::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_sticker) {
		if (_previewLeft > st::boxPhotoPadding.left()) {
			p.fillRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top(), _previewLeft - st::boxPhotoPadding.left(), _previewHeight, st::confirmBg);
		}
		if (_previewLeft + _previewWidth < width() - st::boxPhotoPadding.right()) {
			p.fillRect(_previewLeft + _previewWidth, st::boxPhotoPadding.top(), width() - st::boxPhotoPadding.right() - _previewLeft - _previewWidth, _previewHeight, st::confirmBg);
		}
	}
	if (_gifPreview && _gifPreview->started()) {
		auto s = QSize(_previewWidth, _previewHeight);
		auto paused = _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
		auto frame = _gifPreview->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None, paused ? 0 : crl::now());
		p.drawPixmap(_previewLeft, st::boxPhotoPadding.top(), frame);
	} else if (_lottiePreview && _lottiePreview->ready()) {
		const auto frame = _lottiePreview->frame();
		const auto size = frame.size() / cIntRetinaFactor();
		p.drawImage(
			QRect(
				_previewLeft + (_previewWidth - size.width()) / 2,
				st::boxPhotoPadding.top() + (_previewHeight - size.height()) / 2,
				size.width(),
				size.height()),
			frame);
		_lottiePreview->markFrameShown();
	} else {
		p.drawPixmap(_previewLeft, st::boxPhotoPadding.top(), _preview);
	}
	if (_animated && !_gifPreview && !_lottiePreview) {
		auto inner = QRect(_previewLeft + (_previewWidth - st::msgFileSize) / 2, st::boxPhotoPadding.top() + (_previewHeight - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgDateImgBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		auto icon = &st::historyFileInPlay;
		icon->paintInCenter(p, inner);
	}
}

rpl::producer<int> SingleMediaPreview::desiredHeightValue() const {
	return rpl::single(st::boxPhotoPadding.top() + _previewHeight);
}

SingleFilePreview::SingleFilePreview(
	QWidget *parent,
	const Storage::PreparedFile &file)
: RpWidget(parent) {
	preparePreview(file);
}

void SingleFilePreview::prepareThumb(const QImage &preview) {
	if (preview.isNull()) {
		return;
	}

	auto originalWidth = preview.width();
	auto originalHeight = preview.height();
	auto thumbWidth = st::msgFileThumbSize;
	if (originalWidth > originalHeight) {
		thumbWidth = (originalWidth * st::msgFileThumbSize)
			/ originalHeight;
	}
	auto options = Images::Option::Smooth
		| Images::Option::RoundedSmall
		| Images::Option::RoundedTopLeft
		| Images::Option::RoundedTopRight
		| Images::Option::RoundedBottomLeft
		| Images::Option::RoundedBottomRight;
	_fileThumb = App::pixmapFromImageInPlace(Images::prepare(
		preview,
		thumbWidth * cIntRetinaFactor(),
		0,
		options,
		st::msgFileThumbSize,
		st::msgFileThumbSize));
}

void SingleFilePreview::preparePreview(const Storage::PreparedFile &file) {
	auto preview = QImage();
	if (const auto image = base::get_if<FileMediaInformation::Image>(
		&file.information->media)) {
		preview = image->data;
	} else if (const auto video = base::get_if<FileMediaInformation::Video>(
		&file.information->media)) {
		preview = video->thumbnail;
	}
	prepareThumb(preview);
	const auto filepath = file.path;
	if (filepath.isEmpty()) {
		auto filename = filedialogDefaultName(
			qsl("image"),
			qsl(".png"),
			QString(),
			true);
		_nameText.setText(
			st::semiboldTextStyle,
			filename,
			Ui::NameTextOptions());
		_statusText = qsl("%1x%2").arg(preview.width()).arg(preview.height());
		_statusWidth = qMax(_nameText.maxWidth(), st::normalFont->width(_statusText));
		_fileIsImage = true;
	} else {
		auto fileinfo = QFileInfo(filepath);
		auto filename = fileinfo.fileName();
		_fileIsImage = fileIsImage(filename, Core::MimeTypeForFile(fileinfo).name());

		auto songTitle = QString();
		auto songPerformer = QString();
		if (file.information) {
			if (const auto song = base::get_if<FileMediaInformation::Song>(
					&file.information->media)) {
				songTitle = song->title;
				songPerformer = song->performer;
				_fileIsAudio = true;
			}
		}

		const auto nameString = DocumentData::ComposeNameString(
			filename,
			songTitle,
			songPerformer);
		_nameText.setText(
			st::semiboldTextStyle,
			nameString,
			Ui::NameTextOptions());
		_statusText = formatSizeText(fileinfo.size());
		_statusWidth = qMax(
			_nameText.maxWidth(),
			st::normalFont->width(_statusText));
	}
}

void SingleFilePreview::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
	auto h = _fileThumb.isNull() ? (st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom()) : (st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom());
	auto nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	if (_fileThumb.isNull()) {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nametop = st::msgFileNameTop;
		nameright = st::msgFilePadding.left();
		statustop = st::msgFileStatusTop;
	} else {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nametop = st::msgFileThumbNameTop;
		nameright = st::msgFileThumbPadding.left();
		statustop = st::msgFileThumbStatusTop;
		linktop = st::msgFileThumbLinkTop;
	}
	auto namewidth = w - nameleft - (_fileThumb.isNull() ? st::msgFilePadding.left() : st::msgFileThumbPadding.left());
	int32 x = (width() - w) / 2, y = st::boxPhotoPadding.top();

	App::roundRect(p, x, y, w, h, st::msgOutBg, MessageOutCorners, &st::msgOutShadow);

	if (_fileThumb.isNull()) {
		QRect inner(rtlrect(x + st::msgFilePadding.left(), y + st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, width()));
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgFileOutBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		auto &icon = _fileIsAudio
			? st::historyFileOutPlay
			: _fileIsImage
			? st::historyFileOutImage
			: st::historyFileOutDocument;
		icon.paintInCenter(p, inner);
	} else {
		QRect rthumb(rtlrect(x + st::msgFileThumbPadding.left(), y + st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, width()));
		p.drawPixmap(rthumb.topLeft(), _fileThumb);
	}
	p.setFont(st::semiboldFont);
	p.setPen(st::historyFileNameOutFg);
	_nameText.drawLeftElided(p, x + nameleft, y + nametop, namewidth, width());

	auto &status = st::mediaOutFg;
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(x + nameleft, y + statustop, width(), _statusText);
}

rpl::producer<int> SingleFilePreview::desiredHeightValue() const {
	auto h = _fileThumb.isNull()
		? (st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom())
		: (st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom());
	return rpl::single(st::boxPhotoPadding.top() + h + st::msgShadow);
}

rpl::producer<QString> FieldPlaceholder(
		const Storage::PreparedList &list,
		SendFilesWay way) {
	const auto isAlbum = (way == SendFilesWay::Album);
	const auto compressImages = (way != SendFilesWay::Files);
	return list.canAddCaption(isAlbum, compressImages)
		? tr::lng_photo_caption()
		: tr::lng_photos_comment();
}

} // namespace

class SendFilesBox::AlbumPreview : public Ui::RpWidget {
public:
	AlbumPreview(
		QWidget *parent,
		const Storage::PreparedList &list,
		SendFilesWay way);

	void setSendWay(SendFilesWay way);
	std::vector<int> takeOrder();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	int countLayoutHeight(
		const std::vector<Ui::GroupMediaLayout> &layout) const;
	std::vector<Ui::GroupMediaLayout> generateOrderedLayout() const;
	std::vector<int> defaultOrder() const;
	void prepareThumbs();
	void updateSizeAnimated(const std::vector<Ui::GroupMediaLayout> &layout);
	void updateSize();

	void paintAlbum(Painter &p) const;
	void paintPhotos(Painter &p, QRect clip) const;
	void paintFiles(Painter &p, QRect clip) const;

	void applyCursor(style::cursor cursor);
	int contentLeft() const;
	int contentTop() const;
	AlbumThumb *findThumb(QPoint position) const;
	not_null<AlbumThumb*> findClosestThumb(QPoint position) const;
	void updateSuggestedDrag(QPoint position);
	int orderIndex(not_null<AlbumThumb*> thumb) const;
	void cancelDrag();
	void finishDrag();

	const Storage::PreparedList &_list;
	SendFilesWay _sendWay = SendFilesWay::Files;
	style::cursor _cursor = style::cur_default;
	std::vector<int> _order;
	std::vector<std::unique_ptr<AlbumThumb>> _thumbs;
	int _thumbsHeight = 0;
	int _photosHeight = 0;
	int _filesHeight = 0;

	AlbumThumb *_draggedThumb = nullptr;
	AlbumThumb *_suggestedThumb = nullptr;
	AlbumThumb *_paintedAbove = nullptr;
	QPoint _draggedStartPosition;

	mutable Ui::Animations::Simple _thumbsHeightAnimation;
	mutable Ui::Animations::Simple _shrinkAnimation;
	mutable Ui::Animations::Simple _finishDragAnimation;

};

SendFilesBox::AlbumPreview::AlbumPreview(
	QWidget *parent,
	const Storage::PreparedList &list,
	SendFilesWay way)
: RpWidget(parent)
, _list(list)
, _sendWay(way) {
	setMouseTracking(true);
	prepareThumbs();
	updateSize();
}

void SendFilesBox::AlbumPreview::setSendWay(SendFilesWay way) {
	if (_sendWay != way) {
		cancelDrag();
		_sendWay = way;
	}
	updateSize();
	update();
}

std::vector<int> SendFilesBox::AlbumPreview::takeOrder() {
	auto reordered = std::vector<std::unique_ptr<AlbumThumb>>();
	reordered.reserve(_thumbs.size());
	for (auto index : _order) {
		reordered.push_back(std::move(_thumbs[index]));
	}
	_thumbs = std::move(reordered);
	return std::exchange(_order, defaultOrder());
}

auto SendFilesBox::AlbumPreview::generateOrderedLayout() const
-> std::vector<Ui::GroupMediaLayout> {
	auto sizes = ranges::view::all(
		_order
	) | ranges::view::transform([&](int index) {
		return _list.files[index].shownDimensions;
	}) | ranges::to_vector;

	auto layout = Ui::LayoutMediaGroup(
		sizes,
		st::sendMediaPreviewSize,
		st::historyGroupWidthMin / 2,
		st::historyGroupSkip / 2);
	Assert(layout.size() == _order.size());
	return layout;
}

std::vector<int> SendFilesBox::AlbumPreview::defaultOrder() const {
	const auto count = int(_list.files.size());
	return ranges::view::ints(0, count) | ranges::to_vector;
}

void SendFilesBox::AlbumPreview::prepareThumbs() {
	_order = defaultOrder();

	const auto count = int(_list.files.size());
	const auto layout = generateOrderedLayout();
	_thumbs.reserve(count);
	for (auto i = 0; i != count; ++i) {
		_thumbs.push_back(std::make_unique<AlbumThumb>(
			_list.files[i],
			layout[i]));
	}
	_thumbsHeight = countLayoutHeight(layout);
	_photosHeight = ranges::accumulate(ranges::view::all(
		_thumbs
	) | ranges::view::transform([](const auto &thumb) {
		return thumb->photoHeight();
	}), 0) + (count - 1) * st::sendMediaPreviewPhotoSkip;

	_filesHeight = count * st::sendMediaFileThumbSize
		+ (count - 1) * st::sendMediaFileThumbSkip;
}

int SendFilesBox::AlbumPreview::contentLeft() const {
	return (st::boxWideWidth - st::sendMediaPreviewSize) / 2;
}

int SendFilesBox::AlbumPreview::contentTop() const {
	return 0;
}

AlbumThumb *SendFilesBox::AlbumPreview::findThumb(QPoint position) const {
	position -= QPoint(contentLeft(), contentTop());
	const auto i = ranges::find_if(_thumbs, [&](const auto &thumb) {
		return thumb->containsPoint(position);
	});
	return (i == _thumbs.end()) ? nullptr : i->get();
}

not_null<AlbumThumb*> SendFilesBox::AlbumPreview::findClosestThumb(
		QPoint position) const {
	Expects(_draggedThumb != nullptr);

	if (const auto exact = findThumb(position)) {
		return exact;
	}
	auto result = _draggedThumb;
	auto distance = _draggedThumb->distanceTo(position);
	for (const auto &thumb : _thumbs) {
		const auto check = thumb->distanceTo(position);
		if (check < distance) {
			distance = check;
			result = thumb.get();
		}
	}
	return result;
}

int SendFilesBox::AlbumPreview::orderIndex(
		not_null<AlbumThumb*> thumb) const {
	const auto i = ranges::find_if(_order, [&](int index) {
		return (_thumbs[index].get() == thumb);
	});
	Assert(i != _order.end());
	return int(i - _order.begin());
}

void SendFilesBox::AlbumPreview::cancelDrag() {
	_thumbsHeightAnimation.stop();
	_finishDragAnimation.stop();
	_shrinkAnimation.stop();
	if (_draggedThumb) {
		_draggedThumb->moveInAlbum({ 0, 0 });
		_draggedThumb = nullptr;
	}
	if (_suggestedThumb) {
		const auto suggestedIndex = orderIndex(_suggestedThumb);
		if (suggestedIndex > 0) {
			_thumbs[_order[suggestedIndex - 1]]->suggestMove(0., [] {});
		}
		if (suggestedIndex < int(_order.size() - 1)) {
			_thumbs[_order[suggestedIndex + 1]]->suggestMove(0., [] {});
		}
		_suggestedThumb->suggestMove(0., [] {});
		_suggestedThumb->finishAnimations();
		_suggestedThumb = nullptr;
	}
	_paintedAbove = nullptr;
	update();
}

void SendFilesBox::AlbumPreview::finishDrag() {
	Expects(_draggedThumb != nullptr);
	Expects(_suggestedThumb != nullptr);

	if (_suggestedThumb != _draggedThumb) {
		const auto currentIndex = orderIndex(_draggedThumb);
		const auto newIndex = orderIndex(_suggestedThumb);
		const auto delta = (currentIndex < newIndex) ? 1 : -1;
		const auto realIndex = _order[currentIndex];
		for (auto i = currentIndex; i != newIndex; i += delta) {
			_order[i] = _order[i + delta];
		}
		_order[newIndex] = realIndex;
		const auto layout = generateOrderedLayout();
		for (auto i = 0, count = int(_order.size()); i != count; ++i) {
			_thumbs[_order[i]]->moveToLayout(layout[i]);
		}
		_finishDragAnimation.start([=] { update(); }, 0., 1., kDragDuration);

		updateSizeAnimated(layout);
	} else {
		for (const auto &thumb : _thumbs) {
			thumb->resetLayoutAnimation();
		}
		_draggedThumb->animateLayoutToInitial();
		_finishDragAnimation.start([=] { update(); }, 0., 1., kDragDuration);
	}
}

int SendFilesBox::AlbumPreview::countLayoutHeight(
		const std::vector<Ui::GroupMediaLayout> &layout) const {
	const auto accumulator = [](int current, const auto &item) {
		return std::max(current, item.geometry.y() + item.geometry.height());
	};
	return ranges::accumulate(layout, 0, accumulator);
}

void SendFilesBox::AlbumPreview::updateSizeAnimated(
		const std::vector<Ui::GroupMediaLayout> &layout) {
	const auto newHeight = countLayoutHeight(layout);
	if (newHeight != _thumbsHeight) {
		_thumbsHeightAnimation.start(
			[=] { updateSize(); },
			_thumbsHeight,
			newHeight,
			kDragDuration);
		_thumbsHeight = newHeight;
	}
}

void SendFilesBox::AlbumPreview::updateSize() {
	const auto newHeight = [&] {
		switch (_sendWay) {
		case SendFilesWay::Album:
			return int(std::round(_thumbsHeightAnimation.value(
				_thumbsHeight)));
		case SendFilesWay::Photos: return _photosHeight;
		case SendFilesWay::Files: return _filesHeight;
		}
		Unexpected("Send way in SendFilesBox::AlbumPreview::updateSize");
	}();
	if (height() != newHeight) {
		resize(st::boxWideWidth, newHeight);
	}
}

void SendFilesBox::AlbumPreview::paintEvent(QPaintEvent *e) {
	Painter p(this);

	switch (_sendWay) {
	case SendFilesWay::Album: paintAlbum(p); break;
	case SendFilesWay::Photos: paintPhotos(p, e->rect()); break;
	case SendFilesWay::Files: paintFiles(p, e->rect()); break;
	}
}

void SendFilesBox::AlbumPreview::paintAlbum(Painter &p) const {
	const auto shrink = _shrinkAnimation.value(_draggedThumb ? 1. : 0.);
	const auto moveProgress = _finishDragAnimation.value(1.);
	const auto left = contentLeft();
	const auto top = contentTop();
	for (const auto &thumb : _thumbs) {
		if (thumb.get() != _paintedAbove) {
			thumb->paintInAlbum(p, left, top, shrink, moveProgress);
		}
	}
	if (_paintedAbove) {
		_paintedAbove->paintInAlbum(p, left, top, shrink, moveProgress);
	}
}

void SendFilesBox::AlbumPreview::paintPhotos(Painter &p, QRect clip) const {
	const auto left = (st::boxWideWidth - st::sendMediaPreviewSize) / 2;
	auto top = 0;
	const auto outerWidth = width();
	for (const auto &thumb : _thumbs) {
		const auto bottom = top + thumb->photoHeight();
		const auto guard = gsl::finally([&] {
			top = bottom + st::sendMediaPreviewPhotoSkip;
		});
		if (top >= clip.y() + clip.height()) {
			break;
		} else if (bottom <= clip.y()) {
			continue;
		}
		thumb->paintPhoto(p, left, top, outerWidth);
	}
}

void SendFilesBox::AlbumPreview::paintFiles(Painter &p, QRect clip) const {
	const auto fileHeight = st::sendMediaFileThumbSize
		+ st::sendMediaFileThumbSkip;
	const auto bottom = clip.y() + clip.height();
	const auto from = floorclamp(clip.y(), fileHeight, 0, _thumbs.size());
	const auto till = ceilclamp(bottom, fileHeight, 0, _thumbs.size());
	const auto left = (st::boxWideWidth - st::sendMediaPreviewSize) / 2;
	const auto outerWidth = width();

	auto top = from * fileHeight;
	for (auto i = from; i != till; ++i) {
		_thumbs[i]->paintFile(p, left, top, outerWidth);
		top += fileHeight;
	}
}

void SendFilesBox::AlbumPreview::mousePressEvent(QMouseEvent *e) {
	if (_finishDragAnimation.animating()) {
		return;
	}
	const auto position = e->pos();
	cancelDrag();
	if (const auto thumb = findThumb(position)) {
		_paintedAbove = _suggestedThumb = _draggedThumb = thumb;
		_draggedStartPosition = position;
		_shrinkAnimation.start([=] { update(); }, 0., 1., kShrinkDuration);
	}
}

void SendFilesBox::AlbumPreview::mouseMoveEvent(QMouseEvent *e) {
	if (_sendWay != SendFilesWay::Album) {
		applyCursor(style::cur_default);
		return;
	}
	if (_draggedThumb) {
		const auto position = e->pos();
		_draggedThumb->moveInAlbum(position - _draggedStartPosition);
		updateSuggestedDrag(_draggedThumb->center());
		update();
	} else {
		const auto cursor = findThumb(e->pos())
			? style::cur_sizeall
			: style::cur_default;
		applyCursor(cursor);
	}
}

void SendFilesBox::AlbumPreview::applyCursor(style::cursor cursor) {
	if (_cursor != cursor) {
		_cursor = cursor;
		setCursor(_cursor);
	}
}

void SendFilesBox::AlbumPreview::updateSuggestedDrag(QPoint position) {
	auto closest = findClosestThumb(position);
	auto closestIndex = orderIndex(closest);

	const auto draggedIndex = orderIndex(_draggedThumb);
	const auto closestIsBeforePoint = closest->isPointAfter(position);
	if (closestIndex < draggedIndex && closestIsBeforePoint) {
		closest = _thumbs[_order[++closestIndex]].get();
	} else if (closestIndex > draggedIndex && !closestIsBeforePoint) {
		closest = _thumbs[_order[--closestIndex]].get();
	}

	if (_suggestedThumb == closest) {
		return;
	}

	const auto last = int(_order.size()) - 1;
	if (_suggestedThumb) {
		const auto suggestedIndex = orderIndex(_suggestedThumb);
		if (suggestedIndex < draggedIndex && suggestedIndex > 0) {
			const auto previous = _thumbs[_order[suggestedIndex - 1]].get();
			previous->suggestMove(0., [=] { update(); });
		} else if (suggestedIndex > draggedIndex && suggestedIndex < last) {
			const auto next = _thumbs[_order[suggestedIndex + 1]].get();
			next->suggestMove(0., [=] { update(); });
		}
		_suggestedThumb->suggestMove(0., [=] { update(); });
	}
	_suggestedThumb = closest;
	const auto suggestedIndex = closestIndex;
	if (_suggestedThumb != _draggedThumb) {
		const auto delta = (suggestedIndex < draggedIndex) ? 1. : -1.;
		if (delta > 0. && suggestedIndex > 0) {
			const auto previous = _thumbs[_order[suggestedIndex - 1]].get();
			previous->suggestMove(-delta, [=] { update(); });
		} else if (delta < 0. && suggestedIndex < last) {
			const auto next = _thumbs[_order[suggestedIndex + 1]].get();
			next->suggestMove(-delta, [=] { update(); });
		}
		_suggestedThumb->suggestMove(delta, [=] { update(); });
	}
}

void SendFilesBox::AlbumPreview::mouseReleaseEvent(QMouseEvent *e) {
	if (_draggedThumb) {
		finishDrag();
		_shrinkAnimation.start([=] { update(); }, 1., 0., kShrinkDuration);
		_draggedThumb = nullptr;
		_suggestedThumb = nullptr;
		update();
	}
}

SendFilesBox::SendFilesBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	Storage::PreparedList &&list,
	const TextWithTags &caption,
	CompressConfirm compressed,
	SendLimit limit,
	Api::SendType sendType,
	SendMenuType sendMenuType)
: _controller(controller)
, _sendType(sendType)
, _list(std::move(list))
, _compressConfirmInitial(compressed)
, _compressConfirm(compressed)
, _sendLimit(limit)
, _sendMenuType(sendMenuType)
, _caption(
	this,
	st::confirmCaptionArea,
	Ui::InputField::Mode::MultiLine,
	nullptr,
	caption) {
}

void SendFilesBox::initPreview(rpl::producer<int> desiredPreviewHeight) {
	setupControls();

	updateBoxSize();

	using namespace rpl::mappers;
	rpl::combine(
		std::move(desiredPreviewHeight),
		_footerHeight.value(),
		_titleHeight + _1 + _2
	) | rpl::start_with_next([=](int height) {
		setDimensions(
			st::boxWideWidth,
			std::min(st::sendMediaPreviewHeightMax, height));
	}, lifetime());

	if (_preview) {
		_preview->show();
	}
}

void SendFilesBox::prepareSingleFilePreview() {
	Expects(_list.files.size() == 1);

	const auto &file = _list.files[0];
	const auto media = SingleMediaPreview::Create(this, _controller, file);
	if (media) {
		if (!media->canSendAsPhoto()) {
			_compressConfirm = CompressConfirm::None;
		}
		_preview = media;
		initPreview(media->desiredHeightValue());
	} else {
		const auto preview = Ui::CreateChild<SingleFilePreview>(this, file);
		_compressConfirm = CompressConfirm::None;
		_preview = preview;
		initPreview(preview->desiredHeightValue());
	}
}

void SendFilesBox::prepareAlbumPreview() {
	Expects(_sendWay != nullptr);

	const auto wrap = Ui::CreateChild<Ui::ScrollArea>(
		this,
		st::boxLayerScroll);
	_albumPreview = wrap->setOwnedWidget(object_ptr<AlbumPreview>(
		this,
		_list,
		_sendWay->value()));
	_preview = wrap;
	_albumPreview->show();
	setupShadows(wrap, _albumPreview);

	initPreview(_albumPreview->desiredHeightValue());
}

void SendFilesBox::setupShadows(
		not_null<Ui::ScrollArea*> wrap,
		not_null<AlbumPreview*> content) {
	using namespace rpl::mappers;

	const auto topShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	const auto bottomShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	wrap->geometryValue(
	) | rpl::start_with_next_done([=](const QRect &geometry) {
		topShadow->resizeToWidth(geometry.width());
		topShadow->move(
			geometry.x(),
			geometry.y());
		bottomShadow->resizeToWidth(geometry.width());
		bottomShadow->move(
			geometry.x(),
			geometry.y() + geometry.height() - st::lineWidth);
	}, [t = make_weak(topShadow), b = make_weak(bottomShadow)] {
		Ui::DestroyChild(t.data());
		Ui::DestroyChild(b.data());
	}, topShadow->lifetime());

	topShadow->toggleOn(wrap->scrollTopValue() | rpl::map(_1 > 0));
	bottomShadow->toggleOn(rpl::combine(
		wrap->scrollTopValue(),
		wrap->heightValue(),
		content->heightValue(),
		_1 + _2 < _3));
}

void SendFilesBox::prepare() {
	_send = addButton(tr::lng_send_button(), [=] { send({}); });
	if (_sendType == Api::SendType::Normal) {
		SetupSendMenu(
			_send,
			[=] { return _sendMenuType; },
			[=] { sendSilent(); },
			[=] { sendScheduled(); });
	}
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	initSendWay();
	setupCaption();
	preparePreview();
	boxClosing() | rpl::start_with_next([=] {
		if (!_confirmed && _cancelledCallback) {
			_cancelledCallback();
		}
	}, lifetime());
}

void SendFilesBox::initSendWay() {
	refreshAlbumMediaCount();
	const auto value = [&] {
		if (_sendLimit == SendLimit::One
			&& _list.albumIsPossible
			&& _list.files.size() > 1) {
			return SendFilesWay::Album;
		}
		if (_compressConfirm == CompressConfirm::None) {
			return SendFilesWay::Files;
		} else if (_compressConfirm == CompressConfirm::No) {
			return SendFilesWay::Files;
		} else if (_compressConfirm == CompressConfirm::Yes) {
			return _list.albumIsPossible
				? SendFilesWay::Album
				: SendFilesWay::Photos;
		}
		const auto way = _controller->session().settings().sendFilesWay();
		if (way == SendFilesWay::Files) {
			return way;
		} else if (way == SendFilesWay::Album) {
			return _list.albumIsPossible
				? SendFilesWay::Album
				: SendFilesWay::Photos;
		}
		return (_list.albumIsPossible && !_albumPhotosCount)
			? SendFilesWay::Album
			: SendFilesWay::Photos;
	}();
	_sendWay = std::make_shared<Ui::RadioenumGroup<SendFilesWay>>(value);
	_sendWay->setChangedCallback([=](SendFilesWay value) {
		updateCaptionPlaceholder();
		applyAlbumOrder();
		if (_albumPreview) {
			_albumPreview->setSendWay(value);
		}
		setInnerFocus();
	});
}

void SendFilesBox::updateCaptionPlaceholder() {
	if (!_caption) {
		return;
	}
	const auto sendWay = _sendWay->value();
	const auto isAlbum = (sendWay == SendFilesWay::Album);
	const auto compressImages = (sendWay != SendFilesWay::Files);
	if (!_list.canAddCaption(isAlbum, compressImages)
		&& _sendLimit == SendLimit::One) {
		_caption->hide();
		if (_emojiToggle) {
			_emojiToggle->hide();
		}
	} else {
		_caption->setPlaceholder(FieldPlaceholder(_list, sendWay));
		_caption->show();
		if (_emojiToggle) {
			_emojiToggle->show();
		}
	}
}

void SendFilesBox::refreshAlbumMediaCount() {
	_albumVideosCount = _list.albumIsPossible
		? ranges::count(
			_list.files,
			Storage::PreparedFile::AlbumType::Video,
			[](const Storage::PreparedFile &file) { return file.type; })
		: 0;
	_albumPhotosCount = _list.albumIsPossible
		? (_list.files.size() - _albumVideosCount)
		: 0;
}

void SendFilesBox::preparePreview() {
	if (_list.files.size() == 1) {
		prepareSingleFilePreview();
	} else {
		if (_list.albumIsPossible) {
			prepareAlbumPreview();
		} else {
			auto desiredPreviewHeight = rpl::single(0);
			initPreview(std::move(desiredPreviewHeight));
		}
	}
}

void SendFilesBox::setupControls() {
	setupTitleText();
	setupSendWayControls();
}

void SendFilesBox::setupSendWayControls() {
	_sendAlbum.destroy();
	_sendPhotos.destroy();
	_sendFiles.destroy();
	if (_compressConfirm == CompressConfirm::None
		|| _sendLimit == SendLimit::One) {
		return;
	}
	const auto addRadio = [&](
			object_ptr<Ui::Radioenum<SendFilesWay>> &button,
			SendFilesWay value,
			const QString &text) {
		const auto &style = st::defaultBoxCheckbox;
		button.create(this, _sendWay, value, text, style);
		button->show();
	};
	if (_list.albumIsPossible) {
		addRadio(_sendAlbum, SendFilesWay::Album, tr::lng_send_album(tr::now));
	}
	if (!_list.albumIsPossible || _albumPhotosCount > 0) {
		addRadio(_sendPhotos, SendFilesWay::Photos, (_list.files.size() == 1)
			? tr::lng_send_photo(tr::now)
			: (_albumVideosCount > 0)
			? tr::lng_send_separate_photos_videos(tr::now)
			: (_list.albumIsPossible
				? tr::lng_send_separate_photos(tr::now)
				: tr::lng_send_photos(tr::now, lt_count, _list.files.size())));
	}
	addRadio(_sendFiles, SendFilesWay::Files, (_list.files.size() == 1)
		? tr::lng_send_file(tr::now)
		: tr::lng_send_files(tr::now, lt_count, _list.files.size()));
}

void SendFilesBox::applyAlbumOrder() {
	if (!_albumPreview) {
		return;
	}

	const auto order = _albumPreview->takeOrder();
	const auto isDefault = [&] {
		for (auto i = 0, count = int(order.size()); i != count; ++i) {
			if (order[i] != i) {
				return false;
			}
		}
		return true;
	}();
	if (isDefault) {
		return;
	}

	_list = Storage::PreparedList::Reordered(std::move(_list), order);
}

void SendFilesBox::setupCaption() {
	_caption->setMaxLength(Global::CaptionLengthMax());
	_caption->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
	connect(_caption, &Ui::InputField::resized, [=] {
		captionResized();
	});
	connect(_caption, &Ui::InputField::submitted, [=](
			Qt::KeyboardModifiers modifiers) {
		const auto ctrlShiftEnter = modifiers.testFlag(Qt::ShiftModifier)
			&& (modifiers.testFlag(Qt::ControlModifier)
				|| modifiers.testFlag(Qt::MetaModifier));
		send({}, ctrlShiftEnter);
	});
	connect(_caption, &Ui::InputField::cancelled, [=] { closeBox(); });
	_caption->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return canAddFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return addFiles(data);
		}
		Unexpected("action in MimeData hook.");
	});
	_caption->setInstantReplaces(Ui::InstantReplaces::Default());
	_caption->setInstantReplacesEnabled(
		_controller->session().settings().replaceEmojiValue());
	_caption->setMarkdownReplacesEnabled(rpl::single(true));
	_caption->setEditLinkCallback(
		DefaultEditLinkCallback(&_controller->session(), _caption));
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_caption,
		&_controller->session());

	updateCaptionPlaceholder();
	setupEmojiPanel();
}

void SendFilesBox::setupEmojiPanel() {
	Expects(_caption != nullptr);

	const auto container = getDelegate()->outerContainer();
	_emojiPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		container,
		_controller,
		object_ptr<ChatHelpers::TabbedSelector>(
			nullptr,
			_controller,
			ChatHelpers::TabbedSelector::Mode::EmojiOnly));
	_emojiPanel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_emojiPanel->hide();
	_emojiPanel->selector()->emojiChosen(
	) | rpl::start_with_next([=](EmojiPtr emoji) {
		Ui::InsertEmojiAtCursor(_caption->textCursor(), emoji);
	}, lifetime());

	_emojiFilter.reset(Core::InstallEventFilter(
		container,
		[=](not_null<QEvent*> event) { return emojiFilter(event); }));

	_emojiToggle.create(this, st::boxAttachEmoji);
	_emojiToggle->setVisible(!_caption->isHidden());
	_emojiToggle->installEventFilter(_emojiPanel);
	_emojiToggle->addClickHandler([=] {
		_emojiPanel->toggleAnimated();
	});
}

bool SendFilesBox::emojiFilter(not_null<QEvent*> event) {
	const auto type = event->type();
	if (type == QEvent::Move || type == QEvent::Resize) {
		// updateEmojiPanelGeometry uses not only container geometry, but
		// also container children geometries that will be updated later.
		crl::on_main(this, [=] { updateEmojiPanelGeometry(); });
	}
	return false;
}

void SendFilesBox::updateEmojiPanelGeometry() {
	const auto parent = _emojiPanel->parentWidget();
	const auto global = _emojiToggle->mapToGlobal({ 0, 0 });
	const auto local = parent->mapFromGlobal(global);
	_emojiPanel->moveBottomRight(
		local.y(),
		local.x() + _emojiToggle->width() * 3);
}

void SendFilesBox::captionResized() {
	updateBoxSize();
	updateControlsGeometry();
	updateEmojiPanelGeometry();
	update();
}

bool SendFilesBox::canAddUrls(const QList<QUrl> &urls) const {
	return !urls.isEmpty() && ranges::find_if(
		urls,
		[](const QUrl &url) { return !url.isLocalFile(); }
	) == urls.end();
}

bool SendFilesBox::canAddFiles(not_null<const QMimeData*> data) const {
	const auto urls = data->hasUrls() ? data->urls() : QList<QUrl>();
	auto filesCount = canAddUrls(urls) ? urls.size() : 0;
	if (!filesCount && data->hasImage()) {
		++filesCount;
	}

	if (_list.files.size() + filesCount > Storage::MaxAlbumItems()) {
		return false;
	} else if (_list.files.size() > 1 && !_albumPreview) {
		return false;
	} else if (_list.files.front().type
		== Storage::PreparedFile::AlbumType::None) {
		return false;
	}
	return true;
}

bool SendFilesBox::addFiles(not_null<const QMimeData*> data) {
	auto list = [&] {
		const auto urls = data->hasUrls() ? data->urls() : QList<QUrl>();
		auto result = canAddUrls(urls)
			? Storage::PrepareMediaList(urls, st::sendMediaPreviewSize)
			: Storage::PreparedList(
				Storage::PreparedList::Error::EmptyFile,
				QString());
		if (result.error == Storage::PreparedList::Error::None) {
			return result;
		} else if (data->hasImage()) {
			auto image = qvariant_cast<QImage>(data->imageData());
			if (!image.isNull()) {
				return Storage::PrepareMediaFromImage(
					std::move(image),
					QByteArray(),
					st::sendMediaPreviewSize);
			}
		}
		return result;
	}();
	if (_list.files.size() + list.files.size() > Storage::MaxAlbumItems()) {
		return false;
	} else if (list.error != Storage::PreparedList::Error::None) {
		return false;
	} else if (list.files.size() != 1 && !list.albumIsPossible) {
		return false;
	} else if (list.files.front().type
		== Storage::PreparedFile::AlbumType::None) {
		return false;
	} else if (_list.files.size() > 1 && !_albumPreview) {
		return false;
	} else if (_list.files.front().type
		== Storage::PreparedFile::AlbumType::None) {
		return false;
	}
	applyAlbumOrder();
	delete base::take(_preview);
	_albumPreview = nullptr;

	if (_list.files.size() == 1
		&& _sendWay->value() == SendFilesWay::Photos) {
		_sendWay->setValue(SendFilesWay::Album);
	}
	_list.mergeToEnd(std::move(list));

	_compressConfirm = _compressConfirmInitial;
	refreshAlbumMediaCount();
	preparePreview();
	captionResized();
	return true;
}

void SendFilesBox::setupTitleText() {
	if (_list.files.size() > 1) {
		const auto onlyImages = (_compressConfirm != CompressConfirm::None)
			&& (_albumVideosCount == 0);
		_titleText = onlyImages
			? tr::lng_send_images_selected(tr::now, lt_count, _list.files.size())
			: tr::lng_send_files_selected(tr::now, lt_count, _list.files.size());
		_titleHeight = st::boxTitleHeight;
	} else {
		_titleText = QString();
		_titleHeight = 0;
	}
}

void SendFilesBox::updateBoxSize() {
	auto footerHeight = 0;
	if (_caption) {
		footerHeight += st::boxPhotoCaptionSkip + _caption->height();
	}
	const auto pointers = {
		_sendAlbum.data(),
		_sendPhotos.data(),
		_sendFiles.data()
	};
	for (auto pointer : pointers) {
		if (pointer) {
			footerHeight += st::boxPhotoCompressedSkip
				+ pointer->heightNoMargins();
		}
	}
	_footerHeight = footerHeight;
}

void SendFilesBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		const auto modifiers = e->modifiers();
		const auto ctrl = modifiers.testFlag(Qt::ControlModifier)
			|| modifiers.testFlag(Qt::MetaModifier);
		const auto shift = modifiers.testFlag(Qt::ShiftModifier);
		send({}, ctrl && shift);
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void SendFilesBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	if (!_titleText.isEmpty()) {
		Painter p(this);

		p.setFont(st::boxPhotoTitleFont);
		p.setPen(st::boxTitleFg);
		p.drawTextLeft(
			st::boxPhotoTitlePosition.x(),
			st::boxPhotoTitlePosition.y(),
			width(),
			_titleText);
	}
}

void SendFilesBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	updateControlsGeometry();
}

void SendFilesBox::updateControlsGeometry() {
	auto bottom = height();
	if (_caption) {
		_caption->resize(st::sendMediaPreviewSize, _caption->height());
		_caption->moveToLeft(
			st::boxPhotoPadding.left(),
			bottom - _caption->height());
		bottom -= st::boxPhotoCaptionSkip + _caption->height();

		if (_emojiToggle) {
			_emojiToggle->moveToLeft(
				(st::boxPhotoPadding.left()
					+ st::sendMediaPreviewSize
					- _emojiToggle->width()),
				_caption->y() + st::boxAttachEmojiTop);
		}
	}
	const auto pointers = {
		_sendAlbum.data(),
		_sendPhotos.data(),
		_sendFiles.data()
	};
	for (const auto pointer : ranges::view::reverse(pointers)) {
		if (pointer) {
			pointer->moveToLeft(
				st::boxPhotoPadding.left(),
				bottom - pointer->heightNoMargins());
			bottom -= st::boxPhotoCompressedSkip + pointer->heightNoMargins();
		}
	}
	if (_preview) {
		_preview->resize(width(), bottom - _titleHeight);
		_preview->move(0, _titleHeight);
	}
}

void SendFilesBox::setInnerFocus() {
	if (!_caption || _caption->isHidden()) {
		setFocus();
	} else {
		_caption->setFocusFast();
	}
}

void SendFilesBox::send(
		Api::SendOptions options,
		bool ctrlShiftEnter) {
	if (_sendType == Api::SendType::Scheduled && !options.scheduled) {
		return sendScheduled();
	}

	using Way = SendFilesWay;
	const auto way = _sendWay ? _sendWay->value() : Way::Files;

	if (_compressConfirm == CompressConfirm::Auto) {
		const auto oldWay = _controller->session().settings().sendFilesWay();
		if (way != oldWay) {
			// Check if the user _could_ use the old value, but didn't.
			if ((oldWay == Way::Album && _sendAlbum)
				|| (oldWay == Way::Photos && _sendPhotos)
				|| (oldWay == Way::Files && _sendFiles)
				|| (way == Way::Files && (_sendAlbum || _sendPhotos))) {
				// And in that case save it to settings.
				_controller->session().settings().setSendFilesWay(way);
				_controller->session().saveSettingsDelayed();
			}
		}
	}

	applyAlbumOrder();
	_confirmed = true;
	if (_confirmedCallback) {
		auto caption = (_caption && !_caption->isHidden())
			? _caption->getTextWithAppliedMarkdown()
			: TextWithTags();
		_confirmedCallback(
			std::move(_list),
			way,
			std::move(caption),
			options,
			ctrlShiftEnter);
	}
	closeBox();
}

void SendFilesBox::sendSilent() {
	auto options = Api::SendOptions();
	options.silent = true;
	send(options);
}

void SendFilesBox::sendScheduled() {
	const auto callback = [=](Api::SendOptions options) { send(options); };
	Ui::show(
		HistoryView::PrepareScheduleBox(this, _sendMenuType, callback),
		LayerOption::KeepOther);
}

SendFilesBox::~SendFilesBox() = default;
