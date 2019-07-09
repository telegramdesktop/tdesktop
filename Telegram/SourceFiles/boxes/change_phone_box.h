/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

class ChangePhoneBox : public BoxContent {
public:
	ChangePhoneBox(QWidget*) {
	}

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;

private:
	class EnterPhone;
	class EnterCode;

};

