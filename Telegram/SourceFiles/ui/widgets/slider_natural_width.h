/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/discrete_sliders.h"

namespace Ui {

class CustomWidthSlider final : public SettingsSlider {
public:
	using Ui::SettingsSlider::SettingsSlider;
	void setNaturalWidth(int w) {
		_naturalWidth = w;
	}
	int naturalWidth() const override {
		return _naturalWidth;
	}

private:
	int _naturalWidth = 0;

};

} // namespace Ui
