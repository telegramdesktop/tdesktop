/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_bubble_wrap.h"

#include "ui/chat/message_bubble.h"
#include "ui/rect.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_info_userpic_builder.h"

namespace Ui {

QRect BubbleWrap::innerRect() const {
	return rect() - st::userpicBuilderEmojiBubblePadding;
}

rpl::producer<QRect> BubbleWrap::innerRectValue() const {
	return sizeValue() | rpl::map([](const QSize &s) {
		return Rect(s) - st::userpicBuilderEmojiBubblePadding;
	});
}

not_null<BubbleWrap*> AddBubbleWrap(
		not_null<Ui::VerticalLayout*> container,
		const QSize &size,
		Fn<not_null<const Ui::ChatStyle*>()> chatStyle) {
	const auto bubble = container->add(object_ptr<Ui::CenterWrap<BubbleWrap>>(
		container,
		object_ptr<BubbleWrap>(container)))->entity();
	bubble->resize(size);
	const auto rounding = BubbleRounding{
		.topLeft = BubbleCornerRounding::Small,
		.topRight = BubbleCornerRounding::Small,
		.bottomLeft = BubbleCornerRounding::Small,
		.bottomRight = BubbleCornerRounding::Small,
	};

	bubble->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(bubble);
		const auto innerRect = bubble->innerRect();
		const auto args = SimpleBubble{
			.st = chatStyle(),
			.geometry = innerRect,
			.pattern = nullptr,
			.patternViewport = innerRect,
			.outerWidth = bubble->width(),
			.selected = false,
			.shadowed = true,
			.outbg = false,
			.rounding = rounding,
		};
		PaintBubble(p, args);
	}, bubble->lifetime());

	return bubble;
}

} // namespace Ui
