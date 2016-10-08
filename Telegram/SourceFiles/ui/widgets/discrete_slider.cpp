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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "ui/widgets/discrete_slider.h"

#include "styles/style_widgets.h"

namespace Ui {

DiscreteSlider::DiscreteSlider(QWidget *parent) : TWidget(parent)
, _a_left(animation(this, &DiscreteSlider::step_left)) {
	setCursor(style::cur_pointer);
}

void DiscreteSlider::setSectionActivatedCallback(SectionActivatedCallback &&callback) {
	_callback = std_::move(callback);
}

void DiscreteSlider::setActiveSection(int index) {
	setSelectedSection(index);
	if (_activeIndex != index) {
		_activeIndex = index;
		if (_callback) {
			_callback();
		}
	}
}

void DiscreteSlider::setActiveSectionFast(int index) {
	setActiveSection(index);
	a_left.finish();
	_a_left.stop();
	update();
}

void DiscreteSlider::addSection(const QString &label) {
	auto section = Section(label);
	_sections.push_back(section);
}

void DiscreteSlider::resizeSections(int newWidth) {
	auto count = _sections.size();
	if (!count) return;

	auto skips = count - 1;
	auto sectionsWidth = newWidth - skips * st::discreteSliderSkip;
	auto sectionWidth = sectionsWidth / float64(count);
	auto x = 0.;
	for (int i = 0; i != count; ++i) {
		auto &section = _sections[i];
		auto skip = i * st::discreteSliderThickness;
		section.left = qFloor(x) + skip;
		x += sectionWidth;
		section.width = qRound(x) - (section.left - skip);
	}
	a_left = anim::ivalue(_sections[_activeIndex].left, _sections[_activeIndex].left);
	_a_left.stop();
}

void DiscreteSlider::mousePressEvent(QMouseEvent *e) {
	setSelectedSection(getIndexFromPosition(e->pos()));
	_pressed = true;
}

void DiscreteSlider::mouseMoveEvent(QMouseEvent *e) {
	if (!_pressed) return;
	setSelectedSection(getIndexFromPosition(e->pos()));
}

void DiscreteSlider::mouseReleaseEvent(QMouseEvent *e) {
	if (!_pressed) return;
	_pressed = false;
	setActiveSection(getIndexFromPosition(e->pos()));
}

void DiscreteSlider::setSelectedSection(int index) {
	if (index < 0) return;

	if (_selected != index) {
		_selected = index;
		a_left.start(_sections[_selected].left);
		_a_left.start();
	}
}

void DiscreteSlider::paintEvent(QPaintEvent *e) {
	Painter p(this);

	int activeLeft = a_left.current();

	p.setFont(st::discreteSliderLabelFont);
	p.setPen(st::discreteSliderLabelFg);
	for (int i = 0, count = _sections.size(); i != count; ++i) {
		auto &section = _sections.at(i);
		auto from = section.left, tofill = section.width;
		if (activeLeft > from) {
			auto fill = qMin(tofill, activeLeft - from);
			p.fillRect(myrtlrect(from, st::discreteSliderTop, fill, st::discreteSliderThickness), st::discreteSliderInactiveFg);
			from += fill;
			tofill -= fill;
		}
		if (activeLeft + section.width > from) {
			if (auto fill = qMin(tofill, activeLeft + section.width - from)) {
				p.fillRect(myrtlrect(from, st::discreteSliderTop, fill, st::discreteSliderThickness), st::discreteSliderActiveFg);
				from += fill;
				tofill -= fill;
			}
		}
		if (tofill) {
			p.fillRect(myrtlrect(from, st::discreteSliderTop, tofill, st::discreteSliderThickness), st::discreteSliderInactiveFg);
		}
		p.drawTextLeft(section.left + (section.width - section.labelWidth) / 2, st::discreteSliderLabelTop, width(), section.label, section.labelWidth);
	}
}

int DiscreteSlider::resizeGetHeight(int newWidth) {
	resizeSections(newWidth);
	return st::discreteSliderHeight;
}

int DiscreteSlider::getIndexFromPosition(QPoint pos) {
	int count = _sections.size();
	for (int i = 0; i != count; ++i) {
		if (_sections[i].left + _sections[i].width > pos.x()) {
			return i;
		}
	}
	return count - 1;
}

void DiscreteSlider::step_left(float64 ms, bool timer) {
	auto dt = ms / st::discreteSliderDuration;
	if (dt >= 1) {
		a_left.finish();
		_a_left.stop();
	} else {
		a_left.update(dt, anim::linear);
	}
	if (timer) {
		update();
	}
}

DiscreteSlider::Section::Section(const QString &label)
: label(label)
, labelWidth(st::discreteSliderLabelFont->width(label)) {
}

} // namespace Ui
