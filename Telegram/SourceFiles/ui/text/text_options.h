/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
