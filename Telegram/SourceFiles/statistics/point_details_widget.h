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
		const Data::StatisticalChart &chartData);

	[[nodiscard]] int xIndex() const;
	void setXIndex(int xIndex);
	void setAlpha(float64 alpha);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const Data::StatisticalChart &_chartData;
	const style::TextStyle &_textStyle;
	const style::TextStyle &_headerStyle;
	Ui::Text::String _header;

	[[nodiscard]] int lineYAt(int i) const;

	struct Line final {
		Ui::Text::String name;
		Ui::Text::String value;
		QColor valueColor;
	};

	QRect _innerRect;
	QRect _textRect;

	int _xIndex = -1;
	float64 _alpha = 1.;

	std::vector<Line> _lines;

};

} // namespace Statistic
