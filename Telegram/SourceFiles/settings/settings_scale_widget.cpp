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
#include "settings/settings_scale_widget.h"

#include "styles/style_settings.h"
#include "lang.h"
#include "localstorage.h"
#include "mainwindow.h"
#include "boxes/confirmbox.h"
#include "application.h"

namespace Settings {
namespace {

QString scaleLabel(DBIScale scale) {
	switch (scale) {
	case dbisOne: return qsl("100%");
	case dbisOneAndQuarter: return qsl("125%");
	case dbisOneAndHalf: return qsl("150%");
	case dbisTwo: return qsl("200%");
	}
	return QString();
}

} // namespace

Slider::Slider(QWidget *parent) : TWidget(parent)
, _a_left(animation(this, &Slider::step_left)) {
	setCursor(style::cur_pointer);
}

void Slider::setActiveSection(int index) {
	setSelectedSection(index);
	if (_activeIndex != index) {
		_activeIndex = index;
		emit sectionActivated();
	}
}

void Slider::setActiveSectionFast(int index) {
	setActiveSection(index);
	a_left.finish();
	_a_left.stop();
	update();
}

void Slider::addSection(const QString &label) {
	auto section = Section(label);
	_sections.push_back(section);
}

void Slider::resizeSections(int newWidth) {
	auto count = _sections.size();
	if (!count) return;

	auto skips = count - 1;
	auto sectionsWidth = newWidth - skips * st::settingsSliderSkip;
	auto sectionWidth = sectionsWidth / float64(count);
	auto x = 0.;
	for (int i = 0; i != count; ++i) {
		auto &section = _sections[i];
		auto skip = i * st::settingsSliderThickness;
		section.left = qFloor(x) + skip;
		x += sectionWidth;
		section.width = qRound(x) - (section.left - skip);
	}
	a_left = anim::ivalue(_sections[_activeIndex].left, _sections[_activeIndex].left);
	_a_left.stop();
}

void Slider::mousePressEvent(QMouseEvent *e) {
	setSelectedSection(getIndexFromPosition(e->pos()));
	_pressed = true;
}

void Slider::mouseMoveEvent(QMouseEvent *e) {
	if (!_pressed) return;
	setSelectedSection(getIndexFromPosition(e->pos()));
}

void Slider::mouseReleaseEvent(QMouseEvent *e) {
	if (!_pressed) return;
	_pressed = false;
	setActiveSection(getIndexFromPosition(e->pos()));
}

void Slider::setSelectedSection(int index) {
	if (index < 0) return;

	if (_selected != index) {
		_selected = index;
		a_left.start(_sections[_selected].left);
		_a_left.start();
	}
}

void Slider::paintEvent(QPaintEvent *e) {
	Painter p(this);

	int activeLeft = a_left.current();

	p.setFont(st::settingsSliderLabelFont);
	p.setPen(st::settingsSliderLabelFg);
	for (int i = 0, count = _sections.size(); i != count; ++i) {
		auto &section = _sections.at(i);
		auto from = section.left, tofill = section.width;
		if (activeLeft > from) {
			auto fill = qMin(tofill, activeLeft - from);
			p.fillRect(myrtlrect(from, st::settingsSliderTop, fill, st::settingsSliderThickness), st::settingsSliderInactiveFg);
			from += fill;
			tofill -= fill;
		}
		if (activeLeft + section.width > from) {
			if (auto fill = qMin(tofill, activeLeft + section.width - from)) {
				p.fillRect(myrtlrect(from, st::settingsSliderTop, fill, st::settingsSliderThickness), st::settingsSliderActiveFg);
				from += fill;
				tofill -= fill;
			}
		}
		if (tofill) {
			p.fillRect(myrtlrect(from, st::settingsSliderTop, tofill, st::settingsSliderThickness), st::settingsSliderInactiveFg);
		}
		p.drawTextLeft(section.left + (section.width - section.labelWidth) / 2, st::settingsSliderLabelTop, width(), section.label, section.labelWidth);
	}
}

int Slider::resizeGetHeight(int newWidth) {
	resizeSections(newWidth);
	return st::settingsSliderHeight;
}

int Slider::getIndexFromPosition(QPoint pos) {
	int count = _sections.size();
	for (int i = 0; i != count; ++i) {
		if (_sections[i].left + _sections[i].width > pos.x()) {
			return i;
		}
	}
	return count - 1;
}

void Slider::step_left(float64 ms, bool timer) {
	auto dt = ms / st::settingsSliderDuration;
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

Slider::Section::Section(const QString &label)
: label(label)
, labelWidth(st::settingsSliderLabelFont->width(label)) {
}

ScaleWidget::ScaleWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_scale)) {
	createControls();
}

