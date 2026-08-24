/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"

#include <memory>
#include <vector>

namespace Ui {
class ChatStyle;
class ChatTheme;
} // namespace Ui

namespace Iv::Markdown {

[[nodiscard]] std::vector<Ui::Text::SpecialColor> HighlightColors(
	not_null<const Ui::ChatStyle*> style);

[[nodiscard]] std::unique_ptr<Ui::ChatTheme> CreateStandaloneChatTheme();

} // namespace Iv::Markdown
