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
#pragma once

#include "settings/settings_block_widget.h"

namespace Ui {
class Checkbox;
class DiscreteSlider;
} // namespace Ui

namespace Settings {

class ScaleWidget : public BlockWidget {
	Q_OBJECT

public:
	ScaleWidget(QWidget *parent, UserData *self);

private slots:
	void onAutoChosen();
	void onRestartNow();
	void onCancel();

private:
	void scaleChanged();
	void createControls();
	void setScale(DBIScale newScale);

	ChildWidget<Ui::Checkbox> _auto = { nullptr };
	ChildWidget<Ui::DiscreteSlider> _scale = { nullptr };

	DBIScale _newScale = dbisAuto;

};

} // namespace Settings
