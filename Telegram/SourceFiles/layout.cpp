/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "layout.h"

#include "data/data_document.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "storage/file_upload.h"
#include "mainwindow.h"
#include "core/file_utilities.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "media/audio/media_audio.h"
#include "storage/localstorage.h"
#include "history/view/history_view_cursor_state.h"
#include "ui/cached_round_corners.h"

int32 documentColorIndex(DocumentData *document, QString &ext) {
	auto colorIndex = 0;

	auto name = document
		? (document->filename().isEmpty()
			? (document->sticker()
				? tr::lng_in_dlg_sticker(tr::now)
				: qsl("Unknown File"))
			: document->filename())
		: tr::lng_message_empty(tr::now);
	name = name.toLower();
	auto lastDot = name.lastIndexOf('.');
	auto mime = document
		? document->mimeString().toLower()
		: QString();
	if (name.endsWith(qstr(".doc")) ||
		name.endsWith(qstr(".docx")) ||
		name.endsWith(qstr(".txt")) ||
		name.endsWith(qstr(".psd")) ||
		mime.startsWith(qstr("text/"))) {
		colorIndex = 0;
	} else if (
		name.endsWith(qstr(".xls")) ||
		name.endsWith(qstr(".xlsx")) ||
		name.endsWith(qstr(".csv"))) {
		colorIndex = 1;
	} else if (
		name.endsWith(qstr(".pdf")) ||
		name.endsWith(qstr(".ppt")) ||
		name.endsWith(qstr(".pptx")) ||
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

Ui::CachedRoundCorners documentCorners(int32 colorIndex) {
	return Ui::CachedRoundCorners(Ui::Doc1Corners + (colorIndex & 3));
}

[[nodiscard]] HistoryView::TextState LayoutItemBase::getState(
		QPoint point,
		StateRequest request) const {
	return {};
}

[[nodiscard]] TextSelection LayoutItemBase::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return selection;
}
