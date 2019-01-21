/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/shadow.h"

#include "styles/style_widgets.h"

namespace Ui {

PlainShadow::PlainShadow(QWidget *parent)
: PlainShadow(parent, st::shadowFg) {
}

PlainShadow::PlainShadow(QWidget *parent, style::color color)
: RpWidget(parent)
, _color(color) {
	resize(st::lineWidth, st::lineWidth);
}

void Shadow::paint(Painter &p, const QRect &box, int outerWidth, const style::Shadow &st, RectParts sides) {
	auto left = (sides & RectPart::Left);
	auto top = (sides & RectPart::Top);
	auto right = (sides & RectPart::Right);
	auto bottom = (sides & RectPart::Bottom);
	if (left) {
		auto from = box.y();
		auto to = from + box.height();
		if (top && !st.topLeft.empty()) {
			st.topLeft.paint(p, box.x() - st.extend.left(), box.y() - st.extend.top(), outerWidth);
			from += st.topLeft.height() - st.extend.top();
		}
		if (bottom && !st.bottomLeft.empty()) {
			st.bottomLeft.paint(p, box.x() - st.extend.left(), box.y() + box.height() + st.extend.bottom() - st.bottomLeft.height(), outerWidth);
			to -= st.bottomLeft.height() - st.extend.bottom();
		}
		if (to > from && !st.left.empty()) {
			st.left.fill(p, rtlrect(box.x() - st.extend.left(), from, st.left.width(), to - from, outerWidth));
		}
	}
	if (right) {
		auto from = box.y();
		auto to = from + box.height();
		if (top && !st.topRight.empty()) {
			st.topRight.paint(p, box.x() + box.width() + st.extend.right() - st.topRight.width(), box.y() - st.extend.top(), outerWidth);
			from += st.topRight.height() - st.extend.top();
		}
		if (bottom && !st.bottomRight.empty()) {
			st.bottomRight.paint(p, box.x() + box.width() + st.extend.right() - st.bottomRight.width(), box.y() + box.height() + st.extend.bottom() - st.bottomRight.height(), outerWidth);
			to -= st.bottomRight.height() - st.extend.bottom();
		}
		if (to > from && !st.right.empty()) {
			st.right.fill(p, rtlrect(box.x() + box.width() + st.extend.right() - st.right.width(), from, st.right.width(), to - from, outerWidth));
		}
	}
	if (top && !st.top.empty()) {
		auto from = box.x();
		auto to = from + box.width();
		if (left && !st.topLeft.empty()) from += st.topLeft.width() - st.extend.left();
		if (right && !st.topRight.empty()) to -= st.topRight.width() - st.extend.right();
		if (to > from) {
			st.top.fill(p, rtlrect(from, box.y() - st.extend.top(), to - from, st.top.height(), outerWidth));
		}
	}
	if (bottom && !st.bottom.empty()) {
		auto from = box.x();
		auto to = from + box.width();
		if (left && !st.bottomLeft.empty()) from += st.bottomLeft.width() - st.extend.left();
		if (right && !st.bottomRight.empty()) to -= st.bottomRight.width() - st.extend.right();
		if (to > from) {
			st.bottom.fill(p, rtlrect(from, box.y() + box.height() + st.extend.bottom() - st.bottom.height(), to - from, st.bottom.height(), outerWidth));
		}
	}
}

QPixmap Shadow::grab(
		not_null<TWidget*> target,
		const style::Shadow &shadow,
		RectParts sides) {
	SendPendingMoveResizeEvents(target);
	auto rect = target->rect();
	auto extend = QMargins(
		(sides & RectPart::Left) ? shadow.extend.left() : 0,
		(sides & RectPart::Top) ? shadow.extend.top() : 0,
		(sides & RectPart::Right) ? shadow.extend.right() : 0,
		(sides & RectPart::Bottom) ? shadow.extend.bottom() : 0
	);
	auto full = QRect(0, 0, extend.left() + rect.width() + extend.right(), extend.top() + rect.height() + extend.bottom());
	auto result = QPixmap(full.size() * cIntRetinaFactor());
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		Ui::Shadow::paint(p, full.marginsRemoved(extend), full.width(), shadow);
		target->render(&p, QPoint(extend.left(), extend.top()), rect, QWidget::DrawChildren | QWidget::IgnoreMask);
	}
	return result;
}

void Shadow::paintEvent(QPaintEvent *e) {
	Painter p(this);
	paint(p, rect().marginsRemoved(_st.extend), width(), _st, _sides);
}

} // namespace Ui
