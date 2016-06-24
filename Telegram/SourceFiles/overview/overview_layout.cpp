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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "overview/overview_layout.h"

#include "styles/style_overview.h"
#include "ui/filedialog.h"
#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"
#include "lang.h"
#include "mainwidget.h"
#include "application.h"
#include "fileuploader.h"
#include "mainwindow.h"
#include "playerwidget.h"
#include "audio.h"
#include "localstorage.h"

namespace Overview {
namespace Layout {

void ItemBase::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	App::hoveredLinkItem(active ? _parent : nullptr);
	Ui::repaintHistoryItem(_parent);
}

void ItemBase::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	App::pressedLinkItem(pressed ? _parent : nullptr);
	Ui::repaintHistoryItem(_parent);
}

void RadialProgressItem::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (p == _openl || p == _savel || p == _cancell) {
		a_iconOver.start(active ? 1 : 0);
		_a_iconOver.start();
	}
	ItemBase::clickHandlerActiveChanged(p, active);
}

void RadialProgressItem::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	ItemBase::clickHandlerPressedChanged(p, pressed);
}

void RadialProgressItem::setLinks(ClickHandlerPtr &&openl, ClickHandlerPtr &&savel, ClickHandlerPtr &&cancell) {
	_openl = std_::move(openl);
	_savel = std_::move(savel);
	_cancell = std_::move(cancell);
}

void RadialProgressItem::step_iconOver(float64 ms, bool timer) {
	float64 dt = ms / st::msgFileOverDuration;
	if (dt >= 1) {
		a_iconOver.finish();
		_a_iconOver.stop();
	} else if (!timer) {
		a_iconOver.update(dt, anim::linear);
	}
	if (timer && iconAnimated()) {
		Ui::repaintHistoryItem(_parent);
	}
}

void RadialProgressItem::step_radial(uint64 ms, bool timer) {
	if (timer) {
		Ui::repaintHistoryItem(_parent);
	} else {
		_radial->update(dataProgress(), dataFinished(), ms);
		if (!_radial->animating()) {
			checkRadialFinished();
		}
	}
}

void RadialProgressItem::ensureRadial() const {
	if (!_radial) {
		_radial = new RadialAnimation(animation(const_cast<RadialProgressItem*>(this), &RadialProgressItem::step_radial));
	}
}

void RadialProgressItem::checkRadialFinished() {
	if (_radial && !_radial->animating() && dataLoaded()) {
		delete _radial;
		_radial = nullptr;
	}
}

RadialProgressItem::~RadialProgressItem() {
	deleteAndMark(_radial);
}

void FileBase::setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const {
	_statusSize = newSize;
	if (_statusSize == FileStatusSizeReady) {
		_statusText = (duration >= 0) ? formatDurationAndSizeText(duration, fullSize) : (duration < -1 ? formatGifAndSizeText(fullSize) : formatSizeText(fullSize));
	} else if (_statusSize == FileStatusSizeLoaded) {
		_statusText = (duration >= 0) ? formatDurationText(duration) : (duration < -1 ? qsl("GIF") : formatSizeText(fullSize));
	} else if (_statusSize == FileStatusSizeFailed) {
		_statusText = lang(lng_attach_failed);
	} else if (_statusSize >= 0) {
		_statusText = formatDownloadText(_statusSize, fullSize);
	} else {
		_statusText = formatPlayedText(-_statusSize - 1, realDuration);
	}
}

Date::Date(const QDate &date, bool month)
: _date(date)
, _text(month ? langMonthFull(date) : langDayOfMonthFull(date)) {
	AddComponents(Info::Bit());
}

void Date::initDimensions() {
	_maxw = st::normalFont->width(_text);
	_minh = st::linksDateMargin.top() + st::normalFont->height + st::linksDateMargin.bottom() + st::linksBorder;
}

void Date::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const {
	if (clip.intersects(QRect(0, st::linksDateMargin.top(), _width, st::normalFont->height))) {
		p.setPen(st::linksDateColor);
		p.setFont(st::semiboldFont);
		p.drawTextLeft(0, st::linksDateMargin.top(), _width, _text);
	}
}

namespace {

void paintPhotoVideoCheck(Painter &p, int width, int height, bool selected) {
	int checkPosX = width - st::overviewPhotoCheck.width();
	int checkPosY = height - st::overviewPhotoCheck.height();
	if (selected) {
		p.fillRect(QRect(0, 0, width, height), st::overviewPhotoSelectOverlay);
		st::overviewPhotoChecked.paint(p, QPoint(checkPosX, checkPosY), width);
	} else {
		st::overviewPhotoCheck.paint(p, QPoint(checkPosX, checkPosY), width);
	}
}

} // namespace

Photo::Photo(PhotoData *photo, HistoryItem *parent) : ItemBase(parent)
, _data(photo)
, _link(new PhotoOpenClickHandler(photo))
, _goodLoaded(false) {
}

void Photo::initDimensions() {
	_maxw = 2 * st::overviewPhotoMinSize;
	_minh = _maxw;
}

