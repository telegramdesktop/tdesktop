/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_chat_participant_status.h"

#include "base/unixtime.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_values.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "window/window_session_controller.h"
#include "styles/style_widgets.h"

namespace {

[[nodiscard]] ChatAdminRights ChatAdminRightsFlags(
		const MTPChatAdminRights &rights) {
	return rights.match([](const MTPDchatAdminRights &data) {
		using Flag = ChatAdminRight;
		return (data.is_change_info() ? Flag::ChangeInfo : Flag())
			| (data.is_post_messages() ? Flag::PostMessages : Flag())
			| (data.is_edit_messages() ? Flag::EditMessages : Flag())
			| (data.is_delete_messages() ? Flag::DeleteMessages : Flag())
			| (data.is_ban_users() ? Flag::BanUsers : Flag())
			| (data.is_invite_users() ? Flag::InviteByLinkOrAdd : Flag())
			| (data.is_pin_messages() ? Flag::PinMessages : Flag())
			| (data.is_add_admins() ? Flag::AddAdmins : Flag())
			| (data.is_anonymous() ? Flag::Anonymous : Flag())
			| (data.is_manage_call() ? Flag::ManageCall : Flag())
			| (data.is_other() ? Flag::Other : Flag())
			| (data.is_manage_topics() ? Flag::ManageTopics : Flag())
			| (data.is_post_stories() ? Flag::PostStories : Flag())
			| (data.is_edit_stories() ? Flag::EditStories : Flag())
			| (data.is_delete_stories() ? Flag::DeleteStories : Flag())
			| (data.is_manage_direct_messages()
				? Flag::ManageDirect
				: Flag());
	});
}

[[nodiscard]] ChatRestrictions ChatBannedRightsFlags(
		const MTPChatBannedRights &rights) {
	return rights.match([](const MTPDchatBannedRights &data) {
		using Flag = ChatRestriction;
		return (data.is_view_messages() ? Flag::ViewMessages : Flag())
			| (data.is_send_stickers() ? Flag::SendStickers : Flag())
			| (data.is_send_gifs() ? Flag::SendGifs : Flag())
			| (data.is_send_games() ? Flag::SendGames : Flag())
			| (data.is_send_inline() ? Flag::SendInline : Flag())
			| (data.is_send_polls() ? Flag::SendPolls : Flag())
			| (data.is_send_photos() ? Flag::SendPhotos : Flag())
			| (data.is_send_videos() ? Flag::SendVideos : Flag())
			| (data.is_send_roundvideos() ? Flag::SendVideoMessages : Flag())
			| (data.is_send_audios() ? Flag::SendMusic : Flag())
			| (data.is_send_voices() ? Flag::SendVoiceMessages : Flag())
			| (data.is_send_docs() ? Flag::SendFiles : Flag())
			| (data.is_send_plain() ? Flag::SendOther : Flag())
			| (data.is_embed_links() ? Flag::EmbedLinks : Flag())
			| (data.is_change_info() ? Flag::ChangeInfo : Flag())
			| (data.is_invite_users() ? Flag::AddParticipants : Flag())
			| (data.is_pin_messages() ? Flag::PinMessages : Flag())
			| (data.is_manage_topics() ? Flag::CreateTopics : Flag());
	});
}

[[nodiscard]] TimeId ChatBannedRightsUntilDate(
		const MTPChatBannedRights &rights) {
	return rights.match([](const MTPDchatBannedRights &data) {
		return data.vuntil_date().v;
	});
}

} // namespace

ChatAdminRightsInfo::ChatAdminRightsInfo(const MTPChatAdminRights &rights)
: flags(ChatAdminRightsFlags(rights)) {
}

MTPChatAdminRights AdminRightsToMTP(ChatAdminRightsInfo info) {
	using Flag = MTPDchatAdminRights::Flag;
	using R = ChatAdminRight;
	const auto flags = info.flags;
	return MTP_chatAdminRights(MTP_flags(Flag()
		| ((flags & R::ChangeInfo) ? Flag::f_change_info : Flag())
		| ((flags & R::PostMessages) ? Flag::f_post_messages : Flag())
		| ((flags & R::EditMessages) ? Flag::f_edit_messages : Flag())
		| ((flags & R::DeleteMessages) ? Flag::f_delete_messages : Flag())
		| ((flags & R::BanUsers) ? Flag::f_ban_users : Flag())
		| ((flags & R::InviteByLinkOrAdd) ? Flag::f_invite_users : Flag())
		| ((flags & R::PinMessages) ? Flag::f_pin_messages : Flag())
		| ((flags & R::AddAdmins) ? Flag::f_add_admins : Flag())
		| ((flags & R::Anonymous) ? Flag::f_anonymous : Flag())
		| ((flags & R::ManageCall) ? Flag::f_manage_call : Flag())
		| ((flags & R::Other) ? Flag::f_other : Flag())
		| ((flags & R::ManageTopics) ? Flag::f_manage_topics : Flag())
		| ((flags & R::PostStories) ? Flag::f_post_stories : Flag())
		| ((flags & R::EditStories) ? Flag::f_edit_stories : Flag())
		| ((flags & R::DeleteStories) ? Flag::f_delete_stories : Flag())
		| ((flags & R::ManageDirect)
			? Flag::f_manage_direct_messages
			: Flag())));
}

