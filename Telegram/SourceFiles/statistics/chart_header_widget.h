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

	[[nodiscard]] QString title() const;
	void setTitle(QString title);
	void setRightInfo(QString rightInfo);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	Ui::Text::String _title;
	Ui::Text::String _rightInfo;
	int _infoTop = 0;

};

} // namespace Statistic