int32 Photo::resizeGetHeight(int32 width) {
	width = qMin(width, _maxw);
	if (width != _width || width != _height) {
		_width = qMin(width, _maxw);
		_height = _width;
	}
	return _height;
}

void Photo::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const {
	bool good = _data->loaded(), selected = (selection == FullSelection);
	if (!good) {
		_data->medium->automaticLoad(_parent);
		good = _data->medium->loaded();
	}
	if ((good && !_goodLoaded) || _pix.width() != _width * cIntRetinaFactor()) {
		_goodLoaded = good;

		int32 size = _width * cIntRetinaFactor();
		if (_goodLoaded || _data->thumb->loaded()) {
			QImage img = (_data->loaded() ? _data->full : (_data->medium->loaded() ? _data->medium : _data->thumb))->pix().toImage();
			if (!_goodLoaded) {
				img = imageBlur(img);
			}
			if (img.width() == img.height()) {
				if (img.width() != size) {
					img = img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
				}
			} else if (img.width() > img.height()) {
				img = img.copy((img.width() - img.height()) / 2, 0, img.height(), img.height()).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			} else {
				img = img.copy(0, (img.height() - img.width()) / 2, img.width(), img.width()).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			}
			img.setDevicePixelRatio(cRetinaFactor());
			_data->forget();

			_pix = QPixmap::fromImage(img, Qt::ColorOnly);
		} else if (!_pix.isNull()) {
			_pix = QPixmap();
		}
	}

	if (_pix.isNull()) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoBg);
	} else {
		p.drawPixmap(0, 0, _pix);
	}
	if (selected || context->selecting) {
		paintPhotoVideoCheck(p, _width, _height, selected);
	}
}

void Photo::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	if (hasPoint(x, y)) {
		link = _link;
	}
}

Video::Video(DocumentData *video, HistoryItem *parent) : FileBase(parent)
, _data(video)
, _duration(formatDurationText(_data->duration()))
, _thumbLoaded(false) {
	setDocumentLinks(_data);
}

void Video::initDimensions() {
	_maxw = 2 * st::minPhotoSize;
	_minh = _maxw;
}

int32 Video::resizeGetHeight(int32 width) {
	_width = qMin(width, _maxw);
	_height = _width;
	return _height;
}

void Video::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const {
	bool selected = (selection == FullSelection), thumbLoaded = _data->thumb->loaded();

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();
	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(_data->progress());
		}
	}
	updateStatusText();
	bool radial = isRadialAnimation(context->ms);

	if ((thumbLoaded && !_thumbLoaded) || (_pix.width() != _width * cIntRetinaFactor())) {
		_thumbLoaded = thumbLoaded;

		if (_thumbLoaded && !_data->thumb->isNull()) {
			int32 size = _width * cIntRetinaFactor();
			QImage img = imageBlur(_data->thumb->pix().toImage());
			if (img.width() == img.height()) {
				if (img.width() != size) {
					img = img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
				}
			} else if (img.width() > img.height()) {
				img = img.copy((img.width() - img.height()) / 2, 0, img.height(), img.height()).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			} else {
				img = img.copy(0, (img.height() - img.width()) / 2, img.width(), img.width()).scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			}
			img.setDevicePixelRatio(cRetinaFactor());
			_data->forget();

			_pix = QPixmap::fromImage(img, Qt::ColorOnly);
		} else if (!_pix.isNull()) {
			_pix = QPixmap();
		}
	}

	if (_pix.isNull()) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoBg);
	} else {
		p.drawPixmap(0, 0, _pix);
	}

	if (selected) {
		p.fillRect(QRect(0, 0, _width, _height), st::overviewPhotoSelectOverlay);
	}

	if (!selected && !context->selecting && !loaded) {
		if (clip.intersects(QRect(0, _height - st::normalFont->height, _width, st::normalFont->height))) {
			int32 statusX = st::msgDateImgPadding.x(), statusY = _height - st::normalFont->height - st::msgDateImgPadding.y();
			int32 statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
			int32 statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
			statusX = _width - statusW + statusX;
			p.fillRect(rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg);
			p.setFont(st::normalFont);
			p.setPen(st::black);
			p.drawTextLeft(statusX, statusY, _width, _statusText, statusW - 2 * st::msgDateImgPadding.x());
		}
	}
	if (clip.intersects(QRect(0, 0, _width, st::normalFont->height))) {
		int32 statusX = st::msgDateImgPadding.x(), statusY = st::msgDateImgPadding.y();
		int32 statusW = st::normalFont->width(_duration) + 2 * st::msgDateImgPadding.x();
		int32 statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		p.fillRect(rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg);
		p.setFont(st::normalFont);
		p.setPen(st::black);
		p.drawTextLeft(statusX, statusY, _width, _duration, statusW - 2 * st::msgDateImgPadding.x());
	}

	QRect inner((_width - st::msgFileSize) / 2, (_height - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
	if (clip.intersects(inner)) {
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (_a_iconOver.animating()) {
			_a_iconOver.step(context->ms);
			float64 over = a_iconOver.current();
			p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
			p.setBrush(st::white);
		} else {
			bool over = ClickHandler::showAsActive(loaded ? _openl : (_data->loading() ? _cancell : _savel));
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}

		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawEllipse(inner);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		p.setOpacity((radial && loaded) ? _radial->opacity() : 1);
		style::sprite icon;
		if (radial) {
			icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
		} else if (loaded) {
			icon = (selected ? st::msgFileInPlaySelected : st::msgFileInPlay);
		} else {
			icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
		}
		p.drawSpriteCenter(inner, icon);
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_radial->draw(p, rinner, st::msgFileRadialLine, selected ? st::msgInBgSelected : st::msgInBg);
		}
	}
	if (selected || context->selecting) {
		paintPhotoVideoCheck(p, _width, _height, selected);
	}
}

