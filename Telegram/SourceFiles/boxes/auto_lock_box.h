/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"

namespace Ui {
class Radiobutton;
} // namespace Ui

class AutoLockBox : public Ui::BoxContent {
public:
	AutoLockBox(QWidget*);

protected:
	void prepare() override;

private:
	void durationChanged(int seconds);

	std::vector<object_ptr<Ui::Radiobutton>> _options;

};
