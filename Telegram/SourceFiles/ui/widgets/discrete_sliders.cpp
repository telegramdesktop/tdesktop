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
#include "ui/widgets/discrete_sliders.h"

#include "styles/style_widgets.h"

namespace Ui {

DiscreteSlider::DiscreteSlider(QWidget *parent) : TWidget(parent) {
	setCursor(style::cur_pointer);
}

void DiscreteSlider::setSectionActivatedCallback(SectionActivatedCallback &&callback) {
	_callback = std_::move(callback);
}

void DiscreteSlider::setActiveSection(int index) {
	if (_activeIndex != index) {
		_activeIndex = index;
		if (_callback) {
			_callback();
		}
	}
	setSelectedSection(index);
}

void DiscreteSlider::setActiveSectionFast(int index) {
	setActiveSection(index);
	_a_left.finish();
	update();
}

void DiscreteSlider::setSelectOnPress(bool selectOnPress) {
	_selectOnPress = selectOnPress;
}

void DiscreteSlider::addSection(const QString &label) {
	_sections.push_back(Section(label, getLabelFont()));
	resizeToWidth(width());
}

void DiscreteSlider::setSections(const QStringList &labels) {
	t_assert(!labels.isEmpty());

	_sections.clear();
	for_const (auto &label, labels) {
		_sections.push_back(Section(label, getLabelFont()));
	}
	stopAnimation();
	if (_activeIndex >= _sections.size()) {
		_activeIndex = 0;
	}
	if (_selected >= _sections.size()) {
		_selected = 0;
	}
	resizeToWidth(width());
}

int DiscreteSlider::getCurrentActiveLeft(TimeMs ms) {
	return _a_left.current(ms, _sections.isEmpty() ? 0 : _sections[_selected].left);
}

template <typename Lambda>
void DiscreteSlider::enumerateSections(Lambda callback) {
	for (auto &section : _sections) {
		callback(section);
	}
}

void DiscreteSlider::mousePressEvent(QMouseEvent *e) {
	if (_selectOnPress) {
		setSelectedSection(getIndexFromPosition(e->pos()));
	}
	_pressed = true;
}

void DiscreteSlider::mouseMoveEvent(QMouseEvent *e) {
	if (!_pressed) return;
	if (_selectOnPress) {
		setSelectedSection(getIndexFromPosition(e->pos()));
	}
}

void DiscreteSlider::mouseReleaseEvent(QMouseEvent *e) {
	if (!_pressed) return;
	_pressed = false;
	setActiveSection(getIndexFromPosition(e->pos()));
}

void DiscreteSlider::setSelectedSection(int index) {
	if (index < 0 || index >= _sections.size()) return;

	if (_selected != index) {
		auto from = _sections[_selected].left;
		_selected = index;
		auto to = _sections[_selected].left;
		_a_left.start([this] { update(); }, from, to, getAnimationDuration());
	}
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

DiscreteSlider::Section::Section(const QString &label, const style::font &font)
: label(label)
, labelWidth(font->width(label)) {
}

SettingsSlider::SettingsSlider(QWidget *parent, const style::SettingsSlider &st) : DiscreteSlider(parent)
, _st(st) {
}

const style::font &SettingsSlider::getLabelFont() const {
	return _st.labelFont;
}

int SettingsSlider::getAnimationDuration() const {
	return _st.duration;
}

void SettingsSlider::resizeSections(int newWidth) {
	auto count = getSectionsCount();
	if (!count) return;

	auto sectionsWidth = newWidth - (count - 1) * _st.barSkip;
	auto sectionWidth = sectionsWidth / float64(count);
	auto skip = 0;
	auto x = 0.;
	enumerateSections([this, &x, &skip, sectionWidth](Section &section) {
		section.left = qFloor(x) + skip;
		x += sectionWidth;
		section.width = qRound(x) - (section.left - skip);
		skip += _st.barSkip;
	});
	stopAnimation();
}

int SettingsSlider::resizeGetHeight(int newWidth) {
	resizeSections(newWidth);
	return _st.height;
}

void SettingsSlider::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto activeLeft = getCurrentActiveLeft(getms());

	p.setFont(_st.labelFont);
	enumerateSections([this, &p, activeLeft](Section &section) {
		auto from = section.left, tofill = section.width;
		if (activeLeft > from) {
			auto fill = qMin(tofill, activeLeft - from);
			p.fillRect(myrtlrect(from, _st.barTop, fill, _st.barStroke), _st.barFg);
			from += fill;
			tofill -= fill;
		}
		auto active = 1. - snap(qAbs(activeLeft - section.left) / float64(section.width), 0., 1.);
		if (activeLeft + section.width > from) {
			if (auto fill = qMin(tofill, activeLeft + section.width - from)) {
				p.fillRect(myrtlrect(from, _st.barTop, fill, _st.barStroke), _st.barFgActive);
				from += fill;
				tofill -= fill;
			}
		}
		if (tofill) {
			p.fillRect(myrtlrect(from, _st.barTop, tofill, _st.barStroke), _st.barFg);
		}
		p.setPen(anim::pen(_st.labelFg, _st.labelFgActive, active));
		p.drawTextLeft(section.left + (section.width - section.labelWidth) / 2, _st.labelTop, width(), section.label, section.labelWidth);
	});
}

} // namespace Ui
