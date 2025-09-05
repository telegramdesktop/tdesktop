/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/custom_emoji_helper.h"

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Ui::Earn {

[[nodiscard]] QImage IconCurrencyColored(int size, const QColor &c);
[[nodiscard]] QImage IconCurrencyColored(
	const style::font &font,
	const QColor &c);
[[nodiscard]] QByteArray CurrencySvgColored(const QColor &c);

[[nodiscard]] QImage MenuIconCurrency(const QSize &size);
[[nodiscard]] QImage MenuIconCredits();

std::unique_ptr<Ui::Text::CustomEmoji> MakeCurrencyIconEmoji(
	const style::font &font,
	const QColor &c);

struct IconDescriptor {
	int size = 0;
	std::optional<QMargins> margin;
};
[[nodiscard]] Ui::Text::PaletteDependentEmoji IconCreditsEmoji(
	IconDescriptor descriptor = {});
[[nodiscard]] Ui::Text::PaletteDependentEmoji IconCurrencyEmoji(
	IconDescriptor descriptor = {});

[[nodiscard]] Ui::Text::PaletteDependentEmoji IconCreditsEmojiSmall();
[[nodiscard]] Ui::Text::PaletteDependentEmoji IconCurrencyEmojiSmall();

} // namespace Ui::Earn
