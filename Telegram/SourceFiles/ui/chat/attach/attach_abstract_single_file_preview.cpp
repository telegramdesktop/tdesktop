/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_abstract_single_file_preview.h"

#include "ui/text/text_options.h"
#include "ui/widgets/buttons.h"
#include "ui/image/image_prepare.h"
#include "base/timer_rpl.h"
#include "styles/style_chat.h"
#include "styles/style_boxes.h"

namespace Ui {

AbstractSingleFilePreview::AbstractSingleFilePreview(
	QWidget *parent,
	AttachControls::Type type)
: AbstractSinglePreview(parent)
, _type(type)
, _editMedia(this, st::sendBoxAlbumGroupButtonFile)
, _deleteMedia(this, st::sendBoxAlbumGroupButtonFile) {

	_editMedia->setIconOverride(&st::sendBoxAlbumGroupEditButtonIconFile);
	_deleteMedia->setIconOverride(&st::sendBoxAlbumGroupDeleteButtonIconFile);

	if (type == AttachControls::Type::Full) {
		_deleteMedia->show();
		_editMedia->show();
	} else if (type == AttachControls::Type::EditOnly) {
		_deleteMedia->hide();
		_editMedia->show();
	} else if (type == AttachControls::Type::None) {
		_deleteMedia->hide();
		_editMedia->hide();
	}
}

AbstractSingleFilePreview::~AbstractSingleFilePreview() = default;

rpl::producer<> AbstractSingleFilePreview::editRequests() const {
	return _editMedia->clicks() | rpl::map([] {
		return base::timer_once(st::historyAttach.ripple.hideDuration);
	}) | rpl::flatten_latest();
}

rpl::producer<> AbstractSingleFilePreview::deleteRequests() const {
	return _deleteMedia->clicks() | rpl::to_empty;
}

rpl::producer<> AbstractSingleFilePreview::modifyRequests() const {
	return rpl::never<>();
}

void AbstractSingleFilePreview::prepareThumbFor(
		Data &data,
		const QImage &preview) {
	if (preview.isNull()) {
		return;
	}

	auto originalWidth = preview.width();
	auto originalHeight = preview.height();
	const auto &st = st::attachPreviewThumbLayout;
	auto thumbWidth = st.thumbSize;
	if (originalWidth > originalHeight) {
		thumbWidth = (originalWidth * st.thumbSize) / originalHeight;
	}
	auto options = Images::Option::Smooth
		| Images::Option::RoundedSmall
		| Images::Option::RoundedTopLeft
		| Images::Option::RoundedTopRight
		| Images::Option::RoundedBottomLeft
		| Images::Option::RoundedBottomRight;
	data.fileThumb = PixmapFromImage(Images::prepare(
		preview,
		thumbWidth * style::DevicePixelRatio(),
		0,
		options,
		st.thumbSize,
		st.thumbSize));
}

void AbstractSingleFilePreview::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto w = width()
		- st::boxPhotoPadding.left()
		- st::boxPhotoPadding.right();
	const auto &st = !isThumbedLayout(_data)
		? st::attachPreviewLayout
		: st::attachPreviewThumbLayout;
	const auto nameleft = st.thumbSize + st.padding.right();
	const auto nametop = st.nameTop;
	const auto statustop = st.statusTop;
	const auto x = (width() - w) / 2, y = 0;

	if (!isThumbedLayout(_data)) {
		QRect inner(
			style::rtlrect(x, y, st.thumbSize, st.thumbSize, width()));
		p.setPen(Qt::NoPen);

		if (_data.fileIsAudio && !_data.fileThumb.isNull()) {
			p.drawPixmap(inner.topLeft(), _data.fileThumb);
		} else {
			p.setBrush(st::msgFileInBg);
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		auto &icon = _data.fileIsAudio
			? (_data.fileThumb.isNull()
				? st::historyFileInPlay
				: st::historyFileSongPlay)
			: _data.fileIsImage
			? st::historyFileInImage
			: st::historyFileInDocument;
		icon.paintInCenter(p, inner);
	} else {
		QRect rthumb(
			style::rtlrect(x, y, st.thumbSize, st.thumbSize, width()));
		p.drawPixmap(rthumb.topLeft(), _data.fileThumb);
	}
	p.setFont(st::semiboldFont);
	p.setPen(st::historyFileNameInFg);
	p.drawTextLeft(
		x + nameleft,
		y + nametop, width(),
		_data.name,
		_data.nameWidth);

	p.setFont(st::normalFont);
	p.setPen(st::mediaInFg);
	p.drawTextLeft(
		x + nameleft,
		y + statustop,
		width(),
		_data.statusText,
		_data.statusWidth);
}

void AbstractSingleFilePreview::resizeEvent(QResizeEvent *e) {
	const auto w = width()
		- st::boxPhotoPadding.left()
		- st::boxPhotoPadding.right();
	const auto x = (width() - w) / 2;
	const auto top = st::sendBoxFileGroupSkipTop;
	auto right = st::sendBoxFileGroupSkipRight + x;
	if (_type != AttachControls::Type::EditOnly) {
		_deleteMedia->moveToRight(right, top);
		right += st::sendBoxFileGroupEditInternalSkip + _deleteMedia->width();
	}
	_editMedia->moveToRight(right, top);
}

bool AbstractSingleFilePreview::isThumbedLayout(Data &data) const {
	return (!data.fileThumb.isNull() && !data.fileIsAudio);
}

void AbstractSingleFilePreview::updateTextWidthFor(Data &data) {
	const auto &st = !isThumbedLayout(data)
		? st::attachPreviewLayout
		: st::attachPreviewThumbLayout;
	const auto buttonsCount = (_type == AttachControls::Type::EditOnly)
		? 1
		: (_type == AttachControls::Type::Full)
		? 2
		: 0;
	const auto availableFileWidth = st::sendMediaPreviewSize
		- st.thumbSize
		- st.padding.right()
		// Right buttons.
		- st::sendBoxAlbumGroupButtonFile.width * buttonsCount
		- st::sendBoxAlbumGroupEditInternalSkip * buttonsCount
		- st::sendBoxAlbumGroupSkipRight;
	data.nameWidth = st::semiboldFont->width(data.name);
	if (data.nameWidth > availableFileWidth) {
		data.name = st::semiboldFont->elided(
			data.name,
			availableFileWidth,
			Qt::ElideMiddle);
		data.nameWidth = st::semiboldFont->width(data.name);
	}
	data.statusWidth = st::normalFont->width(data.statusText);
}

void AbstractSingleFilePreview::setData(const Data &data) {
	_data = data;

	updateTextWidthFor(_data);

	const auto &st = !isThumbedLayout(_data)
		? st::attachPreviewLayout
		: st::attachPreviewThumbLayout;
	resize(width(), st.thumbSize);
}

} // namespace Ui
