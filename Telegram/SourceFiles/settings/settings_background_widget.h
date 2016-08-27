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
#include "ui/filedialog.h"

class LinkButton;
class Checkbox;
namespace Ui {
template <typename Widget>
class WidgetSlideWrap;
} // namespace Ui;

namespace Settings {

class BackgroundRow : public TWidget {
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

private:
	float64 radialProgress() const;
	bool radialLoading() const;
	QRect radialRect() const;
	void radialStart();
	uint64 radialTimeShift() const;
	void step_radial(uint64 ms, bool timer);

	QPixmap _background;
	ChildWidget<LinkButton> _chooseFromGallery;
	ChildWidget<LinkButton> _chooseFromFile;

	RadialAnimation _radial;

};

class BackgroundWidget : public BlockWidget {
	Q_OBJECT

public:
	BackgroundWidget(QWidget *parent, UserData *self);

private slots:
	void onChooseFromGallery();
	void onChooseFromFile();
	void onTile();
	void onAdaptive();

private:
	void createControls();
	void needBackgroundUpdate(bool tile);
	void notifyFileQueryUpdated(const FileDialog::QueryUpdate &update);

	ChildWidget<BackgroundRow> _background = { nullptr };
	ChildWidget<Checkbox> _tile = { nullptr };
	ChildWidget<Ui::WidgetSlideWrap<Checkbox>> _adaptive = { nullptr };

	FileDialog::QueryId _chooseFromFileQueryId = 0;

};

} // namespace Settings
