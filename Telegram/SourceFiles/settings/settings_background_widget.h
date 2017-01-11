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
#pragma once

#include "settings/settings_block_widget.h"
#include "ui/effects/radial_animation.h"
#include "ui/filedialog.h"

namespace Settings {

class BackgroundRow : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	BackgroundRow(QWidget *parent);

	void updateImage();

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

signals:
	void chooseFromGallery();
	void chooseFromFile();
	void useDefault();

private:
	void checkNonDefaultTheme();

	float64 radialProgress() const;
	bool radialLoading() const;
	QRect radialRect() const;
	void radialStart();
	TimeMs radialTimeShift() const;
	void step_radial(TimeMs ms, bool timer);

	QPixmap _background;
	object_ptr<Ui::LinkButton> _useDefaultTheme = { nullptr };
	object_ptr<Ui::LinkButton> _chooseFromGallery;
	object_ptr<Ui::LinkButton> _chooseFromFile;

	Ui::RadialAnimation _radial;

};

class BackgroundWidget : public BlockWidget {
	Q_OBJECT

public:
	BackgroundWidget(QWidget *parent, UserData *self);

private slots:
	void onChooseFromGallery();
	void onChooseFromFile();
	void onUseDefaultTheme();
	void onTile();
	void onAdaptive();

private:
	void createControls();
	void needBackgroundUpdate(bool tile);
	void notifyFileQueryUpdated(const FileDialog::QueryUpdate &update);

	object_ptr<BackgroundRow> _background = { nullptr };
	object_ptr<Ui::Checkbox> _tile = { nullptr };
	object_ptr<Ui::WidgetSlideWrap<Ui::Checkbox>> _adaptive = { nullptr };

	FileDialog::QueryId _chooseFromFileQueryId = 0;

};

} // namespace Settings
