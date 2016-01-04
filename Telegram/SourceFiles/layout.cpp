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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "mainwidget.h"
#include "application.h"
#include "fileuploader.h"
#include "window.h"
#include "gui/filedialog.h"

#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"

#include "audio.h"
#include "localstorage.h"

TextParseOptions _textNameOptions = {
	0, // flags
	4096, // maxw
	1, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};
TextParseOptions _textDlgOptions = {
	0, // flags
	0, // maxw is style-dependent
	1, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};
TextParseOptions _historyTextOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags | TextParseMultiline | TextParseRichText | TextParseMono, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _historyBotOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands | TextParseMultiline | TextParseRichText | TextParseMono, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _historyTextNoMonoOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _historyBotNoMonoOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

const TextParseOptions &itemTextOptions(History *h, PeerData *f) {
	if ((h->peer->isUser() && h->peer->asUser()->botInfo) || (f->isUser() && f->asUser()->botInfo) || (h->peer->isChat() && h->peer->asChat()->botStatus >= 0) || (h->peer->isMegagroup() && h->peer->asChannel()->mgInfo->botStatus >= 0)) {
		return _historyBotOptions;
	}
	return _historyTextOptions;
}

const TextParseOptions &itemTextNoMonoOptions(History *h, PeerData *f) {
	if ((h->peer->isUser() && h->peer->asUser()->botInfo) || (f->isUser() && f->asUser()->botInfo) || (h->peer->isChat() && h->peer->asChat()->botStatus >= 0) || (h->peer->isMegagroup() && h->peer->asChannel()->mgInfo->botStatus >= 0)) {
		return _historyBotNoMonoOptions;
	}
	return _historyTextNoMonoOptions;
}

QString formatSizeText(qint64 size) {
	if (size >= 1024 * 1024) { // more than 1 mb
		qint64 sizeTenthMb = (size * 10 / (1024 * 1024));
		return QString::number(sizeTenthMb / 10) + '.' + QString::number(sizeTenthMb % 10) + qsl(" MB");
	}
	if (size >= 1024) {
		qint64 sizeTenthKb = (size * 10 / 1024);
		return QString::number(sizeTenthKb / 10) + '.' + QString::number(sizeTenthKb % 10) + qsl(" KB");
	}
	return QString::number(size) + qsl(" B");
}

QString formatDownloadText(qint64 ready, qint64 total) {
	QString readyStr, totalStr, mb;
	if (total >= 1024 * 1024) { // more than 1 mb
		qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
		readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
		totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
		mb = qsl("MB");
	} else if (total >= 1024) {
		qint64 readyKb = (ready / 1024), totalKb = (total / 1024);
		readyStr = QString::number(readyKb);
		totalStr = QString::number(totalKb);
		mb = qsl("KB");
	} else {
		readyStr = QString::number(ready);
		totalStr = QString::number(total);
		mb = qsl("B");
	}
	return lng_save_downloaded(lt_ready, readyStr, lt_total, totalStr, lt_mb, mb);
}

QString formatDurationText(qint64 duration) {
	qint64 hours = (duration / 3600), minutes = (duration % 3600) / 60, seconds = duration % 60;
	return (hours ? QString::number(hours) + ':' : QString()) + (minutes >= 10 ? QString() : QString('0')) + QString::number(minutes) + ':' + (seconds >= 10 ? QString() : QString('0')) + QString::number(seconds);
}

QString formatDurationAndSizeText(qint64 duration, qint64 size) {
	return lng_duration_and_size(lt_duration, formatDurationText(duration), lt_size, formatSizeText(size));
}

QString formatGifAndSizeText(qint64 size) {
	return lng_duration_and_size(lt_duration, qsl("GIF"), lt_size, formatSizeText(size));
}

QString formatPlayedText(qint64 played, qint64 duration) {
	return lng_duration_played(lt_played, formatDurationText(played), lt_duration, formatDurationText(duration));
}

QString documentName(DocumentData *document) {
	SongData *song = document->song();
	if (!song || (song->title.isEmpty() && song->performer.isEmpty())) {
		return document->name.isEmpty() ? qsl("Unknown File") : document->name;
	}

	if (song->performer.isEmpty()) return song->title;

	return song->performer + QString::fromUtf8(" \xe2\x80\x93 ") + (song->title.isEmpty() ? qsl("Unknown Track") : song->title);
}

int32 documentColorIndex(DocumentData *document, QString &ext) {
	int32 colorIndex = 0;

	QString name = document ? (document->name.isEmpty() ? (document->sticker() ? lang(lng_in_dlg_sticker) : qsl("Unknown File")) : document->name) : lang(lng_message_empty);
	name = name.toLower();
	int32 lastDot = name.lastIndexOf('.');
	QString mime = document ? document->mime.toLower() : QString();
	if (name.endsWith(qstr(".doc")) ||
		name.endsWith(qstr(".txt")) ||
		name.endsWith(qstr(".psd")) ||
		mime.startsWith(qstr("text/"))
		) {
		colorIndex = 0;
	} else if (
		name.endsWith(qstr(".xls")) ||
		name.endsWith(qstr(".csv"))
		) {
		colorIndex = 1;
	} else if (
		name.endsWith(qstr(".pdf")) ||
		name.endsWith(qstr(".ppt")) ||
		name.endsWith(qstr(".key"))
		) {
		colorIndex = 2;
	} else if (
		name.endsWith(qstr(".zip")) ||
		name.endsWith(qstr(".rar")) ||
		name.endsWith(qstr(".ai")) ||
		name.endsWith(qstr(".mp3")) ||
		name.endsWith(qstr(".mov")) ||
		name.endsWith(qstr(".avi"))
		) {
		colorIndex = 3;
	} else {
		QChar ch = (lastDot >= 0 && lastDot + 1 < name.size()) ? name.at(lastDot + 1) : (name.isEmpty() ? (mime.isEmpty() ? '0' : mime.at(0)) : name.at(0));
		colorIndex = (ch.unicode() % 4);
	}

	ext = document ? ((lastDot < 0 || lastDot + 2 > name.size()) ? name : name.mid(lastDot + 1)) : QString();

	return colorIndex;
}

style::color documentColor(int32 colorIndex) {
	static style::color colors[] = { st::msgFileBlueColor, st::msgFileGreenColor, st::msgFileRedColor, st::msgFileYellowColor };
	return colors[colorIndex & 3];
}

style::color documentDarkColor(int32 colorIndex) {
	static style::color colors[] = { st::msgFileBlueDark, st::msgFileGreenDark, st::msgFileRedDark, st::msgFileYellowDark };
	return colors[colorIndex & 3];
}

style::color documentOverColor(int32 colorIndex) {
	static style::color colors[] = { st::msgFileBlueOver, st::msgFileGreenOver, st::msgFileRedOver, st::msgFileYellowOver };
	return colors[colorIndex & 3];
}

style::color documentSelectedColor(int32 colorIndex) {
	static style::color colors[] = { st::msgFileBlueSelected, st::msgFileGreenSelected, st::msgFileRedSelected, st::msgFileYellowSelected };
	return colors[colorIndex & 3];
}

