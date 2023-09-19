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

struct PaintContext final {
	const Data::StatisticalChart &chartData;
	const Limits &xIndices;
	const Limits &xPercentageLimits;
	const Limits &heightLimits;
	const QRect &rect;
	bool footer = false;
};

class AbstractChartView {
public:
	virtual ~AbstractChartView() = default;

	virtual void paint(QPainter &p, const PaintContext &c) = 0;

	virtual void paintSelectedXIndex(
		QPainter &p,
		const PaintContext &c,
		int selectedXIndex,
		float64 progress) = 0;

	[[nodiscard]] virtual int findXIndexByPosition(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 x) = 0;

	virtual void setEnabled(int id, bool enabled, crl::time now) = 0;
	[[nodiscard]] virtual bool isEnabled(int id) const = 0;
	[[nodiscard]] virtual bool isFinished() const = 0;
	[[nodiscard]] virtual float64 alpha(int id) const = 0;

	struct HeightLimits final {
		Limits full;
		Limits ranged;
	};

	[[nodiscard]] virtual HeightLimits heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) = 0;

	virtual void tick(crl::time now) = 0;
	virtual void update(float64 dt) {
	};

};

} // namespace Statistic
