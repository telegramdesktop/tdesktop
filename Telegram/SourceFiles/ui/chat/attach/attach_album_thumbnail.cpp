/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_album_thumbnail.h"

#include "core/mime_type.h" // Core::IsMimeSticker.
#include "ui/chat/attach/attach_prepare.h"
#include "ui/image/image_prepare.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/ui_utility.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "base/call_delayed.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_boxes.h"

#include <QtCore/QFileInfo>

namespace Ui {

AlbumThumbnail::AlbumThumbnail(
	const style::ComposeControls &st,
	const PreparedFile &file,
	const GroupMediaLayout &layout,
	QWidget *parent,
	Fn<void()> repaint,
	Fn<void()> editCallback,
	Fn<void()> deleteCallback)
: _st(st)
, _layout(layout)
, _fullPreview(file.videoCover ? file.videoCover->preview : file.preview)
, _shrinkSize(int(std::ceil(st::roundRadiusLarge / 1.4)))
, _isPhoto(file.type == PreparedFile::Type::Photo)
, _isVideo(file.type == PreparedFile::Type::Video)
, _isCompressedSticker(Core::IsMimeSticker(file.information->filemime))
, _repaint(std::move(repaint)) {
	Expects(!_fullPreview.isNull());

	moveToLayout(layout);

	using Option = Images::Option;
	const auto previewWidth = _fullPreview.width();
	const auto previewHeight = _fullPreview.height();
	const auto imageWidth = std::max(
		previewWidth / style::DevicePixelRatio(),
		st::minPhotoSize);
	const auto imageHeight = std::max(
		previewHeight / style::DevicePixelRatio(),
		st::minPhotoSize);
	_photo = PixmapFromImage(Images::Prepare(
		_fullPreview,
		QSize(previewWidth, previewHeight),
		{
			.options = Option::RoundLarge,
			.outer = { imageWidth, imageHeight },
		}));

	const auto &layoutSt = st::attachPreviewThumbLayout;
	const auto idealSize = layoutSt.thumbSize * style::DevicePixelRatio();
	const auto fileThumbSize = (previewWidth > previewHeight)
		? QSize(previewWidth * idealSize / previewHeight, idealSize)
		: QSize(idealSize, previewHeight * idealSize / previewWidth);
	_fileThumb = PixmapFromImage(Images::Prepare(
		_fullPreview,
		fileThumbSize,
		{
			.options = Option::RoundSmall,
			.outer = { layoutSt.thumbSize, layoutSt.thumbSize },
		}));

	const auto availableFileWidth = st::sendMediaPreviewSize
		- layoutSt.thumbSize
		- layoutSt.thumbSkip
		// Right buttons.
		- st::sendBoxAlbumGroupButtonFile.width * 2
		- st::sendBoxAlbumGroupEditInternalSkip * 2
		- st::sendBoxAlbumGroupSkipRight;
	const auto filepath = file.path;
	if (filepath.isEmpty()) {
		_name = "image.png";
		_status = FormatImageSizeText(file.originalDimensions);
	} else {
		auto fileinfo = QFileInfo(filepath);
		_name = fileinfo.fileName();
		_status = FormatSizeText(fileinfo.size());
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

	_editMedia.create(parent, _st.files.buttonFile);
	_deleteMedia.create(parent, _st.files.buttonFile);

	const auto duration = st::historyAttach.ripple.hideDuration;
	_editMedia->setClickedCallback([=] {
		base::call_delayed(duration, parent, editCallback);
	});
	_deleteMedia->setClickedCallback(deleteCallback);

	_editMedia->setIconOverride(&_st.files.buttonFileEdit);
	_deleteMedia->setIconOverride(&_st.files.buttonFileDelete);

	setSpoiler(file.spoiler);
	setButtonVisible(false);
}

void AlbumThumbnail::setSpoiler(bool spoiler) {
	Expects(_repaint != nullptr);

	_spoiler = spoiler
		? std::make_unique<SpoilerAnimation>(_repaint)
		: nullptr;
	_repaint();
}

bool AlbumThumbnail::hasSpoiler() const {
	return _spoiler != nullptr;
}

void AlbumThumbnail::setButtonVisible(bool value) {
	_editMedia->setVisible(value);
	_deleteMedia->setVisible(value);
}

void AlbumThumbnail::moveButtons(int thumbTop) {
	const auto top = thumbTop + st::sendBoxFileGroupSkipTop;

	auto right = st::sendBoxFileGroupSkipRight + st::boxPhotoPadding.right();
	_deleteMedia->moveToRight(right, top);
	right += st::sendBoxFileGroupEditInternalSkip + _deleteMedia->width();
	_editMedia->moveToRight(right, top);
}

void AlbumThumbnail::resetLayoutAnimation() {
	_animateFromGeometry = std::nullopt;
}

void AlbumThumbnail::animateLayoutToInitial() {
	_animateFromGeometry = countRealGeometry();
	_suggestedMove = 0.;
	_albumPosition = QPoint(0, 0);
}

void AlbumThumbnail::moveToLayout(const GroupMediaLayout &layout) {
	using namespace Images;

	animateLayoutToInitial();
	_layout = layout;

	const auto width = _layout.geometry.width();
	const auto height = _layout.geometry.height();
	_albumCorners = GetCornersFromSides(_layout.sides);
	const auto pixSize = GetImageScaleSizeForGeometry(
		{ _fullPreview.width(), _fullPreview.height() },
		{ width, height });
	const auto pixWidth = pixSize.width() * style::DevicePixelRatio();
	const auto pixHeight = pixSize.height() * style::DevicePixelRatio();

	_albumImage = PixmapFromImage(Prepare(
		_fullPreview,
		QSize(pixWidth, pixHeight),
		{
			.options = RoundOptions(ImageRoundRadius::Large, _albumCorners),
			.outer = { width, height },
		}));
	_albumImageBlurred = QPixmap();
}

int AlbumThumbnail::photoHeight() const {
	return _photo.height() / style::DevicePixelRatio();
}

int AlbumThumbnail::fileHeight() const {
	return _isCompressedSticker
		? photoHeight()
		: st::attachPreviewThumbLayout.thumbSize;
}

bool AlbumThumbnail::isCompressedSticker() const {
	return _isCompressedSticker;
}

void AlbumThumbnail::paintInAlbum(
		QPainter &p,
		int left,
		int top,
		float64 shrinkProgress,
		float64 moveProgress) {
	const auto shrink = anim::interpolate(0, _shrinkSize, shrinkProgress);
	_lastShrinkValue = shrink;
	const auto geometry = countCurrentGeometry(
		moveProgress
	).translated(left, top);
	auto paintedTo = geometry;
	const auto revealed = _spoiler ? shrinkProgress : 1.;
	if (revealed > 0.) {
		if (shrink > 0 || moveProgress < 1.) {
			const auto size = geometry.size();
			paintedTo = geometry.marginsRemoved(
				{ shrink, shrink, shrink, shrink }
			);
			if (shrinkProgress < 1 && _albumCorners != RectPart::None) {
				prepareCache(size, shrink);
				p.drawImage(geometry.topLeft(), _albumCache);
			} else {
				drawSimpleFrame(p, paintedTo, size);
			}
		} else {
			p.drawPixmap(geometry.topLeft(), _albumImage);
		}
		if (_isVideo) {
			paintPlayVideo(p, geometry);
		}
	}
	if (revealed < 1.) {
		auto corners = Images::CornersMaskRef(
			Images::CornersMask(ImageRoundRadius::Large));
		if (!(_albumCorners & RectPart::TopLeft)) {
			corners.p[0] = nullptr;
		}
		if (!(_albumCorners & RectPart::TopRight)) {
			corners.p[1] = nullptr;
		}
		if (!(_albumCorners & RectPart::BottomLeft)) {
			corners.p[2] = nullptr;
		}
		if (!(_albumCorners & RectPart::BottomRight)) {
			corners.p[3] = nullptr;
		}
		p.setOpacity(1. - revealed);
		if (_albumImageBlurred.isNull()) {
			_albumImageBlurred = BlurredPreviewFromPixmap(
				_albumImage,
				_albumCorners);
		}
		p.drawPixmap(paintedTo, _albumImageBlurred);
		const auto paused = On(PowerSaving::kChatSpoiler);
		FillSpoilerRect(
			p,
			paintedTo,
			corners,
			DefaultImageSpoiler().frame(_spoiler->index(crl::now(), paused)),
			_cornerCache);
		p.setOpacity(1.);
	}

	_lastRectOfButtons = paintButtons(
		p,
		geometry,
		shrinkProgress);
	_lastRectOfModify = geometry;
}

void AlbumThumbnail::paintPlayVideo(QPainter &p, QRect geometry) {
	const auto innerSize = st::msgFileLayout.thumbSize;
	const auto inner = QRect(
		geometry.x() + (geometry.width() - innerSize) / 2,
		geometry.y() + (geometry.height() - innerSize) / 2,
		innerSize,
		innerSize);
	{
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgDateImgBg);
		p.drawEllipse(inner);
	}
	st::historyFileThumbPlay.paintInCenter(p, inner);
}

void AlbumThumbnail::prepareCache(QSize size, int shrink) {
	const auto width = std::max(
		_layout.geometry.width(),
		_animateFromGeometry ? _animateFromGeometry->width() : 0);
	const auto height = std::max(
		_layout.geometry.height(),
		_animateFromGeometry ? _animateFromGeometry->height() : 0);
	const auto cacheSize = QSize(width, height) * style::DevicePixelRatio();

	if (_albumCache.width() < cacheSize.width()
		|| _albumCache.height() < cacheSize.height()) {
		_albumCache = QImage(cacheSize, QImage::Format_ARGB32_Premultiplied);
		_albumCache.setDevicePixelRatio(style::DevicePixelRatio());
	}
	_albumCache.fill(Qt::transparent);
	{
		Painter p(&_albumCache);
		const auto to = QRect(QPoint(), size).marginsRemoved(
			{ shrink, shrink, shrink, shrink }
		);
		drawSimpleFrame(p, to, size);
	}
	_albumCache = Images::Round(
		std::move(_albumCache),
		ImageRoundRadius::Large,
		_albumCorners,
		QRect(QPoint(), size * style::DevicePixelRatio()));
}

void AlbumThumbnail::drawSimpleFrame(QPainter &p, QRect to, QSize size) const {
	const auto fullWidth = _fullPreview.width();
	const auto fullHeight = _fullPreview.height();
	const auto previewSize = GetImageScaleSizeForGeometry(
		{ fullWidth, fullHeight },
		{ size.width(), size.height() });
	const auto previewWidth = previewSize.width() * style::DevicePixelRatio();
	const auto previewHeight = previewSize.height() * style::DevicePixelRatio();
	const auto width = size.width() * style::DevicePixelRatio();
	const auto height = size.height() * style::DevicePixelRatio();
	const auto scaleWidth = to.width() / float64(width);
	const auto scaleHeight = to.height() / float64(height);
	const auto Round = [](float64 value) {
		return int(base::SafeRound(value));
	};
	const auto &[from, fillBlack] = [&] {
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

void AlbumThumbnail::paintPhoto(Painter &p, int left, int top, int outerWidth) {
	const auto size = _photo.size() / style::DevicePixelRatio();
	if (_spoiler && _photoBlurred.isNull()) {
		_photoBlurred = BlurredPreviewFromPixmap(
			_photo,
			RectPart::AllCorners);
	}
	const auto &pixmap = _spoiler ? _photoBlurred : _photo;
	const auto rect = QRect(
		left + (st::sendMediaPreviewSize - size.width()) / 2,
		top,
		pixmap.width() / pixmap.devicePixelRatio(),
		pixmap.height() / pixmap.devicePixelRatio());
	p.drawPixmapLeft(
		left + (st::sendMediaPreviewSize - size.width()) / 2,
		top,
		outerWidth,
		pixmap);
	if (_spoiler) {
		const auto paused = On(PowerSaving::kChatSpoiler);
		FillSpoilerRect(
			p,
			rect,
			Images::CornersMaskRef(
				Images::CornersMask(ImageRoundRadius::Large)),
			DefaultImageSpoiler().frame(_spoiler->index(crl::now(), paused)),
			_cornerCache);
	} else if (_isVideo) {
		paintPlayVideo(p, rect);
	}

	const auto topLeft = QPoint{ left, top };

	_lastRectOfButtons = paintButtons(
		p,
		QRect(left, top, st::sendMediaPreviewSize, size.height()),
		0);

	_lastRectOfModify = QRect(topLeft, size);
}

void AlbumThumbnail::paintFile(
		Painter &p,
		int left,
		int top,
		int outerWidth) {

	if (isCompressedSticker()) {
		auto spoiler = base::take(_spoiler);
		paintPhoto(p, left, top, outerWidth);
		_spoiler = base::take(spoiler);
		return;
	}
	const auto &st = st::attachPreviewThumbLayout;
	const auto textLeft = left + st.thumbSize + st.thumbSkip;

	p.drawPixmap(left, top, _fileThumb);
	p.setFont(st::semiboldFont);
	p.setPen(_st.files.nameFg);
	p.drawTextLeft(
		textLeft,
		top + st.nameTop,
		outerWidth,
		_name,
		_nameWidth);
	p.setFont(st::normalFont);
	p.setPen(_st.files.statusFg);
	p.drawTextLeft(
		textLeft,
		top + st.statusTop,
		outerWidth,
		_status,
		_statusWidth);

	_lastRectOfModify = QRect(
		QPoint(left, top),
		_fileThumb.size() / style::DevicePixelRatio());
}

QRect AlbumThumbnail::geometry() const {
	return _layout.geometry;
}

bool AlbumThumbnail::containsPoint(QPoint position) const {
	return _layout.geometry.contains(position);
}

bool AlbumThumbnail::buttonsContainPoint(QPoint position) const {
	return ((_isPhoto && !_isCompressedSticker)
		? _lastRectOfModify
		: _lastRectOfButtons).contains(position);
}

AttachButtonType AlbumThumbnail::buttonTypeFromPoint(QPoint position) const {
	if (!buttonsContainPoint(position)) {
		return AttachButtonType::None;
	}
	return (!_lastRectOfButtons.contains(position) && !_isCompressedSticker)
		? AttachButtonType::Modify
		: (_buttons.vertical()
			? (position.y() < _lastRectOfButtons.center().y())
			: (position.x() < _lastRectOfButtons.center().x()))
		? AttachButtonType::Edit
		: AttachButtonType::Delete;
}

int AlbumThumbnail::distanceTo(QPoint position) const {
	const auto delta = (_layout.geometry.center() - position);
	return QPoint::dotProduct(delta, delta);
}

bool AlbumThumbnail::isPointAfter(QPoint position) const {
	return position.x() > _layout.geometry.center().x();
}

void AlbumThumbnail::moveInAlbum(QPoint to) {
	_albumPosition = to;
}

QPoint AlbumThumbnail::center() const {
	auto realGeometry = _layout.geometry;
	realGeometry.moveTopLeft(realGeometry.topLeft() + _albumPosition);
	return realGeometry.center();
}

void AlbumThumbnail::suggestMove(float64 delta, Fn<void()> callback) {
	if (_suggestedMove != delta) {
		_suggestedMoveAnimation.start(
			std::move(callback),
			_suggestedMove,
			delta,
			kShrinkDuration);
		_suggestedMove = delta;
	}
}

QRect AlbumThumbnail::countRealGeometry() const {
	const auto addLeft = int(base::SafeRound(
		_suggestedMoveAnimation.value(_suggestedMove) * _lastShrinkValue));
	const auto current = _layout.geometry;
	const auto realTopLeft = current.topLeft()
		+ _albumPosition
		+ QPoint(addLeft, 0);
	return { realTopLeft, current.size() };
}

QRect AlbumThumbnail::countCurrentGeometry(float64 progress) const {
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

void AlbumThumbnail::finishAnimations() {
	_suggestedMoveAnimation.stop();
}

QRect AlbumThumbnail::paintButtons(
		QPainter &p,
		QRect geometry,
		float64 shrinkProgress) {
	const auto &skipRight = st::sendBoxAlbumGroupSkipRight;
	const auto &skipTop = st::sendBoxAlbumGroupSkipTop;
	const auto outerWidth = geometry.width();
	const auto outerHeight = geometry.height();
	if (st::sendBoxAlbumGroupSize.width() <= outerWidth) {
		_buttons.setVertical(false);
	} else if (st::sendBoxAlbumGroupSize.height() <= outerHeight) {
		_buttons.setVertical(true);
	} else {
		// If the size is tiny, skip the buttons.
		return QRect();
	}
	const auto groupWidth = _buttons.width();
	const auto groupHeight = _buttons.height();

	// If the width is too small,
	// it would be better to display the buttons in the center.
	const auto groupX = geometry.x() + ((groupWidth + skipRight * 2 > outerWidth)
		? (outerWidth - groupWidth) / 2
		: outerWidth - skipRight - groupWidth);
	const auto groupY = geometry.y() + ((groupHeight + skipTop * 2 > outerHeight)
		? (outerHeight - groupHeight) / 2
		: skipTop);

	const auto opacity = p.opacity();
	p.setOpacity(1.0 - shrinkProgress);
	_buttons.paint(p, groupX, groupY);
	p.setOpacity(opacity);

	return QRect(groupX, groupY, groupWidth, _buttons.height());
}

} // namespace Ui
