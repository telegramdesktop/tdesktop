/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_statistics.h"
#include "ui/rp_widget.h"

namespace Statistic {

class PointDetailsWidget : public Ui::RpWidget {
public:
	PointDetailsWidget(
		not_null<Ui::RpWidget*> parent,
		const Data::StatisticalChart &chartData,
		float64 maxAbsoluteValue);

	[[nodiscard]] int xIndex() const;
	void setXIndex(int xIndex);
	void setAlpha(float64 alpha);
	void setLineAlpha(int lineId, float64 alpha);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const Data::StatisticalChart &_chartData;
	const style::TextStyle &_textStyle;
	const style::TextStyle &_headerStyle;
	const QString _longFormat;
	const QString _shortFormat;
	Ui::Text::String _header;

	[[nodiscard]] int lineYAt(int index) const;

	void resizeHeight();

	struct Line final {
		int id = 0;
		Ui::Text::String name;
		Ui::Text::String value;
		QColor valueColor;
		float64 alpha = 1.;
	};

	QRect _innerRect;
	QRect _textRect;

	int _xIndex = -1;
	float64 _alpha = 1.;

	std::vector<Line> _lines;

};

} // namespace Statistic
