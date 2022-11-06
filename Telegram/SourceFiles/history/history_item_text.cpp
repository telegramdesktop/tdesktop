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
#include "data/data_groups.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "ui/text/text_options.h"

TextForMimeData HistoryItemText(not_null<HistoryItem*> item) {
	const auto media = item->media();

	auto textResult = item->clipboardText();
	auto logEntryOriginalResult = [&] {
		const auto entry = item->Get<HistoryMessageLogEntryOriginal>();
		if (!entry) {
			return TextForMimeData();
		}
		const auto title = TextUtilities::SingleLine(entry->page->title.isEmpty()
			? entry->page->author
			: entry->page->title);
		auto titleResult = TextForMimeData::Rich(
			TextUtilities::ParseEntities(
				title,
				Ui::WebpageTextTitleOptions().flags));
		auto descriptionResult = TextForMimeData::Rich(
			base::duplicate(entry->page->description));
		if (titleResult.empty()) {
			return descriptionResult;
		} else if (descriptionResult.empty()) {
			return titleResult;
		}
		titleResult.append('\n').append(std::move(descriptionResult));
		return titleResult;
	}();
	auto result = textResult;
	if (result.empty()) {
		result = std::move(logEntryOriginalResult);
	} else if (!logEntryOriginalResult.empty()) {
		result.append(qstr("\n\n")).append(std::move(logEntryOriginalResult));
	}
	return result;
}

TextForMimeData HistoryGroupText(not_null<const Data::Group*> group) {
	Expects(!group->items.empty());

	const auto columnAlbum = [&] {
		const auto item = group->items.front();
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				return !document->isVideoFile();
			}
		}
		return false;
	}();
	const auto hasCaption = [](not_null<HistoryItem*> item) {
		return !item->clipboardText().empty();
	};
	if (columnAlbum) {
		const auto simple = !ranges::any_of(group->items, hasCaption);
		if (!simple) {
			auto result = TextForMimeData();
			for (const auto &item : group->items) {
				if (result.empty()) {
					result = HistoryItemText(item);
				} else {
					result.append(qstr("\n\n")).append(HistoryItemText(item));
				}
			}
			return result;
		}
	}
	auto caption = [&] {
		auto &&nonempty = ranges::views::all(
			group->items
		) | ranges::views::filter(
			hasCaption
		) | ranges::views::take(2);
		auto first = nonempty.begin();
		auto end = nonempty.end();
		if (first == end) {
			return TextForMimeData();
		}
		auto result = (*first)->clipboardText();
		return (++first == end) ? result : TextForMimeData();
	}();
	return Data::WithCaptionClipboardText(
		tr::lng_in_dlg_album(tr::now),
		std::move(caption));
}
