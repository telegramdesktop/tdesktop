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

// Mono only where a white shine would vanish or clash, like white marks.
[[nodiscard]] QImage IconCurrencyMono(int size, const QColor &c);
[[nodiscard]] QImage IconCurrencyMono(
	const style::font &font,
	const QColor &c);
[[nodiscard]] QImage IconCurrencyTwoTone(int size, const QColor &c);
[[nodiscard]] QImage IconCurrencyTwoTone(
	const style::font &font,
	const QColor &c);
[[nodiscard]] float64 AlignedMarkTop(
	const style::font &font,
	const QImage &image);
[[nodiscard]] QByteArray CurrencySvgMono(const QColor &c);
[[nodiscard]] QByteArray CurrencySvgTwoTone(const QColor &c);

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
