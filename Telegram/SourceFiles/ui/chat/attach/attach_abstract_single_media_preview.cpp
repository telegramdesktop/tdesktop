/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_abstract_single_media_preview.h"

#include "editor/photo_editor_common.h"
#include "ui/chat/attach/attach_controls.h"
#include "ui/image/image_prepare.h"
#include "ui/widgets/buttons.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

constexpr auto kMinPreviewWidth = 20;

} // namespace

AbstractSingleMediaPreview::AbstractSingleMediaPreview(
	QWidget *parent,
	AttachControls::Type type)
: AbstractSinglePreview(parent)
, _minThumbH(st::sendBoxAlbumGroupSize.height()
	+ st::sendBoxAlbumGroupSkipTop * 2)
, _photoEditorButton(base::make_unique_q<AbstractButton>(this))
, _controls(base::make_unique_q<AttachControlsWidget>(this, type)) {
}

AbstractSingleMediaPreview::~AbstractSingleMediaPreview() = default;

rpl::producer<> AbstractSingleMediaPreview::deleteRequests() const {
	return _controls->deleteRequests();
}

rpl::producer<> AbstractSingleMediaPreview::editRequests() const {
	return _controls->editRequests();
}

rpl::producer<> AbstractSingleMediaPreview::modifyRequests() const {
	return _photoEditorButton->clicks() | rpl::to_empty;
}

void AbstractSingleMediaPreview::preparePreview(QImage preview) {
	auto maxW = 0;
	auto maxH = 0;
	if (_animated && drawBackground()) {
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

	updatePhotoEditorButton();

	resize(width(), std::max(_previewHeight, _minThumbH));
}

void AbstractSingleMediaPreview::updatePhotoEditorButton() {
	_photoEditorButton->resize(_previewWidth, _previewHeight);
	_photoEditorButton->moveToLeft(_previewLeft, _previewTop);
	_photoEditorButton->setVisible(isPhoto());
}

void AbstractSingleMediaPreview::resizeEvent(QResizeEvent *e) {
	_controls->moveToRight(
		st::boxPhotoPadding.right() + st::sendBoxAlbumGroupSkipRight,
		st::sendBoxAlbumGroupSkipTop,
		width());
}

void AbstractSingleMediaPreview::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (drawBackground()) {
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
	if (!tryPaintAnimation(p)) {
		p.drawPixmap(_previewLeft, _previewTop, _preview);
	}
	if (_animated && !isAnimatedPreviewReady()) {
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

int AbstractSingleMediaPreview::previewLeft() const {
	return _previewLeft;
}

int AbstractSingleMediaPreview::previewTop() const {
	return _previewTop;
}

int AbstractSingleMediaPreview::previewWidth() const {
	return _previewWidth;
}

int AbstractSingleMediaPreview::previewHeight() const {
	return _previewHeight;
}

void AbstractSingleMediaPreview::setAnimated(bool animated) {
	_animated = animated;
}

bool AbstractSingleMediaPreview::isPhoto() const {
	return drawBackground()
		&& !isAnimatedPreviewReady()
		&& !_animated;
}

} // namespace Ui
