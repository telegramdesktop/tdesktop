/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_premium_limits.h"

#include "main/main_app_config.h"
#include "main/main_session.h"

namespace Data {

PremiumLimits::PremiumLimits(not_null<Main::Session*> session)
: _session(session) {
}

int PremiumLimits::channelsDefault() const {
	return appConfigLimit("channels_limit_default", 500);
}
int PremiumLimits::channelsPremium() const {
	return appConfigLimit("channels_limit_premium", 1000);
}
int PremiumLimits::channelsCurrent() const {
	return isPremium()
		? channelsPremium()
		: channelsDefault();
}

int PremiumLimits::similarChannelsDefault() const {
	return appConfigLimit("recommended_channels_limit_default", 10);
}
int PremiumLimits::similarChannelsPremium() const {
	return appConfigLimit("recommended_channels_limit_premium", 100);
}
int PremiumLimits::similarChannelsCurrent() const {
	return isPremium()
		? channelsPremium()
		: channelsDefault();
}

int PremiumLimits::gifsDefault() const {
	return appConfigLimit("saved_gifs_limit_default", 200);
}
int PremiumLimits::gifsPremium() const {
	return appConfigLimit("saved_gifs_limit_premium", 400);
}
int PremiumLimits::gifsCurrent() const {
	return isPremium()
		? gifsPremium()
		: gifsDefault();
}

int PremiumLimits::stickersFavedDefault() const {
	return appConfigLimit("stickers_faved_limit_default", 5);
}
int PremiumLimits::stickersFavedPremium() const {
	return appConfigLimit("stickers_faved_limit_premium", 10);
}
int PremiumLimits::stickersFavedCurrent() const {
	return isPremium()
		? stickersFavedPremium()
		: stickersFavedDefault();
}

int PremiumLimits::dialogFiltersDefault() const {
	return appConfigLimit("dialog_filters_limit_default", 10);
}
int PremiumLimits::dialogFiltersPremium() const {
	return appConfigLimit("dialog_filters_limit_premium", 20);
}
int PremiumLimits::dialogFiltersCurrent() const {
	return isPremium()
		? dialogFiltersPremium()
		: dialogFiltersDefault();
}

int PremiumLimits::dialogShareableFiltersDefault() const {
	return appConfigLimit("chatlists_joined_limit_default", 2);
}
int PremiumLimits::dialogShareableFiltersPremium() const {
	return appConfigLimit("chatlists_joined_limit_premium", 20);
}
int PremiumLimits::dialogShareableFiltersCurrent() const {
	return isPremium()
		? dialogShareableFiltersPremium()
		: dialogShareableFiltersDefault();
}

int PremiumLimits::dialogFiltersChatsDefault() const {
	return appConfigLimit("dialog_filters_chats_limit_default", 100);
}
int PremiumLimits::dialogFiltersChatsPremium() const {
	return appConfigLimit("dialog_filters_chats_limit_premium", 200);
}
int PremiumLimits::dialogFiltersChatsCurrent() const {
	return isPremium()
		? dialogFiltersChatsPremium()
		: dialogFiltersChatsDefault();
}

int PremiumLimits::dialogFiltersLinksDefault() const {
	return appConfigLimit("chatlist_invites_limit_default", 3);
}
int PremiumLimits::dialogFiltersLinksPremium() const {
	return appConfigLimit("chatlist_invites_limit_premium", 20);
}
int PremiumLimits::dialogFiltersLinksCurrent() const {
	return isPremium()
		? dialogFiltersLinksPremium()
		: dialogFiltersLinksDefault();
}

int PremiumLimits::dialogsPinnedDefault() const {
	return appConfigLimit("dialogs_pinned_limit_default", 5);
}
int PremiumLimits::dialogsPinnedPremium() const {
	return appConfigLimit("dialogs_pinned_limit_premium", 10);
}
int PremiumLimits::dialogsPinnedCurrent() const {
	return isPremium()
		? dialogsPinnedPremium()
		: dialogsPinnedDefault();
}

int PremiumLimits::dialogsFolderPinnedDefault() const {
	return appConfigLimit("dialogs_folder_pinned_limit_default", 100);
}
int PremiumLimits::dialogsFolderPinnedPremium() const {
	return appConfigLimit("dialogs_folder_pinned_limit_premium", 200);
}
int PremiumLimits::dialogsFolderPinnedCurrent() const {
	return isPremium()
		? dialogsFolderPinnedPremium()
		: dialogsFolderPinnedDefault();
}

int PremiumLimits::topicsPinnedCurrent() const {
	return appConfigLimit("topics_pinned_limit", 5);
}

int PremiumLimits::savedSublistsPinnedDefault() const {
	return appConfigLimit("saved_dialogs_pinned_limit_default", 5);
}
int PremiumLimits::savedSublistsPinnedPremium() const {
	return appConfigLimit("saved_dialogs_pinned_limit_premium", 100);
}
int PremiumLimits::savedSublistsPinnedCurrent() const {
	return isPremium()
		? savedSublistsPinnedPremium()
		: savedSublistsPinnedDefault();
}

int PremiumLimits::channelsPublicDefault() const {
	return appConfigLimit("channels_public_limit_default", 10);
}
int PremiumLimits::channelsPublicPremium() const {
	return appConfigLimit("channels_public_limit_premium", 20);
}
int PremiumLimits::channelsPublicCurrent() const {
	return isPremium()
		? channelsPublicPremium()
		: channelsPublicDefault();
}

int PremiumLimits::captionLengthDefault() const {
	return appConfigLimit("caption_length_limit_default", 1024);
}
int PremiumLimits::captionLengthPremium() const {
	return appConfigLimit("caption_length_limit_premium", 2048);
}
int PremiumLimits::captionLengthCurrent() const {
	return isPremium()
		? captionLengthPremium()
		: captionLengthDefault();
}

int PremiumLimits::uploadMaxDefault() const {
	return appConfigLimit("upload_max_fileparts_default", 4000);
}
int PremiumLimits::uploadMaxPremium() const {
	return appConfigLimit("upload_max_fileparts_premium", 8000);
}
int PremiumLimits::uploadMaxCurrent() const {
	return isPremium()
		? uploadMaxPremium()
		: uploadMaxDefault();
}

int PremiumLimits::aboutLengthDefault() const {
	return appConfigLimit("about_length_limit_default", 70);
}
int PremiumLimits::aboutLengthPremium() const {
	return appConfigLimit("about_length_limit_premium", 140);
}
int PremiumLimits::aboutLengthCurrent() const {
	return isPremium()
		? aboutLengthPremium()
		: aboutLengthDefault();
}

int PremiumLimits::maxBoostLevel() const {
	return appConfigLimit(
		u"boosts_channel_level_max"_q,
		_session->isTestMode() ? 9 : 99);
}

int PremiumLimits::appConfigLimit(
		const QString &key,
		int fallback) const {
	return _session->appConfig().get<int>(key, fallback);
}

bool PremiumLimits::isPremium() const {
	return _session->premium();
}

LevelLimits::LevelLimits(not_null<Main::Session*> session)
: _session(session) {
}

int LevelLimits::channelColorLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_color_level_min"_q,
		5);
}

int LevelLimits::channelBgIconLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_bg_icon_level_min"_q,
		4);
}

int LevelLimits::channelProfileBgIconLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_profile_bg_icon_level_min"_q,
		7);
}

int LevelLimits::channelEmojiStatusLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_emoji_status_level_min"_q,
		8);
}

int LevelLimits::channelWallpaperLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_wallpaper_level_min"_q,
		9);
}

int LevelLimits::channelCustomWallpaperLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_custom_wallpaper_level_min"_q,
		10);
}

int LevelLimits::channelRestrictSponsoredLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_restrict_sponsored_level_min"_q,
		20);
}

int LevelLimits::groupTranscribeLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_transcribe_level_min"_q,
		6);
}

int LevelLimits::groupEmojiStickersLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_emoji_stickers_level_min"_q,
		4);
}

int LevelLimits::groupProfileBgIconLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_profile_bg_icon_level_min"_q,
		5);
}

int LevelLimits::groupEmojiStatusLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_emoji_status_level_min"_q,
		8);
}

int LevelLimits::groupWallpaperLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_wallpaper_level_min"_q,
		9);
}

int LevelLimits::groupCustomWallpaperLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_custom_wallpaper_level_min"_q,
		10);
}

} // namespace Data
