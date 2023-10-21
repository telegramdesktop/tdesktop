/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_statistics_chart.h"

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

struct MessageStatistics final {
	explicit operator bool() const {
		return !messageInteractionGraph.chart.empty() || views;
	}
	Data::StatisticalGraph messageInteractionGraph;
	int publicForwards = 0;
	int privateForwards = 0;
	int views = 0;
};

struct AnyStatistics final {
	Data::ChannelStatistics channel;
	Data::SupergroupStatistics supergroup;
	Data::MessageStatistics message;
};

struct PublicForwardsSlice final {
	struct OffsetToken final {
		int rate = 0;
		FullMsgId fullId;
	};
	QVector<FullMsgId> list;
	int total = 0;
	bool allLoaded = false;
	OffsetToken token;
};

} // namespace Data
