/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_linear_chart_common.h"

#include "data/data_statistics_chart.h"
#include "statistics/chart_lines_filter_controller.h"
#include "statistics/statistics_common.h"

namespace Statistic {

PiePartData PiePartsPercentage(
		const std::vector<float64> &sums,
		float64 totalSum,
		bool round) {
	auto result = PiePartData();
	result.parts.reserve(sums.size());
	auto stackedPercentage = 0.;

	auto sumPercDiffs = 0.;
	auto maxPercDiff = 0.;
	auto minPercDiff = 0.;
	auto maxPercDiffIndex = int(-1);
	auto minPercDiffIndex = int(-1);
	auto roundedPercentagesSum = 0.;

	result.pieHasSinglePart = false;
	constexpr auto kPerChar = '%';
	for (auto k = 0; k < sums.size(); k++) {
		const auto rawPercentage = totalSum ? (sums[k] / totalSum) : 0.;
		const auto rounded = round
			? (0.01 * std::round(rawPercentage * 100.))
			: rawPercentage;
		roundedPercentagesSum += rounded;
		const auto diff = rawPercentage - rounded;
		sumPercDiffs += diff;
		const auto diffAbs = std::abs(diff);
		if (maxPercDiff < diffAbs) {
			maxPercDiff = diffAbs;
			maxPercDiffIndex = k;
		}
		if (minPercDiff < diffAbs) {
			minPercDiff = diffAbs;
			minPercDiffIndex = k;
		}

		stackedPercentage += rounded;
		result.parts.push_back({
			rounded,
			stackedPercentage * 360. - 180.,
			QString::number(int(rounded * 100)) + kPerChar,
		});
		result.pieHasSinglePart |= (rounded == 1.);
	}
	if (round) {
		const auto index = (roundedPercentagesSum > 1.)
			? maxPercDiffIndex
			: minPercDiffIndex;
		if (index >= 0) {
			result.parts[index].roundedPercentage += sumPercDiffs;
			result.parts[index].percentageText = QString::number(
				int(result.parts[index].roundedPercentage * 100)) + kPerChar;
			const auto angleShrink = (sumPercDiffs) * 360.;
			for (auto &part : result.parts) {
				part.stackedAngle += angleShrink;
			}
		}
	}
	return result;
}

PiePartData PiePartsPercentageByIndices(
		const Data::StatisticalChart &chartData,
		const std::shared_ptr<LinesFilterController> &linesFilter,
		const Limits &xIndices) {
	auto sums = std::vector<float64>();
	sums.reserve(chartData.lines.size());
	auto totalSum = 0.;
	for (const auto &line : chartData.lines) {
		auto sum = 0;
		for (auto i = xIndices.min; i <= xIndices.max; i++) {
			sum += line.y[i];
		}
		if (linesFilter) {
			sum *= linesFilter->alpha(line.id);
		}
		totalSum += sum;
		sums.push_back(sum);
	}
	return PiePartsPercentage(sums, totalSum, true);
}

} // namespace Statistic
