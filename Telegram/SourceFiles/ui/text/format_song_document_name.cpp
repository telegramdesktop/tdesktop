/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/format_song_document_name.h"

#include "data/data_document.h"

namespace Ui::Text {

FormatSongName FormatSongNameFor(not_null<DocumentData*> document) {
	const auto song = document->song();

	return FormatSongName(
		document->filename(),
		song ? song->title : QString(),
		song ? song->performer : QString());
}

} // namespace Ui::Text
