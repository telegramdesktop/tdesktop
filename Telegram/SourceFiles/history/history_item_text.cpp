/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item_text.h"

#include "data/data_groups.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "data/data_story.h"
#include "data/data_todo_list.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/view/history_view_item_preview.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "ui/text/text.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"

namespace {

constexpr auto kSelectedCopyReplyPreviewLimit = 64;

TextForMimeData LogEntryOriginalText(not_null<HistoryItem*> item) {
	const auto entry = item->Get<HistoryMessageLogEntryOriginal>();
	if (!entry) {
		return TextForMimeData();
	}
	const auto title = TextUtilities::SingleLine(
		entry->page->title.isEmpty()
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
}

TextForMimeData FactcheckText(not_null<HistoryItem*> item) {
	const auto factcheck = item->Get<HistoryMessageFactcheck>();
	return factcheck
		? TextForMimeData::Rich(base::duplicate(factcheck->data.text))
		: TextForMimeData();
}

TextForMimeData AppendExtraCopyText(
		not_null<HistoryItem*> item,
		TextForMimeData &&result) {
	auto logEntryOriginalResult = LogEntryOriginalText(item);
	auto factcheckResult = FactcheckText(item);
	if (result.empty()) {
		result = std::move(logEntryOriginalResult);
	} else if (!logEntryOriginalResult.empty()) {
		result.append(u"\n\n"_q).append(std::move(logEntryOriginalResult));
	}
	if (result.empty()) {
		result = std::move(factcheckResult);
	} else if (!factcheckResult.empty()) {
		result.append(u"\n\n"_q).append(std::move(factcheckResult));
	}
	return result;
}

TextForMimeData HistoryItemMainText(not_null<HistoryItem*> item) {
	const auto media = item->media();

	auto mediaResult = media ? media->clipboardText() : TextForMimeData();
	auto textResult = mediaResult.empty()
		? item->clipboardText()
		: TextForMimeData();
	auto result = textResult;
	if (result.empty()) {
		result = std::move(mediaResult);
	} else if (!mediaResult.empty()) {
		result.append(qstr("\n\n")).append(std::move(mediaResult));
	}
	return result;
}

[[nodiscard]] TextWithEntities StripIconEmoji(TextWithEntities text) {
	auto i = text.entities.begin();
	while (i != text.entities.end()) {
		if (i->type() != EntityType::CustomEmoji
			|| !Ui::Text::TryMakeSimpleEmoji(i->data())) {
			++i;
			continue;
		}
		const auto offset = i->offset();
		const auto length = i->length();
		text.text.remove(offset, length);
		i = text.entities.erase(i);
		const auto index = int(i - text.entities.begin());
		for (auto &entity : text.entities) {
			const auto till = entity.offset() + entity.length();
			if (entity.offset() >= offset + length) {
				entity.shiftLeft(length);
			} else if (till > offset) {
				entity.shrinkFromRight(std::min(till - offset, length));
			}
		}
		i = text.entities.begin() + index;
	}
	text.entities.erase(
		ranges::remove_if(text.entities, [](const EntityInText &entity) {
			return (entity.length() <= 0);
		}),
		text.entities.end());
	TextUtilities::Trim(text);
	return text;
}

TextForMimeData BracketedSelectedCopyLabel(TextWithEntities label) {
	label = TextUtilities::SingleLine(label);
	if (label.empty()) {
		return TextForMimeData();
	}
	auto result = TextWithEntities();
	result
		.append(u"[ "_q)
		.append(std::move(label))
		.append(u" ]"_q);
	return TextForMimeData::Rich(std::move(result));
}

TextForMimeData SelectedCopyMediaLabel(not_null<Data::Media*> media) {
	return BracketedSelectedCopyLabel(StripIconEmoji(media->toPreview({
		.hideSender = true,
		.hideCaption = true,
		.ignoreMessageText = true,
		.generateImages = false,
		.ignoreGroup = true,
		.ignoreTopic = true,
		.translated = true,
	}).text));
}

bool UsesSelectedCopyPlaceholder(not_null<Data::Media*> media) {
	if (media->webpage()) {
		return false;
	} else if (media->photo() || media->document()) {
		return true;
	} else if (const auto invoice = media->invoice()) {
		return invoice->isPaidMedia && !invoice->extendedMedia.empty();
	}
	return false;
}

bool SelectedCopyMediaBeforeText(
		not_null<HistoryItem*> item,
		not_null<Data::Media*> media) {
	auto result = !media->webpage() && !media->invoice();
	if (item->invertMedia() && !item->emptyText()) {
		result = !result;
	}
	return result;
}

TextForMimeData CombineSelectedCopyTextAndMedia(
		TextForMimeData &&text,
		TextForMimeData &&media,
		bool mediaBeforeText) {
	if (text.empty()) {
		return std::move(media);
	} else if (media.empty()) {
		return std::move(text);
	} else if (mediaBeforeText) {
		media.append(u"\n\n"_q).append(std::move(text));
		return std::move(media);
	}
	text.append(u"\n\n"_q).append(std::move(media));
	return std::move(text);
}

struct SelectedCopyAlbumCounts {
	int photos = 0;
	int videos = 0;
	int audios = 0;
	int files = 0;
};

SelectedCopyAlbumCounts CountSelectedCopyAlbumMedia(
		not_null<const Data::Group*> group) {
	auto result = SelectedCopyAlbumCounts();
	for (const auto &item : group->items) {
		const auto media = item->media();
		if (!media || media->webpage()) {
			continue;
		} else if (media->photo()) {
			++result.photos;
		} else if (const auto document = media->document()) {
			(document->isVideoFile()
				? result.videos
				: document->isAudioFile()
				? result.audios
				: result.files)++;
		}
	}
	return result;
}

TextForMimeData SelectedCopyAlbumLabel(SelectedCopyAlbumCounts counts) {
	const auto medias = counts.photos + counts.videos;
	const auto label = (counts.photos && counts.videos)
		? tr::lng_in_dlg_media_count(tr::now, lt_count, medias)
		: (counts.photos > 1)
		? tr::lng_in_dlg_photo_count(tr::now, lt_count, counts.photos)
		: counts.photos
		? tr::lng_in_dlg_photo(tr::now)
		: (counts.videos > 1)
		? tr::lng_in_dlg_video_count(tr::now, lt_count, counts.videos)
		: counts.videos
		? tr::lng_in_dlg_video(tr::now)
		: (counts.audios > 1)
		? tr::lng_in_dlg_audio_count(tr::now, lt_count, counts.audios)
		: counts.audios
		? tr::lng_in_dlg_audio(tr::now)
		: (counts.files > 1)
		? tr::lng_in_dlg_file_count(tr::now, lt_count, counts.files)
		: counts.files
		? tr::lng_in_dlg_file(tr::now)
		: tr::lng_in_dlg_album(tr::now);
	return BracketedSelectedCopyLabel(TextWithEntities{ label });
}

struct SelectedCopyReplyContext {
	QString senderName;
	TextForMimeData quote;
};

QString ReplySenderNameForSelectedCopy(
		not_null<HistoryItem*> item,
		not_null<HistoryMessageReply*> reply) {
	const auto &fields = reply->fields();
	const auto message = reply->resolvedMessage.get();
	if (const auto story = reply->resolvedStory.get()) {
		return story->peer()->name();
	} else if (!message) {
		auto &owner = item->history()->owner();
		if (fields.externalSenderId) {
			const auto name = owner.peer(fields.externalSenderId)->name();
			if (!name.isEmpty()) {
				return name;
			}
		}
		if (!fields.externalSenderName.isEmpty()) {
			return fields.externalSenderName;
		}
	} else if (item->Has<HistoryMessageForwarded>()) {
		const auto forwarded = message->Get<HistoryMessageForwarded>();
		if (forwarded) {
			if (forwarded->originalSender) {
				return forwarded->originalSender->name();
			} else if (forwarded->originalHiddenSenderInfo) {
				return forwarded->originalHiddenSenderInfo->name;
			}
		}
	}
	if (message) {
		if (const auto from = message->displayFrom()) {
			return from->name();
		}
		return message->author()->name();
	} else if (fields.externalPeerId) {
		return item->history()->owner().peer(fields.externalPeerId)->name();
	}
	return QString();
}

TextWithEntities ReplyPreviewTextForSelectedCopy(
		not_null<HistoryMessageReply*> reply) {
	if (!reply->displaying() && reply->unavailable()) {
		return TextWithEntities();
	}
	const auto &fields = reply->fields();
	const auto message = reply->resolvedMessage.get();
	const auto media = message ? message->media() : nullptr;
	const auto messageMedia = (message
			&& (fields.todoItemId || !fields.pollOption.isEmpty()))
		? media
		: nullptr;
	const auto messageTodoList = messageMedia
		? messageMedia->todolist()
		: nullptr;
	const auto task = (messageTodoList && fields.todoItemId)
		? messageTodoList->itemById(fields.todoItemId)
		: nullptr;
	const auto messagePoll = messageMedia
		? messageMedia->poll()
		: media
		? media->poll()
		: nullptr;
	const auto pollAnswer = (messagePoll && !fields.pollOption.isEmpty())
		? messagePoll->answerByOption(fields.pollOption)
		: nullptr;
	if (task) {
		return task->text;
	} else if (pollAnswer) {
		return pollAnswer->text;
	} else if (messagePoll) {
		return messagePoll->question;
	} else if (message && (fields.quote.empty() || !reply->manualQuote())) {
		return message->inReplyText();
	} else if (!fields.quote.empty()) {
		return fields.quote;
	} else if (const auto story = reply->resolvedStory.get()) {
		return story->inReplyText();
	} else if (const auto externalMedia = fields.externalMedia.get()) {
		return externalMedia->toPreview({
			.hideSender = true,
			.hideCaption = true,
			.ignoreMessageText = true,
			.generateImages = false,
			.ignoreGroup = true,
			.ignoreTopic = true,
		}).text;
	}
	return TextWithEntities();
}

TextWithEntities LimitNonExactReplyPreview(TextWithEntities text) {
	auto left = TextUtilities::SingleLine(text);
	if (left.text.size() <= kSelectedCopyReplyPreviewLimit) {
		return left;
	}
	auto part = TextWithEntities();
	if (!TextUtilities::CutPart(
			part,
			left,
			kSelectedCopyReplyPreviewLimit)) {
		return TextWithEntities();
	}
	TextUtilities::Trim(part);
	TextUtilities::Trim(left);
	if (!left.empty()) {
		part.append(Ui::kQEllipsis);
	}
	return part;
}

std::optional<SelectedCopyReplyContext> ReplyContextForSelectedCopy(
		not_null<HistoryItem*> item) {
	const auto reply = item->Get<HistoryMessageReply>();
	if (!reply) {
		return std::nullopt;
	}
	const auto replyPointer = not_null{ reply };
	const auto senderName = ReplySenderNameForSelectedCopy(
		item,
		replyPointer);
	if (senderName.isEmpty()) {
		return std::nullopt;
	}
	const auto &fields = reply->fields();
	auto quote = (reply->manualQuote() && !fields.quote.empty())
		? TextUtilities::SingleLine(fields.quote)
		: LimitNonExactReplyPreview(StripIconEmoji(
			ReplyPreviewTextForSelectedCopy(replyPointer)));
	return SelectedCopyReplyContext{
		.senderName = senderName,
		.quote = TextForMimeData::WithExpandedLinks(quote),
	};
}

TextForMimeData HistorySelectedItemPlainWrappedText(
		not_null<HistoryItem*> item,
		TextForMimeData &&body) {
	auto result = TextForMimeData();
	const auto time = u"[%1] "_q.arg(
		QLocale().toString(ItemDateTime(item), QLocale::ShortFormat));
	const auto author = item->author()->name();
	const auto size = time.size() + author.size() + 2 + body.expanded.size();
	result.reserve(size);
	result.append(time).append(author).append(u": "_q);
	result.append(std::move(body));
	return result;
}

} // namespace

TextForMimeData HistoryItemText(not_null<HistoryItem*> item) {
	return AppendExtraCopyText(item, HistoryItemMainText(item));
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
					result.append(u"\n\n"_q).append(HistoryItemText(item));
				}
			}
			return result;
		}
	}
	return [&] {
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
}

