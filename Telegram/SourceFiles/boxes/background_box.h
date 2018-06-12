/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class RoundCheckbox;
} // namespace Ui

class BackgroundBox : public BoxContent {
public:
	BackgroundBox(QWidget*);

protected:
	void prepare() override;

private:
	void backgroundChosen(int index);

	class Inner;
	QPointer<Inner> _inner;

};
