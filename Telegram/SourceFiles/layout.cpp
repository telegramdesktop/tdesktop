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
	, _text(langDayOfMonth(date)) {
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
, _data(document) {
}

void LayoutOverviewDocument::initDimensions() {
	_maxw = st::profileMaxWidth;
	_minh = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
}

void LayoutOverviewDocument::paint(Painter &p, const QRect &clip, uint32 selection, uint64 ms) const {

}
