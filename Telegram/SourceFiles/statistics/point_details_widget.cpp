/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/point_details_widget.h"

#include "ui/cached_round_corners.h"
#include "ui/rect.h"
#include "ui/widgets/shadow.h"
#include "styles/style_layers.h"
#include "styles/style_statistics.h"

namespace Statistic {

PointDetailsWidget::PointDetailsWidget(
	not_null<Ui::RpWidget*> parent,
	const Data::StatisticalChart &chartData)
: Ui::RpWidget(parent)
, _chartData(chartData)
, _textStyle(st::statisticsDetailsPopupStyle)
, _headerStyle(st::semiboldTextStyle) {
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto fullRect = s.isNull()
			? Rect(Size(st::statisticsDetailsPopupWidth))
			: Rect(s);
		_innerRect = fullRect - st::statisticsDetailsPopupPadding;
		_textRect = _innerRect - st::statisticsDetailsPopupMargins;
	}, lifetime());

	resize(
		st::statisticsDetailsPopupWidth,
		lineYAt(chartData.lines.size())
			+ st::statisticsDetailsPopupMargins.bottom());
}

int PointDetailsWidget::xIndex() const {
	return _xIndex;
}

void PointDetailsWidget::setXIndex(int xIndex) {
	_xIndex = xIndex;
	if (xIndex < 0) {
		return;
	}
	_header.setText(_headerStyle, _chartData.getDayString(xIndex));

	_lines.clear();
	_lines.reserve(_chartData.lines.size());
	for (const auto &dataLine : _chartData.lines) {
		auto textLine = Line();
		textLine.name.setText(_textStyle, dataLine.name);
		textLine.value.setText(
			_textStyle,
			QString::number(dataLine.y[xIndex]));
		textLine.valueColor = QColor(dataLine.color);
		_lines.push_back(std::move(textLine));
	}
}

void PointDetailsWidget::setAlpha(float64 alpha) {
	_alpha = alpha;
	update();
}

int PointDetailsWidget::lineYAt(int i) const {
	return _textRect.y()
		+ _headerStyle.font->height
		+ st::statisticsDetailsPopupMargins.bottom()
		+ (_textStyle.font->height * i)
		+ (st::statisticsDetailsPopupMidLineSpace * i);
}

void PointDetailsWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	p.setOpacity(_alpha);

	const auto fullRect = rect();

	Ui::Shadow::paint(p, _innerRect, width(), st::boxRoundShadow);
	Ui::FillRoundRect(p, _innerRect, st::boxBg, Ui::BoxCorners);

	p.setPen(st::boxTextFg);
	const auto headerContext = Ui::Text::PaintContext{
		.position = _textRect.topLeft(),
		.availableWidth = _textRect.width(),
	};
	_header.draw(p, headerContext);
	for (auto i = 0; i < _lines.size(); i++) {
		const auto &line = _lines[i];
		const auto lineY = lineYAt(i);
		const auto valueWidth = line.value.maxWidth();
		const auto valueContext = Ui::Text::PaintContext{
			.position = QPoint(rect::right(_textRect) - valueWidth, lineY),
		};
		const auto nameContext = Ui::Text::PaintContext{
			.position = QPoint(_textRect.x(), lineY),
			.outerWidth = _textRect.width() - valueWidth,
			.availableWidth = _textRect.width(),
		};
		p.setPen(st::boxTextFg);
		line.name.draw(p, nameContext);
		p.setPen(line.valueColor);
		line.value.draw(p, valueContext);
	}
}

} // namespace Statistic
