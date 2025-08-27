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
	explicit Header(not_null<Ui::RpWidget*> parent);

	[[nodiscard]] QString title() const;
	void setTitle(QString title);
	void setSubTitle(QString subTitle);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	Ui::Text::String _title;
	Ui::Text::String _subTitle;
	int _infoTop = 0;
	int _height = 0;

};

} // namespace Statistic