TextForMimeData HistoryItemTextForSelectedCopy(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return HistoryItemText(item);
	}
	const auto mediaPointer = not_null{ media };
	if (!UsesSelectedCopyPlaceholder(mediaPointer)) {
		return HistoryItemText(item);
	}
	auto mediaResult = SelectedCopyMediaLabel(mediaPointer);
	if (mediaResult.empty()) {
		return HistoryItemText(item);
	}
	auto textResult = media->document()
		? media->clipboardText()
		: item->clipboardText();
	auto result = CombineSelectedCopyTextAndMedia(
		std::move(textResult),
		std::move(mediaResult),
		SelectedCopyMediaBeforeText(item, mediaPointer));
	return AppendExtraCopyText(item, std::move(result));
}

TextForMimeData HistoryGroupTextForSelectedCopy(
		not_null<const Data::Group*> group) {
	Expects(!group->items.empty());

	auto textResult = HistoryGroupText(group);
	auto mediaResult = SelectedCopyAlbumLabel(CountSelectedCopyAlbumMedia(group));
	const auto item = group->items.back();
	const auto media = item->media();
	return CombineSelectedCopyTextAndMedia(
		std::move(textResult),
		std::move(mediaResult),
		media ? SelectedCopyMediaBeforeText(item, not_null{ media }) : true);
}

