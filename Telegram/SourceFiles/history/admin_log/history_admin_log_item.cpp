/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/admin_log/history_admin_log_item.h"

#include "history/admin_log/history_admin_log_inner.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_location_manager.h"
#include "api/api_chat_participants.h"
#include "api/api_text_entities.h"
#include "data/data_channel.h"
#include "data/data_file_origin.h"
#include "data/data_forum_topic.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_message_reaction_id.h"
#include "data/stickers/data_custom_emoji.h"
#include "lang/lang_keys.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/basic_click_handlers.h"
#include "boxes/sticker_set_box.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"

namespace AdminLog {
namespace {

TextWithEntities PrepareText(
		const QString &value,
		const QString &emptyValue) {
	auto result = TextWithEntities{ value };
	if (result.text.isEmpty()) {
		result.text = emptyValue;
		if (!emptyValue.isEmpty()) {
			result.entities.push_back({
				EntityType::Italic,
				0,
				int(emptyValue.size()) });
		}
	} else {
		TextUtilities::ParseEntities(
			result,
			TextParseLinks
				| TextParseMentions
				| TextParseHashtags
				| TextParseBotCommands);
	}
	return result;
}

[[nodiscard]] TimeId ExtractSentDate(const MTPMessage &message) {
	return message.match([](const MTPDmessageEmpty &) {
		return 0;
	}, [](const MTPDmessageService &data) {
		return data.vdate().v;
	}, [](const MTPDmessage &data) {
		return data.vdate().v;
	});
}

[[nodiscard]] MsgId ExtractRealMsgId(const MTPMessage &message) {
	return MsgId(message.match([](const MTPDmessageEmpty &) {
		return 0;
	}, [](const MTPDmessageService &data) {
		return data.vid().v;
	}, [](const MTPDmessage &data) {
		return data.vid().v;
	}));
}

MTPMessage PrepareLogMessage(const MTPMessage &message, TimeId newDate) {
	return message.match([&](const MTPDmessageEmpty &data) {
		return MTP_messageEmpty(
			data.vflags(),
			data.vid(),
			data.vpeer_id() ? *data.vpeer_id() : MTPPeer());
	}, [&](const MTPDmessageService &data) {
		const auto removeFlags = MTPDmessageService::Flag::f_out
			| MTPDmessageService::Flag::f_post
			| MTPDmessageService::Flag::f_reply_to
			| MTPDmessageService::Flag::f_ttl_period;
		return MTP_messageService(
			MTP_flags(data.vflags().v & ~removeFlags),
			data.vid(),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			data.vpeer_id(),
			MTPMessageReplyHeader(),
			MTP_int(newDate),
			data.vaction(),
			MTPint()); // ttl_period
	}, [&](const MTPDmessage &data) {
		const auto removeFlags = MTPDmessage::Flag::f_out
			| MTPDmessage::Flag::f_post
			| MTPDmessage::Flag::f_reply_to
			| MTPDmessage::Flag::f_replies
			| MTPDmessage::Flag::f_edit_date
			| MTPDmessage::Flag::f_grouped_id
			| MTPDmessage::Flag::f_views
			| MTPDmessage::Flag::f_forwards
			//| MTPDmessage::Flag::f_reactions
			| MTPDmessage::Flag::f_restriction_reason
			| MTPDmessage::Flag::f_ttl_period;
		return MTP_message(
			MTP_flags(data.vflags().v & ~removeFlags),
			data.vid(),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			MTPint(), // from_boosts_applied
			data.vpeer_id(),
			MTPPeer(), // saved_peer_id
			data.vfwd_from() ? *data.vfwd_from() : MTPMessageFwdHeader(),
			MTP_long(data.vvia_bot_id().value_or_empty()),
			MTPMessageReplyHeader(),
			MTP_int(newDate),
			data.vmessage(),
			data.vmedia() ? *data.vmedia() : MTPMessageMedia(),
			data.vreply_markup() ? *data.vreply_markup() : MTPReplyMarkup(),
			(data.ventities()
				? *data.ventities()
				: MTPVector<MTPMessageEntity>()),
			MTP_int(data.vviews().value_or_empty()),
			MTP_int(data.vforwards().value_or_empty()),
			MTPMessageReplies(),
			MTPint(), // edit_date
			MTP_string(),
			MTP_long(0), // grouped_id
			MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>(),
			MTPint(), // ttl_period
			MTPint()); // quick_reply_shortcut_id
	});
}

bool MediaCanHaveCaption(const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return false;
	}
	const auto &data = message.c_message();
	const auto media = data.vmedia();
	const auto mediaType = media ? media->type() : mtpc_messageMediaEmpty;
	return (mediaType == mtpc_messageMediaDocument)
		|| (mediaType == mtpc_messageMediaPhoto);
}

uint64 MediaId(const MTPMessage &message) {
	if (!MediaCanHaveCaption(message)) {
		return 0;
	}
	const auto &media = message.c_message().vmedia();
	return media
		? v::match(
			Data::GetFileReferences(*media).data.begin()->first,
			[](const auto &d) { return d.id; })
		: 0;
}

TextWithEntities ExtractEditedText(
		not_null<Main::Session*> session,
		const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return TextWithEntities();
	}
	const auto &data = message.c_message();
	return {
		qs(data.vmessage()),
		Api::EntitiesFromMTP(session, data.ventities().value_or_empty())
	};
}

const auto CollectChanges = [](
		auto &phraseMap,
		auto plusFlags,
		auto minusFlags) {
	auto withPrefix = [&phraseMap](auto flags, QChar prefix) {
		auto result = QString();
		for (auto &phrase : phraseMap) {
			if (flags & phrase.first) {
				result.append('\n' + (prefix + phrase.second(tr::now)));
			}
		}
		return result;
	};
	const auto kMinus = QChar(0x2212);
	return withPrefix(plusFlags & ~minusFlags, '+')
		+ withPrefix(minusFlags & ~plusFlags, kMinus);
};

TextWithEntities GenerateAdminChangeText(
		not_null<ChannelData*> channel,
		const TextWithEntities &user,
		ChatAdminRightsInfo newRights,
		ChatAdminRightsInfo prevRights) {
	using Flag = ChatAdminRight;
	using Flags = ChatAdminRights;

	auto result = tr::lng_admin_log_promoted(
		tr::now,
		lt_user,
		user,
		Ui::Text::WithEntities);

	const auto useInviteLinkPhrase = channel->isMegagroup()
		&& channel->anyoneCanAddMembers();
	const auto invitePhrase = useInviteLinkPhrase
		? tr::lng_admin_log_admin_invite_link
		: tr::lng_admin_log_admin_invite_users;
	const auto callPhrase = channel->isBroadcast()
		? tr::lng_admin_log_admin_manage_calls_channel
		: tr::lng_admin_log_admin_manage_calls;
	static auto phraseMap = std::map<Flags, tr::phrase<>> {
		{ Flag::ChangeInfo, tr::lng_admin_log_admin_change_info },
		{ Flag::PostMessages, tr::lng_admin_log_admin_post_messages },
		{ Flag::EditMessages, tr::lng_admin_log_admin_edit_messages },
		{ Flag::DeleteMessages, tr::lng_admin_log_admin_delete_messages },
		{ Flag::BanUsers, tr::lng_admin_log_admin_ban_users },
		{ Flag::InviteByLinkOrAdd, invitePhrase },
		{ Flag::ManageTopics, tr::lng_admin_log_admin_manage_topics },
		{ Flag::PinMessages, tr::lng_admin_log_admin_pin_messages },
		{ Flag::ManageCall, tr::lng_admin_log_admin_manage_calls },
		{ Flag::AddAdmins, tr::lng_admin_log_admin_add_admins },
		{ Flag::Anonymous, tr::lng_admin_log_admin_remain_anonymous },
	};
	phraseMap[Flag::InviteByLinkOrAdd] = invitePhrase;
	phraseMap[Flag::ManageCall] = callPhrase;

	if (!channel->isMegagroup()) {
		// Don't display "Ban users" changes in channels.
		newRights.flags &= ~Flag::BanUsers;
		prevRights.flags &= ~Flag::BanUsers;
	}

	const auto changes = CollectChanges(
		phraseMap,
		newRights.flags,
		prevRights.flags);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}

	return result;
};

