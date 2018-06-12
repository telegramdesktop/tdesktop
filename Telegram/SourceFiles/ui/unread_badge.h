/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {

class UnreadBadge : public RpWidget {
public:
	using RpWidget::RpWidget;

	void setText(const QString &text, bool active);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _text;
	bool _active = false;

};

} // namespace Ui
