/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/widgets/checkbox.h"

#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"

namespace Ui {
namespace {

TextParseOptions _checkboxOptions = {
	TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

} // namespace

AbstractCheckView::AbstractCheckView(int duration, bool checked, base::lambda<void()> updateCallback)
: _duration(duration)
, _checked(checked)
, _updateCallback(std::move(updateCallback)) {
}

void AbstractCheckView::setCheckedFast(bool checked) {
	_checked = checked;
	finishAnimation();
	if (_updateCallback) {
		_updateCallback();
	}
}

void AbstractCheckView::setUpdateCallback(base::lambda<void()> updateCallback) {
	_updateCallback = std::move(updateCallback);
	if (_toggleAnimation.animating()) {
		_toggleAnimation.setUpdateCallback(_updateCallback);
	}
}

void AbstractCheckView::setCheckedAnimated(bool checked) {
	if (_checked != checked) {
		_checked = checked;
		_toggleAnimation.start(_updateCallback, _checked ? 0. : 1., _checked ? 1. : 0., _duration);
	}
}

void AbstractCheckView::finishAnimation() {
	_toggleAnimation.finish();
}

float64 AbstractCheckView::currentAnimationValue(TimeMs ms) {
	return ms ? _toggleAnimation.current(ms, _checked ? 1. : 0.) : _toggleAnimation.current(_checked ? 1. : 0.);
}

ToggleView::ToggleView(const style::Toggle &st, bool checked, base::lambda<void()> updateCallback) : AbstractCheckView(st.duration, checked, std::move(updateCallback))
, _st(&st) {
}

QSize ToggleView::getSize() {
	return QSize(_st->diameter + _st->width, _st->diameter);
}

void ToggleView::setStyle(const style::Toggle &st) {
	_st = &st;
}

void ToggleView::paint(Painter &p, int left, int top, int outerWidth, TimeMs ms) {
	PainterHighQualityEnabler hq(p);
	auto toggled = currentAnimationValue(ms);
	auto fullWidth = _st->diameter + _st->width;
	auto innerDiameter = _st->diameter - 2 * _st->shift;
	auto innerRadius = float64(innerDiameter) / 2.;
	auto bgRect = rtlrect(left + _st->shift, top + _st->shift, fullWidth - 2 * _st->shift, innerDiameter, outerWidth);
	auto fgRect = rtlrect(left + anim::interpolate(0, fullWidth - _st->diameter, toggled), top, _st->diameter, _st->diameter, outerWidth);

	p.setPen(Qt::NoPen);
	p.setBrush(anim::brush(_st->untoggledFg, _st->toggledFg, toggled));
	p.drawRoundedRect(bgRect, innerRadius, innerRadius);

	auto pen = anim::pen(_st->untoggledFg, _st->toggledFg, toggled);
	pen.setWidth(_st->border);
	p.setPen(pen);
	p.setBrush(anim::brush(_st->untoggledBg, _st->toggledBg, toggled));
	p.drawEllipse(fgRect);
}

CheckView::CheckView(const style::Check &st, bool checked, base::lambda<void()> updateCallback) : AbstractCheckView(st.duration, checked, std::move(updateCallback))
, _st(&st) {
}

QSize CheckView::getSize() {
	return QSize(_st->diameter, _st->diameter);
}

void CheckView::setStyle(const style::Check &st) {
	_st = &st;
}

void CheckView::paint(Painter &p, int left, int top, int outerWidth, TimeMs ms) {
	auto toggled = currentAnimationValue(ms);
	auto pen = anim::pen(_st->untoggledFg, _st->toggledFg, toggled);
	pen.setWidth(_st->thickness);
	p.setPen(pen);
	p.setBrush(anim::brush(_st->bg, anim::color(_st->untoggledFg, _st->toggledFg, toggled), toggled));

	{
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(rtlrect(QRectF(left, top, _st->diameter, _st->diameter).marginsRemoved(QMarginsF(_st->thickness / 2., _st->thickness / 2., _st->thickness / 2., _st->thickness / 2.)), outerWidth), st::buttonRadius - (_st->thickness / 2.), st::buttonRadius - (_st->thickness / 2.));
	}

	if (toggled > 0) {
		_st->icon.paint(p, QPoint(left, top), outerWidth);
	}
}

RadioView::RadioView(const style::Radio &st, bool checked, base::lambda<void()> updateCallback) : AbstractCheckView(st.duration, checked, std::move(updateCallback))
, _st(&st) {
}

QSize RadioView::getSize() {
	return QSize(_st->diameter, _st->diameter);
}

void RadioView::setStyle(const style::Radio &st) {
	_st = &st;
}

void RadioView::paint(Painter &p, int left, int top, int outerWidth, TimeMs ms) {
	PainterHighQualityEnabler hq(p);

	auto toggled = currentAnimationValue(ms);
	auto pen = anim::pen(_st->untoggledFg, _st->toggledFg, toggled);
	pen.setWidth(_st->thickness);
	p.setPen(pen);
	p.setBrush(_st->bg);
	//int32 skip = qCeil(_st->thickness / 2.);
	//p.drawEllipse(_checkRect.marginsRemoved(QMargins(skip, skip, skip, skip)));
	p.drawEllipse(rtlrect(QRectF(left, top, _st->diameter, _st->diameter).marginsRemoved(QMarginsF(_st->thickness / 2., _st->thickness / 2., _st->thickness / 2., _st->thickness / 2.)), outerWidth));

	if (toggled > 0) {
		p.setPen(Qt::NoPen);
		p.setBrush(anim::brush(_st->untoggledFg, _st->toggledFg, toggled));

		auto skip0 = _st->diameter / 2., skip1 = _st->skip / 10., checkSkip = skip0 * (1. - toggled) + skip1 * toggled;
		p.drawEllipse(rtlrect(QRectF(left, top, _st->diameter, _st->diameter).marginsRemoved(QMarginsF(checkSkip, checkSkip, checkSkip, checkSkip)), outerWidth));
		//int32 fskip = qFloor(checkSkip), cskip = qCeil(checkSkip);
		//if (2 * fskip < _checkRect.width()) {
		//	if (fskip != cskip) {
		//		p.setOpacity(float64(cskip) - checkSkip);
		//		p.drawEllipse(_checkRect.marginsRemoved(QMargins(fskip, fskip, fskip, fskip)));
		//		p.setOpacity(1.);
		//	}
		//	if (2 * cskip < _checkRect.width()) {
		//		p.drawEllipse(_checkRect.marginsRemoved(QMargins(cskip, cskip, cskip, cskip)));
		//	}
		//}
	}
}

Checkbox::Checkbox(QWidget *parent, const QString &text, bool checked, const style::Checkbox &st, const style::Check &checkSt) : Checkbox(parent, text, st, std::make_unique<CheckView>(checkSt, checked, [this] { updateCheck(); })) {
}

Checkbox::Checkbox(QWidget *parent, const QString &text, bool checked, const style::Checkbox &st, const style::Toggle &toggleSt) : Checkbox(parent, text, st, std::make_unique<ToggleView>(toggleSt, checked, [this] { updateCheck(); })) {
}

Checkbox::Checkbox(QWidget *parent, const QString &text, const style::Checkbox &st, std::unique_ptr<AbstractCheckView> check) : RippleButton(parent, st.ripple)
, _st(st)
, _check(std::move(check))
, _text(_st.style, text, _checkboxOptions) {
	resizeToText();
	setCursor(style::cur_pointer);
}

void Checkbox::setText(const QString &text) {
	_text.setText(_st.style, text, _checkboxOptions);
	resizeToText();
	update();
}

bool Checkbox::checked() const {
	return _check->checked();
}

void Checkbox::resizeToText() {
	if (_st.width <= 0) {
		resizeToWidth(_text.maxWidth() - _st.width);
	} else {
		resizeToWidth(_st.width);
	}
	_checkRect = { QPoint(_st.margin.left(), _st.margin.top()), _check->getSize() };
}

void Checkbox::setChecked(bool checked, NotifyAboutChange notify) {
	if (_check->checked() != checked) {
		_check->setCheckedAnimated(checked);
		if (notify == NotifyAboutChange::Notify) {
			checkedChanged.notify(checked, true);
		}
	}
}

void Checkbox::finishAnimations() {
	_check->finishAnimation();
}

int Checkbox::naturalWidth() const {
	return _st.textPosition.x() + _text.maxWidth();
}

void Checkbox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto active = _check->currentAnimationValue(ms);
	auto color = anim::color(_st.rippleBg, _st.rippleBgActive, active);
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms, &color);