style::sprite documentCorner(int32 colorIndex) {
	static style::sprite corners[] = { st::msgFileBlue, st::msgFileGreen, st::msgFileRed, st::msgFileYellow };
	return corners[colorIndex & 3];
}

RoundCorners documentCorners(int32 colorIndex) {
	return RoundCorners(DocBlueCorners + (colorIndex & 3));
}

void LayoutRadialProgressItem::linkOver(const TextLinkPtr &lnk) {
	if (lnk == _openl || lnk == _savel || lnk == _cancell) {
		a_iconOver.start(1);
		_a_iconOver.start();
	}
}

void LayoutRadialProgressItem::linkOut(const TextLinkPtr &lnk) {
	if (lnk == _openl || lnk == _savel || lnk == _cancell) {
		a_iconOver.start(0);
		_a_iconOver.start();
	}
}

void LayoutRadialProgressItem::setLinks(ITextLink *openl, ITextLink *savel, ITextLink *cancell) {
	_openl.reset(openl);
	_savel.reset(savel);
	_cancell.reset(cancell);
}

void LayoutRadialProgressItem::step_iconOver(float64 ms, bool timer) {
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

void LayoutRadialProgressItem::step_radial(uint64 ms, bool timer) {
	if (timer) {
		Ui::repaintHistoryItem(_parent);
	} else {
		_radial->update(dataProgress(), dataFinished(), ms);
		if (!_radial->animating()) {
			checkRadialFinished();
		}
	}
}

void LayoutRadialProgressItem::ensureRadial() const {
	if (!_radial) {
		_radial = new RadialAnimation(animation(const_cast<LayoutRadialProgressItem*>(this), &LayoutRadialProgressItem::step_radial));
	}
}

void LayoutRadialProgressItem::checkRadialFinished() {
	if (_radial && !_radial->animating() && dataLoaded()) {
		delete _radial;
		_radial = 0;
	}
}

LayoutRadialProgressItem::~LayoutRadialProgressItem() {
	deleteAndMark(_radial);
}

void LayoutAbstractFileItem::setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const {
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

LayoutOverviewDate::LayoutOverviewDate(const QDate &date, bool month)
	: _date(date)
	, _text(month ? langMonthFull(date) : langDayOfMonthFull(date)) {
}

void LayoutOverviewDate::initDimensions() {
	_maxw = st::normalFont->width(_text);
	_minh = st::linksDateMargin.top() + st::normalFont->height + st::linksDateMargin.bottom() + st::linksBorder;
}

void LayoutOverviewDate::paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const {
	if (clip.intersects(QRect(0, st::linksDateMargin.top(), _width, st::normalFont->height))) {
		p.setPen(st::linksDateColor);
		p.setFont(st::semiboldFont);
		p.drawTextLeft(0, st::linksDateMargin.top(), _width, _text);
	}
}

LayoutOverviewPhoto::LayoutOverviewPhoto(PhotoData *photo, HistoryItem *parent) : LayoutMediaItem(parent)
, _data(photo)
, _link(new PhotoLink(photo))
, _goodLoaded(false) {

}

void LayoutOverviewPhoto::initDimensions() {
	_maxw = 2 * st::overviewPhotoMinSize;
	_minh = _maxw;
}

int32 LayoutOverviewPhoto::resizeGetHeight(int32 width) {
	width = qMin(width, _maxw);
	if (width != _width || width != _height) {
		_width = qMin(width, _maxw);
		_height = _width;
	}
	return _height;
}

void LayoutOverviewPhoto::paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const {
	bool good = _data->loaded();
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

	if (selection == FullSelection) {
		p.fillRect(QRect(0, 0, _width, _height), st::overviewPhotoSelectOverlay);
		p.drawSprite(QPoint(rtl() ? 0 : (_width - st::overviewPhotoChecked.pxWidth()), _height - st::overviewPhotoChecked.pxHeight()), st::overviewPhotoChecked);
	} else if (context->selecting) {
		p.drawSprite(QPoint(rtl() ? 0 : (_width - st::overviewPhotoCheck.pxWidth()), _height - st::overviewPhotoCheck.pxHeight()), st::overviewPhotoCheck);
	}
}

void LayoutOverviewPhoto::getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
	if (hasPoint(x, y)) {
		link = _link;
	}
}

LayoutOverviewVideo::LayoutOverviewVideo(VideoData *video, HistoryItem *parent) : LayoutAbstractFileItem(parent)
, _data(video)
, _duration(formatDurationText(_data->duration))
, _thumbLoaded(false) {
	setLinks(new VideoOpenLink(_data), new VideoSaveLink(_data), new VideoCancelLink(_data));
}

void LayoutOverviewVideo::initDimensions() {
	_maxw = 2 * st::minPhotoSize;
	_minh = _maxw;
}

int32 LayoutOverviewVideo::resizeGetHeight(int32 width) {
	_width = qMin(width, _maxw);
	_height = _width;
	return _height;
}

void LayoutOverviewVideo::paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const {
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
			QImage img = _data->thumb->pix().toImage();
			img = imageBlur(img);
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
			p.setPen(st::white);
			p.drawTextLeft(statusX, statusY, _width, _statusText, statusW - 2 * st::msgDateImgPadding.x());
		}
	}
	if (clip.intersects(QRect(0, 0, _width, st::normalFont->height))) {
		int32 statusX = st::msgDateImgPadding.x(), statusY = st::msgDateImgPadding.y();
		int32 statusW = st::normalFont->width(_duration) + 2 * st::msgDateImgPadding.x();
		int32 statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		p.fillRect(rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg);
		p.setFont(st::normalFont);
		p.setPen(st::white);
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
			p.setBrush(st::black);
		} else {
			bool over = textlnkDrawOver(loaded ? _openl : (_data->loading() ? _cancell : _savel));
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

	if (selected) {
		p.drawSprite(QPoint(rtl() ? 0 : (_width - st::overviewPhotoChecked.pxWidth()), _height - st::overviewPhotoChecked.pxHeight()), st::overviewPhotoChecked);
	} else if (context->selecting) {
		p.drawSprite(QPoint(rtl() ? 0 : (_width - st::overviewPhotoCheck.pxWidth()), _height - st::overviewPhotoCheck.pxHeight()), st::overviewPhotoCheck);
	}
}

void LayoutOverviewVideo::getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
	bool loaded = _data->loaded();

	if (hasPoint(x, y)) {
		link = loaded ? _openl : (_data->loading() ? _cancell : _savel);
	}
}