void Video::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	bool loaded = _data->loaded();

	if (hasPoint(x, y)) {
		link = loaded ? _openl : (_data->loading() ? _cancell : _savel);
	}
}

void Video::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		statusSize = FileStatusSizeLoaded;
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		int32 status = statusSize, size = _data->size;
		if (statusSize >= 0 && statusSize < 0x7F000000) {
			size = status;
			status = FileStatusSizeReady;
		}
		setStatusSize(status, size, -1, 0);
		_statusSize = statusSize;
	}
}

Voice::Voice(DocumentData *voice, HistoryItem *parent) : FileBase(parent)
, _data(voice)
, _namel(new DocumentOpenClickHandler(_data)) {
	AddComponents(Info::Bit());

	t_assert(_data->voice() != 0);

	setDocumentLinks(_data);

	updateName();
	QString d = textcmdLink(1, textRichPrepare(langDateTime(date(_data->date))));
	TextParseOptions opts = { TextParseRichText, 0, 0, Qt::LayoutDirectionAuto };
	_details.setText(st::normalFont, lng_date_and_duration(lt_date, d, lt_duration, formatDurationText(_data->voice()->duration)), opts);
	_details.setLink(1, MakeShared<GoToMessageClickHandler>(parent));
}

void Voice::initDimensions() {
	_maxw = st::profileMaxWidth;
	_minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom() + st::lineWidth;
}

void Voice::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const {
	bool selected = (selection == FullSelection);

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();

	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(_data->progress());
		}
	}
	bool showPause = updateStatusText();
	int32 nameVersion = _parent->fromOriginal()->nameVersion;
	if (nameVersion > _nameVersion) {
		updateName();
	}
	bool radial = isRadialAnimation(context->ms);

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, datetop = -1;

	nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
	nameright = st::msgFilePadding.left();
	nametop = st::msgFileNameTop;
	statustop = st::msgFileStatusTop;

	if (selected) {
		p.fillRect(clip.intersected(QRect(0, 0, _width, _height)), st::msgInBgSelected);
	}

	QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, _width));
	if (clip.intersects(inner)) {
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgFileInBgSelected);
		} else if (_a_iconOver.animating()) {
			_a_iconOver.step(context->ms);
			float64 over = a_iconOver.current();
			p.setBrush(style::interpolate(st::msgFileInBg, st::msgFileInBgOver, over));
		} else {
			bool over = ClickHandler::showAsActive(loaded ? _openl : (_data->loading() ? _cancell : _openl));
			p.setBrush(over ? st::msgFileInBgOver : st::msgFileInBg);
		}

		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawEllipse(inner);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			style::color bg(selected ? st::msgInBgSelected : st::msgInBg);
			_radial->draw(p, rinner, st::msgFileRadialLine, bg);
		}

		style::sprite icon;
		if (showPause) {
			icon = selected ? st::msgFileInPauseSelected : st::msgFileInPause;
		} else if (_statusSize < 0 || _statusSize == FileStatusSizeLoaded) {
			icon = selected ? st::msgFileInPlaySelected : st::msgFileInPlay;
		} else if (_data->loading()) {
			icon = selected ? st::msgFileInCancelSelected : st::msgFileInCancel;
		} else {
			icon = selected ? st::msgFileInDownloadSelected : st::msgFileInDownload;
		}
		p.drawSpriteCenter(inner, icon);
	}

	int32 namewidth = _width - nameleft - nameright;

	if (clip.intersects(rtlrect(nameleft, nametop, namewidth, st::semiboldFont->height, _width))) {
		p.setPen(st::black);
		_name.drawLeftElided(p, nameleft, nametop, namewidth, _width);
	}

	if (clip.intersects(rtlrect(nameleft, statustop, namewidth, st::normalFont->height, _width))) {
		p.setFont(st::normalFont);
		p.setPen(selected ? st::mediaInFgSelected : st::mediaInFg);
		int32 unreadx = nameleft;
		if (_statusSize == FileStatusSizeLoaded || _statusSize == FileStatusSizeReady) {
			textstyleSet(&(selected ? st::mediaInStyleSelected : st::mediaInStyle));
			_details.drawLeftElided(p, nameleft, statustop, namewidth, _width);
			textstyleRestore();
			unreadx += _details.maxWidth();
		} else {
			int32 statusw = st::normalFont->width(_statusText);
			p.drawTextLeft(nameleft, statustop, _width, _statusText, statusw);
			unreadx += statusw;
		}
		if (_parent->isMediaUnread() && unreadx + st::mediaUnreadSkip + st::mediaUnreadSize <= _width) {
			p.setPen(Qt::NoPen);
			p.setBrush(selected ? st::msgFileInBgSelected : st::msgFileInBg);

			p.setRenderHint(QPainter::HighQualityAntialiasing, true);
			p.drawEllipse(rtlrect(unreadx + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, _width));
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);
		}
	}
}

