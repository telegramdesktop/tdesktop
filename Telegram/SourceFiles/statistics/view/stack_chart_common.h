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

struct LeftStartAndStep final {
	float64 start = 0.;
	float64 step = 0.;
};

[[nodiscard]] LeftStartAndStep ComputeLeftStartAndStep(
	const Data::StatisticalChart &chartData,
	const Limits &xPercentageLimits,
	const QRect &rect,
	float64 xIndexStart);

[[nodiscard]] Limits FindStackXIndicesFromRawXPercentages(
	const Data::StatisticalChart &chartData,
	const Limits &rawXPercentageLimits,
	const Limits &zoomedInLimitXIndices);

} // namespace Statistic
