/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "boxes/send_files_box.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "storage/storage_media_prepare.h"
#include "mainwidget.h"
#include "history/history_media_types.h"
#include "core/file_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/grouped_layout.h"
#include "media/media_clip_reader.h"
#include "window/window_controller.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kMinPreviewWidth = 20;

class SingleMediaPreview : public Ui::RpWidget {
public:
	static SingleMediaPreview *Create(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		const Storage::PreparedFile &file);

	SingleMediaPreview(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		QImage preview,
		bool animated,
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

	not_null<Window::Controller*> _controller;
	bool _animated = false;
	bool _canSendAsPhoto = false;
	QPixmap _preview;
	int _previewLeft = 0;
	int _previewWidth = 0;
	int _previewHeight = 0;
	Media::Clip::ReaderPointer _gifPreview;

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
	Text _nameText;
	bool _fileIsAudio = false;
	bool _fileIsImage = false;
	QString _statusText;
	int _statusWidth = 0;

};

SingleMediaPreview *SingleMediaPreview::Create(
		QWidget *parent,
		not_null<Window::Controller*> controller,
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
	return Ui::CreateChild<SingleMediaPreview>(
		parent,
		controller,
		preview,
		animated,
		animationPreview ? file.path : QString());
}

SingleMediaPreview::SingleMediaPreview(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	QImage preview,
	bool animated,
	const QString &animatedPreviewPath)
: RpWidget(parent)
, _controller(controller)
, _animated(animated) {
	Expects(!preview.isNull());

	_canSendAsPhoto = !_animated && Storage::ValidateThumbDimensions(
		preview.width(),
		preview.height());

	preparePreview(preview, animatedPreviewPath);
}

void SingleMediaPreview::preparePreview(
		QImage preview,
		const QString &animatedPreviewPath) {
	auto maxW = 0;
	auto maxH = 0;
	if (_animated) {
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
	if (!animatedPreviewPath.isEmpty()) {
		auto callback = [this](Media::Clip::Notification notification) {
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

	if (_previewLeft > st::boxPhotoPadding.left()) {
		p.fillRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top(), _previewLeft - st::boxPhotoPadding.left(), _previewHeight, st::confirmBg);
	}
	if (_previewLeft + _previewWidth < width() - st::boxPhotoPadding.right()) {
		p.fillRect(_previewLeft + _previewWidth, st::boxPhotoPadding.top(), width() - st::boxPhotoPadding.right() - _previewLeft - _previewWidth, _previewHeight, st::confirmBg);
	}
	if (_gifPreview && _gifPreview->started()) {
		auto s = QSize(_previewWidth, _previewHeight);
		auto paused = _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
		auto frame = _gifPreview->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None, paused ? 0 : getms());
		p.drawPixmap(_previewLeft, st::boxPhotoPadding.top(), frame);
	} else {
		p.drawPixmap(_previewLeft, st::boxPhotoPadding.top(), _preview);
	}
	if (_animated && !_gifPreview) {
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
	_fileThumb = Images::pixmap(
		preview,
		thumbWidth * cIntRetinaFactor(),
		0,
		options,
		st::msgFileThumbSize,
		st::msgFileThumbSize);
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
		auto filename = filedialogDefaultName(qsl("image"), qsl(".png"), QString(), true);
		_nameText.setText(st::semiboldTextStyle, filename, _textNameOptions);
		_statusText = qsl("%1x%2").arg(preview.width()).arg(preview.height());
		_statusWidth = qMax(_nameText.maxWidth(), st::normalFont->width(_statusText));
		_fileIsImage = true;
	} else {
		auto fileinfo = QFileInfo(filepath);
		auto filename = fileinfo.fileName();
		_fileIsImage = fileIsImage(filename, mimeTypeForFile(fileinfo).name());

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
			_textNameOptions);
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

base::lambda<QString()> FieldPlaceholder(const Storage::PreparedList &list) {
	return langFactory(list.files.size() > 1
		? lng_photos_comment
		: lng_photo_caption);
}

} // namespace

struct SendFilesBox::AlbumThumb {
	Ui::GroupMediaLayout layout;
	QPixmap albumImage;
	QPixmap photo;
	QPixmap fileThumb;
	QString name;
	QString status;
	int nameWidth = 0;
	int statusWidth = 0;
	bool video = false;
};

class SendFilesBox::AlbumPreview : public Ui::RpWidget {
public:
	AlbumPreview(
		QWidget *parent,
		const Storage::PreparedList &list,
		SendFilesWay way);

	void setSendWay(SendFilesWay way);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void prepareThumbs();
	void updateSize();
	AlbumThumb prepareThumb(
		const Storage::PreparedFile &file,
		const Ui::GroupMediaLayout &layout) const;

	void paintAlbum(Painter &p) const;
	void paintPhotos(Painter &p, QRect clip) const;
	void paintFiles(Painter &p, QRect clip) const;

	const Storage::PreparedList &_list;
	SendFilesWay _sendWay = SendFilesWay::Files;
	std::vector<AlbumThumb> _thumbs;
	int _thumbsHeight = 0;
	int _photosHeight = 0;
	int _filesHeight = 0;

};

SendFilesBox::AlbumPreview::AlbumPreview(
	QWidget *parent,
	const Storage::PreparedList &list,
	SendFilesWay way)
: RpWidget(parent)
, _list(list)
, _sendWay(way) {
	prepareThumbs();
	updateSize();
}

void SendFilesBox::AlbumPreview::setSendWay(SendFilesWay way) {
	_sendWay = way;
	updateSize();
	update();
}

void SendFilesBox::AlbumPreview::prepareThumbs() {
	auto sizes = ranges::view::all(
		_list.files
	) | ranges::view::transform([](const Storage::PreparedFile &file) {
		return file.preview.size() / cIntRetinaFactor();
	}) | ranges::to_vector;

	const auto count = int(sizes.size());
	const auto layout = Ui::LayoutMediaGroup(
		sizes,
		st::sendMediaPreviewSize,
		st::historyGroupWidthMin / 2,
		st::historyGroupSkip / 2);
	Assert(layout.size() == count);

	_thumbs.reserve(count);
	for (auto i = 0; i != count; ++i) {
		_thumbs.push_back(prepareThumb(_list.files[i], layout[i]));
		const auto &geometry = layout[i].geometry;
		accumulate_max(_thumbsHeight, geometry.y() + geometry.height());
	}
	_photosHeight = ranges::accumulate(ranges::view::all(
		_thumbs
	) | ranges::view::transform([](const AlbumThumb &thumb) {
		return thumb.photo.height() / cIntRetinaFactor();
	}), 0) + (count - 1) * st::sendMediaPreviewPhotoSkip;

	_filesHeight = count * st::sendMediaFileThumbSize
		+ (count - 1) * st::sendMediaFileThumbSkip;
}

SendFilesBox::AlbumThumb SendFilesBox::AlbumPreview::prepareThumb(
		const Storage::PreparedFile &file,
		const Ui::GroupMediaLayout &layout) const {
	Expects(!file.preview.isNull());

	const auto &preview = file.preview;
	auto result = AlbumThumb();
	result.layout = layout;
	result.video = (file.type == Storage::PreparedFile::AlbumType::Video);

	const auto width = layout.geometry.width();
	const auto height = layout.geometry.height();
	const auto corners = Ui::GetCornersFromSides(layout.sides);
	using Option = Images::Option;
	const auto options = Option::Smooth
		| Option::RoundedLarge
		| ((corners & RectPart::TopLeft) ? Option::RoundedTopLeft : Option::None)
		| ((corners & RectPart::TopRight) ? Option::RoundedTopRight : Option::None)
		| ((corners & RectPart::BottomLeft) ? Option::RoundedBottomLeft : Option::None)
		| ((corners & RectPart::BottomRight) ? Option::RoundedBottomRight : Option::None);
	const auto pixSize = Ui::GetImageScaleSizeForGeometry(
		{ preview.width(), preview.height() },
		{ width, height });
	const auto pixWidth = pixSize.width() * cIntRetinaFactor();
	const auto pixHeight = pixSize.height() * cIntRetinaFactor();

	result.albumImage = App::pixmapFromImageInPlace(Images::prepare(
		preview,
		pixWidth,
		pixHeight,
		options,
		width,
		height));
	result.photo = App::pixmapFromImageInPlace(Images::prepare(
		preview,
		preview.width(),
		preview.height(),
		Option::RoundedLarge | Option::RoundedAll,
		preview.width() / cIntRetinaFactor(),
		preview.height() / cIntRetinaFactor()));

	const auto idealSize = st::sendMediaFileThumbSize * cIntRetinaFactor();
	const auto fileThumbSize = (preview.width() > preview.height())
		? QSize(preview.width() * idealSize / preview.height(), idealSize)
		: QSize(idealSize, preview.height() * idealSize / preview.width());
	result.fileThumb = App::pixmapFromImageInPlace(Images::prepare(
		preview,
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
		result.name = filedialogDefaultName(
			qsl("image"),
			qsl(".png"),
			QString(),
			true);
		result.status = qsl("%1x%2").arg(preview.width()).arg(preview.height());
	} else {
		auto fileinfo = QFileInfo(filepath);
		result.name = fileinfo.fileName();
		result.status = formatSizeText(fileinfo.size());
	}
	result.nameWidth = st::semiboldFont->width(result.name);
	if (result.nameWidth > availableFileWidth) {
		result.name = st::semiboldFont->elided(
			result.name,
			Qt::ElideMiddle);
		result.nameWidth = st::semiboldFont->width(result.name);
	}
	result.statusWidth = st::normalFont->width(result.status);

	return result;
}

void SendFilesBox::AlbumPreview::updateSize() {
	const auto height = [&] {
		switch (_sendWay) {
		case SendFilesWay::Album: return _thumbsHeight;
		case SendFilesWay::Photos: return _photosHeight;
		case SendFilesWay::Files: return _filesHeight;
		}
		Unexpected("Send way in SendFilesBox::AlbumPreview::updateSize");
	}();
	resize(st::boxWideWidth, height);
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
	const auto left = (st::boxWideWidth - st::sendMediaPreviewSize) / 2;
	const auto top = 0;
	for (const auto &thumb : _thumbs) {
		const auto geometry = thumb.layout.geometry;
		const auto x = left + geometry.x();
		const auto y = top + geometry.y();
		p.drawPixmap(x, y, thumb.albumImage);

		if (thumb.video) {
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
}

void SendFilesBox::AlbumPreview::paintPhotos(Painter &p, QRect clip) const {
	const auto left = (st::boxWideWidth - st::sendMediaPreviewSize) / 2;
	auto top = 0;
	for (const auto &thumb : _thumbs) {
		const auto bottom = top + thumb.photo.height() / cIntRetinaFactor();
		const auto guard = gsl::finally([&] {
			top = bottom + st::sendMediaPreviewPhotoSkip;
		});
		if (top >= clip.y() + clip.height()) {
			break;
		} else if (bottom <= clip.y()) {
			continue;
		}
		p.drawPixmap(
			left,
			top,
			thumb.photo);
	}
}

void SendFilesBox::AlbumPreview::paintFiles(Painter &p, QRect clip) const {
	const auto fileHeight = st::sendMediaFileThumbSize
		+ st::sendMediaFileThumbSkip;
	const auto bottom = clip.y() + clip.height();
	const auto from = floorclamp(clip.y(), fileHeight, 0, _thumbs.size());
	const auto till = ceilclamp(bottom, fileHeight, 0, _thumbs.size());
	const auto left = (st::boxWideWidth - st::sendMediaPreviewSize) / 2;
	const auto textLeft = left
		+ st::sendMediaFileThumbSize
		+ st::sendMediaFileThumbSkip;

	auto top = from * fileHeight;
	for (auto i = from; i != till; ++i) {
		const auto &thumb = _thumbs[i];
		p.drawPixmap(left, top, thumb.fileThumb);
		p.setFont(st::semiboldFont);
		p.setPen(st::historyFileNameInFg);
		p.drawTextLeft(
			textLeft,
			top + st::sendMediaFileNameTop,
			width(),
			thumb.name,
			thumb.nameWidth);
		p.setFont(st::normalFont);
		p.setPen(st::mediaInFg);
		p.drawTextLeft(
			textLeft,
			top + st::sendMediaFileStatusTop,
			width(),
			thumb.status,
			thumb.statusWidth);
		top += fileHeight;
	}
}

SendFilesBox::SendFilesBox(
	QWidget*,
	Storage::PreparedList &&list,
	CompressConfirm compressed)
: _list(std::move(list))
, _compressConfirm(compressed)
, _caption(this, st::confirmCaptionArea, FieldPlaceholder(_list)) {
}

void SendFilesBox::initPreview(rpl::producer<int> desiredPreviewHeight) {
	setupControls();

	updateBoxSize();

	using namespace rpl::mappers;
	rpl::combine(
		std::move(desiredPreviewHeight),
		_footerHeight.value(),
		_titleHeight + _1 + _2
	) | rpl::start_with_next([this](int height) {
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
	const auto media = SingleMediaPreview::Create(this, controller(), file);
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
	) | rpl::start_with_next([=](const QRect &geometry) {
		topShadow->resizeToWidth(geometry.width());
		topShadow->move(
			geometry.x(),
			geometry.y());
		bottomShadow->resizeToWidth(geometry.width());
		bottomShadow->move(
			geometry.x(),
			geometry.y() + geometry.height() - st::lineWidth);
	}, topShadow->lifetime());
	topShadow->toggleOn(wrap->scrollTopValue() | rpl::map(_1 > 0));
	bottomShadow->toggleOn(rpl::combine(
		wrap->scrollTopValue(),
		wrap->heightValue(),
		content->heightValue(),
		_1 + _2 < _3));
}

void SendFilesBox::prepare() {
	Expects(controller() != nullptr);

	_send = addButton(langFactory(lng_send_button), [this] { send(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });
	initSendWay();
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

	subscribe(boxClosing, [this] {
		if (!_confirmed && _cancelledCallback) {
			_cancelledCallback();
		}
	});
}

void SendFilesBox::initSendWay() {
	_albumVideosCount = ranges::count(
		_list.files,
		Storage::PreparedFile::AlbumType::Video,
		[](const Storage::PreparedFile &file) { return file.type; });
	_albumPhotosCount = _list.albumIsPossible
		? (_list.files.size() - _albumVideosCount)
		: 0;
	const auto value = [&] {
		if (_compressConfirm == CompressConfirm::None) {
			return SendFilesWay::Files;
		} else if (_compressConfirm == CompressConfirm::No) {
			return SendFilesWay::Files;
		} else if (_compressConfirm == CompressConfirm::Yes) {
			return _list.albumIsPossible
				? SendFilesWay::Album
				: SendFilesWay::Photos;
		}
		const auto currentWay = Auth().data().sendFilesWay();
		if (currentWay == SendFilesWay::Files) {
			return currentWay;
		} else if (currentWay == SendFilesWay::Album) {
			return _list.albumIsPossible
				? SendFilesWay::Album
				: SendFilesWay::Photos;
		}
		return (_list.albumIsPossible && !_albumPhotosCount)
			? SendFilesWay::Album
			: SendFilesWay::Photos;
	}();
	_sendWay = std::make_shared<Ui::RadioenumGroup<SendFilesWay>>(value);
}

void SendFilesBox::setupControls() {
	setupTitleText();
	setupSendWayControls();
	setupCaption();
}

void SendFilesBox::setupSendWayControls() {
	if (_compressConfirm == CompressConfirm::None) {
		return;
	}
	const auto addRadio = [&](
		object_ptr<Ui::Radioenum<SendFilesWay>> &button,
		SendFilesWay value,
		const QString &text) {
		const auto &style = st::defaultBoxCheckbox;
		button.create(this, _sendWay, value, text, style);
	};
	if (_list.albumIsPossible) {
		addRadio(_sendAlbum, SendFilesWay::Album, lang(lng_send_album));
	}
	if (!_list.albumIsPossible || _albumPhotosCount > 0) {
		addRadio(_sendPhotos, SendFilesWay::Photos, (_list.files.size() == 1)
			? lang(lng_send_photo)
			: (_albumVideosCount > 0)
			? (_list.albumIsPossible
				? lang(lng_send_separate_photos_videos)
				: lng_send_photos_videos(lt_count, _list.files.size()))
			: (_list.albumIsPossible
				? lang(lng_send_separate_photos)
				: lng_send_photos(lt_count, _list.files.size())));
	}
	addRadio(_sendFiles, SendFilesWay::Files, (_list.files.size() == 1)
		? lang(lng_send_file)
		: lng_send_files(lt_count, _list.files.size()));
	_sendWay->setChangedCallback([this](SendFilesWay value) {
		if (_albumPreview) {
			_albumPreview->setSendWay(value);
		}
		setInnerFocus();
	});
}

void SendFilesBox::setupCaption() {
	if (!_caption) {
		return;
	}

	_caption->setMaxLength(MaxPhotoCaption);
	_caption->setCtrlEnterSubmit(Ui::CtrlEnterSubmit::Both);
	connect(_caption, &Ui::InputArea::resized, this, [this] {
		captionResized();
	});
	connect(_caption, &Ui::InputArea::submitted, this, [this](
		bool ctrlShiftEnter) {
		send(ctrlShiftEnter);
	});
	connect(_caption, &Ui::InputArea::cancelled, this, [this] {
		closeBox();
	});
}

void SendFilesBox::captionResized() {
	updateBoxSize();
	updateControlsGeometry();
	update();
}

void SendFilesBox::setupTitleText() {
	if (_list.files.size() > 1) {
		const auto onlyImages = (_compressConfirm != CompressConfirm::None)
			&& (_albumVideosCount == 0);
		_titleText = onlyImages
			? lng_send_images_selected(lt_count, _list.files.size())
			: lng_send_files_selected(lt_count, _list.files.size());
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
		send(ctrl && shift);
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
	}
	const auto pointers = {
		_sendAlbum.data(),
		_sendPhotos.data(),
		_sendFiles.data()
	};
	for (auto pointer : base::reversed(pointers)) {
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

void SendFilesBox::send(bool ctrlShiftEnter) {
	using Way = SendFilesWay;
	const auto way = _sendWay ? _sendWay->value() : Way::Files;

	if (_compressConfirm == CompressConfirm::Auto) {
		const auto oldWay = Auth().data().sendFilesWay();
		if (way != oldWay) {
			// Check if the user _could_ use the old value, but didn't.
			if ((oldWay == Way::Album && _sendAlbum)
				|| (oldWay == Way::Photos && _sendPhotos)
				|| (oldWay == Way::Files && _sendFiles)
				|| (way == Way::Files && (_sendAlbum || _sendPhotos))) {
				// And in that case save it to settings.
				Auth().data().setSendFilesWay(way);
				Auth().saveDataDelayed();
			}
		}
	}
	_confirmed = true;
	if (_confirmedCallback) {
		auto caption = _caption
			? TextUtilities::PrepareForSending(
				_caption->getLastText(),
				TextUtilities::PrepareTextOption::CheckLinks)
			: QString();
		_confirmedCallback(
			std::move(_list),
			way,
			std::move(caption),
			ctrlShiftEnter);
	}
	closeBox();
}

SendFilesBox::~SendFilesBox() = default;
