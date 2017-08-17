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

QSize ToggleView::getSize() const {
	return QSize(2 * _st->border + _st->diameter + _st->width, 2 * _st->border + _st->diameter);
}

void ToggleView::setStyle(const style::Toggle &st) {
	_st = &st;
}

void ToggleView::paint(Painter &p, int left, int top, int outerWidth, TimeMs ms) {
	left += _st->border;
	top += _st->border;

	PainterHighQualityEnabler hq(p);
	auto toggled = currentAnimationValue(ms);
	auto fullWidth = _st->diameter + _st->width;
	auto innerDiameter = _st->diameter - 2 * _st->shift;
	auto innerRadius = float64(innerDiameter) / 2.;
	auto toggleLeft = left + anim::interpolate(0, fullWidth - _st->diameter, toggled);
	auto bgRect = rtlrect(left + _st->shift, top + _st->shift, fullWidth - 2 * _st->shift, innerDiameter, outerWidth);
	auto fgRect = rtlrect(toggleLeft, top, _st->diameter, _st->diameter, outerWidth);
	auto fgBrush = anim::brush(_st->untoggledFg, _st->toggledFg, toggled);

	p.setPen(Qt::NoPen);
	p.setBrush(fgBrush);
	p.drawRoundedRect(bgRect, innerRadius, innerRadius);

	auto pen = anim::pen(_st->untoggledFg, _st->toggledFg, toggled);
	pen.setWidth(_st->border);
	p.setPen(pen);
	p.setBrush(anim::brush(_st->untoggledBg, _st->toggledBg, toggled));
	p.drawEllipse(fgRect);

	if (_st->xsize > 0) {
		paintXV(p, toggleLeft, top, outerWidth, toggled, fgBrush);
	}
}

void ToggleView::paintXV(Painter &p, int left, int top, int outerWidth, float64 toggled, const QBrush &brush) {
	Assert(_st->vsize > 0);
	Assert(_st->stroke > 0);
	static const auto sqrt2 = sqrt(2.);
	auto stroke = (0. + _st->stroke) / sqrt2;
	if (toggled < 1) {
		// Just X or X->V.
		auto xSize = 0. + _st->xsize;
		auto xLeft = left + (_st->diameter - xSize) / 2.;
		auto xTop = top + (_st->diameter - xSize) / 2.;
		QPointF pathX[] = {
			{ xLeft, xTop + stroke },
			{ xLeft + stroke, xTop },
			{ xLeft + (xSize / 2.), xTop + (xSize / 2.) - stroke },
			{ xLeft + xSize - stroke, xTop },
			{ xLeft + xSize, xTop + stroke },
			{ xLeft + (xSize / 2.) + stroke, xTop + (xSize / 2.) },
			{ xLeft + xSize, xTop + xSize - stroke },
			{ xLeft + xSize - stroke, xTop + xSize },
			{ xLeft + (xSize / 2.), xTop + (xSize / 2.) + stroke },
			{ xLeft + stroke, xTop + xSize },
			{ xLeft, xTop + xSize - stroke },
			{ xLeft + (xSize / 2.) - stroke, xTop + (xSize / 2.) },
		};
		for (auto &point : pathX) {
			point = rtlpoint(point, outerWidth);
		}
		if (toggled > 0) {
			// X->V.
			auto vSize = 0. + _st->vsize;
			auto fSize = (xSize + vSize - 2. * stroke);
			auto vLeft = left + (_st->diameter - fSize) / 2.;
			auto vTop = 0. + xTop + _st->vshift;
			QPointF pathV[] = {
				{ vLeft, vTop + xSize - vSize + stroke },
				{ vLeft + stroke, vTop + xSize - vSize },
				{ vLeft + vSize - stroke, vTop + xSize - 2 * stroke },
				{ vLeft + fSize - stroke, vTop },
				{ vLeft + fSize, vTop + stroke },
				{ vLeft + vSize, vTop + xSize - stroke },
				{ vLeft + vSize, vTop + xSize - stroke },
				{ vLeft + vSize - stroke, vTop + xSize },
				{ vLeft + vSize - stroke, vTop + xSize },
				{ vLeft + vSize - stroke, vTop + xSize },
				{ vLeft + vSize - 2 * stroke, vTop + xSize - stroke },
				{ vLeft + vSize - 2 * stroke, vTop + xSize - stroke },
			};
			for (auto &point : pathV) {
				point = rtlpoint(point, outerWidth);
			}
			p.fillPath(anim::interpolate(pathX, pathV, toggled), brush);
		} else {
			// Just X.
			p.fillPath(anim::path(pathX), brush);
		}
	} else {
		// Just V.
		auto xSize = 0. + _st->xsize;
		auto xTop = top + (_st->diameter - xSize) / 2.;
		auto vSize = 0. + _st->vsize;
		auto fSize = (xSize + vSize - 2. * stroke);
		auto vLeft = left + (_st->diameter - (_st->xsize + _st->vsize - 2. * stroke)) / 2.;
		auto vTop = 0. + xTop + _st->vshift;
		QPointF pathV[] = {
			{ vLeft, vTop + xSize - vSize + stroke },
			{ vLeft + stroke, vTop + xSize - vSize },
			{ vLeft + vSize - stroke, vTop + xSize - 2 * stroke },
			{ vLeft + fSize - stroke, vTop },
			{ vLeft + fSize, vTop + stroke },
			{ vLeft + vSize, vTop + xSize - stroke },
			{ vLeft + vSize, vTop + xSize - stroke },
			{ vLeft + vSize - stroke, vTop + xSize },
			{ vLeft + vSize - stroke, vTop + xSize },
			{ vLeft + vSize - stroke, vTop + xSize },
			{ vLeft + vSize - 2 * stroke, vTop + xSize - stroke },
			{ vLeft + vSize - 2 * stroke, vTop + xSize - stroke },
		};

		p.fillPath(anim::path(pathV), brush);
	}
}

