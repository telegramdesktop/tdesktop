/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text_options.h"

#include "history/history.h"
#include "history/history_item.h"
#include "styles/style_history.h"
#include "styles/style_window.h"

namespace Ui {
namespace {

TextParseOptions HistoryTextOptions = {
	TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		| TextParseMultiline
		| TextParseRichText
		| TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions HistoryBotOptions = {
	TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		| TextParseBotCommands
		| TextParseMultiline
		| TextParseRichText
		| TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions HistoryServiceOptions = {
	TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		//| TextParseMultiline
		| TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};

TextParseOptions HistoryTextNoMonoOptions = {
	TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		| TextParseMultiline
		| TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions HistoryBotNoMonoOptions = {
	TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		| TextParseBotCommands
		| TextParseMultiline
		| TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions TextNameOptions = {
	0, // flags
	4096, // maxw
	1, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};

TextParseOptions TextDialogOptions = {
	TextParseRichText, // flags
	0, // maxw is style-dependent
	1, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};

TextParseOptions WebpageTitleOptions = {
	TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions WebpageDescriptionOptions = {
	TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		| TextParseMultiline
		| TextParseRichText
		| TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions TwitterDescriptionOptions = {
	TextParseLinks
		| TextParseMentions
		| TextTwitterMentions
		| TextParseHashtags
		| TextTwitterHashtags
		| TextParseMultiline
		| TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions InstagramDescriptionOptions = {
	TextParseLinks
		| TextParseMentions
		| TextInstagramMentions
		| TextParseHashtags
		| TextInstagramHashtags
		| TextParseMultiline
		| TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

bool UseBotTextOptions(
		not_null<History*> history,
		not_null<PeerData*> author) {
	if (const auto user = history->peer->asUser()) {
		if (user->botInfo) {
			return true;
		}
	} else if (const auto chat = history->peer->asChat()) {
		if (chat->botStatus >= 0) {
			return true;
		}
	} else if (const auto group = history->peer->asMegagroup()) {
		if (group->mgInfo->botStatus >= 0) {
			return true;
		}
	}
	if (const auto user = author->asUser()) {
		if (user->botInfo) {
			return true;
		}
	}
	return false;
}

} // namespace

void InitTextOptions() {
	HistoryServiceOptions.dir
		= TextNameOptions.dir
		= TextDialogOptions.dir
		= cLangDir();
	TextDialogOptions.maxw = st::columnMaximalWidthLeft * 2;
	WebpageTitleOptions.maxh = st::webPageTitleFont->height * 2;
	WebpageTitleOptions.maxw
		= WebpageDescriptionOptions.maxw
		= TwitterDescriptionOptions.maxw
		= InstagramDescriptionOptions.maxw
		= st::msgMaxWidth
		- st::msgPadding.left()
		- st::webPageLeft
		- st::msgPadding.right();
	WebpageDescriptionOptions.maxh = st::webPageDescriptionFont->height * 3;
}

const TextParseOptions &ItemTextDefaultOptions() {
	return HistoryTextOptions;
}

const TextParseOptions &ItemTextBotDefaultOptions() {
	return HistoryBotOptions;
}

const TextParseOptions &ItemTextNoMonoOptions() {
	return HistoryTextNoMonoOptions;
}

const TextParseOptions &ItemTextBotNoMonoOptions() {
	return HistoryBotNoMonoOptions;
}

const TextParseOptions &ItemTextServiceOptions() {
	return HistoryServiceOptions;
}

const TextParseOptions &WebpageTextTitleOptions() {
	return WebpageTitleOptions;
}

const TextParseOptions &WebpageTextDescriptionOptions(
		const QString &siteName) {
	if (siteName == qstr("Twitter")) {
		return TwitterDescriptionOptions;
	} else if (siteName == qstr("Instagram")) {
		return InstagramDescriptionOptions;
	}
	return WebpageDescriptionOptions;
}

const TextParseOptions &NameTextOptions() {
	return TextNameOptions;
}

const TextParseOptions &DialogTextOptions() {
	return TextDialogOptions;
}

const TextParseOptions &ItemTextOptions(
		not_null<History*> history,
		not_null<PeerData*> author) {
	return UseBotTextOptions(history, author)
		? HistoryBotOptions
		: HistoryTextOptions;
}

const TextParseOptions &ItemTextOptions(not_null<const HistoryItem*> item) {
	return ItemTextOptions(item->history(), item->author());
}

const TextParseOptions &ItemTextNoMonoOptions(
		not_null<History*> history,
		not_null<PeerData*> author) {
	return UseBotTextOptions(history, author)
		? HistoryBotNoMonoOptions
		: HistoryTextNoMonoOptions;
}

const TextParseOptions &ItemTextNoMonoOptions(
		not_null<const HistoryItem*> item) {
	return ItemTextNoMonoOptions(item->history(), item->author());
}

} // namespace Ui
