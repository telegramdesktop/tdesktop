/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/widgets/chart_header_widget.h"

#include "ui/painter.h"
#include "styles/style_statistics.h"

namespace Statistic {

Header::Header(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent)
, _height(st::statisticsChartHeaderHeight) {
}

QString Header::title() const {
	return _title.toString();
}

void Header::setTitle(QString title) {
	_title.setText(st::statisticsHeaderTitleTextStyle, std::move(title));
}

int Header::resizeGetHeight(int newWidth) {
	return _height;
}

void Header::setSubTitle(QString subTitle) {
	_height = subTitle.isEmpty()
		? st::statisticsHeaderTitleTextStyle.font->height
		: st::statisticsChartHeaderHeight;
	_subTitle.setText(
		st::statisticsHeaderDatesTextStyle,
		std::move(subTitle));
}

void Header::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	p.fillRect(rect(), st::boxBg);

	p.setPen(st::boxTextFg);
	_title.drawLeftElided(p, 0, 0, width(), width());

	p.setPen(st::windowSubTextFg);
	_subTitle.drawLeftElided(p, 0, _infoTop, width(), width());
}

void Header::resizeEvent(QResizeEvent *e) {
	_infoTop = e->size().height()
		- st::statisticsHeaderDatesTextStyle.font->height;
}

} // namespace Statistic