QString GeneratePermissionsChangeText(
		ChatRestrictionsInfo newRights,
		ChatRestrictionsInfo prevRights) {
	using Flag = ChatRestriction;
	using Flags = ChatRestrictions;

	static auto phraseMap = std::map<Flags, tr::phrase<>>{
		{ Flag::ViewMessages, tr::lng_admin_log_banned_view_messages },
		{ Flag::SendOther, tr::lng_admin_log_banned_send_messages },
		{ Flag::SendPhotos, tr::lng_admin_log_banned_send_photos },
		{ Flag::SendVideos, tr::lng_admin_log_banned_send_videos },
		{ Flag::SendMusic, tr::lng_admin_log_banned_send_music },
		{ Flag::SendFiles, tr::lng_admin_log_banned_send_files },
		{
			Flag::SendVoiceMessages,
			tr::lng_admin_log_banned_send_voice_messages },
		{
			Flag::SendVideoMessages,
			tr::lng_admin_log_banned_send_video_messages },
		{ Flag::SendStickers
			| Flag::SendGifs
			| Flag::SendInline
			| Flag::SendGames, tr::lng_admin_log_banned_send_stickers },
		{ Flag::EmbedLinks, tr::lng_admin_log_banned_embed_links },
		{ Flag::SendPolls, tr::lng_admin_log_banned_send_polls },
		{ Flag::ChangeInfo, tr::lng_admin_log_admin_change_info },
		{ Flag::AddParticipants, tr::lng_admin_log_admin_invite_users },
		{ Flag::CreateTopics, tr::lng_admin_log_admin_create_topics },
		{ Flag::PinMessages, tr::lng_admin_log_admin_pin_messages },
	};
	return CollectChanges(phraseMap, prevRights.flags, newRights.flags);
}

TextWithEntities GeneratePermissionsChangeText(
		PeerId participantId,
		const TextWithEntities &user,
		ChatRestrictionsInfo newRights,
		ChatRestrictionsInfo prevRights) {
	using Flag = ChatRestriction;

	const auto newFlags = newRights.flags;
	const auto newUntil = newRights.until;
	const auto prevFlags = prevRights.flags;
	const auto indefinitely = ChannelData::IsRestrictedForever(newUntil);
	if (newFlags & Flag::ViewMessages) {
		return tr::lng_admin_log_banned(
			tr::now,
			lt_user,
			user,
			Ui::Text::WithEntities);
	} else if (newFlags == 0
		&& (prevFlags & Flag::ViewMessages)
		&& !peerIsUser(participantId)) {
		return tr::lng_admin_log_unbanned(
			tr::now,
			lt_user,
			user,
			Ui::Text::WithEntities);
	}
	const auto untilText = indefinitely
		? tr::lng_admin_log_restricted_forever(tr::now)
		: tr::lng_admin_log_restricted_until(
			tr::now,
			lt_date,
			langDateTime(base::unixtime::parse(newUntil)));
	auto result = tr::lng_admin_log_restricted(
		tr::now,
		lt_user,
		user,
		lt_until,
		TextWithEntities { untilText },
		Ui::Text::WithEntities);
	const auto changes = GeneratePermissionsChangeText(newRights, prevRights);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}
	return result;
}

QString PublicJoinLink() {
	return u"(public_join_link)"_q;
}

QString ExtractInviteLink(const MTPExportedChatInvite &data) {
	return data.match([&](const MTPDchatInviteExported &data) {
		return qs(data.vlink());
	}, [&](const MTPDchatInvitePublicJoinRequests &data) {
		return PublicJoinLink();
	});
}

QString ExtractInviteLinkLabel(const MTPExportedChatInvite &data) {
	return data.match([&](const MTPDchatInviteExported &data) {
		return qs(data.vtitle().value_or_empty());
	}, [&](const MTPDchatInvitePublicJoinRequests &data) {
		return PublicJoinLink();
	});
}

QString InternalInviteLinkUrl(const MTPExportedChatInvite &data) {
	const auto base64 = ExtractInviteLink(data).toUtf8().toBase64();
	return "internal:show_invite_link/?link=" + QString::fromLatin1(base64);
}

QString GenerateInviteLinkText(const MTPExportedChatInvite &data) {
	const auto label = ExtractInviteLinkLabel(data);
	return label.isEmpty() ? ExtractInviteLink(data).replace(
		u"https://"_q,
		QString()
	).replace(
		u"t.me/joinchat/"_q,
		QString()
	) : label;
}

TextWithEntities GenerateInviteLinkLink(const MTPExportedChatInvite &data) {
	const auto text = GenerateInviteLinkText(data);
	return text.endsWith(Ui::kQEllipsis)
		? TextWithEntities{ .text = text }
		: Ui::Text::Link(text, InternalInviteLinkUrl(data));
}

TextWithEntities GenerateInviteLinkChangeText(
		const MTPExportedChatInvite &newLink,
		const MTPExportedChatInvite &prevLink) {
	auto link = TextWithEntities{ GenerateInviteLinkText(newLink) };
	if (!link.text.endsWith(Ui::kQEllipsis)) {
		link.entities.push_back({
			EntityType::CustomUrl,
			0,
			int(link.text.size()),
			InternalInviteLinkUrl(newLink) });
	}
	auto result = tr::lng_admin_log_edited_invite_link(
		tr::now,
		lt_link,
		link,
		Ui::Text::WithEntities);
	result.text.append('\n');

	const auto label = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return qs(data.vtitle().value_or_empty());
		}, [&](const MTPDchatInvitePublicJoinRequests &data) {
			return PublicJoinLink();
		});
	};
	const auto expireDate = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.vexpire_date().value_or_empty();
		}, [&](const MTPDchatInvitePublicJoinRequests &data) {
			return TimeId();
		});
	};
	const auto usageLimit = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.vusage_limit().value_or_empty();
		}, [&](const MTPDchatInvitePublicJoinRequests &data) {
			return 0;
		});
	};
	const auto requestApproval = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.is_request_needed();
		}, [&](const MTPDchatInvitePublicJoinRequests &data) {
			return true;
		});
	};
	const auto wrapDate = [](TimeId date) {
		return date
			? langDateTime(base::unixtime::parse(date))
			: tr::lng_group_invite_expire_never(tr::now);
	};
	const auto wrapUsage = [](int count) {
		return count
			? QString::number(count)
			: tr::lng_group_invite_usage_any(tr::now);
	};
	const auto wasLabel = label(prevLink);
	const auto nowLabel = label(newLink);
	const auto wasExpireDate = expireDate(prevLink);
	const auto nowExpireDate = expireDate(newLink);
	const auto wasUsageLimit = usageLimit(prevLink);
	const auto nowUsageLimit = usageLimit(newLink);
	const auto wasRequestApproval = requestApproval(prevLink);
	const auto nowRequestApproval = requestApproval(newLink);
	if (wasLabel != nowLabel) {
		result.text.append('\n').append(
			tr::lng_admin_log_invite_link_label(
				tr::now,
				lt_previous,
				wasLabel,
				lt_limit,
				nowLabel));
	}
	if (wasExpireDate != nowExpireDate) {
		result.text.append('\n').append(
			tr::lng_admin_log_invite_link_expire_date(
				tr::now,
				lt_previous,
				wrapDate(wasExpireDate),
				lt_limit,
				wrapDate(nowExpireDate)));
	}
	if (wasUsageLimit != nowUsageLimit) {
		result.text.append('\n').append(
			tr::lng_admin_log_invite_link_usage_limit(
				tr::now,
				lt_previous,
				wrapUsage(wasUsageLimit),
				lt_limit,
				wrapUsage(nowUsageLimit)));
	}
	if (wasRequestApproval != nowRequestApproval) {
		result.text.append('\n').append(
			nowRequestApproval
				? tr::lng_admin_log_invite_link_request_needed(tr::now)
				: tr::lng_admin_log_invite_link_request_not_needed(tr::now));
	}

	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
};

