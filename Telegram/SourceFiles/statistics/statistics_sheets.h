/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "statistics/statistics_xlsx.h"

namespace Data {
struct AnyStatistics;
struct StatisticalGraph;
} // namespace Data

namespace Statistic {

[[nodiscard]] std::vector<Xlsx::Sheet> Sheets(
	const Data::AnyStatistics &stats,
	const Data::StatisticalGraph &pollVotes);

} // namespace Statistic
