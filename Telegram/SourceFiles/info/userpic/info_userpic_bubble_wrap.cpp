/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_bubble_wrap.h"

#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_chat.h"
#include "styles/style_info_userpic_builder.h"

namespace Ui {
namespace {

void PaintExcludeTopShadow(QPainter &p, int radius, const QRect &r) {
	constexpr auto kHorizontalOffset = 1;
	constexpr auto kVerticalOffset = 2;
	const auto opacity = p.opacity();
	p.setOpacity(opacity * 0.2);
	p.drawRoundedRect(
		r + QMargins(kHorizontalOffset, -radius, kHorizontalOffset, 0),
		radius,
		radius);
	p.setOpacity(opacity * 0.2);
	p.drawRoundedRect(
		r + QMargins(0, 0, 0, kVerticalOffset),
		radius,
		radius);
	p.setOpacity(opacity * 0.4);
	p.drawRoundedRect(
		r + QMargins(0, 0, 0, kVerticalOffset / 2),
		radius,
		radius);
	p.setOpacity(opacity);
}

} // namespace

QRect BubbleWrapInnerRect(const QRect &r) {
	return r - st::userpicBuilderEmojiBubblePadding;
}

not_null<Ui::RpWidget*> AddBubbleWrap(
		not_null<Ui::VerticalLayout*> container,
		const QSize &size) {
	const auto bubble = container->add(object_ptr<Ui::CenterWrap<RpWidget>>(
		container,
		object_ptr<Ui::RpWidget>(container)))->entity();
	bubble->resize(size);

	auto cached = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	cached.setDevicePixelRatio(style::DevicePixelRatio());
	cached.fill(Qt::transparent);
	{
		auto p = QPainter(&cached);
		const auto innerRect = BubbleWrapInnerRect(bubble->rect());
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::bubbleRadiusSmall;
		p.setPen(Qt::NoPen);
		p.setBrush(st::shadowFg);
		PaintExcludeTopShadow(p, radius, innerRect);
		p.setBrush(st::boxBg);
		p.drawRoundedRect(innerRect, radius, radius);
	}

	bubble->paintRequest(
	) | rpl::start_with_next([bubble, cached = std::move(cached)] {
		auto p = QPainter(bubble);
		p.drawImage(0, 0, cached);
	}, bubble->lifetime());

	return bubble;
}

} // namespace Ui