auto GenerateParticipantString(
		not_null<Main::Session*> session,
		PeerId participantId) {
	// User name in "User name (@username)" format with entities.
	const auto peer = session->data().peer(participantId);
	auto name = TextWithEntities { peer->name()};
	if (const auto user = peer->asUser()) {
		const auto data = TextUtilities::MentionNameDataFromFields({
			.selfId = session->userId().bare,
			.userId = peerToUser(user->id).bare,
			.accessHash = user->accessHash(),
		});
		name.entities.push_back({
			EntityType::MentionName,
			0,
			int(name.text.size()),
			data,
		});
	}
	const auto username = peer->userName();
	if (username.isEmpty()) {
		return name;
	}
	auto mention = TextWithEntities { '@' + username };
	mention.entities.push_back({
		EntityType::Mention,
		0,
		int(mention.text.size()) });
	return tr::lng_admin_log_user_with_username(
		tr::now,
		lt_name,
		name,
		lt_mention,
		mention,
		Ui::Text::WithEntities);
}

auto GenerateParticipantChangeText(
		not_null<ChannelData*> channel,
		const Api::ChatParticipant &participant,
		std::optional<Api::ChatParticipant> oldParticipant = std::nullopt) {
	using Type = Api::ChatParticipant::Type;
	const auto oldRights = oldParticipant
		? oldParticipant->rights()
		: ChatAdminRightsInfo();
	const auto oldRestrictions = oldParticipant
		? oldParticipant->restrictions()
		: ChatRestrictionsInfo();

	const auto generateOther = [&](PeerId participantId) {
		auto user = GenerateParticipantString(
			&channel->session(),
			participantId);
		if (oldParticipant && oldParticipant->type() == Type::Admin) {
			return GenerateAdminChangeText(
				channel,
				user,
				ChatAdminRightsInfo(),
				oldRights);
		} else if (oldParticipant && oldParticipant->type() == Type::Banned) {
			return GeneratePermissionsChangeText(
				participantId,
				user,
				ChatRestrictionsInfo(),
				oldRestrictions);
		} else if (oldParticipant
				&& oldParticipant->type() == Type::Restricted
				&& (participant.type() == Type::Member
						|| participant.type() == Type::Left)) {
			return GeneratePermissionsChangeText(
				participantId,
				user,
				ChatRestrictionsInfo(),
				oldRestrictions);
		}
		return tr::lng_admin_log_invited(
			tr::now,
			lt_user,
			user,
			Ui::Text::WithEntities);
	};

	auto result = [&] {
		const auto &peerId = participant.id();
		switch (participant.type()) {
		case Api::ChatParticipant::Type::Creator: {
			// No valid string here :(
			const auto user = GenerateParticipantString(
				&channel->session(),
				peerId);
			if (peerId == channel->session().userPeerId()) {
				return GenerateAdminChangeText(
					channel,
					user,
					participant.rights(),
					oldRights);
			}
			return tr::lng_admin_log_transferred(
				tr::now,
				lt_user,
				user,
				Ui::Text::WithEntities);
		}
		case Api::ChatParticipant::Type::Admin: {
			const auto user = GenerateParticipantString(
				&channel->session(),
				peerId);
			return GenerateAdminChangeText(
				channel,
				user,
				participant.rights(),
				oldRights);
		}
		case Api::ChatParticipant::Type::Restricted:
		case Api::ChatParticipant::Type::Banned: {
			const auto user = GenerateParticipantString(
				&channel->session(),
				peerId);
			return GeneratePermissionsChangeText(
				peerId,
				user,
				participant.restrictions(),
				oldRestrictions);
		}
		case Api::ChatParticipant::Type::Left:
		case Api::ChatParticipant::Type::Member:
			return generateOther(peerId);
		};
		Unexpected("Participant type in GenerateParticipantChangeText.");
	}();

	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
}

TextWithEntities GenerateParticipantChangeText(
		not_null<ChannelData*> channel,
		const MTPChannelParticipant &participant,
		std::optional<MTPChannelParticipant>oldParticipant = std::nullopt) {
	return GenerateParticipantChangeText(
		channel,
		Api::ChatParticipant(participant, channel),
		oldParticipant
			? std::make_optional(Api::ChatParticipant(
				*oldParticipant,
				channel))
			: std::nullopt);
}

TextWithEntities GenerateDefaultBannedRightsChangeText(
		not_null<ChannelData*> channel,
		ChatRestrictionsInfo rights,
		ChatRestrictionsInfo oldRights) {
	auto result = TextWithEntities{
		tr::lng_admin_log_changed_default_permissions(tr::now)
	};
	const auto changes = GeneratePermissionsChangeText(rights, oldRights);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}
	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
}

[[nodiscard]] bool IsTopicClosed(const MTPForumTopic &topic) {
	return topic.match([](const MTPDforumTopic &data) {
		return data.is_closed();
	}, [](const MTPDforumTopicDeleted &) {
		return false;
	});
}

[[nodiscard]] bool IsTopicHidden(const MTPForumTopic &topic) {
	return topic.match([](const MTPDforumTopic &data) {
		return data.is_hidden();
	}, [](const MTPDforumTopicDeleted &) {
		return false;
	});
}

[[nodiscard]] TextWithEntities GenerateTopicLink(
		not_null<ChannelData*> channel,
		const MTPForumTopic &topic) {
	return topic.match([&](const MTPDforumTopic &data) {
		return Ui::Text::Link(
			Data::ForumTopicIconWithTitle(
				data.vid().v,
				data.vicon_emoji_id().value_or_empty(),
				qs(data.vtitle())),
			u"internal:url:https://t.me/c/%1/%2"_q.arg(
				peerToChannel(channel->id).bare).arg(
					data.vid().v));
	}, [](const MTPDforumTopicDeleted &) {
		return TextWithEntities{ u"Deleted"_q };
	});
}

} // namespace

OwnedItem::OwnedItem(std::nullptr_t) {
}

OwnedItem::OwnedItem(
	not_null<HistoryView::ElementDelegate*> delegate,
	not_null<HistoryItem*> data)
: _data(data)
, _view(_data->createView(delegate)) {
}

OwnedItem::OwnedItem(OwnedItem &&other)
: _data(base::take(other._data))
, _view(base::take(other._view)) {
}

OwnedItem &OwnedItem::operator=(OwnedItem &&other) {
	_data = base::take(other._data);
	_view = base::take(other._view);
	return *this;
}

OwnedItem::~OwnedItem() {
	clearView();
	if (_data) {
		_data->destroy();
	}
}

void OwnedItem::refreshView(
		not_null<HistoryView::ElementDelegate*> delegate) {
	_view = _data->createView(delegate);
}

void OwnedItem::clearView() {
	_view = nullptr;
}

