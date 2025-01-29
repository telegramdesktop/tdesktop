/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_abstract_single_media_preview.h"

#include "editor/photo_editor_common.h"
#include "lang/lang_keys.h"
#include "ui/chat/attach/attach_controls.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Ui {
namespace {

constexpr auto kMinPreviewWidth = 20;

} // namespace

AbstractSingleMediaPreview::AbstractSingleMediaPreview(
	QWidget *parent,
	const style::ComposeControls &st,
	AttachControls::Type type,
	Fn<bool(AttachActionType)> actionAllowed)
: AbstractSinglePreview(parent)
, _st(st)
, _actionAllowed(std::move(actionAllowed))
, _minThumbH(st::sendBoxAlbumGroupSize.height()
	+ st::sendBoxAlbumGroupSkipTop * 2)
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
	return _photoEditorRequests.events();
}

rpl::producer<> AbstractSingleMediaPreview::editCoverRequests() const {
	return _editCoverRequests.events();
}

rpl::producer<> AbstractSingleMediaPreview::clearCoverRequests() const {
	return _clearCoverRequests.events();
}

void AbstractSingleMediaPreview::setSendWay(SendFilesWay way) {
	_sendWay = way;
	update();
}

SendFilesWay AbstractSingleMediaPreview::sendWay() const {
	return _sendWay;
}

void AbstractSingleMediaPreview::setSpoiler(bool spoiler) {
	_spoiler = spoiler
		? std::make_unique<SpoilerAnimation>([=] { update(); })
		: nullptr;
	update();
}

bool AbstractSingleMediaPreview::hasSpoiler() const {
	return _spoiler != nullptr;
}

bool AbstractSingleMediaPreview::canHaveSpoiler() const {
	return supportsSpoilers();
}

rpl::producer<bool> AbstractSingleMediaPreview::spoileredChanges() const {
	return _spoileredChanges.events();
}

QImage AbstractSingleMediaPreview::generatePriceTagBackground() const {
	return (_previewBlurred.isNull() ? _preview : _previewBlurred).toImage();
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
		const auto ratio = style::DevicePixelRatio();
		preview = Images::Prepare(
			std::move(preview),
			QSize(maxW, maxH) * ratio,
			{ .outer = { maxW, maxH } });
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
	preview = Images::Opaque(std::move(preview));
	_preview = PixmapFromImage(std::move(preview));
	_preview.setDevicePixelRatio(style::DevicePixelRatio());
	_previewBlurred = QPixmap();

	resize(width(), std::max(_previewHeight, _minThumbH));
}

bool AbstractSingleMediaPreview::isOverPreview(QPoint position) const {
	return QRect(
		_previewLeft,
		_previewTop,
		_previewWidth,
		_previewHeight).contains(position);
}

void AbstractSingleMediaPreview::resizeEvent(QResizeEvent *e) {
	_controls->moveToRight(
		st::boxPhotoPadding.right() + st::sendBoxAlbumGroupSkipRight,
		st::sendBoxAlbumGroupSkipTop,
		width());
}

void AbstractSingleMediaPreview::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	auto takenSpoiler = supportsSpoilers()
		? nullptr
		: base::take(_spoiler);
	const auto guard = gsl::finally([&] {
		if (takenSpoiler) {
			_spoiler = base::take(takenSpoiler);
		}
	});

	if (drawBackground()) {
		const auto &padding = st::boxPhotoPadding;
		if (_previewLeft > padding.left()) {
			p.fillRect(
				padding.left(),
				_previewTop,
				_previewLeft - padding.left(),
				_previewHeight,
				_st.files.confirmBg);
		}
		if ((_previewLeft + _previewWidth) < (width() - padding.right())) {
			p.fillRect(
				_previewLeft + _previewWidth,
				_previewTop,
				width() - padding.right() - _previewLeft - _previewWidth,
				_previewHeight,
				_st.files.confirmBg);
		}
		if (_previewTop > 0) {
			p.fillRect(
				padding.left(),
				0,
				width() - padding.right() - padding.left(),
				height(),
				_st.files.confirmBg);
		}
	}

	if (_spoiler && _previewBlurred.isNull()) {
		_previewBlurred = BlurredPreviewFromPixmap(_preview, RectPart::None);
	}
	if (_spoiler || !tryPaintAnimation(p)) {
		const auto &pixmap = _spoiler ? _previewBlurred : _preview;
		const auto position = QPoint(_previewLeft, _previewTop);
		p.drawPixmap(position, pixmap);
		if (_spoiler) {
			const auto paused = On(PowerSaving::kChatSpoiler);
			FillSpoilerRect(
				p,
				QRect(position, pixmap.size() / pixmap.devicePixelRatio()),
				DefaultImageSpoiler().frame(
					_spoiler->index(crl::now(), paused)));
		}
	}
	if (_animated && !isAnimatedPreviewReady() && !_spoiler) {
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

void AbstractSingleMediaPreview::mousePressEvent(QMouseEvent *e) {
	if (isOverPreview(e->pos())) {
		_pressed = true;
	}
}

void AbstractSingleMediaPreview::mouseMoveEvent(QMouseEvent *e) {
	applyCursor((isPhoto() && isOverPreview(e->pos()))
		? style::cur_pointer
		: style::cur_default);
}

void AbstractSingleMediaPreview::mouseReleaseEvent(QMouseEvent *e) {
	if (base::take(_pressed) && isOverPreview(e->pos())) {
		if (e->button() == Qt::RightButton) {
			showContextMenu(e->globalPos());
		} else if (isPhoto()) {
			_photoEditorRequests.fire({});
		}
	}
}

void AbstractSingleMediaPreview::applyCursor(style::cursor cursor) {
	if (_cursor != cursor) {
		_cursor = cursor;
		setCursor(_cursor);
	}
}

void AbstractSingleMediaPreview::showContextMenu(QPoint position) {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		_st.tabbed.menu);

	const auto &icons = _st.tabbed.icons;
	if (_actionAllowed(AttachActionType::ToggleSpoiler)
		&& _sendWay.sendImagesAsPhotos()
		&& supportsSpoilers()) {
		const auto spoilered = hasSpoiler();
		_menu->addAction(spoilered
			? tr::lng_context_disable_spoiler(tr::now)
			: tr::lng_context_spoiler_effect(tr::now), [=] {
			setSpoiler(!spoilered);
			_spoileredChanges.fire_copy(!spoilered);
		}, spoilered ? &icons.menuSpoilerOff : &icons.menuSpoiler);
	}
	if (_actionAllowed(AttachActionType::EditCover)) {
		_menu->addAction(tr::lng_context_edit_cover(tr::now), [=] {
			_editCoverRequests.fire({});
		}, &st::menuIconEdit);

		if (_actionAllowed(AttachActionType::ClearCover)) {
			_menu->addAction(tr::lng_context_clear_cover(tr::now), [=] {
				_clearCoverRequests.fire({});
			}, &st::menuIconCancel);
		}
	}
	if (_menu->empty()) {
		_menu = nullptr;
	} else {
		_menu->popup(position);
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
