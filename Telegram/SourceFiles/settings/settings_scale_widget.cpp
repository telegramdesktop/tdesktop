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
#include "settings/settings_scale_widget.h"

#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "ui/widgets/checkbox.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "mainwindow.h"
#include "boxes/confirm_box.h"
#include "application.h"
#include "ui/widgets/discrete_sliders.h"

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

ScaleWidget::ScaleWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_scale)) {
	createControls();
}

void ScaleWidget::createControls() {
	style::margins margin(0, 0, 0, st::settingsSmallSkip);

	createChildRow(_auto, margin, lng_settings_scale_auto(lt_cur, scaleLabel(cScreenScale())), [this](bool) { onAutoChanged(); }, (cConfigScale() == dbisAuto));
	createChildRow(_scale, style::margins(0, 0, 0, 0));

	_scale->addSection(scaleLabel(dbisOne));
	_scale->addSection(scaleLabel(dbisOneAndQuarter));
	_scale->addSection(scaleLabel(dbisOneAndHalf));
	_scale->addSection(scaleLabel(dbisTwo));
	_scale->setActiveSectionFast(cEvalScale(cConfigScale()) - 1);
	_scale->sectionActivated()
		| rpl::start_with_next(
			[this] { scaleChanged(); },
			lifetime());
}

void ScaleWidget::onAutoChanged() {
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
	if (_inSetScale) return;
	_inSetScale = true;
	auto guard = gsl::finally([this] { _inSetScale = false; });

	if (newScale == cScreenScale()) newScale = dbisAuto;
	if (newScale == dbisAuto && !_auto->checked()) {
		_auto->setChecked(true);
	} else if (newScale != dbisAuto && _auto->checked()) {
		_auto->setChecked(false);
	}
	_newScale = newScale;
	if (newScale == dbisAuto) newScale = cScreenScale();
	if (_scale->activeSection() != newScale - 1) {
		_scale->setActiveSection(newScale - 1);
	}

	if (cEvalScale(newScale) != cEvalScale(cRealScale())) {
		Ui::show(Box<ConfirmBox>(lang(lng_settings_need_restart), lang(lng_settings_restart_now), base::lambda_guarded(this, [this] {
			cSetConfigScale(_newScale);
			Local::writeSettings();
			App::restart();
		}), base::lambda_guarded(this, [this] {
			App::CallDelayed(st::boxDuration, this, [this] {
				setScale(cRealScale());
			});
		})));
	} else {
		cSetConfigScale(newScale);
		Local::writeSettings();
	}
}

void ScaleWidget::scaleChanged() {
	auto newScale = dbisAuto;
	switch (_scale->activeSection()) {
	case 0: newScale = dbisOne; break;
	case 1: newScale = dbisOneAndQuarter; break;
	case 2: newScale = dbisOneAndHalf; break;
	case 3: newScale = dbisTwo; break;
	}
	setScale(newScale);
}

} // namespace Settings
