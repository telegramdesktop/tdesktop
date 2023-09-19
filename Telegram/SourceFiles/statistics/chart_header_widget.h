/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Statistic {

class Header final : public Ui::RpWidget {
public:
	using Ui::RpWidget::RpWidget;

	void setTitle(QString title);
	void setRightInfo(QString rightInfo);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::Text::String _title;
	Ui::Text::String _rightInfo;
	int _titleWidth = 0;

};

} // namespace Statistic
