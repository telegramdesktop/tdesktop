/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/participants_check_view.h"

#include "ui/effects/ripple_animation.h"
#include "ui/effects/toggle_arrow.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_boxes.h"

namespace Ui {

ParticipantsCheckView::ParticipantsCheckView(
	int count,
	int duration,
	bool checked,
	Fn<void()> updateCallback)
: Ui::AbstractCheckView(duration, checked, std::move(updateCallback))
, _text(QString::number(std::abs(count)))
, _count(count) {
}

QSize ParticipantsCheckView::ComputeSize(int count) {
	return QSize(
		st::moderateBoxExpandHeight
			+ st::moderateBoxExpand.width()
			+ st::moderateBoxExpandInnerSkip * 4
			+ st::moderateBoxExpandFont->width(
				QString::number(std::abs(count)))
			+ st::moderateBoxExpandToggleSize,
		st::moderateBoxExpandHeight);
}

QSize ParticipantsCheckView::getSize() const {
	return ComputeSize(_count);
}

void ParticipantsCheckView::paint(
		QPainter &p,
		int left,
		int top,
		int outerWidth) {
	auto hq = PainterHighQualityEnabler(p);
	const auto size = getSize();
	const auto radius = size.height() / 2;
	p.setPen(Qt::NoPen);
	st::moderateBoxExpand.paint(
		p,
		radius,
		left + (size.height() - st::moderateBoxExpand.height()) / 2,
		top + size.width());

	const auto innerSkip = st::moderateBoxExpandInnerSkip;

	p.setBrush(Qt::NoBrush);
	p.setPen(st::boxTextFg);
	p.setFont(st::moderateBoxExpandFont);
	p.drawText(
		QRect(
			left + innerSkip + radius + st::moderateBoxExpand.width(),
			top,
			size.width(),
			size.height()),
		_text,
		style::al_left);

	const auto path = Ui::ToggleUpDownArrowPath(
		left + size.width() - st::moderateBoxExpandToggleSize - radius,
		top + size.height() / 2,
		st::moderateBoxExpandToggleSize,
		st::moderateBoxExpandToggleFourStrokes,
		Ui::AbstractCheckView::currentAnimationValue());
	p.fillPath(path, st::boxTextFg);
}

QImage ParticipantsCheckView::prepareRippleMask() const {
	const auto size = getSize();
	return Ui::RippleAnimation::RoundRectMask(size, size.height() / 2);
}

bool ParticipantsCheckView::checkRippleStartPosition(QPoint position) const {
	return Rect(getSize()).contains(position);
}

void ParticipantsCheckView::checkedChangedHook(anim::type animated) {
}

ParticipantsCheckView::~ParticipantsCheckView() = default;

} // namespace Ui
