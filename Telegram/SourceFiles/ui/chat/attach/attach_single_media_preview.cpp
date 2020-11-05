/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_single_media_preview.h"

#include "ui/chat/attach/attach_prepare.h"
#include "ui/widgets/buttons.h"
#include "core/mime_type.h"
#include "lottie/lottie_single_player.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

namespace Ui {
namespace {

constexpr auto kMinPreviewWidth = 20;

} // namespace

SingleMediaPreview *SingleMediaPreview::Create(
		QWidget *parent,
		Fn<bool()> gifPaused,
		const PreparedFile &file) {
	auto preview = QImage();
	bool animated = false;
	bool animationPreview = false;
	if (const auto image = std::get_if<PreparedFileInformation::Image>(
			&file.information->media)) {
		preview = image->data;
		animated = animationPreview = image->animated;
	} else if (const auto video = std::get_if<PreparedFileInformation::Video>(
			&file.information->media)) {
		preview = video->thumbnail;
		animated = true;
		animationPreview = video->isGifv;
	}
	if (preview.isNull()) {
		return nullptr;
	} else if (!animated && !ValidateThumbDimensions(
			preview.width(),
			preview.height())) {
		return nullptr;
	}
	return CreateChild<SingleMediaPreview>(
		parent,
		std::move(gifPaused),
		preview,
		animated,
		Core::IsMimeSticker(file.information->filemime),
		animationPreview ? file.path : QString());
}

SingleMediaPreview::SingleMediaPreview(
	QWidget *parent,
	Fn<bool()> gifPaused,
	QImage preview,
	bool animated,
	bool sticker,
	const QString &animatedPreviewPath)
: RpWidget(parent)
, _gifPaused(std::move(gifPaused))
, _animated(animated)
, _sticker(sticker)
, _editMedia(this, st::sendBoxAlbumGroupButtonMedia)
, _deleteMedia(this, st::sendBoxAlbumGroupButtonMedia)
, _buttonsRect(st::sendBoxAlbumGroupRadius, st::callFingerprintBg) {
	Expects(!preview.isNull());

	_deleteMedia->setIconOverride(&st::sendBoxAlbumGroupButtonMediaDelete);

	preparePreview(preview, animatedPreviewPath);
}

SingleMediaPreview::~SingleMediaPreview() = default;

rpl::producer<> SingleMediaPreview::deleteRequests() const {
	return _deleteMedia->clicks() | rpl::to_empty;
}

rpl::producer<> SingleMediaPreview::editRequests() const {
	return _editMedia->clicks() | rpl::to_empty;
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
			maxW * style::DevicePixelRatio(),
			maxH * style::DevicePixelRatio(),
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
	const auto minthumbh = st::sendBoxAlbumGroupHeight
		 + st::sendBoxAlbumGroupSkipTop * 2;
	_previewHeight = qRound(originalHeight * float64(_previewWidth) / originalWidth);
	if (_previewHeight > maxthumbh) {
		_previewWidth = qRound(_previewWidth * float64(maxthumbh) / _previewHeight);
		accumulate_max(_previewWidth, kMinPreviewWidth);
		_previewHeight = maxthumbh;
	} else if (_previewHeight < minthumbh) {
		_previewWidth = qRound(_previewWidth * float64(minthumbh)
			/ _previewHeight);
		accumulate_max(_previewWidth, kMinPreviewWidth);
		_previewHeight = minthumbh;
	}
	_previewLeft = (st::boxWideWidth - _previewWidth) / 2;

	preview = std::move(preview).scaled(
		_previewWidth * style::DevicePixelRatio(),
		_previewHeight * style::DevicePixelRatio(),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	preview = Images::prepareOpaque(std::move(preview));
	_preview = PixmapFromImage(std::move(preview));
	_preview.setDevicePixelRatio(style::DevicePixelRatio());

	prepareAnimatedPreview(animatedPreviewPath);

	resize(width(), _previewHeight);
}

void SingleMediaPreview::resizeEvent(QResizeEvent *e) {
	const auto skipInternal = st::sendBoxAlbumGroupEditInternalSkip;
	const auto size = st::sendBoxAlbumGroupHeight;
	const auto skipRight = st::sendBoxAlbumGroupSkipRight;
	const auto skipTop = st::sendBoxAlbumGroupSkipTop;
	const auto groupWidth = size * 2 + skipInternal;

	const auto right = (st::boxWideWidth - st::sendMediaPreviewSize) / 2
		+ st::sendMediaPreviewSize;
	const auto left = right - groupWidth - skipRight;
	const auto top = skipTop;
	_editMedia->move(left, top);
	_deleteMedia->move(left + size + skipInternal, top);
}

void SingleMediaPreview::prepareAnimatedPreview(
		const QString &animatedPreviewPath) {
	if (_sticker && _animated) {
		const auto box = QSize(_previewWidth, _previewHeight)
			* style::DevicePixelRatio();
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
			p.fillRect(st::boxPhotoPadding.left(), 0, _previewLeft - st::boxPhotoPadding.left(), _previewHeight, st::confirmBg);
		}
		if (_previewLeft + _previewWidth < width() - st::boxPhotoPadding.right()) {
			p.fillRect(_previewLeft + _previewWidth, 0, width() - st::boxPhotoPadding.right() - _previewLeft - _previewWidth, _previewHeight, st::confirmBg);
		}
	}
	if (_gifPreview && _gifPreview->started()) {
		auto s = QSize(_previewWidth, _previewHeight);
		auto paused = _gifPaused();
		auto frame = _gifPreview->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None, paused ? 0 : crl::now());
		p.drawPixmap(_previewLeft, 0, frame);
	} else if (_lottiePreview && _lottiePreview->ready()) {
		const auto frame = _lottiePreview->frame();
		const auto size = frame.size() / style::DevicePixelRatio();
		p.drawImage(
			QRect(
				_previewLeft + (_previewWidth - size.width()) / 2,
				(_previewHeight - size.height()) / 2,
				size.width(),
				size.height()),
			frame);
		_lottiePreview->markFrameShown();
	} else {
		p.drawPixmap(_previewLeft, 0, _preview);
	}
	if (_animated && !_gifPreview && !_lottiePreview) {
		const auto innerSize = st::msgFileLayout.thumbSize;
		auto inner = QRect(_previewLeft + (_previewWidth - innerSize) / 2, (_previewHeight - innerSize) / 2, innerSize, innerSize);
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgDateImgBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		auto icon = &st::historyFileInPlay;
		icon->paintInCenter(p, inner);
	}
	paintButtonsBackground(p);
}

void SingleMediaPreview::paintButtonsBackground(QPainter &p) {
	const auto skipInternal = st::sendBoxAlbumGroupEditInternalSkip;
	const auto size = st::sendBoxAlbumGroupHeight;
	const auto skipRight = st::sendBoxAlbumGroupSkipRight;
	const auto skipTop = st::sendBoxAlbumGroupSkipTop;
	const auto groupWidth = size * 2 + skipInternal;
	const auto right = (st::boxWideWidth - st::sendMediaPreviewSize) / 2
		+ st::sendMediaPreviewSize;
	const auto left = right - groupWidth - skipRight;
	const auto top = skipTop;

	QRect groupRect(left, top, groupWidth, size);
	_buttonsRect.paint(p, groupRect);
}

} // namespace Ui
