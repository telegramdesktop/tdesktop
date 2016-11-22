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

#include "styles/style_widgets.h"

namespace Ui {

class DiscreteSlider : public TWidget {
public:
	DiscreteSlider(QWidget *parent);

	void addSection(const QString &label);
	void setSections(const QStringList &labels);
	int activeSection() const {
		return _activeIndex;
	}
	void setActiveSection(int index);
	void setActiveSectionFast(int index);
	void setSelectOnPress(bool selectOnPress);

	using SectionActivatedCallback = base::lambda<void()>;
	void setSectionActivatedCallback(SectionActivatedCallback &&callback);

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	int resizeGetHeight(int newWidth) override = 0;

	struct Section {
		Section(const QString &label, const style::font &font);

		int left, width;
		QString label;
		int labelWidth;
	};

	int getCurrentActiveLeft(uint64 ms);

	int getSectionsCount() const {
		return _sections.size();
	}

	template <typename Lambda>
	void enumerateSections(Lambda callback);

	void stopAnimation() {
		_a_left.finish();
	}

private:
	virtual const style::font &getLabelFont() const = 0;
	virtual int getAnimationDuration() const = 0;

	int getIndexFromPosition(QPoint pos);
	void setSelectedSection(int index);

	QList<Section> _sections;
	int _activeIndex = 0;
	bool _selectOnPress = true;

	SectionActivatedCallback _callback;

	bool _pressed = false;
	int _selected = 0;
	FloatAnimation _a_left;

};

class SettingsSlider : public DiscreteSlider {
public:
	SettingsSlider(QWidget *parent, const style::SettingsSlider &st = st::defaultSettingsSlider);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	const style::font &getLabelFont() const override;
	int getAnimationDuration() const override;

	void resizeSections(int newWidth);

	const style::SettingsSlider &_st;

};

} // namespace Ui
