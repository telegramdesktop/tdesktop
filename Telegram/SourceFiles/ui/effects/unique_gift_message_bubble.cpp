/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/unique_gift_message_bubble.h"

#include "ui/text/text.h"
#include "ui/painter.h"

#include <QtGui/QPainterPath>

#include "styles/style_basic.h"
#include "styles/style_chat.h"

namespace Ui::UniqueGiftMessageBubble {
namespace {

constexpr auto kFillOpacity = 1. / 6.;
constexpr auto kOutlineOpacity = 0.5;
constexpr auto kArcControl = 0.5522847498;
constexpr auto kTailTipControlX = 0.5459278926;
constexpr auto kTailJoinX = 0.7999998407;
constexpr auto kTailJoinControlX = 0.0073620031;
constexpr auto kTailFirstControlX = 0.2123168130;
constexpr auto kTailMiddleX = 0.3823594269;
constexpr auto kTailSecondControlX = 0.5524008667;
constexpr auto kTailThirdControlX = 0.7668863569;
constexpr auto kTailTipY = 0.0719294110;
constexpr auto kTailTipControlY = 0.1729529402;
constexpr auto kTailStartControlY = 0.4158705899;
constexpr auto kTailJoinControlY = 0.1595529381;
constexpr auto kTailMiddleY = 0.4270352921;

struct Widths {
	int body = 0;
	int text = 0;
};

[[nodiscard]] Widths ResolveWidths(
		const style::UniqueGiftMessageBubble &st,
		const QMargins &hostPadding,
		int outerWidth,
		int naturalTextWidth) {
	const auto availableWidth = std::max(
		outerWidth - hostPadding.left() - hostPadding.right(),
		0);
	const auto maximumBodyWidth = std::max(
		availableWidth - st.avatarSize - st.tailSize.width(),
		0);
	const auto minimumBodyWidth = std::min(
		2 * st.radius + st.tailSize.width(),
		maximumBodyWidth);
	const auto horizontalPadding = st.textPadding.left()
		+ st.textPadding.right();
	const auto minimumTextWidth = std::max(
		minimumBodyWidth - horizontalPadding,
		0);
	const auto maximumTextWidth = std::max(
		maximumBodyWidth - horizontalPadding,
		minimumTextWidth);
	const auto textWidth = std::clamp(
		naturalTextWidth,
		minimumTextWidth,
		maximumTextWidth);

	return {
		.body = std::clamp(
			textWidth + horizontalPadding,
			minimumBodyWidth,
			maximumBodyWidth),
		.text = textWidth,
	};
}

} // namespace

int MaximumTextWidth(
		const style::UniqueGiftMessageBubble &st,
		const QMargins &hostPadding,
		int outerWidth,
		int naturalTextWidth) {
	return ResolveWidths(
		st,
		hostPadding,
		outerWidth,
		naturalTextWidth).text;
}

Layout ResolveLayout(
		const style::UniqueGiftMessageBubble &st,
		const QMargins &hostPadding,
		int outerWidth,
		const Text::String &text) {
	const auto maximum = MaximumTextWidth(
		st,
		hostPadding,
		outerWidth,
		text.maxWidth());
	const auto minimum = ResolveWidths(st, hostPadding, outerWidth, 0).text;
	const auto size = Text::CountOptimalTextSize(text, minimum, maximum);
	return ComputeLayout(
		st,
		hostPadding,
		outerWidth,
		std::max(text.countWidth(size.width()), minimum),
		size.height());
}

Layout ComputeLayout(
		const style::UniqueGiftMessageBubble &st,
		const QMargins &hostPadding,
		int outerWidth,
		int textWidth,
		int textHeight) {
	const auto widths = ResolveWidths(
		st,
		hostPadding,
		outerWidth,
		textWidth);
	const auto bodyHeight = std::max(
		textHeight + st.textPadding.top() + st.textPadding.bottom(),
		2 * st.radius);
	const auto rowHeight = std::max(st.avatarSize, bodyHeight);
	const auto availableWidth = std::max(
		outerWidth - hostPadding.left() - hostPadding.right(),
		0);
	const auto gap = st.tailSize.width();
	const auto groupWidth = st.avatarSize + gap + widths.body;
	const auto groupLeft = hostPadding.left()
		+ ((availableWidth - groupWidth) / 2);
	const auto rowTop = hostPadding.top();
	const auto bottom = rowTop + rowHeight;
	const auto avatar = QRect(
		groupLeft,
		bottom - st.avatarSize,
		st.avatarSize,
		st.avatarSize);
	const auto body = QRect(
		groupLeft + st.avatarSize + gap,
		bottom - bodyHeight,
		widths.body,
		bodyHeight);

	return {
		.avatar = avatar,
		.body = body,
		.text = body.marginsRemoved(st.textPadding),
		.pathBounds = QRect(
			body.x() - st.tailSize.width(),
			body.y(),
			body.width() + st.tailSize.width(),
			body.height()),
		.rowHeight = rowHeight,
		.sectionWidth = hostPadding.left() + groupWidth + hostPadding.right(),
		.sectionHeight = rowTop + rowHeight + hostPadding.bottom(),
	};
}

QPainterPath Path(
		const style::UniqueGiftMessageBubble &st,
		const Layout &layout) {
	const auto outer = QRectF(layout.body);
	if (outer.isEmpty()) {
		return {};
	}
	const auto strokeInset = st::lineWidth / 2.;
	const auto body = outer.marginsRemoved(QMarginsF(
		strokeInset,
		strokeInset,
		strokeInset,
		strokeInset));
	const auto radius = std::max(
		std::min(outer.height() / 2., float64(st.radius)) - strokeInset,
		0.);
	const auto arcControl = radius * kArcControl;
	const auto tailWidth = float64(st.tailSize.width());
	const auto tailHeight = std::min(
		float64(st.tailSize.height()),
		body.height());
	const auto left = body.left();
	const auto top = body.top();
	const auto right = body.right();
	const auto bottom = body.bottom();

	// The supplied contour uses three independent scalable anchors.
	// Its leftward points are normalized to the tail width, while the
	// rightward return points are normalized to the body corner radius.
	// Vertical distances from the bottom are normalized to the tail height,
	// preserving the original cubic character without stretching the tail.
	auto result = QPainterPath();
	result.moveTo(left + radius, top);
	result.lineTo(right - radius, top);
	result.cubicTo(
		right - radius + arcControl,
		top,
		right,
		top + radius - arcControl,
		right,
		top + radius);
	result.lineTo(right, bottom - radius);
	result.cubicTo(
		right,
		bottom - radius + arcControl,
		right - radius + arcControl,
		bottom,
		right - radius,
		bottom);
	result.lineTo(left + radius, bottom);
	result.cubicTo(
		left + radius * kTailThirdControlX,
		bottom,
		left + radius * kTailSecondControlX,
		bottom - tailHeight * kTailMiddleY,
		left + radius * kTailMiddleX,
		bottom - tailHeight * kTailMiddleY);
	result.cubicTo(
		left + radius * kTailFirstControlX,
		bottom - tailHeight * kTailJoinControlY,
		left - tailWidth * kTailJoinControlX,
		bottom,
		left - tailWidth * kTailJoinX,
		bottom);
	result.lineTo(left - tailWidth, bottom);
	result.cubicTo(
		left - tailWidth,
		bottom,
		left - tailWidth,
		bottom - tailHeight * kTailTipY,
		left - tailWidth,
		bottom - tailHeight * kTailTipY);
	result.cubicTo(
		left - tailWidth * kTailTipControlX,
		bottom - tailHeight * kTailTipControlY,
		left,
		bottom - tailHeight * kTailStartControlY,
		left,
		bottom - tailHeight);
	result.lineTo(left, top + radius);
	result.cubicTo(
		left,
		top + radius - arcControl,
		left + radius - arcControl,
		top,
		left + radius,
		top);
	result.closeSubpath();

	return result;
}

void Paint(
		QPainter &p,
		const style::UniqueGiftMessageBubble &st,
		const Layout &layout) {
	const auto path = Path(st, layout);
	p.save();
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.setOpacity(kFillOpacity);
		p.drawPath(path);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(
			Qt::white,
			st::lineWidth,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.setOpacity(kOutlineOpacity);
		p.drawPath(path);
	}
	p.restore();
}

} // namespace Ui::UniqueGiftMessageBubble
