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

style::sprite documentCorner(int32 colorIndex) {
	static style::sprite corners[] = { st::msgFileBlue, st::msgFileGreen, st::msgFileRed, st::msgFileYellow };
	return corners[colorIndex & 3];
}

RoundCorners documentCorners(int32 colorIndex) {
	return RoundCorners(DocBlueCorners + (colorIndex & 3));
}

void LayoutRadialProgressItem::linkOver(const TextLinkPtr &lnk) {
	if (lnk == _savel || lnk == _cancell) {
		a_iconOver.start(1);
		_a_iconOver.start();
	}
}

void LayoutRadialProgressItem::linkOut(const TextLinkPtr &lnk) {
	if (lnk == _savel || lnk == _cancell) {
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
	} else {
		a_iconOver.update(dt, anim::linear);
	}
	if (timer && iconAnimated()) {
		Ui::redrawHistoryItem(_parent);
	}
}

void LayoutRadialProgressItem::step_radial(uint64 ms, bool timer) {
	_radial->update(dataProgress(), dataFinished(), ms);
	if (!_radial->animating()) {
		checkRadialFinished();
	}
	if (timer) {
		Ui::redrawHistoryItem(_parent);
	}
}

void LayoutRadialProgressItem::ensureRadial() const {
	if (!_radial) {
		_radial = new RadialAnimation(
			st::msgFileRadialLine,
			animation(const_cast<LayoutRadialProgressItem*>(this), &LayoutRadialProgressItem::step_radial));
	}
}

void LayoutRadialProgressItem::checkRadialFinished() {
	if (_radial && !_radial->animating() && dataLoaded()) {
		delete _radial;
		_radial = 0;
	}
}

LayoutRadialProgressItem::~LayoutRadialProgressItem() {
	if (_radial) {
		delete _radial;
		setBadPointer(_radial);
	}
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

LayoutOverviewDate::LayoutOverviewDate(const QDate &date, int32 top)
	: _info(top)
	, _date(date)
	, _text(langDayOfMonthFull(date)) {
}

void LayoutOverviewDate::initDimensions() {
	_maxw = st::normalFont->width(_text);
	_minh = st::linksDateMargin + st::normalFont->height + st::linksDateMargin + st::linksBorder;
}

void LayoutOverviewDate::paint(Painter &p, const QRect &clip, uint32 selection, uint64 ms) const {
	if (clip.intersects(QRect(0, st::linksDateMargin, _width, st::normalFont->height))) {
		p.setPen(st::linksDateColor);
		p.setFont(st::normalFont);
		p.drawTextLeft(0, st::linksDateMargin, _width, _text);
	}
}

LayoutOverviewDocument::LayoutOverviewDocument(DocumentData *document, HistoryItem *parent, int32 top) : LayoutAbstractFileItem(parent)
, _info(top)
, _data(document)
, _msgl(new MessageLink(parent))
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
			_thumbw = (tw * st::msgFileThumbSize) / th;
		} else {
			_thumbw = st::msgFileThumbSize;
		}
	} else {
		_thumbw = 0;
	}

	_extw = st::semiboldFont->width(_ext);
	if (_extw > st::msgFileThumbSize - st::msgFileExtPadding * 2) {
		_ext = st::semiboldFont->elided(_ext, st::msgFileThumbSize - st::msgFileExtPadding * 2, Qt::ElideMiddle);
		_extw = st::semiboldFont->width(_ext);
	}
}

void LayoutOverviewDocument::initDimensions() {
	_maxw = st::profileMaxWidth;
	_minh = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom() + st::lineWidth;
}