void GenerateItems(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const MTPDchannelAdminLogEvent &event,
		Fn<void(OwnedItem item, TimeId sentDate, MsgId)> callback) {
	Expects(history->peer->isChannel());

	using LogTitle = MTPDchannelAdminLogEventActionChangeTitle;
	using LogAbout = MTPDchannelAdminLogEventActionChangeAbout;
	using LogUsername = MTPDchannelAdminLogEventActionChangeUsername;
	using LogPhoto = MTPDchannelAdminLogEventActionChangePhoto;
	using LogInvites = MTPDchannelAdminLogEventActionToggleInvites;
	using LogSign = MTPDchannelAdminLogEventActionToggleSignatures;
	using LogPin = MTPDchannelAdminLogEventActionUpdatePinned;
	using LogEdit = MTPDchannelAdminLogEventActionEditMessage;
	using LogDelete = MTPDchannelAdminLogEventActionDeleteMessage;
	using LogJoin = MTPDchannelAdminLogEventActionParticipantJoin;
	using LogLeave = MTPDchannelAdminLogEventActionParticipantLeave;
	using LogInvite = MTPDchannelAdminLogEventActionParticipantInvite;
	using LogBan = MTPDchannelAdminLogEventActionParticipantToggleBan;
	using LogPromote = MTPDchannelAdminLogEventActionParticipantToggleAdmin;
	using LogSticker = MTPDchannelAdminLogEventActionChangeStickerSet;
	using LogEmoji = MTPDchannelAdminLogEventActionChangeEmojiStickerSet;
	using LogPreHistory =
		MTPDchannelAdminLogEventActionTogglePreHistoryHidden;
	using LogPermissions = MTPDchannelAdminLogEventActionDefaultBannedRights;
	using LogPoll = MTPDchannelAdminLogEventActionStopPoll;
	using LogDiscussion = MTPDchannelAdminLogEventActionChangeLinkedChat;
	using LogLocation = MTPDchannelAdminLogEventActionChangeLocation;
	using LogSlowMode = MTPDchannelAdminLogEventActionToggleSlowMode;
	using LogStartCall = MTPDchannelAdminLogEventActionStartGroupCall;
	using LogDiscardCall = MTPDchannelAdminLogEventActionDiscardGroupCall;
	using LogMute = MTPDchannelAdminLogEventActionParticipantMute;
	using LogUnmute = MTPDchannelAdminLogEventActionParticipantUnmute;
	using LogCallSetting =
		MTPDchannelAdminLogEventActionToggleGroupCallSetting;
	using LogJoinByInvite =
		MTPDchannelAdminLogEventActionParticipantJoinByInvite;
	using LogInviteDelete =
		MTPDchannelAdminLogEventActionExportedInviteDelete;
	using LogInviteRevoke =
		MTPDchannelAdminLogEventActionExportedInviteRevoke;
	using LogInviteEdit = MTPDchannelAdminLogEventActionExportedInviteEdit;
	using LogVolume = MTPDchannelAdminLogEventActionParticipantVolume;
	using LogTTL = MTPDchannelAdminLogEventActionChangeHistoryTTL;
	using LogJoinByRequest =
		MTPDchannelAdminLogEventActionParticipantJoinByRequest;
	using LogNoForwards = MTPDchannelAdminLogEventActionToggleNoForwards;
	using LogSendMessage = MTPDchannelAdminLogEventActionSendMessage;
	using LogChangeAvailableReactions = MTPDchannelAdminLogEventActionChangeAvailableReactions;
	using LogChangeUsernames = MTPDchannelAdminLogEventActionChangeUsernames;
	using LogToggleForum = MTPDchannelAdminLogEventActionToggleForum;
	using LogCreateTopic = MTPDchannelAdminLogEventActionCreateTopic;
	using LogEditTopic = MTPDchannelAdminLogEventActionEditTopic;
	using LogDeleteTopic = MTPDchannelAdminLogEventActionDeleteTopic;
	using LogPinTopic = MTPDchannelAdminLogEventActionPinTopic;
	using LogToggleAntiSpam = MTPDchannelAdminLogEventActionToggleAntiSpam;
	using LogChangePeerColor = MTPDchannelAdminLogEventActionChangePeerColor;
	using LogChangeProfilePeerColor = MTPDchannelAdminLogEventActionChangeProfilePeerColor;
	using LogChangeWallpaper = MTPDchannelAdminLogEventActionChangeWallpaper;
	using LogChangeEmojiStatus = MTPDchannelAdminLogEventActionChangeEmojiStatus;

	const auto session = &history->session();
	const auto id = event.vid().v;
	const auto from = history->owner().user(event.vuser_id().v);
	const auto channel = history->peer->asChannel();
	const auto broadcast = channel->isBroadcast();
	const auto &action = event.vaction();
	const auto date = event.vdate().v;
	const auto addPart = [&](
			not_null<HistoryItem*> item,
			TimeId sentDate = 0,
			MsgId realId = MsgId()) {
		return callback(OwnedItem(delegate, item), sentDate, realId);
	};

	const auto fromName = from->name();
	const auto fromLink = from->createOpenLink();
	const auto fromLinkText = Ui::Text::Link(fromName, QString());

	const auto addSimpleServiceMessage = [&](
			const TextWithEntities &text,
			MsgId realId = MsgId(),
			PhotoData *photo = nullptr) {
		auto message = PreparedServiceText{ text };
		message.links.push_back(fromLink);
		addPart(
			history->makeMessage({
				.id = history->nextNonHistoryEntryId(),
				.flags = MessageFlag::AdminLogEntry,
				.from = from->id,
				.date = date,
			}, std::move(message), photo),
			0,
			realId);
	};

	const auto createChangeTitle = [&](const LogTitle &action) {
		auto text = (channel->isMegagroup()
			? tr::lng_action_changed_title
			: tr::lng_admin_log_changed_title_channel)(
				tr::now,
				lt_from,
				fromLinkText,
				lt_title,
				{ .text = qs(action.vnew_value()) },
				Ui::Text::WithEntities);
		addSimpleServiceMessage(std::move(text));
	};

	const auto makeSimpleTextMessage = [&](TextWithEntities &&text) {
		return history->makeMessage({
			.id = history->nextNonHistoryEntryId(),
			.flags = MessageFlag::HasFromId | MessageFlag::AdminLogEntry,
			.from = from->id,
		}, std::move(text), MTP_messageMediaEmpty());
	};

	const auto addSimpleTextMessage = [&](TextWithEntities &&text) {
		addPart(makeSimpleTextMessage(std::move(text)));
	};

	const auto createChangeAbout = [&](const LogAbout &action) {
		const auto newValue = qs(action.vnew_value());
		const auto oldValue = qs(action.vprev_value());
		const auto text = (channel->isMegagroup()
			? (newValue.isEmpty()
				? tr::lng_admin_log_removed_description_group
				: tr::lng_admin_log_changed_description_group)
			: (newValue.isEmpty()
				? tr::lng_admin_log_removed_description_channel
				: tr::lng_admin_log_changed_description_channel)
			)(tr::now, lt_from, fromLinkText, Ui::Text::WithEntities);
		addSimpleServiceMessage(text);

		const auto body = makeSimpleTextMessage(
			PrepareText(newValue, QString()));
		if (!oldValue.isEmpty()) {
			const auto oldDescription = PrepareText(oldValue, QString());
			body->addLogEntryOriginal(
				id,
				tr::lng_admin_log_previous_description(tr::now),
				oldDescription);
		}
		addPart(body);
	};

	const auto createChangeUsername = [&](const LogUsername &action) {
		const auto newValue = qs(action.vnew_value());
		const auto oldValue = qs(action.vprev_value());
		const auto text = (channel->isMegagroup()
			? (newValue.isEmpty()
				? tr::lng_admin_log_removed_link_group
				: tr::lng_admin_log_changed_link_group)
			: (newValue.isEmpty()
				? tr::lng_admin_log_removed_link_channel
				: tr::lng_admin_log_changed_link_channel)
			)(tr::now, lt_from, fromLinkText, Ui::Text::WithEntities);
		addSimpleServiceMessage(text);

		const auto body = makeSimpleTextMessage(newValue.isEmpty()
			? TextWithEntities()
			: PrepareText(
				history->session().createInternalLinkFull(newValue),
				QString()));
		if (!oldValue.isEmpty()) {
			const auto oldLink = PrepareText(
				history->session().createInternalLinkFull(oldValue),
				QString());
			body->addLogEntryOriginal(
				id,
				tr::lng_admin_log_previous_link(tr::now),
				oldLink);
		}
		addPart(body);
	};

	const auto createChangePhoto = [&](const LogPhoto &action) {
		action.vnew_photo().match([&](const MTPDphoto &data) {
			const auto photo = history->owner().processPhoto(data);
			const auto text = (channel->isMegagroup()
				? tr::lng_admin_log_changed_photo_group
				: tr::lng_admin_log_changed_photo_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
			addSimpleServiceMessage(text, MsgId(), photo);
		}, [&](const MTPDphotoEmpty &data) {
			const auto text = (channel->isMegagroup()
				? tr::lng_admin_log_removed_photo_group
				: tr::lng_admin_log_removed_photo_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		});
	};

	const auto createToggleInvites = [&](const LogInvites &action) {
		const auto enabled = (action.vnew_value().type() == mtpc_boolTrue);
		const auto text = (enabled
			? tr::lng_admin_log_invites_enabled
			: tr::lng_admin_log_invites_disabled)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createToggleSignatures = [&](const LogSign &action) {
		const auto enabled = (action.vnew_value().type() == mtpc_boolTrue);
		const auto text = (enabled
			? tr::lng_admin_log_signatures_enabled
			: tr::lng_admin_log_signatures_disabled)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createUpdatePinned = [&](const LogPin &action) {
		action.vmessage().match([&](const MTPDmessage &data) {
			const auto pinned = data.is_pinned();
			const auto realId = ExtractRealMsgId(action.vmessage());
			const auto text = (pinned
				? tr::lng_admin_log_pinned_message
				: tr::lng_admin_log_unpinned_message)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
			addSimpleServiceMessage(text, realId);

			const auto detachExistingItem = false;
			addPart(
				history->createItem(
					history->nextNonHistoryEntryId(),
					PrepareLogMessage(action.vmessage(), date),
					MessageFlag::AdminLogEntry,
					detachExistingItem),
				ExtractSentDate(action.vmessage()),
				realId);
		}, [&](const auto &) {
			const auto text = tr::lng_admin_log_unpinned_message(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		});
	};

	const auto createEditMessage = [&](const LogEdit &action) {
		const auto realId = ExtractRealMsgId(action.vnew_message());
		const auto sentDate = ExtractSentDate(action.vnew_message());
		const auto newValue = ExtractEditedText(
			session,
			action.vnew_message());
		auto oldValue = ExtractEditedText(
			session,
			action.vprev_message());

		const auto canHaveCaption = MediaCanHaveCaption(
			action.vnew_message());
		const auto changedCaption = (newValue != oldValue);
		const auto changedMedia = MediaId(action.vnew_message())
			!= MediaId(action.vprev_message());
		const auto removedCaption = !oldValue.text.isEmpty()
			&& newValue.text.isEmpty();
		const auto text = (!canHaveCaption
			? tr::lng_admin_log_edited_message
			: (changedMedia && removedCaption)
			? tr::lng_admin_log_edited_media_and_removed_caption
			: (changedMedia && changedCaption)
			? tr::lng_admin_log_edited_media_and_caption
			: changedMedia
			? tr::lng_admin_log_edited_media
			: removedCaption
			? tr::lng_admin_log_removed_caption
			: changedCaption
			? tr::lng_admin_log_edited_caption
			: tr::lng_admin_log_edited_message)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text, realId);

		const auto detachExistingItem = false;
		const auto body = history->createItem(
			history->nextNonHistoryEntryId(),
			PrepareLogMessage(action.vnew_message(), date),
			MessageFlag::AdminLogEntry,
			detachExistingItem);
		if (oldValue.text.isEmpty()) {
			oldValue = PrepareText(
				QString(),
				tr::lng_admin_log_empty_text(tr::now));
		}

		body->addLogEntryOriginal(
			id,
			(canHaveCaption
				? tr::lng_admin_log_previous_caption
				: tr::lng_admin_log_previous_message)(tr::now),
			oldValue);
		addPart(body, sentDate, realId);
	};

	const auto createDeleteMessage = [&](const LogDelete &action) {
		const auto realId = ExtractRealMsgId(action.vmessage());
		const auto text = tr::lng_admin_log_deleted_message(
			tr::now,
			lt_from,
			fromLinkText,
			Ui::Text::WithEntities);
		addSimpleServiceMessage(text, realId);

		const auto detachExistingItem = false;
		addPart(
			history->createItem(
				history->nextNonHistoryEntryId(),
				PrepareLogMessage(action.vmessage(), date),
				MessageFlag::AdminLogEntry,
				detachExistingItem),
			ExtractSentDate(action.vmessage()),
			realId);
	};

	const auto createParticipantJoin = [&](const LogJoin&) {
		const auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_joined
			: tr::lng_admin_log_participant_joined_channel)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createParticipantLeave = [&](const LogLeave&) {
		const auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_left
			: tr::lng_admin_log_participant_left_channel)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createParticipantInvite = [&](const LogInvite &action) {
		addSimpleTextMessage(
			GenerateParticipantChangeText(channel, action.vparticipant()));
	};

	const auto createParticipantToggleBan = [&](const LogBan &action) {
		addSimpleTextMessage(
			GenerateParticipantChangeText(
				channel,
				action.vnew_participant(),
				action.vprev_participant()));
	};

	const auto createParticipantToggleAdmin = [&](const LogPromote &action) {
		if ((action.vnew_participant().type() == mtpc_channelParticipantAdmin)
			&& (action.vprev_participant().type()
				== mtpc_channelParticipantCreator)) {
			// In case of ownership transfer we show that message in
			// the "User > Creator" part and skip the "Creator > Admin" part.
			return;
		}
		addSimpleTextMessage(
			GenerateParticipantChangeText(
				channel,
				action.vnew_participant(),
				action.vprev_participant()));
	};

	const auto createChangeStickerSet = [&](const LogSticker &action) {
		const auto set = action.vnew_stickerset();
		const auto removed = (set.type() == mtpc_inputStickerSetEmpty);
		if (removed) {
			const auto text = tr::lng_admin_log_removed_stickers_group(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		} else {
			const auto text = tr::lng_admin_log_changed_stickers_group(
				tr::now,
				lt_from,
				fromLinkText,
				lt_sticker_set,
				Ui::Text::Link(
					tr::lng_admin_log_changed_stickers_set(tr::now),
					QString()),
				Ui::Text::WithEntities);
			const auto setLink = std::make_shared<LambdaClickHandler>([=](
					ClickContext context) {
				const auto my = context.other.value<ClickHandlerContext>();
				if (const auto controller = my.sessionWindow.get()) {
					controller->show(
						Box<StickerSetBox>(
							controller->uiShow(),
							Data::FromInputSet(set),
							Data::StickersType::Stickers),
						Ui::LayerOption::CloseOther);
				}
			});
			auto message = PreparedServiceText{ text };
			message.links.push_back(fromLink);
			message.links.push_back(setLink);
			addPart(history->makeMessage({
				.id = history->nextNonHistoryEntryId(),
				.flags = MessageFlag::AdminLogEntry,
				.from = from->id,
				.date = date,
			}, std::move(message)));
		}
	};

	const auto createChangeEmojiSet = [&](const LogEmoji &action) {
		const auto set = action.vnew_stickerset();
		const auto removed = (set.type() == mtpc_inputStickerSetEmpty);
		if (removed) {
			const auto text = tr::lng_admin_log_removed_emoji_group(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		} else {
			const auto text = tr::lng_admin_log_changed_emoji_group(
				tr::now,
				lt_from,
				fromLinkText,
				lt_sticker_set,
				Ui::Text::Link(
					tr::lng_admin_log_changed_emoji_set(tr::now),
					QString()),
				Ui::Text::WithEntities);
			const auto setLink = std::make_shared<LambdaClickHandler>([=](
					ClickContext context) {
				const auto my = context.other.value<ClickHandlerContext>();
				if (const auto controller = my.sessionWindow.get()) {
					controller->show(
						Box<StickerSetBox>(
							controller->uiShow(),
							Data::FromInputSet(set),
							Data::StickersType::Emoji),
						Ui::LayerOption::CloseOther);
				}
			});
			auto message = PreparedServiceText{ text };
			message.links.push_back(fromLink);
			message.links.push_back(setLink);
			addPart(history->makeMessage({
				.id = history->nextNonHistoryEntryId(),
				.flags = MessageFlag::AdminLogEntry,
				.from = from->id,
				.date = date,
			}, std::move(message)));
		}
	};

	const auto createTogglePreHistoryHidden = [&](
			const LogPreHistory &action) {
		const auto hidden = (action.vnew_value().type() == mtpc_boolTrue);
		const auto text = (hidden
			? tr::lng_admin_log_history_made_hidden
			: tr::lng_admin_log_history_made_visible)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createDefaultBannedRights = [&](
			const LogPermissions &action) {
		addSimpleTextMessage(
			GenerateDefaultBannedRightsChangeText(
				channel,
				ChatRestrictionsInfo(action.vnew_banned_rights()),
				ChatRestrictionsInfo(action.vprev_banned_rights())));
	};

	const auto createStopPoll = [&](const LogPoll &action) {
		const auto realId = ExtractRealMsgId(action.vmessage());
		const auto text = tr::lng_admin_log_stopped_poll(
			tr::now,
			lt_from,
			fromLinkText,
			Ui::Text::WithEntities);
		addSimpleServiceMessage(text, realId);

		const auto detachExistingItem = false;
		addPart(
			history->createItem(
				history->nextNonHistoryEntryId(),
				PrepareLogMessage(action.vmessage(), date),
				MessageFlag::AdminLogEntry,
				detachExistingItem),
			ExtractSentDate(action.vmessage()),
			realId);
	};

	const auto createChangeLinkedChat = [&](const LogDiscussion &action) {
		const auto now = history->owner().channelLoaded(
			action.vnew_value().v);
		if (!now) {
			const auto text = (broadcast
				? tr::lng_admin_log_removed_linked_chat
				: tr::lng_admin_log_removed_linked_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		} else {
			const auto text = (broadcast
				? tr::lng_admin_log_changed_linked_chat
				: tr::lng_admin_log_changed_linked_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_chat,
					Ui::Text::Link(now->name(), QString()),
					Ui::Text::WithEntities);
			const auto chatLink = std::make_shared<LambdaClickHandler>([=] {
				if (const auto window = now->session().tryResolveWindow()) {
					window->showPeerHistory(now);
				}
			});
			auto message = PreparedServiceText{ text };
			message.links.push_back(fromLink);
			message.links.push_back(chatLink);
			addPart(history->makeMessage({
				.id = history->nextNonHistoryEntryId(),
				.flags = MessageFlag::AdminLogEntry,
				.from = from->id,
				.date = date,
			}, std::move(message)));
		}
	};

	const auto createChangeLocation = [&](const LogLocation &action) {
		action.vnew_value().match([&](const MTPDchannelLocation &data) {
			const auto address = qs(data.vaddress());
			const auto link = data.vgeo_point().match([&](
					const MTPDgeoPoint &data) {
				return Ui::Text::Link(
					address,
					LocationClickHandler::Url(Data::LocationPoint(data)));
			}, [&](const MTPDgeoPointEmpty &) {
				return TextWithEntities{ .text = address };
			});
			const auto text = tr::lng_admin_log_changed_location_chat(
				tr::now,
				lt_from,
				fromLinkText,
				lt_address,
				link,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		}, [&](const MTPDchannelLocationEmpty &) {
			const auto text = tr::lng_admin_log_removed_location_chat(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		});
	};

	const auto createToggleSlowMode = [&](const LogSlowMode &action) {
		if (const auto seconds = action.vnew_value().v) {
			const auto duration = (seconds >= 60)
				? tr::lng_minutes(tr::now, lt_count, seconds / 60)
				: tr::lng_seconds(tr::now, lt_count, seconds);
			const auto text = tr::lng_admin_log_changed_slow_mode(
				tr::now,
				lt_from,
				fromLinkText,
				lt_duration,
				{ .text = duration },
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		} else {
			const auto text = tr::lng_admin_log_removed_slow_mode(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		}
	};

	const auto createStartGroupCall = [&](const LogStartCall &data) {
		const auto text = (broadcast
			? tr::lng_admin_log_started_group_call_channel
			: tr::lng_admin_log_started_group_call)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createDiscardGroupCall = [&](const LogDiscardCall &data) {
		const auto text = (broadcast
			? tr::lng_admin_log_discarded_group_call_channel
			: tr::lng_admin_log_discarded_group_call)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto groupCallParticipantPeer = [&](
			const MTPGroupCallParticipant &data) {
		return data.match([&](const MTPDgroupCallParticipant &data) {
			return history->owner().peer(peerFromMTP(data.vpeer()));
		});
	};

	const auto addServiceMessageWithLink = [&](
			const TextWithEntities &text,
			const ClickHandlerPtr &link) {
		auto message = PreparedServiceText{ text };
		message.links.push_back(fromLink);
		message.links.push_back(link);
		addPart(history->makeMessage({
			.id = history->nextNonHistoryEntryId(),
			.flags = MessageFlag::AdminLogEntry,
			.from = from->id,
			.date = date,
		}, std::move(message)));
	};

	const auto createParticipantMute = [&](const LogMute &data) {
		const auto participantPeer = groupCallParticipantPeer(
			data.vparticipant());
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = Ui::Text::Link(
			participantPeer->name(),
			QString());
		const auto text = (broadcast
			? tr::lng_admin_log_muted_participant_channel
			: tr::lng_admin_log_muted_participant)(
			tr::now,
			lt_from,
			fromLinkText,
			lt_user,
			participantPeerLinkText,
			Ui::Text::WithEntities);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	const auto createParticipantUnmute = [&](const LogUnmute &data) {
		const auto participantPeer = groupCallParticipantPeer(
			data.vparticipant());
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = Ui::Text::Link(
			participantPeer->name(),
			QString());
		const auto text = (broadcast
			? tr::lng_admin_log_unmuted_participant_channel
			: tr::lng_admin_log_unmuted_participant)(
			tr::now,
			lt_from,
			fromLinkText,
			lt_user,
			participantPeerLinkText,
			Ui::Text::WithEntities);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	const auto createToggleGroupCallSetting = [&](
			const LogCallSetting &data) {
		const auto text = (mtpIsTrue(data.vjoin_muted())
			? (broadcast
				? tr::lng_admin_log_disallowed_unmute_self_channel
				: tr::lng_admin_log_disallowed_unmute_self)
			: (broadcast
				? tr::lng_admin_log_allowed_unmute_self_channel
				: tr::lng_admin_log_allowed_unmute_self))(
			tr::now,
			lt_from,
			fromLinkText,
			Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto addInviteLinkServiceMessage = [&](
			const TextWithEntities &text,
			const MTPExportedChatInvite &data,
			ClickHandlerPtr additional = nullptr) {
		auto message = PreparedServiceText{ text };
		message.links.push_back(fromLink);
		if (!ExtractInviteLink(data).endsWith(Ui::kQEllipsis)) {
			message.links.push_back(std::make_shared<UrlClickHandler>(
				InternalInviteLinkUrl(data)));
		}
		if (additional) {
			message.links.push_back(std::move(additional));
		}
		addPart(history->makeMessage({
			.id = history->nextNonHistoryEntryId(),
			.flags = MessageFlag::AdminLogEntry,
			.from = from->id,
			.date = date,
		}, std::move(message)));
	};

	const auto createParticipantJoinByInvite = [&](
			const LogJoinByInvite &data) {
		const auto text = data.is_via_chatlist()
			? (channel->isMegagroup()
				? tr::lng_admin_log_participant_joined_by_filter_link
				: tr::lng_admin_log_participant_joined_by_filter_link_channel)
			: (channel->isMegagroup()
				? tr::lng_admin_log_participant_joined_by_link
				: tr::lng_admin_log_participant_joined_by_link_channel);
		addInviteLinkServiceMessage(
			text(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite()),
				Ui::Text::WithEntities),
			data.vinvite());
	};

	const auto createExportedInviteDelete = [&](const LogInviteDelete &data) {
		addInviteLinkServiceMessage(
			tr::lng_admin_log_delete_invite_link(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite()),
				Ui::Text::WithEntities),
			data.vinvite());
	};

	const auto createExportedInviteRevoke = [&](const LogInviteRevoke &data) {
		addInviteLinkServiceMessage(
			tr::lng_admin_log_revoke_invite_link(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite()),
				Ui::Text::WithEntities),
			data.vinvite());
	};

	const auto createExportedInviteEdit = [&](const LogInviteEdit &data) {
		addSimpleTextMessage(
			GenerateInviteLinkChangeText(
				data.vnew_invite(),
				data.vprev_invite()));
	};

	const auto createParticipantVolume = [&](const LogVolume &data) {
		const auto participantPeer = groupCallParticipantPeer(
			data.vparticipant());
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = Ui::Text::Link(
			participantPeer->name(),
			QString());
		const auto volume = data.vparticipant().match([&](
				const MTPDgroupCallParticipant &data) {
			return data.vvolume().value_or(10000);
		});
		const auto volumeText = QString::number(volume / 100) + '%';
		auto text = (broadcast
			? tr::lng_admin_log_participant_volume_channel
			: tr::lng_admin_log_participant_volume)(
				tr::now,
				lt_from,
				fromLinkText,
				lt_user,
				participantPeerLinkText,
				lt_percent,
				{ .text = volumeText },
				Ui::Text::WithEntities);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	const auto createChangeHistoryTTL = [&](const LogTTL &data) {
		const auto was = data.vprev_value().v;
		const auto now = data.vnew_value().v;
		const auto wrap = [](int duration) -> TextWithEntities {
			const auto text = (duration == 5)
				? u"5 seconds"_q
				: Ui::FormatTTL(duration);
			return { .text = text };
		};
		const auto text = !was
			? tr::lng_admin_log_messages_ttl_set(
				tr::now,
				lt_from,
				fromLinkText,
				lt_duration,
				wrap(now),
				Ui::Text::WithEntities)
			: !now
			? tr::lng_admin_log_messages_ttl_removed(
				tr::now,
				lt_from,
				fromLinkText,
				lt_duration,
				wrap(was),
				Ui::Text::WithEntities)
			: tr::lng_admin_log_messages_ttl_changed(
				tr::now,
				lt_from,
				fromLinkText,
				lt_previous,
				wrap(was),
				lt_duration,
				wrap(now),
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createParticipantJoinByRequest = [&](
			const LogJoinByRequest &data) {
		const auto user = channel->owner().user(UserId(data.vapproved_by()));
		const auto linkText = GenerateInviteLinkLink(data.vinvite());
		const auto text = (linkText.text == PublicJoinLink())
			? (channel->isMegagroup()
				? tr::lng_admin_log_participant_approved_by_request
				: tr::lng_admin_log_participant_approved_by_request_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_user,
					Ui::Text::Link(user->name(), QString()),
					Ui::Text::WithEntities)
			: (channel->isMegagroup()
				? tr::lng_admin_log_participant_approved_by_link
				: tr::lng_admin_log_participant_approved_by_link_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_link,
					linkText,
					lt_user,
					Ui::Text::Link(user->name(), QString()),
					Ui::Text::WithEntities);
		addInviteLinkServiceMessage(
			text,
			data.vinvite(),
			user->createOpenLink());
	};

	const auto createToggleNoForwards = [&](const LogNoForwards &data) {
		const auto disabled = (data.vnew_value().type() == mtpc_boolTrue);
		const auto text = (disabled
			? tr::lng_admin_log_forwards_disabled
			: tr::lng_admin_log_forwards_enabled)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createSendMessage = [&](const LogSendMessage &data) {
		const auto realId = ExtractRealMsgId(data.vmessage());
		const auto text = tr::lng_admin_log_sent_message(
			tr::now,
			lt_from,
			fromLinkText,
			Ui::Text::WithEntities);
		addSimpleServiceMessage(text, realId);

		const auto detachExistingItem = false;
		addPart(
			history->createItem(
				history->nextNonHistoryEntryId(),
				PrepareLogMessage(data.vmessage(), date),
				MessageFlag::AdminLogEntry,
				detachExistingItem),
			ExtractSentDate(data.vmessage()),
			realId);
	};

	const auto createChangeAvailableReactions = [&](
			const LogChangeAvailableReactions &data) {
		const auto text = data.vnew_value().match([&](
				const MTPDchatReactionsNone&) {
			return tr::lng_admin_log_reactions_disabled(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		}, [&](const MTPDchatReactionsSome &data) {
			using namespace Window::Notifications;
			auto list = TextWithEntities();
			for (const auto &one : data.vreactions().v) {
				if (!list.empty()) {
					list.append(", ");
				}
				list.append(Manager::ComposeReactionEmoji(
					session,
					Data::ReactionFromMTP(one)));
			}
			return tr::lng_admin_log_reactions_updated(
				tr::now,
				lt_from,
				fromLinkText,
				lt_emoji,
				list,
				Ui::Text::WithEntities);
		}, [&](const MTPDchatReactionsAll &data) {
			return (data.is_allow_custom()
				? tr::lng_admin_log_reactions_allowed_all
				: tr::lng_admin_log_reactions_allowed_official)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
		});
		addSimpleServiceMessage(text);
	};

	const auto createChangeUsernames = [&](const LogChangeUsernames &data) {
		const auto newValue = data.vnew_value().v;
		const auto oldValue = data.vprev_value().v;

		const auto list = [&](const auto &tlList) {
			auto result = TextWithEntities();
			for (const auto &tlValue : tlList) {
				result.append(PrepareText(
					history->session().createInternalLinkFull(qs(tlValue)),
					QString()));
				result.append('\n');
			}
			return result;
		};

		if (newValue.size() == oldValue.size()) {
			if (newValue.size() == 1) {
				const auto tl = MTP_channelAdminLogEventActionChangeUsername(
					newValue.front(),
					oldValue.front());
				tl.match([&](const LogUsername &data) {
					createChangeUsername(data);
				}, [](const auto &) {
				});
				return;
			} else {
				const auto wasReordered = [&] {
					for (const auto &newLink : newValue) {
						if (!ranges::contains(oldValue, newLink)) {
							return false;
						}
					}
					return true;
				}();
				if (wasReordered) {
					addSimpleServiceMessage((channel->isMegagroup()
						? tr::lng_admin_log_reordered_link_group
						: tr::lng_admin_log_reordered_link_channel)(
							tr::now,
							lt_from,
							fromLinkText,
							Ui::Text::WithEntities));
					const auto body = makeSimpleTextMessage(list(newValue));
					body->addLogEntryOriginal(
						id,
						tr::lng_admin_log_previous_links_order(tr::now),
						list(oldValue));
					addPart(body);
					return;
				}
			}
		} else if (std::abs(newValue.size() - oldValue.size()) == 1) {
			const auto activated = newValue.size() > oldValue.size();
			const auto changed = [&] {
				const auto value = activated ? oldValue : newValue;
				for (const auto &link : (activated ? newValue : oldValue)) {
					if (!ranges::contains(value, link)) {
						return qs(link);
					}
				}
				return QString();
			}();
			addSimpleServiceMessage((activated
				? tr::lng_admin_log_activated_link
				: tr::lng_admin_log_deactivated_link)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_link,
					{ changed },
					Ui::Text::WithEntities));
			return;
		}
		// Probably will never happen.
		auto resultText = fromLinkText;
		addSimpleServiceMessage(resultText.append({
			.text = channel->isMegagroup()
				? u" changed list of group links:"_q
				: u" changed list of channel links:"_q,
		}));
		const auto body = makeSimpleTextMessage(list(newValue));
		body->addLogEntryOriginal(
			id,
			"Previous links",
			list(oldValue));
		addPart(body);
	};

	const auto createToggleForum = [&](const LogToggleForum &data) {
		const auto enabled = (data.vnew_value().type() == mtpc_boolTrue);
		const auto text = (enabled
			? tr::lng_admin_log_topics_enabled
			: tr::lng_admin_log_topics_disabled)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createCreateTopic = [&](const LogCreateTopic &data) {
		auto topicLink = GenerateTopicLink(channel, data.vtopic());
		addSimpleServiceMessage(tr::lng_admin_log_topics_created(
			tr::now,
			lt_from,
			fromLinkText,
			lt_topic,
			topicLink,
			Ui::Text::WithEntities));
	};

	const auto createEditTopic = [&](const LogEditTopic &data) {
		const auto prevLink = GenerateTopicLink(channel, data.vprev_topic());
		const auto nowLink = GenerateTopicLink(channel, data.vnew_topic());
		if (prevLink != nowLink) {
			addSimpleServiceMessage(tr::lng_admin_log_topics_changed(
				tr::now,
				lt_from,
				fromLinkText,
				lt_topic,
				prevLink,
				lt_new_topic,
				nowLink,
				Ui::Text::WithEntities));
		}
		const auto wasClosed = IsTopicClosed(data.vprev_topic());
		const auto nowClosed = IsTopicClosed(data.vnew_topic());
		if (nowClosed != wasClosed) {
			addSimpleServiceMessage((nowClosed
				? tr::lng_admin_log_topics_closed
				: tr::lng_admin_log_topics_reopened)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_topic,
					nowLink,
					Ui::Text::WithEntities));
		}
		const auto wasHidden = IsTopicHidden(data.vprev_topic());
		const auto nowHidden = IsTopicHidden(data.vnew_topic());
		if (nowHidden != wasHidden) {
			addSimpleServiceMessage((nowHidden
				? tr::lng_admin_log_topics_hidden
				: tr::lng_admin_log_topics_unhidden)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_topic,
					nowLink,
					Ui::Text::WithEntities));
		}
	};

	const auto createDeleteTopic = [&](const LogDeleteTopic &data) {
		auto topicLink = GenerateTopicLink(channel, data.vtopic());
		if (!topicLink.entities.empty()) {
			topicLink.entities.erase(topicLink.entities.begin());
		}
		addSimpleServiceMessage(tr::lng_admin_log_topics_deleted(
			tr::now,
			lt_from,
			fromLinkText,
			lt_topic,
			topicLink,
			Ui::Text::WithEntities));
	};

	const auto createPinTopic = [&](const LogPinTopic &data) {
		if (const auto &topic = data.vnew_topic()) {
			auto topicLink = GenerateTopicLink(channel, *topic);
			addSimpleServiceMessage(tr::lng_admin_log_topics_pinned(
				tr::now,
				lt_from,
				fromLinkText,
				lt_topic,
				topicLink,
				Ui::Text::WithEntities));
		} else if (const auto &previous = data.vprev_topic()) {
			auto topicLink = GenerateTopicLink(channel, *previous);
			addSimpleServiceMessage(tr::lng_admin_log_topics_unpinned(
				tr::now,
				lt_from,
				fromLinkText,
				lt_topic,
				topicLink,
				Ui::Text::WithEntities));
		}
	};

	const auto createToggleAntiSpam = [&](const LogToggleAntiSpam &data) {
		const auto enabled = (data.vnew_value().type() == mtpc_boolTrue);
		const auto text = (enabled
			? tr::lng_admin_log_antispam_enabled
			: tr::lng_admin_log_antispam_disabled)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createColorChange = [&](
			const MTPPeerColor &was,
			const MTPPeerColor &now,
			const auto &colorPhrase,
			const auto &setEmoji,
			const auto &removeEmoji,
			const auto &changeEmoji) {
		const auto prevColor = was.data().vcolor();
		const auto nextColor = now.data().vcolor();
		if (prevColor != nextColor) {
			const auto wrap = [&](tl::conditional<MTPint> value) {
				return value
					? value->v
					: Data::DecideColorIndex(history->peer->id);
			};
			const auto text = colorPhrase(
				tr::now,
				lt_from,
				fromLinkText,
				lt_previous,
				{ '#' + QString::number(wrap(prevColor) + 1) },
				lt_color,
				{ '#' + QString::number(wrap(nextColor) + 1) },
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		}
		const auto prevEmoji = was.data().vbackground_emoji_id().value_or_empty();
		const auto nextEmoji = now.data().vbackground_emoji_id().value_or_empty();
		if (prevEmoji != nextEmoji) {
			const auto text = !prevEmoji
				? setEmoji(
					tr::now,
					lt_from,
					fromLinkText,
					lt_emoji,
					Ui::Text::SingleCustomEmoji(
						Data::SerializeCustomEmojiId(nextEmoji)),
					Ui::Text::WithEntities)
				: !nextEmoji
				? removeEmoji(
					tr::now,
					lt_from,
					fromLinkText,
					lt_emoji,
					Ui::Text::SingleCustomEmoji(
						Data::SerializeCustomEmojiId(prevEmoji)),
					Ui::Text::WithEntities)
				: changeEmoji(
					tr::now,
					lt_from,
					fromLinkText,
					lt_previous,
					Ui::Text::SingleCustomEmoji(
						Data::SerializeCustomEmojiId(prevEmoji)),
					lt_emoji,
					Ui::Text::SingleCustomEmoji(
						Data::SerializeCustomEmojiId(nextEmoji)),
					Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		}
	};

	const auto createChangePeerColor = [&](const LogChangePeerColor &data) {
		createColorChange(
			data.vprev_value(),
			data.vnew_value(),
			tr::lng_admin_log_change_color,
			tr::lng_admin_log_set_background_emoji,
			tr::lng_admin_log_removed_background_emoji,
			tr::lng_admin_log_change_background_emoji);
	};

	const auto createChangeProfilePeerColor = [&](const LogChangeProfilePeerColor &data) {
		createColorChange(
			data.vprev_value(),
			data.vnew_value(),
			tr::lng_admin_log_change_profile_color,
			tr::lng_admin_log_set_profile_background_emoji,
			tr::lng_admin_log_removed_profile_background_emoji,
			tr::lng_admin_log_change_profile_background_emoji);
	};

	const auto createChangeWallpaper = [&](const LogChangeWallpaper &data) {
		addSimpleServiceMessage(tr::lng_admin_log_change_wallpaper(
			tr::now,
			lt_from,
			fromLinkText,
			Ui::Text::WithEntities));
	};

	const auto createChangeEmojiStatus = [&](const LogChangeEmojiStatus &data) {
		const auto parse = [](const MTPEmojiStatus &status) {
			return status.match([](
					const MTPDemojiStatus &data) {
				return data.vdocument_id().v;
			}, [](const MTPDemojiStatusEmpty &) {
				return DocumentId();
			}, [](const MTPDemojiStatusUntil &data) {
				return data.vdocument_id().v;
			});
		};
		const auto prevEmoji = parse(data.vprev_value());
		const auto nextEmoji = parse(data.vnew_value());
		const auto nextUntil = data.vnew_value().match([](
				const MTPDemojiStatusUntil &data) {
			return data.vuntil().v;
		}, [](const auto &) { return TimeId(); });

		const auto text = !prevEmoji
			? (nextUntil
				? tr::lng_admin_log_set_status_until(
					tr::now,
					lt_from,
					fromLinkText,
					lt_emoji,
					Ui::Text::SingleCustomEmoji(
						Data::SerializeCustomEmojiId(nextEmoji)),
					lt_date,
					TextWithEntities{
						langDateTime(base::unixtime::parse(nextUntil)) },
					Ui::Text::WithEntities)
				: tr::lng_admin_log_set_status(
					tr::now,
					lt_from,
					fromLinkText,
					lt_emoji,
					Ui::Text::SingleCustomEmoji(
						Data::SerializeCustomEmojiId(nextEmoji)),
					Ui::Text::WithEntities))
			: !nextEmoji
			? tr::lng_admin_log_removed_status(
				tr::now,
				lt_from,
				fromLinkText,
				lt_emoji,
				Ui::Text::SingleCustomEmoji(
					Data::SerializeCustomEmojiId(prevEmoji)),
				Ui::Text::WithEntities)
			: (nextUntil
				? tr::lng_admin_log_change_status_until(
					tr::now,
					lt_from,
					fromLinkText,
					lt_previous,
					Ui::Text::SingleCustomEmoji(
						Data::SerializeCustomEmojiId(prevEmoji)),
					lt_emoji,
					Ui::Text::SingleCustomEmoji(
						Data::SerializeCustomEmojiId(nextEmoji)),
					lt_date,
					TextWithEntities{
						langDateTime(base::unixtime::parse(nextUntil)) },
					Ui::Text::WithEntities)
				: tr::lng_admin_log_change_status(
					tr::now,
					lt_from,
					fromLinkText,
					lt_previous,
					Ui::Text::SingleCustomEmoji(
						Data::SerializeCustomEmojiId(prevEmoji)),
					lt_emoji,
					Ui::Text::SingleCustomEmoji(
						Data::SerializeCustomEmojiId(nextEmoji)),
					Ui::Text::WithEntities));
		addSimpleServiceMessage(text);
	};

	action.match(
		createChangeTitle,
		createChangeAbout,
		createChangeUsername,
		createChangePhoto,
		createToggleInvites,
		createToggleSignatures,
		createUpdatePinned,
		createEditMessage,
		createDeleteMessage,
		createParticipantJoin,
		createParticipantLeave,
		createParticipantInvite,
		createParticipantToggleBan,
		createParticipantToggleAdmin,
		createChangeStickerSet,
		createChangeEmojiSet,
		createTogglePreHistoryHidden,
		createDefaultBannedRights,
		createStopPoll,
		createChangeLinkedChat,
		createChangeLocation,
		createToggleSlowMode,
		createStartGroupCall,
		createDiscardGroupCall,
		createParticipantMute,
		createParticipantUnmute,
		createToggleGroupCallSetting,
		createParticipantJoinByInvite,
		createExportedInviteDelete,
		createExportedInviteRevoke,
		createExportedInviteEdit,
		createParticipantVolume,
		createChangeHistoryTTL,
		createParticipantJoinByRequest,
		createToggleNoForwards,
		createSendMessage,
		createChangeAvailableReactions,
		createChangeUsernames,
		createToggleForum,
		createCreateTopic,
		createEditTopic,
		createDeleteTopic,
		createPinTopic,
		createToggleAntiSpam,
		createChangePeerColor,
		createChangeProfilePeerColor,
		createChangeWallpaper,
		createChangeEmojiStatus);
}

} // namespace AdminLog
