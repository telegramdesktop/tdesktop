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
	_title.setText(st::statisticsHeaderTitleTextStyle, std::move(title));
}

int Header::resizeGetHeight(int newWidth) {
	return st::statisticsChartHeaderHeight;
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
	_title.drawLeftElided(p, 0, 0, width(), width());

	p.setPen(st::windowSubTextFg);
	_rightInfo.drawLeftElided(p, 0, _infoTop, width(), width());
}

void Header::resizeEvent(QResizeEvent *e) {
	_infoTop = e->size().height()
		- st::statisticsHeaderDatesTextStyle.font->height;
}

} // namespace Statistic
