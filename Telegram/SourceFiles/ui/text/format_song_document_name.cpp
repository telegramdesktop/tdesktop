/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/format_song_document_name.h"

#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history_item_helpers.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"

#include <QtCore/QLocale>

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

FormatSongName FormatVoiceName(
		not_null<DocumentData*> document,
		FullMsgId contextId) {
	if (const auto item = document->owner().message(contextId)) {
		const auto name = (!item->out() || item->isPost())
			? item->fromOriginal()->name()
			: tr::lng_from_you(tr::now);
		const auto date = [item] {
			const auto parsed = ItemDateTime(item);
			const auto date = parsed.date();
			const auto time = QLocale().toString(
				parsed.time(),
				QLocale::ShortFormat);
			const auto today = QDateTime::currentDateTime().date();
			if (date == today) {
				return tr::lng_player_message_today(
					tr::now,
					lt_time,
					time);
			} else if (date.addDays(1) == today) {
				return tr::lng_player_message_yesterday(
					tr::now,
					lt_time,
					time);
			}
			return tr::lng_player_message_date(
				tr::now,
				lt_date,
				langDayOfMonthFull(date),
				lt_time,
				time);
		};
		auto result = FormatSongName(QString(), date(), name);
		result.setNoDash(true);
		return result;
	} else if (document->isVideoMessage()) {
		return FormatSongName(QString(), tr::lng_media_round(tr::now), {});
	} else {
		return FormatSongName(QString(), tr::lng_media_audio(tr::now), {});
	}
}

} // namespace Ui::Text