void LayoutOverviewVideo::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (!_data->already().isEmpty()) {
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

LayoutOverviewAudio::LayoutOverviewAudio(AudioData *audio, HistoryItem *parent) : LayoutAbstractFileItem(parent)
, _data(audio)
, _namel(new AudioOpenLink(_data)) {
	setLinks(new AudioOpenLink(_data), new AudioOpenLink(_data), new AudioCancelLink(_data));
	updateName();
	QString d = textcmdLink(1, textRichPrepare(langDateTime(date(_data->date))));
	TextParseOptions opts = { TextParseRichText, 0, 0, Qt::LayoutDirectionAuto };
	_details.setText(st::normalFont, lng_date_and_duration(lt_date, d, lt_duration, formatDurationText(_data->duration)), opts);
	_details.setLink(1, TextLinkPtr(new MessageLink(parent)));
}

void LayoutOverviewAudio::initDimensions() {
	_maxw = st::profileMaxWidth;
	_minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom() + st::lineWidth;
}

void LayoutOverviewAudio::paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const {
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
	int32 nameVersion = _parent->from()->nameVersion;
	if (HistoryForwarded *fwd = _parent->toHistoryForwarded()) nameVersion = fwd->fromForwarded()->nameVersion;
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
			bool over = textlnkDrawOver(loaded ? _openl : (_data->loading() ? _cancell : _openl));
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

void LayoutOverviewAudio::getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
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
			bool inText = false;
			_details.getStateLeft(link, inText, x - nameleft, y - statustop, _width, _width);
			cursor = inText ? HistoryInTextCursorState : HistoryDefaultCursorState;
		}
	}
	if (hasPoint(x, y) && !link && !_data->loading()) {
		link = _namel;
		return;
	}
}

void LayoutOverviewAudio::updateName() const {
	int32 version = 0;
	if (HistoryForwarded *fwd = _parent->toHistoryForwarded()) {
		_name.setText(st::semiboldFont, lang(lng_forwarded_from) + ' ' + App::peerName(fwd->fromForwarded()), _textNameOptions);
		version = fwd->fromForwarded()->nameVersion;
	} else {
		_name.setText(st::semiboldFont, App::peerName(_parent->from()), _textNameOptions);
		version = _parent->from()->nameVersion;
	}
	_nameVersion = version;
}

bool LayoutOverviewAudio::updateStatusText() const {
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

		if (playing.msgId == _parent->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
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
		setStatusSize(statusSize, _data->size, _data->duration, realDuration);
	}
	return showPause;
}

LayoutOverviewDocument::LayoutOverviewDocument(DocumentData *document, HistoryItem *parent) : LayoutAbstractFileItem(parent)
, _data(document)
, _msgl(new MessageLink(parent))
, _namel(new DocumentOpenLink(_data))
, _thumbForLoaded(false)
, _name(documentName(_data))
, _date(langDateTime(date(_data->date)))
, _namew(st::semiboldFont->width(_name))
, _datew(st::normalFont->width(_date))
, _colorIndex(documentColorIndex(_data, _ext)) {
	setLinks(new DocumentOpenLink(_data), new DocumentSaveLink(_data), new DocumentCancelLink(_data));

	setStatusSize(FileStatusSizeReady, _data->size, _data->song() ? _data->song()->duration : -1, 0);

	if (withThumb()) {
		_data->thumb->load();
		int32 tw = _data->thumb->width(), th = _data->thumb->height();
		if (tw > th) {
			_thumbw = (tw * st::overviewFileSize) / th;
		} else {
			_thumbw = st::overviewFileSize;
		}
	} else {
		_thumbw = 0;
	}

	_extw = st::semiboldFont->width(_ext);
	if (_extw > st::overviewFileSize - st::msgFileExtPadding * 2) {
		_ext = st::semiboldFont->elided(_ext, st::overviewFileSize - st::msgFileExtPadding * 2, Qt::ElideMiddle);
		_extw = st::semiboldFont->width(_ext);
	}
}

void LayoutOverviewDocument::initDimensions() {
	_maxw = st::profileMaxWidth;
	if (_data->song()) {
		_minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	} else {
		_minh = st::overviewFilePadding.top() + st::overviewFileSize + st::overviewFilePadding.bottom() + st::lineWidth;
	}
}

