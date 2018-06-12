/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/introwidget.h"

namespace Ui {
class FlatLabel;
class LinkButton;
class RoundButton;
} // namespace Ui

namespace Intro {

class StartWidget : public Widget::Step {
public:
	StartWidget(QWidget *parent, Widget::Data *data);

	void submit() override;
	QString nextButtonText() const override;

};

} // namespace Intro
