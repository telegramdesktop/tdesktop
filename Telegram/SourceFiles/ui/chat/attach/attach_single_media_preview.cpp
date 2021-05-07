/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_single_media_preview.h"

#include "editor/photo_editor_common.h"
#include "ui/chat/attach/attach_controls.h"
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
		preview = Editor::ImageModified(image->data, image->modifications);
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
, _minThumbH(st::sendBoxAlbumGroupSize.height()
	+ st::sendBoxAlbumGroupSkipTop * 2)
, _photoEditorButton(base::make_unique_q<AbstractButton>(this))
, _controls(base::make_unique_q<AttachControlsWidget>(this)) {
	Expects(!preview.isNull());

	preparePreview(preview, animatedPreviewPath);
}

SingleMediaPreview::~SingleMediaPreview() = default;

rpl::producer<> SingleMediaPreview::deleteRequests() const {
	return _controls->deleteRequests();
}

rpl::producer<> SingleMediaPreview::editRequests() const {
	return _controls->editRequests();
}

rpl::producer<> SingleMediaPreview::modifyRequests() const {
	return _photoEditorButton->clicks() | rpl::to_empty;
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
	_previewHeight = qRound(originalHeight
		* float64(_previewWidth)
		/ originalWidth);
	if (_previewHeight > maxthumbh) {
		_previewWidth = qRound(_previewWidth
			* float64(maxthumbh)
			/ _previewHeight);
		accumulate_max(_previewWidth, kMinPreviewWidth);
		_previewHeight = maxthumbh;
	}
	_previewLeft = (st::boxWideWidth - _previewWidth) / 2;
	if (_previewHeight < _minThumbH) {
		_previewTop = (_minThumbH - _previewHeight) / 2;
	}

	preview = std::move(preview).scaled(
		_previewWidth * style::DevicePixelRatio(),
		_previewHeight * style::DevicePixelRatio(),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	preview = Images::prepareOpaque(std::move(preview));
	_preview = PixmapFromImage(std::move(preview));
	_preview.setDevicePixelRatio(style::DevicePixelRatio());

	prepareAnimatedPreview(animatedPreviewPath);

	_photoEditorButton->resize(_previewWidth, _previewHeight);
	_photoEditorButton->moveToLeft(_previewLeft, _previewTop);
	_photoEditorButton->setVisible(!_sticker
		&& !_gifPreview
		&& !_lottiePreview
		&& !_animated);

	resize(width(), std::max(_previewHeight, _minThumbH));
}

void SingleMediaPreview::resizeEvent(QResizeEvent *e) {
	_controls->moveToRight(
		st::boxPhotoPadding.right() + st::sendBoxAlbumGroupSkipRight,
		st::sendBoxAlbumGroupSkipTop,
		width());
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

void SingleMediaPreview::clipCallback(
		Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gifPreview && _gifPreview->state() == State::Error) {
			_gifPreview.setBad();
		}

		if (_gifPreview && _gifPreview->ready() && !_gifPreview->started()) {
			auto s = QSize(_previewWidth, _previewHeight);
			_gifPreview->start(
				s.width(),
				s.height(),
				s.width(),
				s.height(),
				ImageRoundRadius::None,
				RectPart::None);
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
		const auto &padding = st::boxPhotoPadding;
		if (_previewLeft > padding.left()) {
			p.fillRect(
				padding.left(),
				_previewTop,
				_previewLeft - padding.left(),
				_previewHeight,
				st::confirmBg);
		}
		if ((_previewLeft + _previewWidth) < (width() - padding.right())) {
			p.fillRect(
				_previewLeft + _previewWidth,
				_previewTop,
				width() - padding.right() - _previewLeft - _previewWidth,
				_previewHeight,
				st::confirmBg);
		}
		if (_previewTop > 0) {
			p.fillRect(
				padding.left(),
				0,
				width() - padding.right() - padding.left(),
				height(),
				st::confirmBg);
		}
	}
	if (_gifPreview && _gifPreview->started()) {
		auto s = QSize(_previewWidth, _previewHeight);
		auto paused = _gifPaused();
		auto frame = _gifPreview->current(
			s.width(),
			s.height(),
			s.width(),
			s.height(),
			ImageRoundRadius::None,
			RectPart::None,
			paused ? 0 : crl::now());
		p.drawPixmap(_previewLeft, _previewTop, frame);
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
		p.drawPixmap(_previewLeft, _previewTop, _preview);
	}
	if (_animated && !_gifPreview && !_lottiePreview) {
		const auto innerSize = st::msgFileLayout.thumbSize;
		auto inner = QRect(
			_previewLeft + (_previewWidth - innerSize) / 2,
			_previewTop + (_previewHeight - innerSize) / 2,
			innerSize,
			innerSize);
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

} // namespace Ui