void LayoutOverviewDocument::paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const {
	bool selected = (selection == FullSelection);

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded() || Local::willStickerImageLoad(mediaKey(DocumentFileLocation, _data->dc, _data->id)), displayLoading = _data->displayLoading();

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
				bool over = textlnkDrawOver(loaded ? _openl : (_data->loading() ? _cancell : _openl));
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

		const OverviewPaintContext *pcontext = context->toOverviewPaintContext();
		t_assert(pcontext != 0);
		QRect border(rtlrect(nameleft, 0, _width - nameleft, st::linksBorder, _width));
		if (!pcontext->isAfterDate && clip.intersects(border)) {
			p.fillRect(clip.intersected(border), st::linksBorderFg);
		}

		QRect rthumb(rtlrect(0, st::linksBorder + st::overviewFilePadding.top(), st::overviewFileSize, st::overviewFileSize, _width));
		if (clip.intersects(rthumb)) {
			if (wthumb) {
				if (_data->thumb->loaded()) {
					if (_thumb.isNull() || loaded != _thumbForLoaded) {
						_thumbForLoaded = loaded;
						_thumb = _data->thumb->pixNoCache(_thumbw, 0, true, !_thumbForLoaded, false, st::overviewFileSize, st::overviewFileSize);
					}
					p.drawPixmap(rthumb.topLeft(), _thumb);
				} else {
					p.fillRect(rthumb, st::black);
				}
			} else {
				p.fillRect(rthumb, documentColor(_colorIndex));
				if (!radial && loaded && !_ext.isEmpty()) {
					p.setFont(st::semiboldFont);
					p.setPen(st::white);
					p.drawText(rthumb.left() + (rthumb.width() - _extw) / 2, rthumb.top() + st::msgFileExtTop + st::semiboldFont->ascent, _ext);
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
							p.setBrush(st::black);
						} else {
							p.setBrush(style::interpolate(documentDarkColor(_colorIndex), documentOverColor(_colorIndex), over));
						}
					} else {
						bool over = textlnkDrawOver(_data->loading() ? _cancell : _savel);
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
				p.drawSpriteCenter(check, st::defaultCheckbox.checkIcon);
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
		p.setFont(textlnkDrawOver(_msgl) ? st::normalFont->underline() : st::normalFont);
		p.setPen(st::mediaInFg);
		p.drawTextLeft(nameleft, datetop, _width, _date, _datew);
	}
}

void LayoutOverviewDocument::getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
	bool loaded = _data->loaded() || Local::willStickerImageLoad(mediaKey(DocumentFileLocation, _data->dc, _data->id));

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
		if (!_data->loading() && _data->access) {
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

bool LayoutOverviewDocument::updateStatusText() const {
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

			if (playing.msgId == _parent->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				statusSize = -1 - (playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
				realDuration = playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);
				showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
			} else {
				statusSize = FileStatusSizeLoaded;
			}
			if (!showPause && playing.msgId == _parent->fullId() && App::main() && App::main()->player()->seekingSong(playing)) {
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

namespace {
	ITextLink *linkFromUrl(const QString &url) {
		int32 at = url.indexOf('@'), slash = url.indexOf('/');
		if ((at > 0) && (slash < 0 || slash > at)) {
			return new EmailLink(url);
		}
		return new TextLink(url);
	}
}

LayoutOverviewLink::LayoutOverviewLink(HistoryMedia *media, HistoryItem *parent) : LayoutMediaItem(parent)
, _titlew(0)
, _page(0)
, _pixw(0)
, _pixh(0)
, _text(st::msgMinWidth) {
	QString text = _parent->originalText();
	EntitiesInText entities = _parent->originalEntities();

	int32 from = 0, till = text.size(), lnk = entities.size();
	for (int32 i = 0; i < lnk; ++i) {
		if (entities[i].type != EntityInTextUrl && entities[i].type != EntityInTextCustomUrl && entities[i].type != EntityInTextEmail) {
			continue;
		}
		QString u = entities[i].text, t = text.mid(entities[i].offset, entities[i].length);
		_links.push_back(Link(u.isEmpty() ? t : u, t));
	}
	while (lnk > 0 && till > from) {
		--lnk;
		if (entities[lnk].type != EntityInTextUrl && entities[lnk].type != EntityInTextCustomUrl && entities[lnk].type != EntityInTextEmail) {
			++lnk;
			break;
		}
		int32 afterLinkStart = entities[lnk].offset + entities[lnk].length;
		if (till > afterLinkStart) {
			if (!QRegularExpression(qsl("^[,.\\s_=+\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$")).match(text.mid(afterLinkStart, till - afterLinkStart)).hasMatch()) {
				++lnk;
				break;
			}
		}
		till = entities[lnk].offset;
	}
	if (!lnk) {
		if (QRegularExpression(qsl("^[,.\\s\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$")).match(text.mid(from, till - from)).hasMatch()) {
			till = from;
		}
	}

	_page = (media && media->type() == MediaTypeWebPage) ? static_cast<HistoryWebPage*>(media)->webpage() : 0;
	if (_page) {
		if (_page->doc) {
			_photol = TextLinkPtr(new DocumentOpenLink(_page->doc));
		} else if (_page->photo) {
			if (_page->type == WebPageProfile || _page->type == WebPageVideo) {
				_photol = TextLinkPtr(linkFromUrl(_page->url));
			} else if (_page->type == WebPagePhoto || _page->siteName == qstr("Twitter") || _page->siteName == qstr("Facebook")) {
				_photol = TextLinkPtr(new PhotoLink(_page->photo));
			} else {
				_photol = TextLinkPtr(linkFromUrl(_page->url));
			}
		} else {
			_photol = TextLinkPtr(linkFromUrl(_page->url));
		}
	} else if (!_links.isEmpty()) {
		_photol = TextLinkPtr(linkFromUrl(_links.at(0).lnk->text()));
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
	} else if (_page && _page->doc) {
		if (!_page->doc->thumb->loaded()) _page->doc->thumb->load(false, false);

		tw = convertScale(_page->doc->thumb->width());
		th = convertScale(_page->doc->thumb->height());
	}
	if (tw > st::dlgPhotoSize) {
		if (th > tw) {
			th = th * st::dlgPhotoSize / tw;
			tw = st::dlgPhotoSize;
		} else if (th > st::dlgPhotoSize) {
			tw = tw * st::dlgPhotoSize / th;
			th = st::dlgPhotoSize;
		}
	}
	_pixw = qMax(tw, 1);
	_pixh = qMax(th, 1);

	if (_page) {
		_title = _page->title;
	}
    QString url(_page ? _page->url : (_links.isEmpty() ? QString() : _links.at(0).lnk->text()));
    QVector<QStringRef> parts = url.splitRef('/');
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

void LayoutOverviewLink::initDimensions() {
	_maxw = st::linksMaxWidth;
	_minh = 0;
	if (!_title.isEmpty()) {
		_minh += st::semiboldFont->height;
	}
	if (!_text.isEmpty()) {
		_minh += qMin(3 * st::normalFont->height, _text.countHeight(_maxw - st::dlgPhotoSize - st::dlgPhotoPadding));
	}
	_minh += _links.size() * st::normalFont->height;
	_minh = qMax(_minh, int32(st::dlgPhotoSize)) + st::linksMargin.top() + st::linksMargin.bottom() + st::linksBorder;
}

int32 LayoutOverviewLink::resizeGetHeight(int32 width) {
	_width = qMin(width, _maxw);
	int32 w = _width - st::dlgPhotoSize - st::dlgPhotoPadding;
	for (int32 i = 0, l = _links.size(); i < l; ++i) {
		_links.at(i).lnk->setFullDisplayed(w >= _links.at(i).width);
	}

	_height = 0;
	if (!_title.isEmpty()) {
		_height += st::semiboldFont->height;
	}
	if (!_text.isEmpty()) {
		_height += qMin(3 * st::normalFont->height, _text.countHeight(_width - st::dlgPhotoSize - st::dlgPhotoPadding));
	}
	_height += _links.size() * st::normalFont->height;
	_height = qMax(_height, int32(st::dlgPhotoSize)) + st::linksMargin.top() + st::linksMargin.bottom() + st::linksBorder;
	return _height;
}

void LayoutOverviewLink::paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const {
	int32 left = st::dlgPhotoSize + st::dlgPhotoPadding, top = st::linksMargin.top() + st::linksBorder, w = _width - left;
	if (clip.intersects(rtlrect(0, top, st::dlgPhotoSize, st::dlgPhotoSize, _width))) {
		if (_page && _page->photo) {
			QPixmap pix;
			if (_page->photo->loaded()) {
				pix = _page->photo->full->pixSingle(_pixw, _pixh, st::dlgPhotoSize, st::dlgPhotoSize);
			} else if (_page->photo->medium->loaded()) {
				pix = _page->photo->medium->pixSingle(_pixw, _pixh, st::dlgPhotoSize, st::dlgPhotoSize);
			} else {
				pix = _page->photo->thumb->pixSingle(_pixw, _pixh, st::dlgPhotoSize, st::dlgPhotoSize);
			}
			p.drawPixmapLeft(0, top, _width, pix);
		} else if (_page && _page->doc && !_page->doc->thumb->isNull()) {
			p.drawPixmapLeft(0, top, _width, _page->doc->thumb->pixSingle(_pixw, _pixh, st::dlgPhotoSize, st::dlgPhotoSize));
		} else {
			int32 index = _letter.isEmpty() ? 0 : (_letter.at(0).unicode() % 4);
			switch (index) {
			case 0: App::roundRect(p, rtlrect(0, top, st::dlgPhotoSize, st::dlgPhotoSize, _width), st::msgFileRedColor, DocRedCorners); break;
			case 1: App::roundRect(p, rtlrect(0, top, st::dlgPhotoSize, st::dlgPhotoSize, _width), st::msgFileYellowColor, DocYellowCorners); break;
			case 2: App::roundRect(p, rtlrect(0, top, st::dlgPhotoSize, st::dlgPhotoSize, _width), st::msgFileGreenColor, DocGreenCorners); break;
			case 3: App::roundRect(p, rtlrect(0, top, st::dlgPhotoSize, st::dlgPhotoSize, _width), st::msgFileBlueColor, DocBlueCorners); break;
			}

			if (!_letter.isEmpty()) {
				p.setFont(st::linksLetterFont->f);
				p.setPen(st::white->p);
				p.drawText(rtlrect(0, top, st::dlgPhotoSize, st::dlgPhotoSize, _width), _letter, style::al_center);
			}
		}

		if (selection == FullSelection) {
			App::roundRect(p, rtlrect(0, top, st::dlgPhotoSize, st::dlgPhotoSize, _width), st::overviewPhotoSelectOverlay, PhotoSelectOverlayCorners);
			p.drawSpriteLeft(QPoint(st::dlgPhotoSize - st::linksPhotoCheck.pxWidth(), top + st::dlgPhotoSize - st::linksPhotoCheck.pxHeight()), _width, st::linksPhotoChecked);
		} else if (context->selecting) {
			p.drawSpriteLeft(QPoint(st::dlgPhotoSize - st::linksPhotoCheck.pxWidth(), top + st::dlgPhotoSize - st::linksPhotoCheck.pxHeight()), _width, st::linksPhotoCheck);
		}
	}

	if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
		top += (st::dlgPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
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
			p.setFont(textlnkDrawOver(_links.at(i).lnk) ? st::normalFont->underline() : st::normalFont);
			p.drawTextLeft(left, top, _width, (w < _links.at(i).width) ? st::normalFont->elided(_links.at(i).text, w) : _links.at(i).text);
		}
		top += st::normalFont->height;
	}

	const OverviewPaintContext *pcontext = context->toOverviewPaintContext();
	t_assert(pcontext != 0);
	QRect border(rtlrect(left, 0, w, st::linksBorder, _width));
	if (!pcontext->isAfterDate && clip.intersects(border)) {
		p.fillRect(clip.intersected(border), st::linksBorderFg);
	}
}

void LayoutOverviewLink::getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
	int32 left = st::dlgPhotoSize + st::dlgPhotoPadding, top = st::linksMargin.top() + st::linksBorder, w = _width - left;
	if (rtlrect(0, top, st::dlgPhotoSize, st::dlgPhotoSize, _width).contains(x, y)) {
		link = _photol;
		return;
	}

	if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
		top += (st::dlgPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
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

LayoutOverviewLink::Link::Link(const QString &url, const QString &text)
: text(text)
, width(st::normalFont->width(text))
, lnk(linkFromUrl(url)) {
}

LayoutInlineItem::LayoutInlineItem(InlineResult *result, DocumentData *doc, PhotoData *photo)
: _result(result)
, _doc(doc)
, _photo(photo)
, _position(0) {
}

void LayoutInlineItem::setPosition(int32 position) {
	_position = position;
}

int32 LayoutInlineItem::position() const {
	return _position;
}

InlineResult *LayoutInlineItem::result() const {
	return _result;
}

DocumentData *LayoutInlineItem::document() const {
	return _doc;
}

PhotoData *LayoutInlineItem::photo() const {
	return _photo;
}

void LayoutInlineItem::preload() {
	if (_result) {
		if (_result->photo) {
			_result->photo->thumb->load();
		} else if (_result->doc) {
			_result->doc->thumb->load();
		} else if (!_result->thumb->isNull()) {
			_result->thumb->load();
		}
	} else if (_doc) {
		_doc->thumb->load();
	} else if (_photo) {
		_photo->medium->load();
	}
}

void LayoutInlineItem::update() {
	if (_position >= 0) {
		Ui::repaintInlineItem(this);
	}
}

LayoutInlineGif::LayoutInlineGif(InlineResult *result, DocumentData *doc, bool saved) : LayoutInlineItem(result, doc, 0)
, _state(0)
, _gif(0)
, _send(new SendInlineItemLink())
, _delete((doc && saved) ? new DeleteSavedGifLink(doc) : 0)
, _animation(0) {
}

void LayoutInlineGif::initDimensions() {
	int32 w = content_width(), h = content_height();
	if (w <= 0 || h <= 0) {
		_maxw = 0;
	} else {
		w = w * st::inlineMediaHeight / h;
		_maxw = qMax(w, int32(st::inlineResultsMinWidth));
	}
	_minh = st::inlineMediaHeight + st::inlineResultsSkip;
}

void LayoutInlineGif::setPosition(int32 position) {
	LayoutInlineItem::setPosition(position);
	if (_position < 0) {
		if (gif()) delete _gif;
		_gif = 0;
	}
}

void DeleteSavedGifLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;

	int32 index = cSavedGifs().indexOf(_data);
	if (index >= 0) {
		cRefSavedGifs().remove(index);
		Local::writeSavedGifs();

		MTP::send(MTPmessages_SaveGif(MTP_inputDocument(MTP_long(_data->id), MTP_long(_data->access)), MTP_bool(true)));
	}
	if (App::main()) emit App::main()->savedGifsUpdated();
}

void LayoutInlineGif::paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const {
	content_automaticLoad();

	bool loaded = content_loaded(), loading = content_loading(), displayLoading = content_displayLoading();
	if (loaded && !gif() && _gif != BadClipReader) {
		LayoutInlineGif *that = const_cast<LayoutInlineGif*>(this);
		that->_gif = new ClipReader(content_location(), content_data(), func(that, &LayoutInlineGif::clipCallback));
		if (gif()) _gif->setAutoplay();
	}

	bool animating = (gif() && _gif->started());
	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(content_progress());
		}
	}
	bool radial = isRadialAnimation(context->ms);

	int32 height = st::inlineMediaHeight;
	QSize frame = countFrameSize();

	QRect r(0, 0, _width, height);
	if (animating) {
		if (!_thumb.isNull()) _thumb = QPixmap();
		const InlinePaintContext *ctx = context->toInlinePaintContext();
		t_assert(ctx);
		p.drawPixmap(r.topLeft(), _gif->current(frame.width(), frame.height(), _width, height, ctx->paused ? 0 : context->ms));
	} else {
		prepareThumb(_width, height, frame);
		if (_thumb.isNull()) {
			p.fillRect(r, st::overviewPhotoBg);
		} else {
			p.drawPixmap(r.topLeft(), _thumb);
		}
	}

	if (radial || (!_gif && !loaded && !loading) || (_gif == BadClipReader)) {
		float64 radialOpacity = (radial && loaded) ? _animation->radial.opacity() : 1;
		if (_animation && _animation->_a_over.animating(context->ms)) {
			float64 over = _animation->_a_over.current();
			p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
			p.fillRect(r, st::black);
		} else {
			p.fillRect(r, (_state & StateOver) ? st::msgDateImgBgOver : st::msgDateImgBg);
		}
		p.setOpacity(radialOpacity * p.opacity());

		p.setOpacity(radialOpacity);
		style::sprite icon;
		if (loaded && !radial) {
			icon = st::msgFileInPlay;
		} else if (radial || loading) {
			icon = st::msgFileInCancel;
		} else {
			icon = st::msgFileInDownload;
		}
		QRect inner((_width - st::msgFileSize) / 2, (height - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.drawSpriteCenter(inner, icon);
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, st::msgInBg);
		}
	}

	if (_delete && (_state & StateOver)) {
		float64 deleteOver = _a_deleteOver.current(context->ms, (_state & StateDeleteOver) ? 1 : 0);
		QPoint deletePos = QPoint(_width - st::stickerPanDelete.pxWidth(), 0);
		p.setOpacity(deleteOver + (1 - deleteOver) * st::stickerPanDeleteOpacity);
		p.drawSpriteLeft(deletePos, _width, st::stickerPanDelete);
		p.setOpacity(1);
	}
}

void LayoutInlineGif::getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
	if (x >= 0 && x < _width && y >= 0 && y < st::inlineMediaHeight) {
		if (_delete && (rtl() ? _width - x : x) >= _width - st::stickerPanDelete.pxWidth() && y < st::stickerPanDelete.pxHeight()) {
			link = _delete;
		} else {
			link = _send;
		}
	}
}

void LayoutInlineGif::linkOver(const TextLinkPtr &link) {
	if (_delete && link == _delete) {
		if (!(_state & StateDeleteOver)) {
			EnsureAnimation(_a_deleteOver, 0, func(this, &LayoutInlineGif::update));
			_state |= StateDeleteOver;
			_a_deleteOver.start(1, st::stickersRowDuration);
		}
	}
	if ((_delete && link == _delete) || link == _send) {
		if (!content_loaded()) {
			ensureAnimation();
			if (!(_state & StateOver)) {
				EnsureAnimation(_animation->_a_over, 0, func(this, &LayoutInlineGif::update));
				_animation->_a_over.start(1, st::stickersRowDuration);
			}
		}
		_state |= StateOver;
	}
}

void LayoutInlineGif::linkOut(const TextLinkPtr &link) {
	if (_delete && link == _delete) {
		if (_state & StateDeleteOver) {
			update();
			EnsureAnimation(_a_deleteOver, 1, func(this, &LayoutInlineItem::update));
			_state &= ~StateDeleteOver;
			_a_deleteOver.start(0, st::stickersRowDuration);
		}
	}
	if ((_delete && link == _delete) || link == _send) {
		if (!content_loaded()) {
			ensureAnimation();
			if (_state & StateOver) {
				EnsureAnimation(_animation->_a_over, 1, func(this, &LayoutInlineItem::update));
				_animation->_a_over.start(0, st::stickersRowDuration);
			}
		}
		_state &= ~StateOver;
	}
}

QSize LayoutInlineGif::countFrameSize() const {
	bool animating = (gif() && _gif->ready());
	int32 framew = animating ? _gif->width() : content_width(), frameh = animating ? _gif->height() : content_height(), height = st::inlineMediaHeight;
	if (framew * height > frameh * _width) {
		if (framew < st::maxStickerSize || frameh > height) {
			if (frameh > height || (framew * height / frameh) <= st::maxStickerSize) {
				framew = framew * height / frameh;
				frameh = height;
			} else {
				frameh = int32(frameh * st::maxStickerSize) / framew;
				framew = st::maxStickerSize;
			}
		}
	} else {
		if (frameh < st::maxStickerSize || framew > _width) {
			if (framew > _width || (frameh * _width / framew) <= st::maxStickerSize) {
				frameh = frameh * _width / framew;
				framew = _width;
			} else {
				framew = int32(framew * st::maxStickerSize) / frameh;
				frameh = st::maxStickerSize;
			}
		}
	}
	return QSize(framew, frameh);
}

LayoutInlineGif::~LayoutInlineGif() {
	if (gif()) deleteAndMark(_gif);
	deleteAndMark(_animation);
}

void LayoutInlineGif::prepareThumb(int32 width, int32 height, const QSize &frame) const {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	if (doc && !doc->thumb->isNull()) {
		if (doc->thumb->loaded()) {
			if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
				_thumb = doc->thumb->pixNoCache(frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), true, false, false, width, height);
			}
		} else {
			doc->thumb->load();
		}
	} else if (_result && !_result->thumb_url.isEmpty()) {
		if (_result->thumb->loaded()) {
			if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
				_thumb = _result->thumb->pixNoCache(frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), true, false, false, width, height);
			}
		} else {
			_result->thumb->load();
		}
	}
}

