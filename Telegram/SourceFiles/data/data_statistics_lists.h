/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct StatisticsMessageInteractionInfo final {
	MsgId messageId;
	StoryId storyId = StoryId(0);
	int viewsCount = 0;
	int forwardsCount = 0;
	int reactionsCount = 0;
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

struct StatisticsLists final {
	std::vector<StatisticsMessageInteractionInfo> recentMessageInteractions;
	std::vector<StatisticsMessageSenderInfo> topSenders;
	std::vector<StatisticsAdministratorActionsInfo> topAdministrators;
	std::vector<StatisticsInviterInfo> topInviters;
};

struct RecentPostId final {
	FullMsgId messageId;
	FullStoryId storyId;

	[[nodiscard]] bool valid() const {
		return messageId || storyId;
	}
	explicit operator bool() const {
		return valid();
	}
	friend inline auto operator<=>(RecentPostId, RecentPostId) = default;
	friend inline bool operator==(RecentPostId, RecentPostId) = default;
};

struct PublicForwardsSlice final {
	using OffsetToken = QString;
	QVector<RecentPostId> list;
	int total = 0;
	bool allLoaded = false;
	OffsetToken token;
};

} // namespace Data
