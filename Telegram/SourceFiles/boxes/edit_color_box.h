/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