void LayoutInlineGif::ensureAnimation() const {
	if (!_animation) {
		_animation = new AnimationData(animation(const_cast<LayoutInlineGif*>(this), &LayoutInlineGif::step_radial));
	}
}

bool LayoutInlineGif::isRadialAnimation(uint64 ms) const {
	if (!_animation || !_animation->radial.animating()) return false;

	_animation->radial.step(ms);
	return _animation && _animation->radial.animating();
}

void LayoutInlineGif::step_radial(uint64 ms, bool timer) {
	if (timer) {
		update();
	} else {
		_animation->radial.update(content_progress(), !content_loading() || content_loaded(), ms);
		if (!_animation->radial.animating() && content_loaded()) {
			delete _animation;
			_animation = 0;
		}
	}
}

void LayoutInlineGif::clipCallback(ClipReaderNotification notification) {
	switch (notification) {
	case ClipReaderReinit: {
		if (gif()) {
			if (_gif->state() == ClipError) {
				delete _gif;
				_gif = BadClipReader;
				content_forget();
			} else if (_gif->ready() && !_gif->started()) {
				int32 height = st::inlineMediaHeight;
				QSize frame = countFrameSize();
				_gif->start(frame.width(), frame.height(), _width, height, false);
			} else if (_gif->paused() && !Ui::isInlineItemVisible(this)) {
				delete _gif;
				_gif = 0;
				content_forget();
			}
		}

		update();
	} break;

	case ClipReaderRepaint: {
		if (gif() && !_gif->currentDisplayed()) {
			update();
		}
	} break;
	}
}