void LayoutOverviewDocument::paint(Painter &p, const QRect &clip, uint32 selection, uint64 ms) const {
	bool selected = (selection == FullSelection);
	bool already = !_data->already().isEmpty(), hasdata = !_data->data.isEmpty();
	if (_data->loader) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(_data->progress());
		}
	}
	updateStatusText();
	bool radial = isRadialAnimation(ms);

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	bool wthumb = withThumb();

	nameleft = st::msgFileThumbSize + st::msgFileThumbPadding.right();
	nametop = st::linksBorder + st::msgFileThumbNameTop;
	statustop = st::linksBorder + st::msgFileThumbStatusTop;
	linktop = st::linksBorder + st::msgFileThumbLinkTop;

	QRect shadow(rtlrect(nameleft, 0, _width - nameleft, st::linksBorder, _width));
	if (clip.intersects(shadow)) {
		p.fillRect(clip.intersected(shadow), st::linksBorderColor);
	}

	QRect rthumb(rtlrect(0, st::linksBorder + st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, _width));
	if (clip.intersects(rthumb)) {
		if (wthumb) {
			if (_data->thumb->loaded()) {
				QPixmap thumb = (already || hasdata) ? _data->thumb->pixSingle(_thumbw, 0, st::msgFileThumbSize, st::msgFileThumbSize) : _data->thumb->pixBlurredSingle(_thumbw, 0, st::msgFileThumbSize, st::msgFileThumbSize);
				p.drawPixmap(rthumb.topLeft(), thumb);
			} else {
				App::roundRect(p, rthumb, st::black, BlackCorners);
			}
		} else {
			App::roundRect(p, rthumb, documentColor(_colorIndex), documentCorners(_colorIndex));
			if (!radial && (already || hasdata)) {
				style::sprite icon = documentCorner(_colorIndex);
				p.drawSprite(rthumb.topLeft() + QPoint(rtl() ? 0 : (rthumb.width() - icon.pxWidth()), 0), icon);
				if (!_ext.isEmpty()) {
					p.setFont(st::semiboldFont);
					p.setPen(st::white);
					p.drawText(rthumb.left() + (rthumb.width() - _extw) / 2, rthumb.top() + st::msgFileExtTop + st::semiboldFont->ascent, _ext);
				}
			}
		}
		if (selected) {
			App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
			p.drawSprite(rthumb.topLeft() + QPoint(rtl() ? 0 : (rthumb.width() - st::linksPhotoCheck.pxWidth()), rthumb.height() - st::linksPhotoCheck.pxHeight()), st::linksPhotoChecked);
		}

		if (!radial && (already || hasdata)) {
		} else {
			QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
			if (clip.intersects(inner)) {
				p.setPen(Qt::NoPen);
				if (selected) {
					p.setBrush(st::msgDateImgBgSelected);
				} else if (radial && (already || hasdata)) {
					p.setOpacity(st::msgDateImgBg->c.alphaF() * _radial->opacity());
					p.setBrush(st::black);
				} else if (_a_iconOver.animating()) {
					_a_iconOver.step(ms);
					float64 over = a_iconOver.current();
					p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
					p.setBrush(st::black);
				} else {
					bool over = textlnkDrawOver(_data->loader ? _cancell : _savel);
					p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
				}

				p.setRenderHint(QPainter::HighQualityAntialiasing);
				p.drawEllipse(inner);
				p.setRenderHint(QPainter::HighQualityAntialiasing, false);

				style::sprite icon;
				if (already || hasdata || _data->loader) {
					icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
				} else {
					icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
				}
				p.setOpacity(radial ? _radial->opacity() : 1);
				p.drawSpriteCenter(inner, icon);
				if (radial) {
					p.setOpacity(1);

					QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
					_radial->draw(p, rinner, selected ? st::msgInBgSelected : st::msgInBg);
				}
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

	if (clip.intersects(QRect(0, statustop, _width, st::normalFont->height))) {
		p.setFont(st::normalFont);
		p.setPen(st::mediaInFg);
		p.drawTextLeft(nameleft, statustop, _width, _statusText);
	}
	if (clip.intersects(rtlrect(nameleft, linktop, _datew, st::normalFont->height, _width))) {
		p.setFont(textlnkDrawOver(_msgl) ? st::normalFont->underline() : st::normalFont);
		p.setPen(st::mediaInFg);
		p.drawTextLeft(nameleft, linktop, _width, _date, _datew);
	}
}

void LayoutOverviewDocument::getState(TextLinkPtr &link, HistoryCursorState &cursor, int32 x, int32 y) const {
	bool already = !_data->already().isEmpty(), hasdata = !_data->data.isEmpty();

	updateStatusText();

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	bool wthumb = withThumb();

	nameleft = st::msgFileThumbSize + st::msgFileThumbPadding.right();
	nametop = st::linksBorder + st::msgFileThumbNameTop;
	statustop = st::linksBorder + st::msgFileThumbStatusTop;
	linktop = st::linksBorder + st::msgFileThumbLinkTop;

	QRect rthumb(rtlrect(0, st::linksBorder + st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, _width));

	if (already || hasdata) {
	} else {
		if (rthumb.contains(x, y)) {
			link = (_data->loader || _data->status == FileUploading) ? _cancell : _savel;
			return;
		}
	}

	if (_data->status != FileUploadFailed) {
		if (rtlrect(nameleft, linktop, _datew, st::normalFont->height, _width).contains(x, y)) {
			link = _msgl;
			return;
		}
	}
	if (!_data->loader && _data->access) {
		if (rtlrect(0, st::linksBorder, nameleft, _height - st::linksBorder, _width).contains(x, y)) {
			link = _openl;
			return;
		}
		if (rtlrect(nameleft, nametop, qMin(_width - nameleft - nameright, _namew), st::semiboldFont->height, _width).contains(x, y)) {
			link = _openl;
			return;
		}
	}
}

void LayoutOverviewDocument::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loader) {
		statusSize = _data->loader->currentOffset();
	} else if (_data->song() && (!_data->already().isEmpty() || !_data->data.isEmpty())) {
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
	} else if (!_data->already().isEmpty() || !_data->data.isEmpty()) {
		statusSize = FileStatusSizeLoaded;
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize, _data->size, _data->song() ? _data->song()->duration : -1, realDuration);
	}
}
