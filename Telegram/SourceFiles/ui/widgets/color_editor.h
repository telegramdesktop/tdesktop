/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

class ColorEditor : public Ui::RpWidget {
public:
	enum class Mode {
		RGBA,
		HSL,
	};
	ColorEditor(
		QWidget *parent,
		Mode mode,
		QColor current);

	void setLightnessLimits(int min, int max);

	[[nodiscard]] QColor color() const;
	[[nodiscard]] rpl::producer<QColor> colorValue() const;
	[[nodiscard]] rpl::producer<> submitRequests() const;

	void showColor(QColor color);
	void setCurrent(QColor color);

	void setInnerFocus() const;

protected:
	void prepare();

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void mousePressEvent(QMouseEvent *e) override;

private:
	struct HSB { // HSV or HSL depending on Mode.
		int hue = 0;
		int saturation = 0;
		int brightness = 0;
	};
	void fieldSubmitted();

	[[nodiscard]] HSB hsbFromControls() const;
	void updateFromColor(QColor color);
	void updateControlsFromColor();
	void updateControlsFromHSB(HSB hsb);
	void updateHSBFields();
	void updateRGBFields();
	void updateResultField();
	void updateFromControls();
	void updateFromHSBFields();
	void updateFromRGBFields();
	void updateFromResultField();
	void setHSB(HSB hsb, int alpha);
	void setRGB(int red, int green, int blue, int alpha);
	[[nodiscard]] QColor applyLimits(QColor color) const;

	int percentFromByte(int byte) {
		return std::clamp(qRound(byte * 100 / 255.), 0, 100);
	}
	int percentToByte(int percent) {
		return std::clamp(qRound(percent * 255 / 100.), 0, 255);
	}

	class Picker;
	class Slider;
	class Field;
	class ResultField;

	Mode _mode = Mode();

	object_ptr<Picker> _picker;
	object_ptr<Slider> _hueSlider = { nullptr };
	object_ptr<Slider> _opacitySlider = { nullptr };
	object_ptr<Slider> _lightnessSlider = { nullptr };

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

	int _lightnessMin = 0;
	int _lightnessMax = 255;

	rpl::event_stream<> _submitRequests;
	rpl::event_stream<QColor> _newChanges;

};
