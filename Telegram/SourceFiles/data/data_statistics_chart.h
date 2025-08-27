/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "statistics/segment_tree.h"

namespace Data {

enum class StatisticalCurrency {
	None,
	Ton,
	Credits,
};

struct StatisticalChart {
	StatisticalChart() = default;

	[[nodiscard]] bool empty() const {
		return lines.empty();
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}

	void measure();

	[[nodiscard]] QString getDayString(int i) const;

	[[nodiscard]] int findStartIndex(float64 v) const;
	[[nodiscard]] int findEndIndex(int left, float64 v) const;
	[[nodiscard]] int findIndex(int left, int right, float64 v) const;

	struct Line final {
		std::vector<Statistic::ChartValue> y;

		Statistic::SegmentTree segmentTree;
		int id = 0;
		QString idString;
		QString name;
		Statistic::ChartValue maxValue = 0;
		Statistic::ChartValue minValue
			= std::numeric_limits<Statistic::ChartValue>::max();
		QString colorKey;
		QColor color;
		QColor colorDark;
		bool isHiddenOnStart = false;
	};

	std::vector<float64> x;
	std::vector<float64> xPercentage;
	std::vector<QString> daysLookup;

	std::vector<Line> lines;

	struct {
		float64 min = 0.;
		float64 max = 0.;
	} defaultZoomXIndex;

	Statistic::ChartValue maxValue = 0;
	Statistic::ChartValue minValue
		= std::numeric_limits<Statistic::ChartValue>::max();

	float64 oneDayPercentage = 0.;

	float64 timeStep = 1.;

	bool isFooterHidden = false;
	bool hasPercentages = false;
	bool weekFormat = false;

	StatisticalCurrency currency = StatisticalCurrency::None;
	float64 currencyRate = 0.;

	// View data.
	int dayStringMaxWidth = 0;

};

struct StatisticalGraph final {
	StatisticalChart chart;
	QString zoomToken;
	QString error;
};

} // namespace Data