void Voice::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	bool loaded = _data->loaded();

	bool showPause = updateStatusText();

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, datetop = 0;

	nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
	nameright = st::msgFilePadding.left();
	nametop = st::msgFileNameTop;
	statustop = st::msgFileStatusTop;

	QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, _width));
	if (inner.contains(x, y)) {
		link = loaded ? _openl : ((_data->loading() || _data->status == FileUploading) ? _cancell : _openl);
		return;
	}
	if (rtlrect(nameleft, statustop, _width - nameleft - nameright, st::normalFont->height, _width).contains(x, y)) {
		if (_statusSize == FileStatusSizeLoaded || _statusSize == FileStatusSizeReady) {
			auto textState = _details.getStateLeft(x - nameleft, y - statustop, _width, _width);
			link = textState.link;
			cursor = textState.uponSymbol ? HistoryInTextCursorState : HistoryDefaultCursorState;
		}
	}
	if (hasPoint(x, y) && !link && !_data->loading()) {
		link = _namel;
		return;
	}
}

void Voice::updateName() const {
	int32 version = 0;
	if (const HistoryMessageForwarded *fwd = _parent->Get<HistoryMessageForwarded>()) {
		if (_parent->fromOriginal()->isChannel()) {
			_name.setText(st::semiboldFont, lng_forwarded_channel(lt_channel, App::peerName(_parent->fromOriginal())), _textNameOptions);
		} else {
			_name.setText(st::semiboldFont, lng_forwarded(lt_user, App::peerName(_parent->fromOriginal())), _textNameOptions);
		}
	} else {
		_name.setText(st::semiboldFont, App::peerName(_parent->from()), _textNameOptions);
	}
	version = _parent->fromOriginal()->nameVersion;
	_nameVersion = version;
}

bool Voice::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->loaded()) {
		AudioMsgId playing;
		AudioPlayerState playingState = AudioPlayerStopped;
		int64 playingPosition = 0, playingDuration = 0;
		int32 playingFrequency = 0;
		if (audioPlayer()) {
			audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
		}

		if (playing == AudioMsgId(_data, _parent->fullId()) && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
			statusSize = -1 - (playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
			realDuration = playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);
			showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
		} else {
			statusSize = FileStatusSizeLoaded;
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize, _data->size, _data->voice()->duration, realDuration);
	}
	return showPause;
}

Document::Document(DocumentData *document, HistoryItem *parent) : FileBase(parent)
, _data(document)
, _msgl(new GoToMessageClickHandler(parent))
, _namel(new DocumentOpenClickHandler(_data))
, _thumbForLoaded(false)
, _name(documentName(_data))
, _date(langDateTime(date(_data->date)))
, _namew(st::semiboldFont->width(_name))
, _datew(st::normalFont->width(_date))
, _colorIndex(documentColorIndex(_data, _ext)) {
	AddComponents(Info::Bit());

	setDocumentLinks(_data);

	setStatusSize(FileStatusSizeReady, _data->size, _data->song() ? _data->song()->duration : -1, 0);

	if (withThumb()) {
		_data->thumb->load();
		int32 tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
		if (tw > th) {
			_thumbw = (tw * st::overviewFileSize) / th;
		} else {
			_thumbw = st::overviewFileSize;
		}
	} else {
		_thumbw = 0;
	}

	_extw = st::overviewFileExtFont->width(_ext);
	if (_extw > st::overviewFileSize - st::overviewFileExtPadding * 2) {
		_ext = st::overviewFileExtFont->elided(_ext, st::overviewFileSize - st::overviewFileExtPadding * 2, Qt::ElideMiddle);
		_extw = st::overviewFileExtFont->width(_ext);
	}
}

void Document::initDimensions() {
	_maxw = st::profileMaxWidth;
	if (_data->song()) {
		_minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	} else {
		_minh = st::overviewFilePadding.top() + st::overviewFileSize + st::overviewFilePadding.bottom() + st::lineWidth;
	}
}