ChatRestrictionsInfo::ChatRestrictionsInfo(const MTPChatBannedRights &rights)
: flags(ChatBannedRightsFlags(rights))
, until(ChatBannedRightsUntilDate(rights)) {
}

MTPChatBannedRights RestrictionsToMTP(ChatRestrictionsInfo info) {
	using Flag = MTPDchatBannedRights::Flag;
	using R = ChatRestriction;
	const auto flags = info.flags;
	return MTP_chatBannedRights(
		MTP_flags(Flag()
			| ((flags & R::ViewMessages) ? Flag::f_view_messages : Flag())
			| ((flags & R::SendStickers) ? Flag::f_send_stickers : Flag())
			| ((flags & R::SendGifs) ? Flag::f_send_gifs : Flag())
			| ((flags & R::SendGames) ? Flag::f_send_games : Flag())
			| ((flags & R::SendInline) ? Flag::f_send_inline : Flag())
			| ((flags & R::SendPolls) ? Flag::f_send_polls : Flag())
			| ((flags & R::SendPhotos) ? Flag::f_send_photos : Flag())
			| ((flags & R::SendVideos) ? Flag::f_send_videos : Flag())
			| ((flags & R::SendVideoMessages) ? Flag::f_send_roundvideos : Flag())
			| ((flags & R::SendMusic) ? Flag::f_send_audios : Flag())
			| ((flags & R::SendVoiceMessages) ? Flag::f_send_voices : Flag())
			| ((flags & R::SendFiles) ? Flag::f_send_docs : Flag())
			| ((flags & R::SendOther) ? Flag::f_send_plain : Flag())
			| ((flags & R::EmbedLinks) ? Flag::f_embed_links : Flag())
			| ((flags & R::ChangeInfo) ? Flag::f_change_info : Flag())
			| ((flags & R::AddParticipants) ? Flag::f_invite_users : Flag())
			| ((flags & R::PinMessages) ? Flag::f_pin_messages : Flag())
			| ((flags & R::CreateTopics) ? Flag::f_manage_topics : Flag())),
		MTP_int(info.until));
}