int32 LayoutInlineGif::content_width() const {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	if (doc) {
		if (doc->dimensions.width() > 0) {
			return doc->dimensions.width();
		}
		if (!doc->thumb->isNull()) {
			return doc->thumb->width();
		}
	} else if (_result) {
		return _result->width;
	}
	return 0;
}

int32 LayoutInlineGif::content_height() const {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	if (doc) {
		if (doc->dimensions.height() > 0) {
			return doc->dimensions.height();
		}
		if (!doc->thumb->isNull()) {
			return doc->thumb->height();
		}
	} else if (_result) {
		return _result->height;
	}
	return 0;
}

bool LayoutInlineGif::content_loading() const {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	return doc ? doc->loading() : _result->loading();
}

bool LayoutInlineGif::content_displayLoading() const {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	return doc ? doc->displayLoading() : _result->displayLoading();
}

bool LayoutInlineGif::content_loaded() const {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	return doc ? doc->loaded() : _result->loaded();
}

float64 LayoutInlineGif::content_progress() const {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	return doc ? doc->progress() : _result->progress();
}

void LayoutInlineGif::content_automaticLoad() const {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	if (doc) {
		doc->automaticLoad(0);
	} else {
		_result->automaticLoadGif();
	}
}

void LayoutInlineGif::content_forget() {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	if (doc) {
		doc->forget();
	} else {
		_result->forget();
	}
}

FileLocation LayoutInlineGif::content_location() const {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	return doc ? doc->location() : FileLocation();
}

