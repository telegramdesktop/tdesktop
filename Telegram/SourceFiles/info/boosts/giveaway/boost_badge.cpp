/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/giveaway/boost_badge.h"

#include "ui/painter.h"
#include "ui/rect.h"

namespace Info::Statistics {

QImage CreateBadge(
		const style::TextStyle &textStyle,
		const QString &text,
		int badgeHeight,
		const style::margins &textPadding,
		const style::color &bg,
		const style::color &fg,
		float64 bgOpacity,
		const style::margins &iconPadding,
		const style::icon &icon) {
	auto badgeText = Ui::Text::String(textStyle, text);
	const auto badgeTextWidth = badgeText.maxWidth();
	const auto badgex = 0;
	const auto badgey = 0;
	const auto badgeh = 0 + badgeHeight;
	const auto badgew = badgeTextWidth
		+ rect::m::sum::h(textPadding);
	auto result = QImage(
		QSize(badgew, badgeh) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = Painter(&result);

		p.setPen(Qt::NoPen);
		p.setBrush(bg);

		const auto r = QRect(badgex, badgey, badgew, badgeh);
		{
			auto hq = PainterHighQualityEnabler(p);
			auto o = ScopedPainterOpacity(p, bgOpacity);
			p.drawRoundedRect(r, badgeh / 2, badgeh / 2);
		}

		p.setPen(fg);
		p.setBrush(Qt::NoBrush);
		badgeText.drawLeftElided(
			p,
			r.x() + textPadding.left(),
			badgey + textPadding.top(),
			badgew,
			badgew * 2);

		icon.paint(
			p,
			QPoint(r.x() + iconPadding.left(), r.y() + iconPadding.top()),
			badgew * 2);
	}
	return result;
}

} // namespace Info::Statistics
