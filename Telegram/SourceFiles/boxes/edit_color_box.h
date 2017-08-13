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

#include "boxes/abstract_box.h"

class EditColorBox : public BoxContent {
	Q_OBJECT

public:
	EditColorBox(QWidget*, const QString &title, QColor current = QColor(255, 255, 255));

	void setSaveCallback(base::lambda<void(QColor)> callback) {
		_saveCallback = std::move(callback);
	}

	void setCancelCallback(base::lambda<void()> callback) {
		_cancelCallback = std::move(callback);
	}

	void showColor(QColor color) {
		updateFromColor(color);
	}

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void mousePressEvent(QMouseEvent *e) override;

	void setInnerFocus() override;

private slots:
	void onFieldChanged();
	void onFieldSubmitted();

private:
	void saveColor();

	void updateFromColor(QColor color);
	void updateControlsFromColor();
	void updateControlsFromHSV(int hue, int saturation, int brightness);
	void updateHSVFields();
	void updateRGBFields();
	void updateResultField();
	void updateFromControls();
	void updateFromHSVFields();
	void updateFromRGBFields();
	void updateFromResultField();
	void setHSV(int hue, int saturation, int brightness, int alpha);
	void setRGB(int red, int green, int blue, int alpha);

	int percentFromByte(int byte) {
		return snap(qRound(byte * 100 / 255.), 0, 100);
	}
	int percentToByte(int percent) {
		return snap(qRound(percent * 255 / 100.), 0, 255);
	}

	class Picker;
	class Slider;
	class Field;
	class ResultField;

	QString _title;

	object_ptr<Picker> _picker;
	object_ptr<Slider> _hueSlider;
	object_ptr<Slider> _opacitySlider;

	object_ptr<Field> _hueField;
	object_ptr<Field> _saturationField;
	object_ptr<Field> _brightnessField;
	object_ptr<Field> _redField;
	object_ptr<Field> _greenField;
	object_ptr<Field> _blueField;
	object_ptr<ResultField> _result;

	QBrush _transparent;
	QColor _current;
	QColor _new;

	QRect _currentRect;
	QRect _newRect;

	base::lambda<void(QColor)> _saveCallback;
	base::lambda<void()> _cancelCallback;

};
