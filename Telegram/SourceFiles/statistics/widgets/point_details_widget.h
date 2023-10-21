/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_statistics_chart.h"
#include "ui/abstract_button.h"

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Statistic {

void PaintDetails(
	QPainter &p,
	const Data::StatisticalChart::Line &line,
	int absoluteValue,
	const QRect &rect);

class PointDetailsWidget : public Ui::AbstractButton {
public:
	PointDetailsWidget(
		not_null<Ui::RpWidget*> parent,
		const Data::StatisticalChart &chartData,
		float64 maxAbsoluteValue,
		bool zoomEnabled);

	[[nodiscard]] int xIndex() const;
	void setXIndex(int xIndex);
	void setAlpha(float64 alpha);
	[[nodiscard]] float64 alpha() const;
	void setLineAlpha(int lineId, float64 alpha);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	const bool _zoomEnabled;
	const Data::StatisticalChart &_chartData;
	const style::TextStyle &_textStyle;
	const style::TextStyle &_headerStyle;
	const QString _longFormat;
	const QString _shortFormat;
	Ui::Text::String _header;

	void invalidateCache();

	[[nodiscard]] int lineYAt(int index) const;

	void resizeHeight();

	struct Line final {
		int id = 0;
		Ui::Text::String name;
		Ui::Text::String value;
		Ui::Text::String percentage;
		QColor valueColor;
		float64 alpha = 1.;
	};

	bool _hasPositiveValues = true;

	int _maxPercentageWidth = 0;

	QRect _innerRect;
	QRect _textRect;
	QImage _arrow;

	QImage _cache;

	int _xIndex = -1;
	float64 _alpha = 1.;

	std::vector<Line> _lines;

	std::unique_ptr<Ui::RippleAnimation> _ripple;

};

} // namespace Statistic
