/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_statistics_chart.h"

#include <QtCore/QDateTime>

namespace Data {

using BotEarnInt = uint64;

struct BotEarnStatistics final {
	explicit operator bool() const {
		return !!usdRate;
	}
	Data::StatisticalGraph revenueGraph;
	BotEarnInt currentBalance = 0;
	BotEarnInt availableBalance = 0;
	BotEarnInt overallRevenue = 0;
	float64 usdRate = 0.;
	bool isWithdrawalEnabled = false;
	QDateTime nextWithdrawalAt;
};

} // namespace Data
