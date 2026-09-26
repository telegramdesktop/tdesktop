/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/ttl_icon.h"

#include "ui/arc_angles.h"
#include "ui/painter.h"
#include "styles/style_dialogs.h"

namespace Ui {

void PaintTimerIcon(
		QPainter &p,
		const QRect &innerRect,
		const QString &text,
		const QColor &color) {
	p.setFont(st::dialogsScamFont);
	p.setPen(color);
	p.drawText(
		innerRect,
		(text.size() > 2) ? text.mid(0, 2) : text,
		style::al_center);

	constexpr auto kPenWidth = 1.5;
	const auto penWidth = style::ConvertScaleExact(kPenWidth);
	auto pen = QPen(color);
	pen.setJoinStyle(Qt::RoundJoin);
	pen.setCapStyle(Qt::RoundCap);
	pen.setWidthF(penWidth);

	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawArc(innerRect, arc::kQuarterLength, arc::kHalfLength);

	p.setClipRect(innerRect
		- QMargins(innerRect.width() / 2, -penWidth, -penWidth, -penWidth));
	pen.setStyle(Qt::DotLine);
	p.setPen(pen);
	p.drawEllipse(innerRect);
}

} // namespace Ui
