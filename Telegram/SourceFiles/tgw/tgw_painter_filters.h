/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/src/display_list.h"

namespace Tgw {

struct FilterCost {
	int64 work = 0;
	int64 scratch = 0;
};

[[nodiscard]] bool UsableFilters(const twlite::Filters &filters);
[[nodiscard]] QMarginsF FilterReach(
	const twlite::Filters &filters,
	float64 scale);
[[nodiscard]] std::optional<FilterCost> MeasureFilters(
	const twlite::Filters &filters,
	QSize size,
	float64 scale);
[[nodiscard]] bool ApplyFilters(
	QImage &image,
	const twlite::Filters &filters,
	float64 scale);

} // namespace Tgw
