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
#include "layout.h"

#include "lang.h"
#include "mainwidget.h"
#include "application.h"
#include "fileuploader.h"
#include "mainwindow.h"
#include "ui/filedialog.h"
#include "playerwidget.h"
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
