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

class Checkbox;

namespace Settings {

class Slider : public TWidget {
	Q_OBJECT

public:
	Slider(QWidget *parent);

	int activeSection() const {
		return _activeIndex;
	}
	void setActiveSection(int index);
	void setActiveSectionFast(int index);
	void addSection(const QString &label);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	int resizeGetHeight(int newWidth) override;

signals:
	void sectionActivated();

private:
	void resizeSections(int newWidth);
	int getIndexFromPosition(QPoint pos);
	void setSelectedSection(int index);
	void step_left(float64 ms, bool timer);

	struct Section {
		Section(const QString &label);

		int left, width;
		QString label;
		int labelWidth;
	};
	QList<Section> _sections;
	int _activeIndex = 0;

	bool _pressed = false;
	int _selected = 0;
	anim::ivalue a_left = { 0 };
	Animation _a_left;

};

class ScaleWidget : public BlockWidget {
	Q_OBJECT

public:
	ScaleWidget(QWidget *parent, UserData *self);

private slots:
	void onAutoChosen();
	void onSectionActivated();
	void onRestartNow();

private:
	void createControls();
	void setScale(DBIScale newScale);

	ChildWidget<Checkbox> _auto = { nullptr };
	ChildWidget<Slider> _scale = { nullptr };

};

} // namespace Settings
