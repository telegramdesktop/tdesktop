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

using EarnInt = uint64;

constexpr auto kEarnMultiplier = EarnInt(1000000000);

struct EarnHistoryEntry final {
	enum class Type {
		In,
		Out,
		Return,
	};

	enum class Status {
		Success,
		Failed,
		Pending,
	};

	Type type;
	Status status;

	CreditsAmount amount;
	QDateTime date;
	QDateTime dateTo;

	QString provider;

	QDateTime successDate;
	QString successLink;

};

struct EarnHistorySlice final {
	using OffsetToken = QString;
	std::vector<EarnHistoryEntry> list;
	int total = 0;
	bool allLoaded = false;
	OffsetToken token;
};

struct EarnStatistics final {
	explicit operator bool() const {
		return !!usdRate;
	}
	Data::StatisticalGraph topHoursGraph;
	Data::StatisticalGraph revenueGraph;
	CreditsAmount currentBalance;
	CreditsAmount availableBalance;
	CreditsAmount overallRevenue;
	float64 usdRate = 0.;
	bool switchedOff = false;

	EarnHistorySlice firstHistorySlice;
};

} // namespace Data
