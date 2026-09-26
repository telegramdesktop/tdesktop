/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/filter_icons.h"

#include "ui/emoji_config.h"
#include "data/data_chat_filters.h"
#include "styles/style_filter_icons.h"

namespace Ui {
namespace {

const auto kIcons = std::vector<FilterIcons>{
	{
		&st::foldersCat,
		&st::foldersCatActive,
		&st::foldersTabsCat,
		&st::foldersUserpicCat,
		"\xF0\x9F\x90\xB1"_cs.utf16()
	},
	{
		&st::foldersBook,
		&st::foldersBookActive,
		&st::foldersTabsBook,
		&st::foldersUserpicBook,
		"\xF0\x9F\x93\x95"_cs.utf16()
	},
	{
		&st::foldersMoney,
		&st::foldersMoneyActive,
		&st::foldersTabsMoney,
		&st::foldersUserpicMoney,
		"\xF0\x9F\x92\xB0"_cs.utf16()
	},
	//{
	//	&st::foldersCamera,
	//	&st::foldersCameraActive,
	//	"\xF0\x9F\x93\xB8"_cs.utf16()
	//},
	{
		&st::foldersGame,
		&st::foldersGameActive,
		&st::foldersTabsGame,
		&st::foldersUserpicGame,
		"\xF0\x9F\x8E\xAE"_cs.utf16()
	},
	//{
	//	&st::foldersHouse,
	//	&st::foldersHouseActive,
	//	"\xF0\x9F\x8F\xA1"_cs.utf16()
	//},
	{
		&st::foldersLight,
		&st::foldersLightActive,
		&st::foldersTabsLight,
		&st::foldersUserpicLight,
		"\xF0\x9F\x92\xA1"_cs.utf16()
	},
	{
		&st::foldersLike,
		&st::foldersLikeActive,
		&st::foldersTabsLike,
		&st::foldersUserpicLike,
		"\xF0\x9F\x91\x8C"_cs.utf16()
	},
	//{
	//	&st::foldersPlus,
	//	&st::foldersPlusActive,
	//	"\xE2\x9E\x95"_cs.utf16()
	//},
	{
		&st::foldersNote,
		&st::foldersNoteActive,
		&st::foldersTabsNote,
		&st::foldersUserpicNote,
		"\xF0\x9F\x8E\xB5"_cs.utf16()
	},
	{
		&st::foldersPalette,
		&st::foldersPaletteActive,
		&st::foldersTabsPalette,
		&st::foldersUserpicPalette,
		"\xF0\x9F\x8E\xA8"_cs.utf16()
	},
	{
		&st::foldersTravel,
		&st::foldersTravelActive,
		&st::foldersTabsTravel,
		&st::foldersUserpicTravel,
		"\xE2\x9C\x88\xEF\xB8\x8F"_cs.utf16()
	},
	{
		&st::foldersSport,
		&st::foldersSportActive,
		&st::foldersTabsSport,
		&st::foldersUserpicSport,
		"\xE2\x9A\xBD\xEF\xB8\x8F"_cs.utf16()
	},
	{
		&st::foldersFavorite,
		&st::foldersFavoriteActive,
		&st::foldersTabsFavorite,
		&st::foldersUserpicFavorite,
		"\xE2\xAD\x90"_cs.utf16()
	},
	{
		&st::foldersStudy,
		&st::foldersStudyActive,
		&st::foldersTabsStudy,
		&st::foldersUserpicStudy,
		"\xF0\x9F\x8E\x93"_cs.utf16()
	},
	{
		&st::foldersAirplane,
		&st::foldersAirplaneActive,
		&st::foldersTabsAirplane,
		&st::foldersUserpicAirplane,
		"\xF0\x9F\x9B\xAB"_cs.utf16()
	},
	//{
	//	&st::foldersMicrobe,
	//	&st::foldersMicrobeActive,
	//	"\xF0\x9F\xA6\xA0"_cs.utf16()
	//},
	//{
	//	&st::foldersWorker,
	//	&st::foldersWorkerActive,
	//	"\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x92\xBC"_cs.utf16()
	//},
	{
		&st::foldersPrivate,
		&st::foldersPrivateActive,
		&st::foldersTabsPrivate,
		&st::foldersUserpicPrivate,
		"\xF0\x9F\x91\xA4"_cs.utf16()
	},
	{
		&st::foldersGroups,
		&st::foldersGroupsActive,
		&st::foldersTabsGroups,
		&st::foldersUserpicGroups,
		"\xF0\x9F\x91\xA5"_cs.utf16()
	},
	{
		&st::foldersAll,
		&st::foldersAllActive,
		&st::foldersTabsAll,
		&st::foldersUserpicAll,
		"\xF0\x9F\x92\xAC"_cs.utf16()
	},
	{
		&st::foldersUnread,
		&st::foldersUnreadActive,
		&st::foldersTabsUnread,
		&st::foldersUserpicUnread,
		"\xE2\x9C\x85"_cs.utf16()
	},
	//{
	//	&st::foldersCheck,
	//	&st::foldersCheckActive,
	//	"\xE2\x98\x91\xEF\xB8\x8F"_cs.utf16()
	//},
	{
		&st::foldersBots,
		&st::foldersBotsActive,
		&st::foldersTabsBots,
		&st::foldersUserpicBots,
		"\xF0\x9F\xA4\x96"_cs.utf16()
	},
	//{
	//	&st::foldersFolders,
	//	&st::foldersFoldersActive,
	//	"\xF0\x9F\x97\x82"_cs.utf16()
	//},
	{
		&st::foldersCrown,
		&st::foldersCrownActive,
		&st::foldersTabsCrown,
		&st::foldersUserpicCrown,
		"\xF0\x9F\x91\x91"_cs.utf16()
	},
	{
		&st::foldersFlower,
		&st::foldersFlowerActive,
		&st::foldersTabsFlower,
		&st::foldersUserpicFlower,
		"\xF0\x9F\x8C\xB9"_cs.utf16()
	},
	{
		&st::foldersHome,
		&st::foldersHomeActive,
		&st::foldersTabsHome,
		&st::foldersUserpicHome,
		"\xF0\x9F\x8F\xA0"_cs.utf16()
	},
	{
		&st::foldersLove,
		&st::foldersLoveActive,
		&st::foldersTabsLove,
		&st::foldersUserpicLove,
		"\xE2\x9D\xA4"_cs.utf16()
	},
	{
		&st::foldersMask,
		&st::foldersMaskActive,
		&st::foldersTabsMask,
		&st::foldersUserpicMask,
		"\xF0\x9F\x8E\xAD"_cs.utf16()
	},
	{
		&st::foldersParty,
		&st::foldersPartyActive,
		&st::foldersTabsParty,
		&st::foldersUserpicParty,
		"\xF0\x9F\x8D\xB8"_cs.utf16()
	},
	{
		&st::foldersTrade,
		&st::foldersTradeActive,
		&st::foldersTabsTrade,
		&st::foldersUserpicTrade,
		"\xF0\x9F\x93\x88"_cs.utf16()
	},
	{
		&st::foldersWork,
		&st::foldersWorkActive,
		&st::foldersTabsWork,
		&st::foldersUserpicWork,
		"\xF0\x9F\x92\xBC"_cs.utf16()
	},
	{
		&st::foldersUnmuted,
		&st::foldersUnmutedActive,
		&st::foldersTabsUnmuted,
		&st::foldersUserpicUnmuted,
		"\xF0\x9F\x94\x94"_cs.utf16()
	},
	{
		&st::foldersChannels,
		&st::foldersChannelsActive,
		&st::foldersTabsChannels,
		&st::foldersUserpicChannels,
		"\xF0\x9F\x93\xA2"_cs.utf16()
	},
	{
		&st::foldersCustom,
		&st::foldersCustomActive,
		&st::foldersTabsCustom,
		&st::foldersUserpicCustom,
		"\xF0\x9F\x93\x81"_cs.utf16()
	},
	{
		&st::foldersSetup,
		&st::foldersSetupActive,
		&st::foldersTabsSetup,
		&st::foldersUserpicSetup,
		"\xF0\x9F\x93\x8B"_cs.utf16()
	},
	//{
	//	&st::foldersPoo,
	//	&st::foldersPooActive,
	//	"\xF0\x9F\x92\xA9"_cs.utf16()
	//},
	{
		&st::filtersEdit,
		&st::filtersEdit,
		&st::foldersTabsEdit,
		&st::foldersUserpicEdit,
		QString()
	}
};

} // namespace

const FilterIcons &LookupFilterIcon(FilterIcon icon) {
	Expects(static_cast<int>(icon) < kIcons.size());

	return kIcons[static_cast<int>(icon)];
}

std::optional<FilterIcon> LookupFilterIconByEmoji(const QString &emoji) {
	static const auto kMap = [] {
		auto result = base::flat_map<EmojiPtr, FilterIcon>();
		auto index = 0;
		for (const auto &entry : kIcons) {
			if (entry.emoji.isEmpty()) {
				continue;
			}
			const auto emoji = Ui::Emoji::Find(entry.emoji);
			Assert(emoji != nullptr);
			result.emplace(emoji, static_cast<FilterIcon>(index++));
		}
		return result;
	}();
	const auto i = kMap.find(Ui::Emoji::Find(emoji));
	return (i != end(kMap)) ? std::make_optional(i->second) : std::nullopt;
}

FilterIcon ComputeDefaultFilterIcon(const Data::ChatFilter &filter) {
	using Icon = FilterIcon;
	using Flag = Data::ChatFilter::Flag;

	const auto all = Flag::Contacts
		| Flag::NonContacts
		| Flag::Groups
		| Flag::Channels
		| Flag::Bots;
	const auto removed = Flag::NoRead | Flag::NoMuted;
	const auto people = Flag::Contacts | Flag::NonContacts;
	if (!filter.always().empty()
		|| !filter.never().empty()
		|| !(filter.flags() & all)) {
		return Icon::Custom;
	} else if ((filter.flags() & all) == Flag::Contacts
		|| (filter.flags() & all) == Flag::NonContacts
		|| (filter.flags() & all) == people) {
		return Icon::Private;
	} else if ((filter.flags() & all) == Flag::Groups) {
		return Icon::Groups;
	} else if ((filter.flags() & all) == Flag::Channels) {
		return Icon::Channels;
	} else if ((filter.flags() & all) == Flag::Bots) {
		return Icon::Bots;
	} else if ((filter.flags() & removed) == Flag::NoRead) {
		return Icon::Unread;
	} else if ((filter.flags() & removed) == Flag::NoMuted) {
		return Icon::Unmuted;
	}
	return Icon::Custom;
}

FilterIcon ComputeFilterIcon(const Data::ChatFilter &filter) {
	return LookupFilterIconByEmoji(filter.iconEmoji()).value_or(
		ComputeDefaultFilterIcon(filter));
}

} // namespace Ui
