/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item_text.h"

#include "history/history_item.h"
#include "history/history_item_components.h"
#include "data/data_media_types.h"
#include "data/data_web_page.h"
#include "lang/lang_keys.h"
#include "ui/text_options.h"

TextWithEntities WrapAsReply(
		TextWithEntities &&text,
		not_null<HistoryItem*> to) {
	const auto name = to->author()->name;
	auto result = TextWithEntities();
	result.text.reserve(
		lang(lng_in_reply_to).size()
		+ name.size()
		+ 4
		+ text.text.size());
	result.text.append('['
	).append(lang(lng_in_reply_to)
	).append(' '
	).append(name
	).append(qsl("]\n")
	);
	TextUtilities::Append(result, std::move(text));
	return result;
}

TextWithEntities WrapAsForwarded(
		TextWithEntities &&text,
		not_null<HistoryMessageForwarded*> forwarded) {
	auto info = forwarded->text.originalTextWithEntities(
		AllTextSelection,
		ExpandLinksAll);
	auto result = TextWithEntities();
	result.text.reserve(
		info.text.size()
		+ 4
		+ text.text.size());
	result.entities.reserve(
		info.entities.size()
		+ text.entities.size());
	result.text.append('[');
	TextUtilities::Append(result, std::move(info));
	result.text.append(qsl("]\n"));
	TextUtilities::Append(result, std::move(text));
	return result;
}

TextWithEntities HistoryItemText(not_null<HistoryItem*> item) {
	const auto media = item->media();

	auto textResult = item->clipboardText();
	auto mediaResult = media ? media->clipboardText() : TextWithEntities();
	auto logEntryOriginalResult = [&] {
		const auto entry = item->Get<HistoryMessageLogEntryOriginal>();
		if (!entry) {
			return TextWithEntities();
		}
		const auto title = TextUtilities::SingleLine(entry->page->title.isEmpty()
			? entry->page->author
			: entry->page->title);
		auto titleResult = TextUtilities::ParseEntities(
			title,
			Ui::WebpageTextTitleOptions().flags);
		auto descriptionResult = entry->page->description;
		if (titleResult.text.isEmpty()) {
			return descriptionResult;
		} else if (descriptionResult.text.isEmpty()) {
			return titleResult;
		}
		titleResult.text += '\n';
		TextUtilities::Append(titleResult, std::move(descriptionResult));
		return titleResult;
	}();
	auto result = textResult;
	if (result.text.isEmpty()) {
		result = std::move(mediaResult);
	} else if (!mediaResult.text.isEmpty()) {
		result.text += qstr("\n\n");
		TextUtilities::Append(result, std::move(mediaResult));
	}
	if (result.text.isEmpty()) {
		result = std::move(logEntryOriginalResult);
	} else if (!logEntryOriginalResult.text.isEmpty()) {
		result.text += qstr("\n\n");
		TextUtilities::Append(result, std::move(logEntryOriginalResult));
	}
	if (const auto reply = item->Get<HistoryMessageReply>()) {
		if (const auto message = reply->replyToMsg) {
			result = WrapAsReply(std::move(result), message);
		}
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		result = WrapAsForwarded(std::move(result), forwarded);
	}
	return result;
}