void Document::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const {
	bool selected = (selection == FullSelection);

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded() || Local::willStickerImageLoad(_data->mediaKey()), displayLoading = _data->displayLoading();

	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(_data->progress());
		}
	}
	bool showPause = updateStatusText();
	bool radial = isRadialAnimation(context->ms);

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, datetop = -1;
	bool wthumb = withThumb();

	if (_data->song()) {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nameright = st::msgFilePadding.left();
		nametop = st::msgFileNameTop;
		statustop = st::msgFileStatusTop;

		if (selected) {
			p.fillRect(QRect(0, 0, _width, _height), st::msgInBgSelected);
		}

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, _width));
		if (clip.intersects(inner)) {
			p.setPen(Qt::NoPen);
			if (selected) {
				p.setBrush(st::msgFileInBgSelected);
			} else if (_a_iconOver.animating()) {
				_a_iconOver.step(context->ms);
				float64 over = a_iconOver.current();
				p.setBrush(style::interpolate(st::msgFileInBg, st::msgFileInBgOver, over));
			} else {
				bool over = ClickHandler::showAsActive(loaded ? _openl : (_data->loading() ? _cancell : _openl));
				p.setBrush(over ? st::msgFileInBgOver : st::msgFileInBg);
			}

			p.setRenderHint(QPainter::HighQualityAntialiasing);
			p.drawEllipse(inner);
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);

			if (radial) {
				QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
				style::color bg(selected ? st::msgInBgSelected : st::msgInBg);
				_radial->draw(p, rinner, st::msgFileRadialLine, bg);
			}

			style::sprite icon;
			if (showPause) {
				icon = selected ? st::msgFileInPauseSelected : st::msgFileInPause;
			} else if (loaded) {
				icon = selected ? st::msgFileInPlaySelected : st::msgFileInPlay;
			} else if (_data->loading()) {
				icon = selected ? st::msgFileInCancelSelected : st::msgFileInCancel;
			} else {
				icon = selected ? st::msgFileInDownloadSelected : st::msgFileInDownload;
			}
			p.drawSpriteCenter(inner, icon);
		}
	} else {
		nameleft = st::overviewFileSize + st::overviewFilePadding.right();
		nametop = st::linksBorder + st::overviewFileNameTop;
		statustop = st::linksBorder + st::overviewFileStatusTop;
		datetop = st::linksBorder + st::overviewFileDateTop;

		QRect border(rtlrect(nameleft, 0, _width - nameleft, st::linksBorder, _width));
		if (!context->isAfterDate && clip.intersects(border)) {
			p.fillRect(clip.intersected(border), st::linksBorderFg);
		}

		QRect rthumb(rtlrect(0, st::linksBorder + st::overviewFilePadding.top(), st::overviewFileSize, st::overviewFileSize, _width));
		if (clip.intersects(rthumb)) {
			if (wthumb) {
				if (_data->thumb->loaded()) {
					if (_thumb.isNull() || loaded != _thumbForLoaded) {
						_thumbForLoaded = loaded;
						ImagePixOptions options = ImagePixSmooth;
						if (!_thumbForLoaded) options |= ImagePixBlurred;
						_thumb = _data->thumb->pixNoCache(_thumbw * cIntRetinaFactor(), 0, options, st::overviewFileSize, st::overviewFileSize);
					}
					p.drawPixmap(rthumb.topLeft(), _thumb);
				} else {
					p.fillRect(rthumb, st::white);
				}
			} else {
				p.fillRect(rthumb, documentColor(_colorIndex));
				if (!radial && loaded && !_ext.isEmpty()) {
					p.setFont(st::overviewFileExtFont);
					p.setPen(st::black);
					p.drawText(rthumb.left() + (rthumb.width() - _extw) / 2, rthumb.top() + st::overviewFileExtTop + st::overviewFileExtFont->ascent, _ext);
				}
			}
			if (selected) {
				p.fillRect(rthumb, textstyleCurrent()->selectOverlay);
			}

			if (radial || (!loaded && !_data->loading())) {
				QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
				if (clip.intersects(inner)) {
					float64 radialOpacity = (radial && loaded && !_data->uploading()) ? _radial->opacity() : 1;
					p.setPen(Qt::NoPen);
					if (selected) {
						p.setBrush(wthumb ? st::msgDateImgBgSelected : documentSelectedColor(_colorIndex));
					} else if (_a_iconOver.animating()) {
						_a_iconOver.step(context->ms);
						float64 over = a_iconOver.current();
						if (wthumb) {
							p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
							p.setBrush(st::white);
						} else {
							p.setBrush(style::interpolate(documentDarkColor(_colorIndex), documentOverColor(_colorIndex), over));
						}
					} else {
						bool over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
						p.setBrush(over ? (wthumb ? st::msgDateImgBgOver : documentOverColor(_colorIndex)) : (wthumb ? st::msgDateImgBg : documentDarkColor(_colorIndex)));
					}
					p.setOpacity(radialOpacity * p.opacity());

					p.setRenderHint(QPainter::HighQualityAntialiasing);
					p.drawEllipse(inner);
					p.setRenderHint(QPainter::HighQualityAntialiasing, false);

					p.setOpacity(radialOpacity);
					style::sprite icon;
					if (loaded || _data->loading()) {
						icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
					} else {
						icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
					}
					p.drawSpriteCenter(inner, icon);
					if (radial) {
						p.setOpacity(1);

						QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
						_radial->draw(p, rinner, st::msgFileRadialLine, selected ? st::msgInBgSelected : st::msgInBg);
					}
				}
			}
			if (selected || context->selecting) {
				QRect check(rthumb.topLeft() + QPoint(rtl() ? 0 : (rthumb.width() - st::defaultCheckbox.diameter), rthumb.height() - st::defaultCheckbox.diameter), QSize(st::defaultCheckbox.diameter, st::defaultCheckbox.diameter));
				p.fillRect(check, selected ? st::overviewFileChecked : st::overviewFileCheck);
				st::defaultCheckbox.checkIcon.paint(p, QPoint(rthumb.width() - st::defaultCheckbox.diameter, rthumb.y() + rthumb.height() - st::defaultCheckbox.diameter), _width);
			}
		}
	}

	int32 namewidth = _width - nameleft - nameright;

	if (clip.intersects(rtlrect(nameleft, nametop, qMin(namewidth, _namew), st::semiboldFont->height, _width))) {
		p.setFont(st::semiboldFont);
		p.setPen(st::black);
		if (namewidth < _namew) {
			p.drawTextLeft(nameleft, nametop, _width, st::semiboldFont->elided(_name, namewidth));
		} else {
			p.drawTextLeft(nameleft, nametop, _width, _name, _namew);
		}
	}

	if (clip.intersects(rtlrect(nameleft, statustop, namewidth, st::normalFont->height, _width))) {
		p.setFont(st::normalFont);
		p.setPen(st::mediaInFg);
		p.drawTextLeft(nameleft, statustop, _width, _statusText);
	}
	if (datetop >= 0 && clip.intersects(rtlrect(nameleft, datetop, _datew, st::normalFont->height, _width))) {
		p.setFont(ClickHandler::showAsActive(_msgl) ? st::normalFont->underline() : st::normalFont);
		p.setPen(st::mediaInFg);
		p.drawTextLeft(nameleft, datetop, _width, _date, _datew);
	}
}

