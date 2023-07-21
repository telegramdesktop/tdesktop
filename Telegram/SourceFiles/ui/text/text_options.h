/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
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
