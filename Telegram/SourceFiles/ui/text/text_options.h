/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

struct TextParseOptions;

namespace Ui {

void InitTextOptions();

const TextParseOptions &ItemTextDefaultOptions();
const TextParseOptions &ItemTextBotDefaultOptions();
const TextParseOptions &ItemTextNoMonoOptions();
const TextParseOptions &ItemTextBotNoMonoOptions();
const TextParseOptions &ItemTextServiceOptions();

const TextParseOptions &WebpageTextTitleOptions();
const TextParseOptions &WebpageTextDescriptionOptions();

const TextParseOptions &NameTextOptions();
const TextParseOptions &DialogTextOptions();

} // namespace Ui
