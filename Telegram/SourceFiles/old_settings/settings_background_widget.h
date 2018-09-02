/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "old_settings/settings_block_widget.h"
#include "ui/effects/radial_animation.h"
#include "ui/rp_widget.h"

namespace OldSettings {

class BackgroundRow : public Ui::RpWidget, private base::Subscriber {
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
	void editTheme();
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
	object_ptr<Ui::LinkButton> _editTheme;

	Ui::RadialAnimation _radial;

};

class BackgroundWidget : public BlockWidget {
	Q_OBJECT

public:
	BackgroundWidget(QWidget *parent, UserData *self);

private slots:
	void onChooseFromGallery();
	void onChooseFromFile();
	void onEditTheme();
	void onUseDefaultTheme();
	void onTile();
	void onAdaptive();

private:
	void createControls();
	void needBackgroundUpdate(bool tile);

	BackgroundRow *_background = nullptr;
	Ui::Checkbox *_tile = nullptr;
	Ui::SlideWrap<Ui::Checkbox> *_adaptive = nullptr;

};

} // namespace Settings
