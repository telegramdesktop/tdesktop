/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Statistic {

class AbstractChartView;
enum class ChartViewType;

[[nodiscard]] std::unique_ptr<AbstractChartView> CreateChartView(
	ChartViewType type);

} // namespace Statistic
