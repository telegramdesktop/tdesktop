/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_abstract_sparse_ids.h"

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

using StoriesIdsSlice = AbstractSparseIds<base::flat_set<StoryId>>;

[[nodiscard]] rpl::producer<StoriesIdsSlice> SavedStoriesIds(
	not_null<PeerData*> peer,
	StoryId aroundId,
	int limit);

[[nodiscard]] rpl::producer<StoriesIdsSlice> ArchiveStoriesIds(
	not_null<PeerData*> peer,
	StoryId aroundId,
	int limit);

} // namespace Data