QByteArray LayoutInlineGif::content_data() const {
	DocumentData *doc = _doc ? _doc : (_result ? _result->doc : 0);
	return doc ? doc->data() : _result->data();
}

LayoutInlinePhoto::LayoutInlinePhoto(InlineResult *result, PhotoData *photo) : LayoutInlineItem(result, 0, photo)
, _send(new SendInlineItemLink())
, _thumbLoaded(false) {
}

void LayoutInlinePhoto::initDimensions() {
	int32 w = content_width(), h = content_height();
	if (w <= 0 || h <= 0) {
		_maxw = 0;
	} else {
		w = w * st::inlineMediaHeight / h;
		_maxw = qMax(w, int32(st::inlineResultsMinWidth));
	}
	_minh = st::inlineMediaHeight + st::inlineResultsSkip;
}

void LayoutInlinePhoto::paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const {
	bool loaded = content_loaded();

	int32 height = st::inlineMediaHeight;
	QSize frame = countFrameSize();

	QRect r(0, 0, _width, height);

	prepareThumb(_width, height, frame);
	if (_thumb.isNull()) {
		p.fillRect(r, st::overviewPhotoBg);
	} else {
		p.drawPixmap(r.topLeft(), _thumb);
	}
}

void LayoutInlinePhoto::getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
	if (x >= 0 && x < _width && y >= 0 && y < st::inlineMediaHeight) {
		link = _send;
	}
}

QSize LayoutInlinePhoto::countFrameSize() const {
	int32 framew = content_width(), frameh = content_height(), height = st::inlineMediaHeight;
	if (framew * height > frameh * _width) {
		if (framew < st::maxStickerSize || frameh > height) {
			if (frameh > height || (framew * height / frameh) <= st::maxStickerSize) {
				framew = framew * height / frameh;
				frameh = height;
			} else {
				frameh = int32(frameh * st::maxStickerSize) / framew;
				framew = st::maxStickerSize;
			}
		}
	} else {
		if (frameh < st::maxStickerSize || framew > _width) {
			if (framew > _width || (frameh * _width / framew) <= st::maxStickerSize) {
				frameh = frameh * _width / framew;
				framew = _width;
			} else {
				framew = int32(framew * st::maxStickerSize) / frameh;
				frameh = st::maxStickerSize;
			}
		}
	}
	return QSize(framew, frameh);
}

void LayoutInlinePhoto::prepareThumb(int32 width, int32 height, const QSize &frame) const {
	PhotoData *photo = _photo ? _photo : (_result ? _result->photo : 0);
	if (photo) {
		if (photo->medium->loaded()) {
			if (!_thumbLoaded || _thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
				_thumb = photo->medium->pixNoCache(frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), true, false, false, width, height);
			}
			_thumbLoaded = true;
		} else {
			if (photo->thumb->loaded()) {
				if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
					_thumb = photo->thumb->pixNoCache(frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), true, false, false, width, height);
				}
			}
			photo->medium->load();
		}
	} else {
		if (_result->thumb->loaded()) {
			if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
				_thumb = _result->thumb->pixNoCache(frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), true, false, false, width, height);
			}
		} else {
			_result->thumb->load();
		}
	}
}

int32 LayoutInlinePhoto::content_width() const {
	PhotoData *photo = _photo ? _photo : (_result ? _result->photo : 0);
	if (photo) {
		return photo->full->width();
	} else if (_result) {
		return _result->width;
	}
	return 0;
}

int32 LayoutInlinePhoto::content_height() const {
	PhotoData *photo = _photo ? _photo : (_result ? _result->photo : 0);
	if (photo) {
		return photo->full->height();
	} else if (_result) {
		return _result->height;
	}
	return 0;
}

bool LayoutInlinePhoto::content_loaded() const {
	PhotoData *photo = _photo ? _photo : (_result ? _result->photo : 0);
	return photo ? photo->loaded() : _result->loaded();
}

void LayoutInlinePhoto::content_forget() {
	PhotoData *photo = _photo ? _photo : (_result ? _result->photo : 0);
	if (photo) {
		photo->forget();
	} else {
		_result->forget();
	}
}

LayoutInlineWebVideo::LayoutInlineWebVideo(InlineResult *result) : LayoutInlineItem(result, 0, 0)
, _send(new SendInlineItemLink())
, _link(result->content_url.isEmpty() ? 0 : linkFromUrl(result->content_url))
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
	if (_result->duration) {
		_duration = formatDurationText(_result->duration);
	}
}

void LayoutInlineWebVideo::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int32 textWidth = _maxw - (_result->thumb->isNull() ? 0 : (st::inlineThumbSize + st::inlineThumbSkip));
	TextParseOptions titleOpts = { 0, _maxw, 2 * st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldFont, textOneLine(_result->title), titleOpts);
	int32 titleHeight = qMin(_title.countHeight(_maxw), 2 * st::semiboldFont->height);

	int32 descriptionLines = _result->thumb->isNull() ? 3 : (titleHeight > st::semiboldFont->height ? 1 : 2);

	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, descriptionLines * st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::normalFont, _result->description, descriptionOpts);
	int32 descriptionHeight = qMin(_description.countHeight(_maxw), descriptionLines * st::normalFont->height);

	_minh = _result->thumb->isNull() ? titleHeight + descriptionHeight : st::inlineThumbSize;
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

void LayoutInlineWebVideo::paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const {
	int32 left = st::inlineThumbSize + st::inlineThumbSkip;
	prepareThumb(st::inlineThumbSize, st::inlineThumbSize);
	if (_thumb.isNull()) {
		p.fillRect(rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width), _result->thumb->isNull() ? st::black : st::overviewPhotoBg);
	} else {
		p.drawPixmapLeft(0, st::inlineRowMargin, _width, _thumb);
	}

	if (!_duration.isEmpty()) {
		int32 durationTop = st::inlineRowMargin + st::inlineThumbSize - st::normalFont->height - st::inlineDurationMargin;
		p.fillRect(rtlrect(0, durationTop - st::inlineDurationMargin, st::inlineThumbSize, st::normalFont->height + 2 * st::inlineDurationMargin, _width), st::msgDateImgBg);
		p.setPen(st::white);
		p.setFont(st::normalFont);
		p.drawTextRight(_width - st::inlineThumbSize + st::inlineDurationMargin, durationTop, _width, _duration);
	}

	p.setPen(st::black);
	_title.drawLeftElided(p, left, st::inlineRowMargin, _width - left, _width, 2);
	int32 titleHeight = qMin(_title.countHeight(_width - left), st::semiboldFont->height * 2);

	p.setPen(st::inlineDescriptionFg);
	int32 descriptionLines = _result->thumb->isNull() ? 3 : (titleHeight > st::semiboldFont->height ? 1 : 2);
	_description.drawLeftElided(p, left, st::inlineRowMargin + titleHeight, _width - left, _width, descriptionLines);

	const InlinePaintContext *ctx = context->toInlinePaintContext();
	t_assert(ctx);
	if (!ctx->lastRow) {
		p.fillRect(rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

void LayoutInlineWebVideo::getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
	if (x >= 0 && x < st::inlineThumbSize && y >= st::inlineRowMargin && y < st::inlineRowMargin + st::inlineThumbSize) {
		link = _link;
		return;
	}
	if (x >= st::inlineThumbSize + st::inlineThumbSkip && x < _width && y >= 0 && y < _height) {
		link = _send;
		return;
	}
}

void LayoutInlineWebVideo::prepareThumb(int32 width, int32 height) const {
	if (_result->thumb->loaded()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			int32 w = qMax(_result->thumb->width(), 1), h = qMax(_result->thumb->height(), 1);
			if (w * height > h * width) {
				if (height < h) {
					w = w * height / h;
					h = height;
				}
			} else {
				if (width < w) {
					h = h * width / w;
					w = width;
				}
			}
			_thumb = _result->thumb->pixNoCache(w * cIntRetinaFactor(), h * cIntRetinaFactor(), true, false, false, width, height);
		}
	} else {
		_result->thumb->load();
	}
}