void Document::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	bool loaded = _data->loaded() || Local::willStickerImageLoad(_data->mediaKey());

	bool showPause = updateStatusText();

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, datetop = 0;
	bool wthumb = withThumb();

	if (_data->song()) {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nameright = st::msgFilePadding.left();
		nametop = st::msgFileNameTop;
		statustop = st::msgFileStatusTop;

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, _width));
		if (inner.contains(x, y)) {
			link = loaded ? _openl : ((_data->loading() || _data->status == FileUploading) ? _cancell : _openl);
			return;
		}
		if (hasPoint(x, y) && !_data->loading()) {
			link = _namel;
			return;
		}
	} else {
		nameleft = st::overviewFileSize + st::overviewFilePadding.right();
		nametop = st::linksBorder + st::overviewFileNameTop;
		statustop = st::linksBorder + st::overviewFileStatusTop;
		datetop = st::linksBorder + st::overviewFileDateTop;

		QRect rthumb(rtlrect(0, st::linksBorder + st::overviewFilePadding.top(), st::overviewFileSize, st::overviewFileSize, _width));

		if (rthumb.contains(x, y)) {
			link = loaded ? _openl : ((_data->loading() || _data->status == FileUploading) ? _cancell : _savel);
			return;
		}

		if (_data->status != FileUploadFailed) {
			if (rtlrect(nameleft, datetop, _datew, st::normalFont->height, _width).contains(x, y)) {
				link = _msgl;
				return;
			}
		}
		if (!_data->loading() && _data->isValid()) {
			if (loaded && rtlrect(0, st::linksBorder, nameleft, _height - st::linksBorder, _width).contains(x, y)) {
				link = _namel;
				return;
			}
			if (rtlrect(nameleft, nametop, qMin(_width - nameleft - nameright, _namew), st::semiboldFont->height, _width).contains(x, y)) {
				link = _namel;
				return;
			}
		}
	}
}

bool Document::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		if (_data->song()) {
			SongMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			int64 playingPosition = 0, playingDuration = 0;
			int32 playingFrequency = 0;
			if (audioPlayer()) {
				audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
			}

			if (playing == SongMsgId(_data, _parent->fullId()) && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				statusSize = -1 - (playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
				realDuration = playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);
				showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
			} else {
				statusSize = FileStatusSizeLoaded;
			}
			if (!showPause && (playing == SongMsgId(_data, _parent->fullId())) && App::main() && App::main()->player()->seekingSong(playing)) {
				showPause = true;
			}
		} else {
			statusSize = FileStatusSizeLoaded;
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize, _data->size, _data->song() ? _data->song()->duration : -1, realDuration);
	}
	return showPause;
}

