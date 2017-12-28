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
#include "layout.h"

#include "data/data_document.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "application.h"
#include "storage/file_upload.h"
#include "mainwindow.h"
#include "core/file_utilities.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "media/media_audio.h"
#include "storage/localstorage.h"

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

QString formatDurationWords(qint64 duration) {
	if (duration > 59) {
		auto minutes = (duration / 60);
		auto minutesCount = lng_duration_minsec_minutes(lt_count, minutes);
		auto seconds = (duration % 60);
		auto secondsCount = lng_duration_minsec_seconds(lt_count, seconds);
		return lng_duration_minutes_seconds(lt_minutes_count, minutesCount, lt_seconds_count, secondsCount);
	}
	return lng_duration_seconds(lt_count, duration);
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

int32 documentColorIndex(DocumentData *document, QString &ext) {
	auto colorIndex = 0;

	auto name = document
		? (document->filename().isEmpty()
			? (document->sticker()
				? lang(lng_in_dlg_sticker)
				: qsl("Unknown File"))
			: document->filename())
		: lang(lng_message_empty);
	name = name.toLower();
	auto lastDot = name.lastIndexOf('.');
	auto mime = document
		? document->mimeString().toLower()
		: QString();
	if (name.endsWith(qstr(".doc")) ||
		name.endsWith(qstr(".txt")) ||
		name.endsWith(qstr(".psd")) ||
		mime.startsWith(qstr("text/"))) {
		colorIndex = 0;
	} else if (
		name.endsWith(qstr(".xls")) ||
		name.endsWith(qstr(".csv"))) {
		colorIndex = 1;
	} else if (
		name.endsWith(qstr(".pdf")) ||
		name.endsWith(qstr(".ppt")) ||
		name.endsWith(qstr(".key"))) {
		colorIndex = 2;
	} else if (
		name.endsWith(qstr(".zip")) ||
		name.endsWith(qstr(".rar")) ||
		name.endsWith(qstr(".ai")) ||
		name.endsWith(qstr(".mp3")) ||
		name.endsWith(qstr(".mov")) ||
		name.endsWith(qstr(".avi"))) {
		colorIndex = 3;
	} else {
		auto ch = (lastDot >= 0 && lastDot + 1 < name.size())
			? name.at(lastDot + 1)
			: (name.isEmpty()
				? (mime.isEmpty() ? '0' : mime.at(0))
				: name.at(0));
		colorIndex = (ch.unicode() % 4);
	}

	ext = document
		? ((lastDot < 0 || lastDot + 2 > name.size())
			? name
			: name.mid(lastDot + 1))
		: QString();

	return colorIndex;
}

style::color documentColor(int32 colorIndex) {
	const style::color colors[] = {
		st::msgFile1Bg,
		st::msgFile2Bg,
		st::msgFile3Bg,
		st::msgFile4Bg
	};
	return colors[colorIndex & 3];
}

style::color documentDarkColor(int32 colorIndex) {
	static style::color colors[] = {
		st::msgFile1BgDark,
		st::msgFile2BgDark,
		st::msgFile3BgDark,
		st::msgFile4BgDark
	};
	return colors[colorIndex & 3];
}

style::color documentOverColor(int32 colorIndex) {
	static style::color colors[] = {
		st::msgFile1BgOver,
		st::msgFile2BgOver,
		st::msgFile3BgOver,
		st::msgFile4BgOver
	};
	return colors[colorIndex & 3];
}

style::color documentSelectedColor(int32 colorIndex) {
	static style::color colors[] = {
		st::msgFile1BgSelected,
		st::msgFile2BgSelected,
		st::msgFile3BgSelected,
		st::msgFile4BgSelected
	};
	return colors[colorIndex & 3];
}

RoundCorners documentCorners(int32 colorIndex) {
	return RoundCorners(Doc1Corners + (colorIndex & 3));
}

bool documentIsValidMediaFile(const QString &filepath) {
	static StaticNeverFreedPointer<QList<QString>> validMediaTypes(([] {
		std::unique_ptr<QList<QString>> result = std::make_unique<QList<QString>>();
		*result = qsl("\
webm mkv flv vob ogv ogg drc gif gifv mng avi mov qt wmv yuv rm rmvb asf amv mp4 m4p \
m4v mpg mp2 mpeg mpe mpv m2v svi 3gp 3g2 mxf roq nsv f4v f4p f4a f4b wma divx evo mk3d \
mka mks mcf m2p ps ts m2ts ifo aaf avchd cam dat dsh dvr-ms m1v fla flr sol wrap smi swf \
wtv 8svx 16svx iff aiff aif aifc au bwf cdda raw wav flac la pac m4a ape ofr ofs off rka \
shn tak tta wv brstm dts dtshd dtsma ast amr mp3 spx gsm aac mpc vqf ra ots swa vox voc \
dwd smp aup cust mid mus sib sid ly gym vgm psf nsf mod ptb s3m xm it mt2 minipsf psflib \
2sf dsf gsf psf2 qsf ssf usf rmj spc niff mxl xml txm ym jam mp1 mscz\
").split(' ');
		return result.release();
	})());

	QFileInfo info(filepath);
	auto parts = info.fileName().split('.', QString::SkipEmptyParts);
	return !parts.isEmpty() && (validMediaTypes->indexOf(parts.back().toLower()) >= 0);
}

bool documentIsExecutableName(const QString &filename) {
	static StaticNeverFreedPointer<QList<QString>> executableTypes(([] {
		std::unique_ptr<QList<QString>> result = std::make_unique<QList<QString>>();
#ifdef Q_OS_MAC
		*result = qsl("\
action app bin command csh osx workflow\
").split(' ');
#elif defined Q_OS_LINUX // Q_OS_MAC
		*result = qsl("\
bin csh ksh out run\
").split(' ');
#else // Q_OS_MAC || Q_OS_LINUX
		*result = qsl("\
bat bin cmd com cpl exe gadget inf ins inx isu job jse lnk msc msi \
msp mst paf pif ps1 reg rgs sct shb shs u3p vb vbe vbs vbscript ws wsf\
").split(' ');
#endif // !Q_OS_MAC && !Q_OS_LINUX
		return result.release();
	})());

	auto lastDotIndex = filename.lastIndexOf('.');
	return (lastDotIndex >= 0) && (executableTypes->indexOf(filename.mid(lastDotIndex + 1).toLower()) >= 0);
}
