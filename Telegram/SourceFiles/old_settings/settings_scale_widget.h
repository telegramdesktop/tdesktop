/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "old_settings/settings_block_widget.h"

namespace Ui {
class Checkbox;
class SettingsSlider;
} // namespace Ui

namespace OldSettings {

class ScaleWidget : public BlockWidget {
	Q_OBJECT

public:
	ScaleWidget(QWidget *parent, UserData *self);

private slots:
	void onAutoChanged();

private:
	void scaleChanged();
	void createControls();
	void setScale(DBIScale newScale);

	Ui::Checkbox *_auto = nullptr;
	Ui::SettingsSlider *_scale = nullptr;

	DBIScale _newScale = dbisAuto;
	bool _inSetScale = false;

};

} // namespace Settings
