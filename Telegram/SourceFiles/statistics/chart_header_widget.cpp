/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_header_widget.h"

#include "ui/painter.h"
#include "styles/style_statistics.h"

namespace Statistic {

void Header::setTitle(QString title) {
	_titleWidth = st::statisticsHeaderTitleTextStyle.font->width(title);
	_title.setText(st::statisticsHeaderTitleTextStyle, std::move(title));
}

void Header::setRightInfo(QString rightInfo) {
	_rightInfo.setText(
		st::statisticsHeaderDatesTextStyle,
		std::move(rightInfo));
}

void Header::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	p.fillRect(rect(), st::boxBg);

	p.setPen(st::boxTextFg);
	const auto top = (height()
		- st::statisticsHeaderTitleTextStyle.font->height) / 2;
	_title.drawLeftElided(p, 0, top, width(), width());
	_rightInfo.drawRightElided(
		p,
		0,
		top,
		width() - _titleWidth,
		width(),
		1,
		style::al_right);
}

} // namespace Statistic