Link::Link(HistoryMedia *media, HistoryItem *parent) : ItemBase(parent) {
	AddComponents(Info::Bit());

	const auto textWithEntities = _parent->originalText();
	QString mainUrl;

	auto text = textWithEntities.text;
	auto &entities = textWithEntities.entities;
	int32 from = 0, till = text.size(), lnk = entities.size();
	for_const (const auto &entity, entities) {
		auto type = entity.type();
		if (type != EntityInTextUrl && type != EntityInTextCustomUrl && type != EntityInTextEmail) {
			continue;
		}
		auto customUrl = entity.data(), entityText = text.mid(entity.offset(), entity.length());
		auto url = customUrl.isEmpty() ? entityText : customUrl;
		if (_links.isEmpty()) {
			mainUrl = url;
		}
		_links.push_back(LinkEntry(url, entityText));
	}
	while (lnk > 0 && till > from) {
		--lnk;
		const auto &entity = entities.at(lnk);
		auto type = entity.type();
		if (type != EntityInTextUrl && type != EntityInTextCustomUrl && type != EntityInTextEmail) {
			++lnk;
			break;
		}
		int32 afterLinkStart = entity.offset() + entity.length();
		if (till > afterLinkStart) {
			if (!QRegularExpression(qsl("^[,.\\s_=+\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$")).match(text.mid(afterLinkStart, till - afterLinkStart)).hasMatch()) {
				++lnk;
				break;
			}
		}
		till = entity.offset();
	}
	if (!lnk) {
		if (QRegularExpression(qsl("^[,.\\s\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$")).match(text.mid(from, till - from)).hasMatch()) {
			till = from;
		}
	}

	_page = (media && media->type() == MediaTypeWebPage) ? static_cast<HistoryWebPage*>(media)->webpage() : 0;
	if (_page) {
		mainUrl = _page->url;
		if (_page->document) {
			_photol.reset(new DocumentOpenClickHandler(_page->document));
		} else if (_page->photo) {
			if (_page->type == WebPageProfile || _page->type == WebPageVideo) {
				_photol = MakeShared<UrlClickHandler>(_page->url);
			} else if (_page->type == WebPagePhoto || _page->siteName == qstr("Twitter") || _page->siteName == qstr("Facebook")) {
				_photol.reset(new PhotoOpenClickHandler(_page->photo));
			} else {
				_photol = MakeShared<UrlClickHandler>(_page->url);
			}
		} else {
			_photol = MakeShared<UrlClickHandler>(_page->url);
		}
	} else if (!mainUrl.isEmpty()) {
		_photol = MakeShared<UrlClickHandler>(mainUrl);
	}
	if (from >= till && _page) {
		text = _page->description;
		from = 0;
		till = text.size();
	}
	if (till > from) {
		TextParseOptions opts = { TextParseMultiline, int32(st::linksMaxWidth), 3 * st::normalFont->height, Qt::LayoutDirectionAuto };
		_text.setText(st::normalFont, text.mid(from, till - from), opts);
	}
	int32 tw = 0, th = 0;
	if (_page && _page->photo) {
		if (!_page->photo->loaded()) _page->photo->thumb->load(false, false);

		tw = convertScale(_page->photo->thumb->width());
		th = convertScale(_page->photo->thumb->height());
	} else if (_page && _page->document) {
		if (!_page->document->thumb->loaded()) _page->document->thumb->load(false, false);

		tw = convertScale(_page->document->thumb->width());
		th = convertScale(_page->document->thumb->height());
	}
	if (tw > st::linksPhotoSize) {
		if (th > tw) {
			th = th * st::linksPhotoSize / tw;
			tw = st::linksPhotoSize;
		} else if (th > st::linksPhotoSize) {
			tw = tw * st::linksPhotoSize / th;
			th = st::linksPhotoSize;
		}
	}
	_pixw = qMax(tw, 1);
	_pixh = qMax(th, 1);

	if (_page) {
		_title = _page->title;
	}
	QVector<QStringRef> parts = mainUrl.splitRef('/');
	if (!parts.isEmpty()) {
		QStringRef domain = parts.at(0);
		if (parts.size() > 2 && domain.endsWith(':') && parts.at(1).isEmpty()) { // http:// and others
			domain = parts.at(2);
		}

		parts = domain.split('@').back().split('.');
		if (parts.size() > 1) {
			_letter = parts.at(parts.size() - 2).at(0).toUpper();
			if (_title.isEmpty()) {
				_title.reserve(parts.at(parts.size() - 2).size());
				_title.append(_letter).append(parts.at(parts.size() - 2).mid(1));
			}
		}
	}
	_titlew = st::semiboldFont->width(_title);
}

void Link::initDimensions() {
	_maxw = st::linksMaxWidth;
	_minh = 0;
	if (!_title.isEmpty()) {
		_minh += st::semiboldFont->height;
	}
	if (!_text.isEmpty()) {
		_minh += qMin(3 * st::normalFont->height, _text.countHeight(_maxw - st::linksPhotoSize - st::linksPhotoPadding));
	}
	_minh += _links.size() * st::normalFont->height;
	_minh = qMax(_minh, int32(st::linksPhotoSize)) + st::linksMargin.top() + st::linksMargin.bottom() + st::linksBorder;
}

int32 Link::resizeGetHeight(int32 width) {
	_width = qMin(width, _maxw);
	int32 w = _width - st::linksPhotoSize - st::linksPhotoPadding;
	for (int32 i = 0, l = _links.size(); i < l; ++i) {
		_links.at(i).lnk->setFullDisplayed(w >= _links.at(i).width);
	}

	_height = 0;
	if (!_title.isEmpty()) {
		_height += st::semiboldFont->height;
	}
	if (!_text.isEmpty()) {
		_height += qMin(3 * st::normalFont->height, _text.countHeight(_width - st::linksPhotoSize - st::linksPhotoPadding));
	}
	_height += _links.size() * st::normalFont->height;
	_height = qMax(_height, int32(st::linksPhotoSize)) + st::linksMargin.top() + st::linksMargin.bottom() + st::linksBorder;
	return _height;
}

