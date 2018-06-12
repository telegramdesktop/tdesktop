/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
	_scale->sectionActivated(
	) | rpl::start_with_next(
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
		Ui::show(Box<ConfirmBox>(lang(lng_settings_need_restart), lang(lng_settings_restart_now), crl::guard(this, [this] {
			cSetConfigScale(_newScale);
			Local::writeSettings();
			App::restart();
		}), crl::guard(this, [this] {
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
