/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct StatisticalChart;
} // namespace Data

namespace Statistic {

struct Limits;

class AbstractChartView {
public:
	virtual ~AbstractChartView() = default;

	virtual void paint(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xIndices,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		bool footer) = 0;

	virtual void paintSelectedXIndex(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		int selectedXIndex) = 0;

	virtual void setEnabled(int id, bool enabled, crl::time now) = 0;
	[[nodiscard]] virtual bool isEnabled(int id) const = 0;
	[[nodiscard]] virtual bool isFinished() const = 0;
	[[nodiscard]] virtual float64 alpha(int id) const = 0;

	virtual void tick(crl::time now) = 0;

};

} // namespace Statistic
