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
#include "ui/text/text.h"
#include "styles/style_boxes.h"

namespace Ui {
namespace {

void SetExpanderText(
		Ui::Text::String &target,
		const TextWithEntities &text,
		Fn<void()> repaint = nullptr) {
	target.setMarkedText(
		st::moderateBoxExpandTextStyle,
		text,
		kMarkupTextOptions,
		Ui::Text::MarkedContext{ .repaint = std::move(repaint) });
}

} // namespace

ExpanderCheckView::ExpanderCheckView(
	TextWithEntities text,
	int duration,
	bool checked,
	Fn<void()> updateCallback)
: Ui::AbstractCheckView(duration, checked, std::move(updateCallback))
, _size(ComputeSize(text)) {
	SetExpanderText(_text, text, [=] { update(); });
}

QSize ExpanderCheckView::ComputeSize(const TextWithEntities &text) {
	auto string = Ui::Text::String();
	SetExpanderText(string, text);
	return QSize(
		st::moderateBoxExpandHeight
			+ st::moderateBoxExpandInnerSkip * 4
			+ string.maxWidth()
			+ st::moderateBoxExpandToggleSize,
		st::moderateBoxExpandHeight);
}

void ExpanderCheckView::setText(TextWithEntities text) {
	SetExpanderText(_text, text, [=] { update(); });
	update();
}

QSize ExpanderCheckView::getSize() const {
	return _size;
}

void ExpanderCheckView::paint(QPainter &p, int left, int top, int outerWidth) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = _size.height() / 2;
	const auto innerSkip = st::moderateBoxExpandInnerSkip;
	const auto textLeft = left + innerSkip + radius;
	const auto textWidth = _size.width()
		- st::moderateBoxExpandHeight
		- st::moderateBoxExpandInnerSkip * 4
		- st::moderateBoxExpandToggleSize;
	p.setPen(st::boxTextFg);
	_text.draw(p, {
		.position = { textLeft, top + (_size.height() - _text.minHeight()) / 2 },
		.outerWidth = outerWidth,
		.availableWidth = textWidth,
		.elisionLines = 1,
	});

	const auto path = Ui::ToggleUpDownArrowPath(
		left + _size.width() - st::moderateBoxExpandToggleSize - radius,
		top + _size.height() / 2,
		st::moderateBoxExpandToggleSize,
		st::moderateBoxExpandToggleFourStrokes,
		currentAnimationValue());
	p.fillPath(path, st::boxTextFg);
}

QImage ExpanderCheckView::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(_size, _size.height() / 2);
}

bool ExpanderCheckView::checkRippleStartPosition(QPoint position) const {
	return Rect(_size).contains(position);
}

void ExpanderCheckView::checkedChangedHook(anim::type) {
}

ExpanderCheckView::~ExpanderCheckView() = default;

ExpanderButton::ExpanderButton(
	not_null<QWidget*> parent,
	TextWithEntities text)
: Ui::RippleButton(parent, st::defaultRippleAnimation)
, _view(std::make_unique<Ui::ExpanderCheckView>(
	std::move(text),
	st::slideWrapDuration,
	false,
	[=] { update(); })) {
	resize(_view->getSize());
}

QSize ExpanderButton::ComputeSize(const TextWithEntities &text) {
	return Ui::ExpanderCheckView::ComputeSize(text);
}

void ExpanderButton::setText(TextWithEntities text) {
	_view->setText(std::move(text));
}

not_null<Ui::AbstractCheckView*> ExpanderButton::checkView() const {
	return _view.get();
}

QImage ExpanderButton::prepareRippleMask() const {
	return _view->prepareRippleMask();
}

QPoint ExpanderButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

void ExpanderButton::paintEvent(QPaintEvent *) {
	auto p = QPainter(this);
	Ui::RippleButton::paintRipple(p, QPoint());
	_view->paint(p, 0, 0, width());
}

} // namespace Ui