LayoutInlineArticle::LayoutInlineArticle(InlineResult *result, bool withThumb) : LayoutInlineItem(result, 0, 0)
, _send(new SendInlineItemLink())
, _url(result->url.isEmpty() ? 0 : linkFromUrl(result->url))
, _link(result->content_url.isEmpty() ? 0 : linkFromUrl(result->content_url))
, _withThumb(withThumb)
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
	QVector<QStringRef> parts = _result->url.splitRef('/');
	if (!parts.isEmpty()) {
		QStringRef domain = parts.at(0);
		if (parts.size() > 2 && domain.endsWith(':') && parts.at(1).isEmpty()) { // http:// and others
			domain = parts.at(2);
		}

		parts = domain.split('@').back().split('.');
		if (parts.size() > 1) {
			_letter = parts.at(parts.size() - 2).at(0).toUpper();
		}
	}
	if (_letter.isEmpty() && !_result->title.isEmpty()) {
		_letter = _result->title.at(0).toUpper();
	}
}

void LayoutInlineArticle::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int32 textWidth = _maxw - (_withThumb ? (st::inlineThumbSize + st::inlineThumbSkip) : 0);
	TextParseOptions titleOpts = { 0, _maxw, 2 * st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldFont, textOneLine(_result->title), titleOpts);
	int32 titleHeight = qMin(_title.countHeight(_maxw), 2 * st::semiboldFont->height);

	int32 descriptionLines = (_withThumb || _url) ? 2 : 3;

	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, descriptionLines * st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::normalFont, _result->description, descriptionOpts);
	int32 descriptionHeight = qMin(_description.countHeight(_maxw), descriptionLines * st::normalFont->height);

	_minh = titleHeight + descriptionHeight;
	if (_url) _minh += st::normalFont->height;
	if (_withThumb) _minh = qMax(_minh, int32(st::inlineThumbSize));
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

int32 LayoutInlineArticle::resizeGetHeight(int32 width) {
	_width = qMin(width, _maxw);
	if (_url) {
		_urlText = _result->url;
		_urlWidth = st::normalFont->width(_urlText);
		if (_urlWidth > _width - st::inlineThumbSize - st::inlineThumbSkip) {
			_urlText = st::normalFont->elided(_result->url, _width - st::inlineThumbSize - st::inlineThumbSkip);
			_urlWidth = st::normalFont->width(_urlText);
		}
	}
	_height = _minh;
	return _height;
}

void LayoutInlineArticle::paint(Painter &p, const QRect &clip, uint32 selection, const PaintContext *context) const {
	int32 left = st::emojiPanHeaderLeft - st::inlineResultsLeft;
	if (_withThumb) {
		left = st::inlineThumbSize + st::inlineThumbSkip;
		prepareThumb(st::inlineThumbSize, st::inlineThumbSize);
		QRect rthumb(rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width));
		if (_thumb.isNull()) {
			if (_result->thumb->isNull() && !_letter.isEmpty()) {
				int32 index = (_letter.at(0).unicode() % 4);
				style::color colors[] = { st::msgFileRedColor, st::msgFileYellowColor, st::msgFileGreenColor, st::msgFileBlueColor };

				p.fillRect(rthumb, colors[index]);
				if (!_letter.isEmpty()) {
					p.setFont(st::linksLetterFont);
					p.setPen(st::white);
					p.drawText(rthumb, _letter, style::al_center);
				}
			} else {
				p.fillRect(rthumb, st::overviewPhotoBg);
			}
		} else {
			p.drawPixmapLeft(rthumb.topLeft(), _width, _thumb);
		}
	}

	p.setPen(st::black);
	_title.drawLeftElided(p, left, st::inlineRowMargin, _width - left, _width, 2);
	int32 titleHeight = qMin(_title.countHeight(_width - left), st::semiboldFont->height * 2);

	p.setPen(st::inlineDescriptionFg);
	int32 descriptionLines = (_withThumb || _url) ? 2 : 3;
	_description.drawLeftElided(p, left, st::inlineRowMargin + titleHeight, _width - left, _width, descriptionLines);

	if (_url) {
		int32 descriptionHeight = qMin(_description.countHeight(_width - left), st::normalFont->height * descriptionLines);
		p.drawTextLeft(left, st::inlineRowMargin + titleHeight + descriptionHeight, _width, _urlText, _urlWidth);
	}

	const InlinePaintContext *ctx = context->toInlinePaintContext();
	t_assert(ctx);
	if (!ctx->lastRow) {
		p.fillRect(rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

void LayoutInlineArticle::getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
	int32 left = _withThumb ? (st::inlineThumbSize + st::inlineThumbSkip) : 0;
	if (x >= 0 && x < left - st::inlineThumbSkip && y >= st::inlineRowMargin && y < st::inlineRowMargin + st::inlineThumbSize) {
		link = _link;
		return;
	}
	if (x >= left && x < _width && y >= 0 && y < _height) {
		if (_url) {
			int32 left = st::inlineThumbSize + st::inlineThumbSkip;
			int32 titleHeight = qMin(_title.countHeight(_width - left), st::semiboldFont->height * 2);
			int32 descriptionLines = 2;
			int32 descriptionHeight = qMin(_description.countHeight(_width - left), st::normalFont->height * descriptionLines);
			if (rtlrect(left, st::inlineRowMargin + titleHeight + descriptionHeight, _urlWidth, st::normalFont->height, _width).contains(x, y)) {
				link = _url;
				return;
			}
		}
		link = _send;
		return;
	}
}

void LayoutInlineArticle::prepareThumb(int32 width, int32 height) const {
	if (_result->thumb->isNull()) return;

	if (_result->thumb->loaded()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			int32 w = qMax(_result->thumb->width(), 1), h = qMax(_result->thumb->height(), 1);
			if (w * height > h * width) {
				if (height < h) {
					w = w * height / h;
					h = height;
				}
			} else {
				if (width < w) {
					h = h * width / w;
					w = width;
				}
			}
			_thumb = _result->thumb->pixNoCache(w * cIntRetinaFactor(), h * cIntRetinaFactor(), true, false, false, width, height);
		}
	} else {
		_result->thumb->load();
	}
}
