/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "statistics/segment_tree.h"

namespace Data {

struct StatisticsMessageInteractionInfo final {
	MsgId messageId;
	int viewsCount = 0;
	int forwardsCount = 0;
};

struct StatisticsMessageSenderInfo final {
	UserId userId = UserId(0);
	int sentMessageCount = 0;
	int averageCharacterCount = 0;
};

struct StatisticsAdministratorActionsInfo final {
	UserId userId = UserId(0);
	int deletedMessageCount = 0;
	int bannedUserCount = 0;
	int restrictedUserCount = 0;
};

struct StatisticsInviterInfo final {
	UserId userId = UserId(0);
	int addedMemberCount = 0;
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

	[[nodiscard]] int findStartIndex(float v) const;
	[[nodiscard]] int findEndIndex(int left, float v) const;
	[[nodiscard]] int findIndex(int left, int right, float v) const;

	struct Line final {
		std::vector<int> y;

		Statistic::SegmentTree segmentTree;
		QString id;
		QString name;
		int maxValue = 0;
		int minValue = std::numeric_limits<int>::max();
		int colorKey = 0;
		QColor color;
		QColor colorDark;
	};

	std::vector<float64> x;
	std::vector<float64> xPercentage;
	std::vector<QString> daysLookup;

	std::vector<Line> lines;

	int maxValue = 0;
	int minValue = std::numeric_limits<int>::max();

	float64 oneDayPercentage = 0.;

	float64 timeStep = 0.;

};

struct StatisticalGraph final {
	StatisticalChart chart;
};

struct StatisticalValue final {
	float64 value = 0.;
	float64 previousValue = 0.;
	float64 growthRatePercentage = 0.;
};

struct ChannelStatistics final {
	[[nodiscard]] bool empty() const {
		return !startDate || !endDate;
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}

	int startDate = 0;
	int endDate = 0;

	StatisticalValue memberCount;
	StatisticalValue meanViewCount;
	StatisticalValue meanShareCount;

	float64 enabledNotificationsPercentage = 0.;

	StatisticalGraph memberCountGraph;
	StatisticalGraph joinGraph;
	StatisticalGraph muteGraph;
	StatisticalGraph viewCountByHourGraph;
	StatisticalGraph viewCountBySourceGraph;
	StatisticalGraph joinBySourceGraph;
	StatisticalGraph languageGraph;
	StatisticalGraph messageInteractionGraph;
	StatisticalGraph instantViewInteractionGraph;

	std::vector<StatisticsMessageInteractionInfo> recentMessageInteractions;

};

struct SupergroupStatistics final {
	[[nodiscard]] bool empty() const {
		return !startDate || !endDate;
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}

	int startDate = 0;
	int endDate = 0;

	StatisticalValue memberCount;
	StatisticalValue messageCount;
	StatisticalValue viewerCount;
	StatisticalValue senderCount;

	StatisticalGraph memberCountGraph;
	StatisticalGraph joinGraph;
	StatisticalGraph joinBySourceGraph;
	StatisticalGraph languageGraph;
	StatisticalGraph messageContentGraph;
	StatisticalGraph actionGraph;
	StatisticalGraph dayGraph;
	StatisticalGraph weekGraph;

	std::vector<StatisticsMessageSenderInfo> topSenders;
	std::vector<StatisticsAdministratorActionsInfo> topAdministrators;
	std::vector<StatisticsInviterInfo> topInviters;

};

} // namespace Data
