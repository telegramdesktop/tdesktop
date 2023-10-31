/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/text_options.h"

#include "styles/style_window.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

TextParseOptions HistoryTextOptions = {
	TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		| TextParseMultiline
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
		| TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions HistoryServiceOptions = {
	TextParseLinks
		| TextParseMentions
		//| TextParseMultiline
		| TextParseHashtags
		| TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};

TextParseOptions HistoryTextNoMonoOptions = {
	TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		| TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions HistoryBotNoMonoOptions = {
	TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		| TextParseBotCommands
		| TextParseMultiline, // flags
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
	TextParseColorized | TextParseMarkdown, // flags
	0, // maxw is style-dependent
	1, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};

TextParseOptions WebpageTitleOptions = {
	TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions WebpageDescriptionOptions = {
	TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		| TextParseMultiline
		| TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

} // namespace

void InitTextOptions() {
	HistoryServiceOptions.dir
		= TextNameOptions.dir
		= TextDialogOptions.dir
		= Qt::LeftToRight;
	TextDialogOptions.maxw = st::columnMaximalWidthLeft * 2;
	WebpageTitleOptions.maxh = st::webPageTitleFont->height * 2;
	WebpageTitleOptions.maxw
		= WebpageDescriptionOptions.maxw
		= st::msgMaxWidth
		- st::msgPadding.left()
		- st::messageQuoteStyle.padding.left()
		- st::messageQuoteStyle.padding.right()
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

const TextParseOptions &WebpageTextDescriptionOptions() {
	return WebpageDescriptionOptions;
}

const TextParseOptions &NameTextOptions() {
	return TextNameOptions;
}

const TextParseOptions &DialogTextOptions() {
	return TextDialogOptions;
}

} // namespace Ui