QSize ToggleView::rippleSize() const {
	return getSize() + 2 * QSize(_st->rippleAreaPadding, _st->rippleAreaPadding);
}

QImage ToggleView::prepareRippleMask() const {
	auto size = rippleSize();
	return RippleAnimation::roundRectMask(size, size.height() / 2);
}

bool ToggleView::checkRippleStartPosition(QPoint position) const {
	return QRect(QPoint(0, 0), rippleSize()).contains(position);
}

CheckView::CheckView(const style::Check &st, bool checked, base::lambda<void()> updateCallback) : AbstractCheckView(st.duration, checked, std::move(updateCallback))
, _st(&st) {
}

QSize CheckView::getSize() const {
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

QSize CheckView::rippleSize() const {
	return getSize() + 2 * QSize(_st->rippleAreaPadding, _st->rippleAreaPadding);
}

QImage CheckView::prepareRippleMask() const {
	return RippleAnimation::ellipseMask(rippleSize());
}

bool CheckView::checkRippleStartPosition(QPoint position) const {
	return QRect(QPoint(0, 0), rippleSize()).contains(position);
}

RadioView::RadioView(const style::Radio &st, bool checked, base::lambda<void()> updateCallback) : AbstractCheckView(st.duration, checked, std::move(updateCallback))
, _st(&st) {
}

QSize RadioView::getSize() const {
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

QSize RadioView::rippleSize() const {
	return getSize() + 2 * QSize(_st->rippleAreaPadding, _st->rippleAreaPadding);
}

QImage RadioView::prepareRippleMask() const {
	return RippleAnimation::ellipseMask(rippleSize());
}

bool RadioView::checkRippleStartPosition(QPoint position) const {
	return QRect(QPoint(0, 0), rippleSize()).contains(position);
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
	return _checkRect.width() + _st.textPosition.x() + _text.maxWidth();
}

void Checkbox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	if (isDisabled()) {
		p.setOpacity(_st.disabledOpacity);
	} else {
		auto active = _check->currentAnimationValue(ms);
		auto color = anim::color(_st.rippleBg, _st.rippleBgActive, active);
		paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms, &color);
	}

	auto realCheckRect = myrtlrect(_checkRect);
	if (realCheckRect.intersects(e->rect())) {
		if (isDisabled()) {
			p.drawPixmapLeft(_checkRect.left(), _checkRect.top(), width(), _checkCache);
		} else {
			_check->paint(p, _checkRect.left(), _checkRect.top(), width());
		}
	}
	if (realCheckRect.contains(e->rect())) return;

	auto textWidth = qMax(width() - (_checkRect.width() + _st.textPosition.x() + _st.textPosition.x()), 1);

	p.setPen(_st.textFg);
	_text.drawLeftElided(p, _st.margin.left() + _checkRect.width() + _st.textPosition.x(), _st.margin.top() + _st.textPosition.y(), textWidth, width());
}

QPixmap Checkbox::grabCheckCache() const {
	auto image = QImage(_checkRect.size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&image);
		_check->paint(p, 0, 0, _checkRect.width());
	}
	return App::pixmapFromImageInPlace(std::move(image));
}

void Checkbox::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	if (isDisabled() && !(was & StateFlag::Disabled)) {
		setCursor(style::cur_default);
		finishAnimations();
		_checkCache = grabCheckCache();
	} else if (!isDisabled() && (was & StateFlag::Disabled)) {
		setCursor(style::cur_pointer);
		_checkCache = QPixmap();
	}

	auto now = state();
	if (!isDisabled() && (was & StateFlag::Over) && (now & StateFlag::Over)) {
		if ((was & StateFlag::Down) && !(now & StateFlag::Down)) {
			handlePress();
		}
	}
}

void Checkbox::handlePress() {
	setChecked(!checked());
}

int Checkbox::resizeGetHeight(int newWidth) {
	return _check->getSize().height();
}

QImage Checkbox::prepareRippleMask() const {
	return _check->prepareRippleMask();
}

QPoint Checkbox::prepareRippleStartPosition() const {
	if (isDisabled()) {
		return DisabledRippleStartPosition();
	}
	auto position = mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
	return _check->checkRippleStartPosition(position) ? position : DisabledRippleStartPosition();
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

void Radiobutton::handlePress() {
	if (!checkbox()->checked()) {
		checkbox()->setChecked(true);
	}
}

Radiobutton::~Radiobutton() {
	_group->unregisterButton(this);
}

} // namespace Ui