TextForMimeData HistorySelectedItemWrappedText(
		not_null<HistoryItem*> item,
		TextForMimeData &&body,
		bool richContext) {
	if (!richContext) {
		return HistorySelectedItemPlainWrappedText(item, std::move(body));
	}
	auto context = ReplyContextForSelectedCopy(item);
	if (!context) {
		return HistorySelectedItemPlainWrappedText(item, std::move(body));
	}
	auto result = TextForMimeData();
	const auto time = u"[%1] "_q.arg(
		QLocale().toString(ItemDateTime(item), QLocale::ShortFormat));
	const auto author = item->author()->name();
	const auto replyTo = tr::lng_context_copy_in_reply_to(
		tr::now,
		lt_name,
		context->senderName);
	const auto size = time.size()
		+ author.size()
		+ 1
		+ replyTo.size()
		+ 2
		+ context->quote.expanded.size()
		+ body.expanded.size();
	result.reserve(size);
	result
		.append(time)
		.append(author)
		.append(' ')
		.append(replyTo)
		.append(':');
	if (context->quote.empty()) {
		result.append(' ').append(std::move(body));
	} else {
		result.append(u"\n> "_q).append(std::move(context->quote));
		if (!body.empty()) {
			result.append('\n').append(std::move(body));
		}
	}
	return result;
}
