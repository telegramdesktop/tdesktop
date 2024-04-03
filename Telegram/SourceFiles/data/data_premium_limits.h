/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Data {

class PremiumLimits final {
public:
	PremiumLimits(not_null<Main::Session*> session);

	[[nodiscard]] int channelsDefault() const;
	[[nodiscard]] int channelsPremium() const;
	[[nodiscard]] int channelsCurrent() const;

	[[nodiscard]] int similarChannelsDefault() const;
	[[nodiscard]] int similarChannelsPremium() const;
	[[nodiscard]] int similarChannelsCurrent() const;

	[[nodiscard]] int gifsDefault() const;
	[[nodiscard]] int gifsPremium() const;
	[[nodiscard]] int gifsCurrent() const;

	[[nodiscard]] int stickersFavedDefault() const;
	[[nodiscard]] int stickersFavedPremium() const;
	[[nodiscard]] int stickersFavedCurrent() const;

	[[nodiscard]] int dialogFiltersDefault() const;
	[[nodiscard]] int dialogFiltersPremium() const;
	[[nodiscard]] int dialogFiltersCurrent() const;

	[[nodiscard]] int dialogShareableFiltersDefault() const;
	[[nodiscard]] int dialogShareableFiltersPremium() const;
	[[nodiscard]] int dialogShareableFiltersCurrent() const;

	[[nodiscard]] int dialogFiltersChatsDefault() const;
	[[nodiscard]] int dialogFiltersChatsPremium() const;
	[[nodiscard]] int dialogFiltersChatsCurrent() const;

	[[nodiscard]] int dialogFiltersLinksDefault() const;
	[[nodiscard]] int dialogFiltersLinksPremium() const;
	[[nodiscard]] int dialogFiltersLinksCurrent() const;

	[[nodiscard]] int dialogsPinnedDefault() const;
	[[nodiscard]] int dialogsPinnedPremium() const;
	[[nodiscard]] int dialogsPinnedCurrent() const;

	[[nodiscard]] int dialogsFolderPinnedDefault() const;
	[[nodiscard]] int dialogsFolderPinnedPremium() const;
	[[nodiscard]] int dialogsFolderPinnedCurrent() const;

	[[nodiscard]] int topicsPinnedCurrent() const;

	[[nodiscard]] int savedSublistsPinnedDefault() const;
	[[nodiscard]] int savedSublistsPinnedPremium() const;
	[[nodiscard]] int savedSublistsPinnedCurrent() const;

	[[nodiscard]] int channelsPublicDefault() const;
	[[nodiscard]] int channelsPublicPremium() const;
	[[nodiscard]] int channelsPublicCurrent() const;

	[[nodiscard]] int captionLengthDefault() const;
	[[nodiscard]] int captionLengthPremium() const;
	[[nodiscard]] int captionLengthCurrent() const;

	[[nodiscard]] int uploadMaxDefault() const;
	[[nodiscard]] int uploadMaxPremium() const;
	[[nodiscard]] int uploadMaxCurrent() const;

	[[nodiscard]] int aboutLengthDefault() const;
	[[nodiscard]] int aboutLengthPremium() const;
	[[nodiscard]] int aboutLengthCurrent() const;

	[[nodiscard]] int maxBoostLevel() const;

private:
	[[nodiscard]] int appConfigLimit(
		const QString &key,
		int fallback) const;
	[[nodiscard]] bool isPremium() const;

	const not_null<Main::Session*> _session;

};

class LevelLimits final {
public:
	LevelLimits(not_null<Main::Session*> session);

	[[nodiscard]] int channelColorLevelMin() const;
	[[nodiscard]] int channelBgIconLevelMin() const;
	[[nodiscard]] int channelProfileBgIconLevelMin() const;
	[[nodiscard]] int channelEmojiStatusLevelMin() const;
	[[nodiscard]] int channelWallpaperLevelMin() const;
	[[nodiscard]] int channelCustomWallpaperLevelMin() const;
	[[nodiscard]] int channelRestrictSponsoredLevelMin() const;
	[[nodiscard]] int groupTranscribeLevelMin() const;
	[[nodiscard]] int groupEmojiStickersLevelMin() const;
	[[nodiscard]] int groupProfileBgIconLevelMin() const;
	[[nodiscard]] int groupEmojiStatusLevelMin() const;
	[[nodiscard]] int groupWallpaperLevelMin() const;
	[[nodiscard]] int groupCustomWallpaperLevelMin() const;

private:
	const not_null<Main::Session*> _session;

};

} // namespace Data
