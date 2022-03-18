/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/format_song_document_name.h"

#include "ui/text/text_utilities.h"
#include "data/data_document.h"
#include "lang/lang_keys.h"

namespace Ui::Text {

FormatSongName FormatSongNameFor(not_null<DocumentData*> document) {
	const auto song = document->song();

	return FormatSongName(
		document->filename(),
		song ? song->title : QString(),
		song ? song->performer : QString());
}

TextWithEntities FormatDownloadsName(not_null<DocumentData*> document) {
	return document->isVideoFile()
		? Bold(tr::lng_in_dlg_video(tr::now))
		: document->isVoiceMessage()
		? Bold(tr::lng_in_dlg_audio(tr::now))
		: document->isVideoMessage()
		? Bold(tr::lng_in_dlg_video_message(tr::now))
		: document->sticker()
		? Bold(document->sticker()->alt.isEmpty()
			? tr::lng_in_dlg_sticker(tr::now)
			: tr::lng_in_dlg_sticker_emoji(
				tr::now,
				lt_emoji,
				document->sticker()->alt))
		: FormatSongNameFor(document).textWithEntities();
}

} // namespace Ui::Text