namespace Data {

std::vector<ChatRestrictions> ListOfRestrictions(
		RestrictionsSetOptions options) {
	auto labels = RestrictionLabels(options);
	return ranges::views::all(labels)
		| ranges::views::transform(&RestrictionLabel::flags)
		| ranges::to_vector;
}

ChatRestrictions AllSendRestrictions() {
	constexpr auto result = [] {
		auto result = ChatRestrictions();
		for (const auto right : AllSendRestrictionsList()) {
			result |= right;
		}
		return result;
	}();
	return result;
}

ChatRestrictions FilesSendRestrictions() {
	constexpr auto result = [] {
		auto result = ChatRestrictions();
		for (const auto right : FilesSendRestrictionsList()) {
			result |= right;
		}
		return result;
	}();
	return result;
}

ChatRestrictions TabbedPanelSendRestrictions() {
	constexpr auto result = [] {
		auto result = ChatRestrictions();
		for (const auto right : TabbedPanelSendRestrictionsList()) {
			result |= right;
		}
		return result;
	}();
	return result;
}

// Duplicated in CanSendAnyOfValue().
bool CanSendAnyOf(
		not_null<const Thread*> thread,
		ChatRestrictions rights,
		bool forbidInForums) {
	const auto peer = thread->peer();
	const auto topic = thread->asTopic();
	return CanSendAnyOf(peer, rights, forbidInForums && !topic)
		&& (!topic || !topic->closed() || topic->canToggleClosed());
}

// Duplicated in CanSendAnyOfValue().
bool CanSendAnyOf(
		not_null<const PeerData*> peer,
		ChatRestrictions rights,
		bool forbidInForums) {
	if (peer->session().frozen()
		&& !peer->isFreezeAppealChat()) {
		return false;
	} else if (const auto user = peer->asUser()) {
		if (user->isInaccessible()
			|| user->isRepliesChat()
			|| user->isVerifyCodes()) {
			return false;
		} else if (user->requiresPremiumToWrite()
			&& !user->session().premium()) {
			return false;
		} else if (rights
			& ~(ChatRestriction::SendVoiceMessages
				| ChatRestriction::SendVideoMessages
				| ChatRestriction::SendPolls)) {
			return true;
		}
		for (const auto right : {
			ChatRestriction::SendVoiceMessages,
			ChatRestriction::SendVideoMessages,
			ChatRestriction::SendPolls,
		}) {
			if ((rights & right) && !user->amRestricted(right)) {
				return true;
			}
		}
		return false;
	} else if (const auto chat = peer->asChat()) {
		if (!chat->amIn()) {
			return false;
		}
		for (const auto right : AllSendRestrictionsList()) {
			if ((rights & right) && !chat->amRestricted(right)) {
				return true;
			}
		}
		return false;
	} else if (const auto channel = peer->asChannel()) {
		if (channel->monoforumDisabled()) {
			return false;
		}
		using Flag = ChannelDataFlag;
		const auto allowed = channel->amIn()
			|| ((channel->flags() & Flag::HasLink)
				&& !(channel->flags() & Flag::JoinToWrite))
			|| channel->isMonoforum();
		if (!allowed || (forbidInForums && channel->isForum())) {
			return false;
		} else if (channel->canPostMessages()) {
			return true;
		} else if (channel->isBroadcast()) {
			return false;
		}
		for (const auto right : AllSendRestrictionsList()) {
			if ((rights & right) && !channel->amRestricted(right)) {
				return true;
			}
		}
		return false;
	}
	Unexpected("Peer type in CanSendAnyOf.");
}

SendError RestrictionError(
		not_null<PeerData*> peer,
		ChatRestriction restriction) {
	using Flag = ChatRestriction;
	if (peer->session().frozen()
		&& !peer->isFreezeAppealChat()) {
		return SendError({
			.text = tr::lng_frozen_restrict_title(tr::now),
			.frozen = true,
		});
	} else if (const auto restricted = peer->amRestricted(restriction)) {
		if (const auto user = peer->asUser()) {
			if (user->requiresPremiumToWrite()
				&& !user->session().premium()) {
				return SendError({
					.text = tr::lng_restricted_send_non_premium(
						tr::now,
						lt_user,
						user->shortName()),
					.premiumToLift = true,
				});
			}
			const auto result = (restriction == Flag::SendVoiceMessages)
				? tr::lng_restricted_send_voice_messages(
					tr::now,
					lt_user,
					user->name())
				: (restriction == Flag::SendVideoMessages)
				? tr::lng_restricted_send_video_messages(
					tr::now,
					lt_user,
					user->name())
				: (restriction == Flag::SendPolls)
				? u"can't send polls :("_q
				: (restriction == Flag::PinMessages)
				? u"can't pin :("_q
				: SendError();

			Ensures(result.has_value());
			return result;
		}
		const auto all = restricted.isWithEveryone();
		const auto channel = peer->asChannel();
		if (channel && channel->monoforumDisabled()) {
			return tr::lng_action_direct_messages_disabled(tr::now);
		}
		if (!all && channel) {
			auto restrictedUntil = channel->restrictedUntil();
			if (restrictedUntil > 0
				&& !ChannelData::IsRestrictedForever(restrictedUntil)) {
				auto restrictedUntilDateTime = base::unixtime::parse(
					channel->restrictedUntil());
				auto date = QLocale().toString(
					restrictedUntilDateTime.date(),
					QLocale::ShortFormat);
				auto time = QLocale().toString(
					restrictedUntilDateTime.time(),
					QLocale::ShortFormat);

				switch (restriction) {
				case Flag::SendPolls:
					return tr::lng_restricted_send_polls_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendOther:
					return tr::lng_restricted_send_message_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendPhotos:
					return tr::lng_restricted_send_photos_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendVideos:
					return tr::lng_restricted_send_videos_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendMusic:
					return tr::lng_restricted_send_music_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendFiles:
					return tr::lng_restricted_send_files_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendVideoMessages:
					return tr::lng_restricted_send_video_messages_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendVoiceMessages:
					return tr::lng_restricted_send_voice_messages_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendStickers:
					return tr::lng_restricted_send_stickers_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendGifs:
					return tr::lng_restricted_send_gifs_until(
						tr::now, lt_date, date, lt_time, time);
				case Flag::SendInline:
				case Flag::SendGames:
					return tr::lng_restricted_send_inline_until(
						tr::now, lt_date, date, lt_time, time);
				}
				Unexpected("Restriction in Data::RestrictionErrorKey.");
			}
		}
		if (all
			&& channel
			&& channel->boostsUnrestrict()
			&& !channel->unrestrictedByBoosts()) {
			return SendError({
				.text = tr::lng_restricted_boost_group(tr::now),
				.boostsToLift = (channel->boostsUnrestrict()
					- channel->boostsApplied()),
			});
		}
		switch (restriction) {
		case Flag::SendPolls:
			return all
				? tr::lng_restricted_send_polls_all(tr::now)
				: tr::lng_restricted_send_polls(tr::now);
		case Flag::SendOther:
			return all
				? tr::lng_restricted_send_message_all(tr::now)
				: tr::lng_restricted_send_message(tr::now);
		case Flag::SendPhotos:
			return all
				? tr::lng_restricted_send_photos_all(tr::now)
				: tr::lng_restricted_send_photos(tr::now);
		case Flag::SendVideos:
			return all
				? tr::lng_restricted_send_videos_all(tr::now)
				: tr::lng_restricted_send_videos(tr::now);
		case Flag::SendMusic:
			return all
				? tr::lng_restricted_send_music_all(tr::now)
				: tr::lng_restricted_send_music(tr::now);
		case Flag::SendFiles:
			return all
				? tr::lng_restricted_send_files_all(tr::now)
				: tr::lng_restricted_send_files(tr::now);
		case Flag::SendVideoMessages:
			return all
				? tr::lng_restricted_send_video_messages_all(tr::now)
				: tr::lng_restricted_send_video_messages_group(tr::now);
		case Flag::SendVoiceMessages:
			return all
				? tr::lng_restricted_send_voice_messages_all(tr::now)
				: tr::lng_restricted_send_voice_messages_group(tr::now);
		case Flag::SendStickers:
			return all
				? tr::lng_restricted_send_stickers_all(tr::now)
				: tr::lng_restricted_send_stickers(tr::now);
		case Flag::SendGifs:
			return all
				? tr::lng_restricted_send_gifs_all(tr::now)
				: tr::lng_restricted_send_gifs(tr::now);
		case Flag::SendInline:
		case Flag::SendGames:
			return all
				? tr::lng_restricted_send_inline_all(tr::now)
				: tr::lng_restricted_send_inline(tr::now);
		}
		Unexpected("Restriction in Data::RestrictionErrorKey.");
	}
	return SendError();
}

SendError AnyFileRestrictionError(not_null<PeerData*> peer) {
	using Restriction = ChatRestriction;
	for (const auto right : FilesSendRestrictionsList()) {
		if (!RestrictionError(peer, right)) {
			return {};
		}
	}
	return RestrictionError(peer, Restriction::SendFiles);
}

SendError FileRestrictionError(
		not_null<PeerData*> peer,
		const Ui::PreparedList &list,
		std::optional<bool> compress) {
	const auto slowmode = peer->slowmodeApplied();
	if (slowmode) {
		if (!list.canBeSentInSlowmode()) {
			return tr::lng_slowmode_no_many(tr::now);
		} else if (list.files.size() > 1 && list.hasSticker()) {
			if (compress == false) {
				return tr::lng_slowmode_no_many(tr::now);
			} else {
				compress = true;
			}
		}
	}
	for (const auto &file : list.files) {
		if (const auto error = FileRestrictionError(peer, file, compress)) {
			return error;
		}
	}
	return {};
}

SendError FileRestrictionError(
		not_null<PeerData*> peer,
		const Ui::PreparedFile &file,
		std::optional<bool> compress) {
	using Type = Ui::PreparedFile::Type;
	using Restriction = ChatRestriction;
	const auto stickers = RestrictionError(peer, Restriction::SendStickers);
	const auto gifs = RestrictionError(peer, Restriction::SendGifs);
	const auto photos = RestrictionError(peer, Restriction::SendPhotos);
	const auto videos = RestrictionError(peer, Restriction::SendVideos);
	const auto music = RestrictionError(peer, Restriction::SendMusic);
	const auto files = RestrictionError(peer, Restriction::SendFiles);
	if (!stickers && !gifs && !photos && !videos && !music && !files) {
		return {};
	}
	switch (file.type) {
	case Type::Photo:
		if (compress == true && photos) {
			return photos;
		} else if (const auto other = file.isSticker() ? stickers : files) {
			if ((compress == false || photos) && other) {
				return (file.isSticker() || !photos) ? other : photos;
			}
		}
		break;
	case Type::Video:
		if (const auto error = file.isGifv() ? gifs : videos) {
			return error;
		}
		break;
	case Type::Music:
		if (music) {
			return music;
		}
		break;
	case Type::File:
		if (files) {
			return files;
		}
		break;
	}
	return {};
}

void ShowSendErrorToast(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Data::SendError error) {
	return ShowSendErrorToast(navigation->uiShow(), peer, error);
}

void ShowSendErrorToast(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		Data::SendError error) {
	if (!error.boostsToLift) {
		show->showToast(*error);
		return;
	}
	const auto boost = [=] {
		const auto window = show->resolveWindow();
		window->resolveBoostState(peer->asChannel(), error.boostsToLift);
	};
	show->showToast({
		.text = Ui::Text::Link(*error),
		.filter = [=](const auto &...) { boost(); return false; },
	});
}

} // namespace Data
