/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/filter_icons.h"

#include "styles/style_filter_icons.h"

namespace Ui {
namespace {

const auto kIcons = std::vector<FilterIcons>{
	{ &st::filtersAll, &st::filtersAllActive },
	{ &st::filtersUnread, &st::filtersAllActive },
	{ &st::filtersUnmuted, &st::filtersAllActive },
	{ &st::filtersBots, &st::filtersAllActive },
	{ &st::filtersChannels, &st::filtersChannelsActive },
	{ &st::filtersGroups, &st::filtersGroupsActive },
	{ &st::filtersPrivate, &st::filtersPrivateActive },
	{ &st::filtersCustom, &st::filtersCustomActive },
	{ &st::filtersSetup, &st::filtersSetup },
	{ &st::foldersCat, &st::foldersCatActive },
	{ &st::foldersCrown, &st::foldersCrownActive },
	{ &st::foldersFavorite, &st::foldersFavoriteActive },
	{ &st::foldersFlower, &st::foldersFlowerActive },
	{ &st::foldersGame, &st::foldersGameActive },
	{ &st::foldersHome, &st::foldersHomeActive },
	{ &st::foldersLove, &st::foldersLoveActive },
	{ &st::foldersMask, &st::foldersMaskActive },
	{ &st::foldersParty, &st::foldersPartyActive },
	{ &st::foldersSport, &st::foldersSportActive },
	{ &st::foldersStudy, &st::foldersStudyActive },
	{ &st::foldersTrade, &st::foldersTrade },
	{ &st::foldersTravel, &st::foldersTravelActive },
	{ &st::foldersWork, &st::foldersWorkActive },
};

} // namespace

const FilterIcons &LookupFilterIcon(FilterIcon icon) {
	Expects(static_cast<int>(icon) >= 0
		&& static_cast<int>(icon) < kIcons.size());

	return kIcons[static_cast<int>(icon)];
}

} // namespace Ui