	auto realCheckRect = myrtlrect(_checkRect);
	if (realCheckRect.intersects(e->rect())) {
		_check->paint(p, _checkRect.left(), _checkRect.top(), width());
	}
	if (realCheckRect.contains(e->rect())) return;

	auto textWidth = qMax(width() - (_st.textPosition.x() + (_st.textPosition.x() - _check->getSize().width())), 1);

	p.setPen(_st.textFg);
	_text.drawLeftElided(p, _st.margin.left() + _st.textPosition.x(), _st.margin.top() + _st.textPosition.y(), textWidth, width());
}

void Checkbox::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	if (isDisabled() && !(was & StateFlag::Disabled)) {
		setCursor(style::cur_default);
	} else if (!isDisabled() && (was & StateFlag::Disabled)) {
		setCursor(style::cur_pointer);
	}

	auto now = state();
	if (!isDisabled() && (was & StateFlag::Over) && (now & StateFlag::Over)) {
		if ((was & StateFlag::Down) && !(now & StateFlag::Down)) {
			setChecked(!checked());
		}
	}
}

int Checkbox::resizeGetHeight(int newWidth) {
	return _st.height;
}

QImage Checkbox::prepareRippleMask() const {
	return RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint Checkbox::prepareRippleStartPosition() const {
	auto position = mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
	if (QRect(0, 0, _st.rippleAreaSize, _st.rippleAreaSize).contains(position)) {
		return position;
	}
	return disabledRippleStartPosition();
}

void RadiobuttonGroup::setValue(int value) {
	if (_hasValue && _value == value) {
		return;
	}
	_hasValue = true;
	_value = value;
	for (auto button : _buttons) {
		button->handleNewGroupValue(_value);
	}
	if (_changedCallback) {
		_changedCallback(_value);
	}
}

Radiobutton::Radiobutton(QWidget *parent, const std::shared_ptr<RadiobuttonGroup> &group, int value, const QString &text, const style::Checkbox &st, const style::Radio &radioSt) : Checkbox(parent, text, st, std::make_unique<RadioView>(radioSt, (group->hasValue() && group->value() == value), [this] { updateCheck(); }))
, _group(group)
, _value(value) {
	_group->registerButton(this);
	subscribe(checkbox()->checkedChanged, [this](bool checked) {
		if (checked) {
			_group->setValue(_value);
		}
	});
}

void Radiobutton::handleNewGroupValue(int value) {
	auto checked = (value == _value);
	if (checkbox()->checked() != checked) {
		checkbox()->setChecked(checked, Ui::Checkbox::NotifyAboutChange::DontNotify);
	}
}

void Radiobutton::onStateChanged(State was, StateChangeSource source) {
	Checkbox::onStateChanged(was, source);

	auto now = state();
	if (!isDisabled() && (was & StateFlag::Over) && (now & StateFlag::Over)) {
		if ((was & StateFlag::Down) && !(now & StateFlag::Down)) {
			_group->setValue(_value);
		}
	}
}

Radiobutton::~Radiobutton() {
	_group->unregisterButton(this);
}

} // namespace Ui