void ScaleWidget::createControls() {
	style::margins margin(0, 0, 0, st::settingsSmallSkip);

	addChildRow(_auto, margin, lng_settings_scale_auto(lt_cur, scaleLabel(cScreenScale())), SLOT(onAutoChosen()), (cConfigScale() == dbisAuto));
	addChildRow(_scale, style::margins(0, 0, 0, 0));

	_scale->addSection(scaleLabel(dbisOne));
	_scale->addSection(scaleLabel(dbisOneAndQuarter));
	_scale->addSection(scaleLabel(dbisOneAndHalf));
	_scale->addSection(scaleLabel(dbisTwo));
	_scale->setActiveSectionFast(cEvalScale(cConfigScale()) - 1);

	connect(_scale, SIGNAL(sectionActivated()), this, SLOT(onSectionActivated()));
}

void ScaleWidget::onAutoChosen() {
	auto newScale = _auto->checked() ? dbisAuto : cEvalScale(cConfigScale());
	if (newScale == cScreenScale()) {
		if (newScale != cScale()) {
			newScale = cScale();
		} else {
			switch (newScale) {
			case dbisOne: newScale = dbisOneAndQuarter; break;
			case dbisOneAndQuarter: newScale = dbisOne; break;
			case dbisOneAndHalf: newScale = dbisOneAndQuarter; break;
			case dbisTwo: newScale = dbisOneAndHalf; break;
			}
		}
	}
	setScale(newScale);
}

void ScaleWidget::setScale(DBIScale newScale) {
	if (cConfigScale() == newScale) return;

	cSetConfigScale(newScale);
	Local::writeSettings();
	App::wnd()->getTitle()->showUpdateBtn();
	if (newScale == dbisAuto && !_auto->checked()) {
		_auto->setChecked(true);
	} else if (newScale != dbisAuto && _auto->checked()) {
		_auto->setChecked(false);
	}
	if (newScale == dbisAuto) newScale = cScreenScale();
	if (_scale->activeSection() != newScale - 1) {
		_scale->setActiveSection(newScale - 1);
	}
	if (cEvalScale(cConfigScale()) != cEvalScale(cRealScale())) {
		auto box = new ConfirmBox(lang(lng_settings_need_restart), lang(lng_settings_restart_now), st::defaultBoxButton, lang(lng_settings_restart_later));
		connect(box, SIGNAL(confirmed()), this, SLOT(onRestartNow()));
		Ui::showLayer(box);
	}
}

void ScaleWidget::onSectionActivated() {
	auto newScale = dbisAuto;
	switch (_scale->activeSection()) {
	case 0: newScale = dbisOne; break;
	case 1: newScale = dbisOneAndQuarter; break;
	case 2: newScale = dbisOneAndHalf; break;
	case 3: newScale = dbisTwo; break;
	}
	if (newScale == cScreenScale()) {
		newScale = dbisAuto;
	}
	setScale(newScale);
}

void ScaleWidget::onRestartNow() {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	bool updateReady = (Sandbox::updatingState() == Application::UpdatingReady);
#else
	bool updateReady = false;
#endif
	if (updateReady) {
		cSetRestartingUpdate(true);
	} else {
		cSetRestarting(true);
		cSetRestartingToSettings(true);
	}
	App::quit();
}

} // namespace Settings
