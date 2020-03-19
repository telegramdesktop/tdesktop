/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/filter_icons.h"

#include "ui/emoji_config.h"
#include "styles/style_filter_icons.h"

namespace Ui {
namespace {

const auto kIcons = std::vector<FilterIcons>{
	{
		&st::filtersAll,
		&st::filtersAllActive,
		"\xF0\x9F\x92\xAC"_cs.utf16()
	},
	{
		&st::filtersUnread,
		&st::filtersUnreadActive,
		"\xE2\x9C\x85"_cs.utf16()
	},
	{
		&st::filtersUnmuted,
		&st::filtersUnmutedActive,
		"\xF0\x9F\x94\x94"_cs.utf16()
	},
	{
		&st::filtersBots,
		&st::filtersBotsActive,
		"\xF0\x9F\xA4\x96"_cs.utf16()
	},
	{
		&st::filtersChannels,
		&st::filtersChannelsActive,
		"\xF0\x9F\x93\xA2"_cs.utf16()
	},
	{
		&st::filtersGroups,
		&st::filtersGroupsActive,
		"\xF0\x9F\x91\xA5"_cs.utf16()
	},
	{
		&st::filtersPrivate,
		&st::filtersPrivateActive,
		"\xF0\x9F\x91\xA4"_cs.utf16()
	},
	{
		&st::filtersCustom,
		&st::filtersCustomActive,
		"\xF0\x9F\x93\x81"_cs.utf16()
	},
	{
		&st::filtersSetup,
		&st::filtersSetup,
		"\xF0\x9F\x93\x8B"_cs.utf16()
	},
	{
		&st::foldersCat,
		&st::foldersCatActive,
		"\xF0\x9F\x90\x88"_cs.utf16()
	},
	{
		&st::foldersCrown,
		&st::foldersCrownActive,
		"\xF0\x9F\x91\x91"_cs.utf16()
	},
	{
		&st::foldersFavorite,
		&st::foldersFavoriteActive,
		"\xE2\xAD\x90\xEF\xB8\x8F"_cs.utf16()
	},
	{
		&st::foldersFlower,
		&st::foldersFlowerActive,
		"\xF0\x9F\x8C\xB9"_cs.utf16()
	},
	{
		&st::foldersGame,
		&st::foldersGameActive,
		"\xF0\x9F\x8E\xAE"_cs.utf16()
	},
	{
		&st::foldersHome,
		&st::foldersHomeActive,
		"\xF0\x9F\x8F\xA0"_cs.utf16()
	},
	{
		&st::foldersLove,
		&st::foldersLoveActive,
		"\xE2\x9D\xA4\xEF\xB8\x8F"_cs.utf16()
	},
	{
		&st::foldersMask,
		&st::foldersMaskActive,
		"\xF0\x9F\x8E\xAD"_cs.utf16()
	},
	{
		&st::foldersParty,
		&st::foldersPartyActive,
		"\xF0\x9F\x8D\xB8"_cs.utf16()
	},
	{
		&st::foldersSport,
		&st::foldersSportActive,
		"\xE2\x9A\xBD\xEF\xB8\x8F"_cs.utf16()
	},
	{
		&st::foldersStudy,
		&st::foldersStudyActive,
		"\xF0\x9F\x8E\x93"_cs.utf16()
	},
	{
		&st::foldersTrade,
		&st::foldersTrade,
		"\xF0\x9F\x93\x88"_cs.utf16()
	},
	{
		&st::foldersTravel,
		&st::foldersTravelActive,
		"\xE2\x9C\x88\xEF\xB8\x8F"_cs.utf16()
	},
	{
		&st::foldersWork,
		&st::foldersWorkActive,
		"\xF0\x9F\x92\xBC"_cs.utf16()
	},
};

} // namespace

const FilterIcons &LookupFilterIcon(FilterIcon icon) {
	Expects(static_cast<int>(icon) >= 0
		&& static_cast<int>(icon) < kIcons.size());

	return kIcons[static_cast<int>(icon)];
}

std::optional<FilterIcon> LookupFilterIconByEmoji(const QString &emoji) {
	static const auto kMap = [] {
		auto result = base::flat_map<EmojiPtr, FilterIcon>();
		auto index = 0;
		for (const auto &entry : kIcons) {
			const auto emoji = Ui::Emoji::Find(entry.emoji);
			Assert(emoji != nullptr);
			result.emplace(emoji, static_cast<FilterIcon>(index++));
		}
		return result;
	}();
	const auto i = kMap.find(Ui::Emoji::Find(emoji));
	return (i != end(kMap)) ? std::make_optional(i->second) : std::nullopt;
}

} // namespace Ui
