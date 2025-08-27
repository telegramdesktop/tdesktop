/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "layout/layout_document_generic_preview.h"

#include "data/data_document.h"
#include "lang/lang_keys.h"
#include "styles/style_media_view.h"

namespace Layout {

const style::icon *DocumentGenericPreview::icon() const {
	switch (index) {
	case 0: return &st::mediaviewFileBlue;
	case 1: return &st::mediaviewFileGreen;
	case 2: return &st::mediaviewFileRed;
	case 3: return &st::mediaviewFileYellow;
	}
	Unexpected("Color index in DocumentGenericPreview::icon.");
}

DocumentGenericPreview DocumentGenericPreview::Create(
		DocumentData *document) {
	auto colorIndex = 0;

	const auto name = (document
		? (document->filename().isEmpty()
			? (document->sticker()
				? tr::lng_in_dlg_sticker(tr::now)
				: u"Unknown File"_q)
			: document->filename())
		: tr::lng_message_empty(tr::now)).toLower();
	auto lastDot = name.lastIndexOf('.');
	const auto mime = document ? document->mimeString() : QString();
	if (name.endsWith(u".doc"_q) ||
		name.endsWith(u".docx"_q) ||
		name.endsWith(u".txt"_q) ||
		name.endsWith(u".psd"_q) ||
		mime.startsWith(u"text/"_q)) {
		colorIndex = 0;
	} else if (
		name.endsWith(u".xls"_q) ||
		name.endsWith(u".xlsx"_q) ||
		name.endsWith(u".csv"_q)) {
		colorIndex = 1;
	} else if (
		name.endsWith(u".pdf"_q) ||
		name.endsWith(u".ppt"_q) ||
		name.endsWith(u".pptx"_q) ||
		name.endsWith(u".key"_q)) {
		colorIndex = 2;
	} else if (
		name.endsWith(u".zip"_q) ||
		name.endsWith(u".rar"_q) ||
		name.endsWith(u".ai"_q) ||
		name.endsWith(u".mp3"_q) ||
		name.endsWith(u".mov"_q) ||
		name.endsWith(u".avi"_q)) {
		colorIndex = 3;
	} else {
		auto ch = (lastDot >= 0 && lastDot + 1 < name.size())
			? name.at(lastDot + 1)
			: (name.isEmpty()
				? (mime.isEmpty() ? '0' : mime.at(0))
				: name.at(0));
		colorIndex = (ch.unicode() % 4) & 3;
	}

	const auto ext = document
		? ((lastDot < 0 || lastDot + 2 > name.size())
			? name
			: name.mid(lastDot + 1))
		: QString();

	switch (colorIndex) {
	case 0: return {
		.index = colorIndex,
		.color = st::msgFile1Bg,
		.dark = st::msgFile1BgDark,
		.over = st::msgFile1BgOver,
		.selected = st::msgFile1BgSelected,
		.ext = ext,
	};
	case 1: return {
		.index = colorIndex,
		.color = st::msgFile2Bg,
		.dark = st::msgFile2BgDark,
		.over = st::msgFile2BgOver,
		.selected = st::msgFile2BgSelected,
		.ext = ext,
	};
	case 2: return {
		.index = colorIndex,
		.color = st::msgFile3Bg,
		.dark = st::msgFile3BgDark,
		.over = st::msgFile3BgOver,
		.selected = st::msgFile3BgSelected,
		.ext = ext,
	};
	case 3: return {
		.index = colorIndex,
		.color = st::msgFile4Bg,
		.dark = st::msgFile4BgDark,
		.over = st::msgFile4BgOver,
		.selected = st::msgFile4BgSelected,
		.ext = ext,
	};
	}
	Unexpected("Color index in CreateDocumentGenericPreview.");
}

// Ui::CachedRoundCorners DocumentCorners(int32 colorIndex) {
// 	return Ui::CachedRoundCorners(Ui::Doc1Corners + (colorIndex & 3));
// }

} // namespace Layout
