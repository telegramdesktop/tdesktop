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

class QByteArray;

namespace Statistic {

[[nodiscard]] Data::StatisticalChart StatisticalChartFromJSON(
	const QByteArray &json);

} // namespace Statistic
