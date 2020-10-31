/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_single_file_preview.h"

#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/widgets/buttons.h"
#include "ui/image/image_prepare.h"
#include "base/timer_rpl.h"
#include "core/mime_type.h"
#include "styles/style_chat.h"
#include "styles/style_boxes.h"

#include <QtCore/QFileInfo>

namespace Ui {

SingleFilePreview::SingleFilePreview(
	QWidget *parent,
	const PreparedFile &file)
: RpWidget(parent)
, _editMedia(this, st::sendBoxAlbumGroupButtonFile)
, _deleteMedia(this, st::sendBoxAlbumGroupButtonFile) {
	preparePreview(file);

	_editMedia->setIconOverride(&st::sendBoxAlbumGroupEditButtonIconFile);
	_deleteMedia->setIconOverride(&st::sendBoxAlbumGroupDeleteButtonIconFile);

	const auto &st = _fileThumb.isNull()
		? st::attachPreviewLayout
		: st::attachPreviewThumbLayout;
	resize(width(), st.thumbSize);
}

SingleFilePreview::~SingleFilePreview() = default;

rpl::producer<> SingleFilePreview::editRequests() const {
	return _editMedia->clicks() | rpl::map([] {
		return base::timer_once(st::historyAttach.ripple.hideDuration);
	}) | rpl::flatten_latest();
}

rpl::producer<> SingleFilePreview::deleteRequests() const {
	return _deleteMedia->clicks() | rpl::to_empty;
}

void SingleFilePreview::prepareThumb(const QImage &preview) {
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
	_fileThumb = PixmapFromImage(Images::prepare(
		preview,
		thumbWidth * style::DevicePixelRatio(),
		0,
		options,
		st.thumbSize,
		st.thumbSize));
}

void SingleFilePreview::preparePreview(const PreparedFile &file) {
	auto preview = QImage();
	if (const auto image = std::get_if<PreparedFileInformation::Image>(
		&file.information->media)) {
		preview = image->data;
	} else if (const auto video = std::get_if<PreparedFileInformation::Video>(
		&file.information->media)) {
		preview = video->thumbnail;
	}
	prepareThumb(preview);
	const auto filepath = file.path;
	if (filepath.isEmpty()) {
		auto filename = "image.png";
		_name = filename;
		_statusText = u"%1x%2"_q.arg(preview.width()).arg(preview.height());
		_fileIsImage = true;
	} else {
		auto fileinfo = QFileInfo(filepath);
		auto filename = fileinfo.fileName();
		_fileIsImage = Core::FileIsImage(filename, Core::MimeTypeForFile(fileinfo).name());

		auto songTitle = QString();
		auto songPerformer = QString();
		if (file.information) {
			if (const auto song = std::get_if<PreparedFileInformation::Song>(
					&file.information->media)) {
				songTitle = song->title;
				songPerformer = song->performer;
				_fileIsAudio = true;
			}
		}

		_name = ComposeNameString(filename, songTitle, songPerformer);
		_statusText = FormatSizeText(fileinfo.size());
	}
	const auto &st = _fileThumb.isNull()
		? st::attachPreviewLayout
		: st::attachPreviewThumbLayout;
	const auto nameleft = st.thumbSize + st.padding.right();
	const auto nametop = st.nameTop;
	const auto statustop = st.statusTop;
	const auto availableFileWidth = st::sendMediaPreviewSize
		- st.thumbSize
		- st.padding.right()
		// Right buttons.
		- st::sendBoxAlbumGroupButtonFile.width * 2
		- st::sendBoxAlbumGroupEditInternalSkip * 2
		- st::sendBoxAlbumGroupSkipRight;
	_nameWidth = st::semiboldFont->width(_name);
	if (_nameWidth > availableFileWidth) {
		_name = st::semiboldFont->elided(
			_name,
			availableFileWidth,
			Qt::ElideMiddle);
		_nameWidth = st::semiboldFont->width(_name);
	}
	_statusWidth = st::normalFont->width(_statusText);
}

void SingleFilePreview::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
	auto h = height();
	const auto &st = _fileThumb.isNull()
		? st::attachPreviewLayout
		: st::attachPreviewThumbLayout;
	const auto nameleft = st.thumbSize + st.padding.right();
	const auto nametop = st.nameTop;
	const auto statustop = st.statusTop;
	const auto x = (width() - w) / 2, y = 0;

	if (_fileThumb.isNull()) {
		QRect inner(style::rtlrect(x, y, st.thumbSize, st.thumbSize, width()));
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgFileInBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		auto &icon = _fileIsAudio
			? st::historyFileInPlay
			: _fileIsImage
			? st::historyFileInImage
			: st::historyFileInDocument;
		icon.paintInCenter(p, inner);
	} else {
		QRect rthumb(style::rtlrect(x, y, st.thumbSize, st.thumbSize, width()));
		p.drawPixmap(rthumb.topLeft(), _fileThumb);
	}
	p.setFont(st::semiboldFont);
	p.setPen(st::historyFileNameInFg);
	p.drawTextLeft(x + nameleft, y + nametop, width(), _name, _nameWidth);

	p.setFont(st::normalFont);
	p.setPen(st::mediaInFg);
	p.drawTextLeft(x + nameleft, y + statustop, width(), _statusText, _statusWidth);
}

void SingleFilePreview::resizeEvent(QResizeEvent *e) {
	const auto w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
	const auto x = (width() - w) / 2;
	const auto top = st::sendBoxFileGroupSkipTop;
	auto right = st::sendBoxFileGroupSkipRight + x;
	_deleteMedia->moveToRight(right, top);
	right += st::sendBoxFileGroupEditInternalSkip + _deleteMedia->width();
	_editMedia->moveToRight(right, top);
}

} // namespace Ui