void Link::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) const {
	int32 left = st::linksPhotoSize + st::linksPhotoPadding, top = st::linksMargin.top() + st::linksBorder, w = _width - left;
	if (clip.intersects(rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width))) {
		if (_page && _page->photo) {
			QPixmap pix;
			if (_page->photo->medium->loaded()) {
				pix = _page->photo->medium->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize);
			} else if (_page->photo->loaded()) {
				pix = _page->photo->full->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize);
			} else {
				pix = _page->photo->thumb->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize);
			}
			p.drawPixmapLeft(0, top, _width, pix);
		} else if (_page && _page->document && !_page->document->thumb->isNull()) {
			p.drawPixmapLeft(0, top, _width, _page->document->thumb->pixSingle(_pixw, _pixh, st::linksPhotoSize, st::linksPhotoSize));
		} else {
			int32 index = _letter.isEmpty() ? 0 : (_letter.at(0).unicode() % 4);
			switch (index) {
			case 0: App::roundRect(p, rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), st::msgFileRedColor, DocRedCorners); break;
			case 1: App::roundRect(p, rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), st::msgFileYellowColor, DocYellowCorners); break;
			case 2: App::roundRect(p, rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), st::msgFileGreenColor, DocGreenCorners); break;
			case 3: App::roundRect(p, rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), st::msgFileBlueColor, DocBlueCorners); break;
			}

			if (!_letter.isEmpty()) {
				p.setFont(st::linksLetterFont->f);
				p.setPen(st::black->p);
				p.drawText(rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), _letter, style::al_center);
			}
		}

		if (selection == FullSelection) {
			App::roundRect(p, rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width), st::overviewPhotoSelectOverlay, PhotoSelectOverlayCorners);
			st::overviewLinksChecked.paint(p, QPoint(st::linksPhotoSize - st::overviewLinksChecked.width(), top + st::linksPhotoSize - st::overviewLinksChecked.height()), _width);
		} else if (context->selecting) {
			st::overviewLinksCheck.paint(p, QPoint(st::linksPhotoSize - st::overviewLinksCheck.width(), top + st::linksPhotoSize - st::overviewLinksCheck.height()), _width);
		}
	}

	if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
		top += (st::linksPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
	} else {
		top = st::linksTextTop;
	}

	p.setPen(st::black);
	p.setFont(st::semiboldFont);
	if (!_title.isEmpty()) {
		if (clip.intersects(rtlrect(left, top, qMin(w, _titlew), st::semiboldFont->height, _width))) {
			p.drawTextLeft(left, top, _width, (w < _titlew) ? st::semiboldFont->elided(_title, w) : _title);
		}
		top += st::semiboldFont->height;
	}
	p.setFont(st::msgFont->f);
	if (!_text.isEmpty()) {
		int32 h = qMin(st::normalFont->height * 3, _text.countHeight(w));
		if (clip.intersects(rtlrect(left, top, w, h, _width))) {
			_text.drawLeftElided(p, left, top, w, _width, 3);
		}
		top += h;
	}

	p.setPen(st::btnYesColor);
	for (int32 i = 0, l = _links.size(); i < l; ++i) {
		if (clip.intersects(rtlrect(left, top, qMin(w, _links.at(i).width), st::normalFont->height, _width))) {
			p.setFont(ClickHandler::showAsActive(_links.at(i).lnk) ? st::normalFont->underline() : st::normalFont);
			p.drawTextLeft(left, top, _width, (w < _links.at(i).width) ? st::normalFont->elided(_links.at(i).text, w) : _links.at(i).text);
		}
		top += st::normalFont->height;
	}

	QRect border(rtlrect(left, 0, w, st::linksBorder, _width));
	if (!context->isAfterDate && clip.intersects(border)) {
		p.fillRect(clip.intersected(border), st::linksBorderFg);
	}
}

void Link::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	int32 left = st::linksPhotoSize + st::linksPhotoPadding, top = st::linksMargin.top() + st::linksBorder, w = _width - left;
	if (rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width).contains(x, y)) {
		link = _photol;
		return;
	}

	if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
		top += (st::linksPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
	}
	if (!_title.isEmpty()) {
		if (rtlrect(left, top, qMin(w, _titlew), st::semiboldFont->height, _width).contains(x, y)) {
			link = _photol;
			return;
		}
		top += st::webPageTitleFont->height;
	}
	if (!_text.isEmpty()) {
		top += qMin(st::normalFont->height * 3, _text.countHeight(w));
	}
	for (int32 i = 0, l = _links.size(); i < l; ++i) {
		if (rtlrect(left, top, qMin(w, _links.at(i).width), st::normalFont->height, _width).contains(x, y)) {
			link = _links.at(i).lnk;
			return;
		}
		top += st::normalFont->height;
	}
}

Link::LinkEntry::LinkEntry(const QString &url, const QString &text)
: text(text)
, width(st::normalFont->width(text))
, lnk(MakeShared<UrlClickHandler>(url)) {
}

} // namespace Layout
} // namespace Overview
