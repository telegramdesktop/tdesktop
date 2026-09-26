/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_statistics.h"
#include "data/data_statistics_lists.h"

namespace Info::Statistics {

struct SavedState final {
	Data::AnyStatistics stats;
	Data::StatisticsLists lists;
	Data::StatisticalGraph pollVotesGraph;
	base::flat_map<Data::RecentPostId, QImage> recentPostPreviews;
	Data::PublicForwardsSlice publicForwardsFirstSlice;
	int recentPostsExpanded = 0;
};

} // namespace Info::Statistics
