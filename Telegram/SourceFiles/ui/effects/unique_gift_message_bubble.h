/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QMargins>
#include <QtCore/QRect>

class QPainter;
class QPainterPath;

namespace style {
struct UniqueGiftMessageBubble;
} // namespace style

namespace Ui::Text {
class String;
} // namespace Ui::Text

namespace Ui::UniqueGiftMessageBubble {

struct Layout {
	QRect avatar;
	QRect body;
	QRect text;
	QRect pathBounds;
	int rowHeight = 0;
	int sectionWidth = 0;
	int sectionHeight = 0;
};

[[nodiscard]] int MaximumTextWidth(
	const style::UniqueGiftMessageBubble &st,
	const QMargins &hostPadding,
	int outerWidth,
	int naturalTextWidth);

[[nodiscard]] Layout ResolveLayout(
	const style::UniqueGiftMessageBubble &st,
	const QMargins &hostPadding,
	int outerWidth,
	const Text::String &text);

[[nodiscard]] Layout ComputeLayout(
	const style::UniqueGiftMessageBubble &st,
	const QMargins &hostPadding,
	int outerWidth,
	int textWidth,
	int textHeight);

[[nodiscard]] QPainterPath Path(
	const style::UniqueGiftMessageBubble &st,
	const Layout &layout);

void Paint(
	QPainter &p,
	const style::UniqueGiftMessageBubble &st,
	const Layout &layout);

} // namespace Ui::UniqueGiftMessageBubble
