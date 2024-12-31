/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "emoji.h"

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Ui {

struct ChatsFilterTagContext {
	base::flat_map<QString, std::unique_ptr<Text::CustomEmoji>> emoji;
	std::any textContext;
	QColor color;
	bool active = false;
	bool loading = false;
};

[[nodiscard]] QImage ChatsFilterTag(
	const TextWithEntities &text,
	ChatsFilterTagContext &context);

[[nodiscard]] std::unique_ptr<Text::CustomEmoji> MakeScaledSimpleEmoji(
	EmojiPtr emoji);

[[nodiscard]] std::unique_ptr<Text::CustomEmoji> MakeScaledCustomEmoji(
	std::unique_ptr<Text::CustomEmoji> wrapped);

} // namespace Ui
