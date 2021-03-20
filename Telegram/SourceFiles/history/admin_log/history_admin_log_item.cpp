/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/admin_log/history_admin_log_item.h"

#include "history/admin_log/history_admin_log_inner.h"
#include "history/view/history_view_element.h"
#include "history/history_location_manager.h"
#include "history/history_service.h"
#include "history/history_message.h"
#include "history/history.h"
#include "api/api_text_entities.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/basic_click_handlers.h"
#include "boxes/sticker_set_box.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "mainwindow.h" // App::wnd()->sessionController
#include "main/main_session.h"
#include "facades.h"

namespace AdminLog {
namespace {

TextWithEntities PrepareText(const QString &value, const QString &emptyValue) {
	auto result = TextWithEntities { TextUtilities::Clean(value) };
	if (result.text.isEmpty()) {
		result.text = emptyValue;
		if (!emptyValue.isEmpty()) {
			result.entities.push_back({
				EntityType::Italic,
				0,
				emptyValue.size() });
		}
	} else {
		TextUtilities::ParseEntities(result, TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands);
	}
	return result;
}

TimeId ExtractSentDate(const MTPMessage &message) {
	return message.match([&](const MTPDmessageEmpty &) {
		return 0;
	}, [&](const MTPDmessageService &data) {
		return data.vdate().v;
	}, [&](const MTPDmessage &data) {
		return data.vdate().v;
	});
}

MTPMessage PrepareLogMessage(
		const MTPMessage &message,
		MsgId newId,
		TimeId newDate) {
	return message.match([&](const MTPDmessageEmpty &data) {
		return MTP_messageEmpty(
			data.vflags(),
			MTP_int(newId),
			data.vpeer_id() ? *data.vpeer_id() : MTPPeer());
	}, [&](const MTPDmessageService &data) {
		const auto removeFlags = MTPDmessageService::Flag::f_out
			| MTPDmessageService::Flag::f_post
			| MTPDmessageService::Flag::f_reply_to
			| MTPDmessageService::Flag::f_ttl_period;
		return MTP_messageService(
			MTP_flags(data.vflags().v & ~removeFlags),
			MTP_int(newId),
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
			MTP_int(newId),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			data.vpeer_id(),
			data.vfwd_from() ? *data.vfwd_from() : MTPMessageFwdHeader(),
			MTP_int(data.vvia_bot_id().value_or_empty()),
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
			//MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>(),
			MTPint()); // ttl_period
	});
}

bool MediaCanHaveCaption(const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return false;
	}
	const auto &data = message.c_message();
	const auto media = data.vmedia();
	const auto mediaType = media ? media->type() : mtpc_messageMediaEmpty;
	return (mediaType == mtpc_messageMediaDocument || mediaType == mtpc_messageMediaPhoto);
}

TextWithEntities ExtractEditedText(
		not_null<Main::Session*> session,
		const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return TextWithEntities();
	}
	const auto &data = message.c_message();
	return {
		TextUtilities::Clean(qs(data.vmessage())),
		Api::EntitiesFromMTP(session, data.ventities().value_or_empty())
	};
}

const auto CollectChanges = [](auto &phraseMap, auto plusFlags, auto minusFlags) {
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
	return withPrefix(plusFlags & ~minusFlags, '+') + withPrefix(minusFlags & ~plusFlags, kMinus);
};

TextWithEntities GenerateAdminChangeText(
		not_null<ChannelData*> channel,
		const TextWithEntities &user,
		const MTPChatAdminRights *newRights,
		const MTPChatAdminRights *prevRights) {
	Expects(!newRights || newRights->type() == mtpc_chatAdminRights);
	Expects(!prevRights || prevRights->type() == mtpc_chatAdminRights);

	using Flag = MTPDchatAdminRights::Flag;
	using Flags = MTPDchatAdminRights::Flags;

	auto newFlags = newRights ? newRights->c_chatAdminRights().vflags().v : MTPDchatAdminRights::Flags(0);
	auto prevFlags = prevRights ? prevRights->c_chatAdminRights().vflags().v : MTPDchatAdminRights::Flags(0);
	auto result = tr::lng_admin_log_promoted(tr::now, lt_user, user, Ui::Text::WithEntities);

	auto useInviteLinkPhrase = channel->isMegagroup() && channel->anyoneCanAddMembers();
	auto invitePhrase = useInviteLinkPhrase
		? tr::lng_admin_log_admin_invite_link
		: tr::lng_admin_log_admin_invite_users;
	static auto phraseMap = std::map<Flags, tr::phrase<>> {
		{ Flag::f_change_info, tr::lng_admin_log_admin_change_info },
		{ Flag::f_post_messages, tr::lng_admin_log_admin_post_messages },
		{ Flag::f_edit_messages, tr::lng_admin_log_admin_edit_messages },
		{ Flag::f_delete_messages, tr::lng_admin_log_admin_delete_messages },
		{ Flag::f_ban_users, tr::lng_admin_log_admin_ban_users },
		{ Flag::f_invite_users, invitePhrase },
		{ Flag::f_pin_messages, tr::lng_admin_log_admin_pin_messages },
		{ Flag::f_manage_call, tr::lng_admin_log_admin_manage_calls },
		{ Flag::f_add_admins, tr::lng_admin_log_admin_add_admins },
	};
	phraseMap[Flag::f_invite_users] = invitePhrase;

	if (!channel->isMegagroup()) {
		// Don't display "Ban users" changes in channels.
		newFlags &= ~Flag::f_ban_users;
		prevFlags &= ~Flag::f_ban_users;
	}

	auto changes = CollectChanges(phraseMap, newFlags, prevFlags);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}

	return result;
};

QString GenerateBannedChangeText(
		const MTPChatBannedRights *newRights,
		const MTPChatBannedRights *prevRights) {
	using Flag = MTPDchatBannedRights::Flag;
	using Flags = MTPDchatBannedRights::Flags;

	auto newFlags = newRights ? Data::ChatBannedRightsFlags(*newRights) : Flags(0);
	auto prevFlags = prevRights ? Data::ChatBannedRightsFlags(*prevRights) : Flags(0);
	static auto phraseMap = std::map<Flags, tr::phrase<>>{
		{ Flag::f_view_messages, tr::lng_admin_log_banned_view_messages },
		{ Flag::f_send_messages, tr::lng_admin_log_banned_send_messages },
		{ Flag::f_send_media, tr::lng_admin_log_banned_send_media },
		{ Flag::f_send_stickers
			| Flag::f_send_gifs
			| Flag::f_send_inline
			| Flag::f_send_games, tr::lng_admin_log_banned_send_stickers },
		{ Flag::f_embed_links, tr::lng_admin_log_banned_embed_links },
		{ Flag::f_send_polls, tr::lng_admin_log_banned_send_polls },
		{ Flag::f_change_info, tr::lng_admin_log_admin_change_info },
		{ Flag::f_invite_users, tr::lng_admin_log_admin_invite_users },
		{ Flag::f_pin_messages, tr::lng_admin_log_admin_pin_messages },
	};
	return CollectChanges(phraseMap, prevFlags, newFlags);
}

TextWithEntities GenerateBannedChangeText(
		PeerId participantId,
		const TextWithEntities &user,
		const MTPChatBannedRights *newRights,
		const MTPChatBannedRights *prevRights) {
	using Flag = MTPDchatBannedRights::Flag;
	using Flags = MTPDchatBannedRights::Flags;

	auto newFlags = newRights ? Data::ChatBannedRightsFlags(*newRights) : Flags(0);
	auto newUntil = newRights ? Data::ChatBannedRightsUntilDate(*newRights) : TimeId(0);
	auto prevFlags = prevRights ? Data::ChatBannedRightsFlags(*prevRights) : Flags(0);
	auto indefinitely = ChannelData::IsRestrictedForever(newUntil);
	if (newFlags & Flag::f_view_messages) {
		return tr::lng_admin_log_banned(tr::now, lt_user, user, Ui::Text::WithEntities);
	} else if (newFlags == 0 && (prevFlags & Flag::f_view_messages) && !peerIsUser(participantId)) {
		return tr::lng_admin_log_unbanned(tr::now, lt_user, user, Ui::Text::WithEntities);
	}
	auto untilText = indefinitely
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
	const auto changes = GenerateBannedChangeText(newRights, prevRights);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}
	return result;
}

QString ExtractInviteLink(const MTPExportedChatInvite &data) {
	return data.match([&](const MTPDchatInviteExported &data) {
		return qs(data.vlink());
	});
}

QString InternalInviteLinkUrl(const MTPExportedChatInvite &data) {
	const auto base64 = ExtractInviteLink(data).toUtf8().toBase64();
	return "internal:show_invite_link/?link=" + QString::fromLatin1(base64);
}

QString GenerateInviteLinkText(const MTPExportedChatInvite &data) {
	return ExtractInviteLink(data).replace(
		qstr("https://"),
		QString()
	).replace(
		qstr("t.me/+"),
		QString()
	).replace(
		qstr("t.me/joinchat/"),
		QString()
	);
}

QString GenerateInviteLinkLink(const MTPExportedChatInvite &data) {
	const auto text = GenerateInviteLinkText(data);
	return text.endsWith("...")
		? text
		: textcmdLink(InternalInviteLinkUrl(data), text);
}

TextWithEntities GenerateInviteLinkChangeText(
		const MTPExportedChatInvite &newLink,
		const MTPExportedChatInvite &prevLink) {
	auto link = TextWithEntities{ GenerateInviteLinkText(newLink) };
	if (!link.text.endsWith("...")) {
		link.entities.push_back({
			EntityType::CustomUrl,
			0,
			link.text.size(),
			InternalInviteLinkUrl(newLink) });
	}
	auto result = tr::lng_admin_log_edited_invite_link(tr::now, lt_link, link, Ui::Text::WithEntities);
	result.text.append('\n');

	const auto expireDate = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.vexpire_date().value_or_empty();
		});
	};
	const auto usageLimit = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.vusage_limit().value_or_empty();
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
	const auto wasExpireDate = expireDate(prevLink);
	const auto nowExpireDate = expireDate(newLink);
	const auto wasUsageLimit = usageLimit(prevLink);
	const auto nowUsageLimit = usageLimit(newLink);
	if (wasExpireDate != nowExpireDate) {
		result.text.append('\n').append(tr::lng_admin_log_invite_link_expire_date(tr::now, lt_previous, wrapDate(wasExpireDate), lt_limit, wrapDate(nowExpireDate)));
	}
	if (wasUsageLimit != nowUsageLimit) {
		result.text.append('\n').append(tr::lng_admin_log_invite_link_usage_limit(tr::now, lt_previous, wrapUsage(wasUsageLimit), lt_limit, wrapUsage(nowUsageLimit)));
	}

	result.entities.push_front(EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
};

auto GenerateParticipantString(
		not_null<Main::Session*> session,
		PeerId participantId) {
	// User name in "User name (@username)" format with entities.
	auto peer = session->data().peer(participantId);
	auto name = TextWithEntities { peer->name };
	if (const auto user = peer->asUser()) {
		auto entityData = QString::number(user->id)
			+ '.'
			+ QString::number(user->accessHash());
		name.entities.push_back({
			EntityType::MentionName,
			0,
			name.text.size(),
			entityData });
	}
	auto username = peer->userName();
	if (username.isEmpty()) {
		return name;
	}
	auto mention = TextWithEntities { '@' + username };
	mention.entities.push_back({
		EntityType::Mention,
		0,
		mention.text.size() });
	return tr::lng_admin_log_user_with_username(
		tr::now,
		lt_name,
		name,
		lt_mention,
		mention,
		Ui::Text::WithEntities);
}

auto GenerateParticipantChangeTextInner(
		not_null<ChannelData*> channel,
		const MTPChannelParticipant &participant,
		const MTPChannelParticipant *oldParticipant) {
	const auto oldType = oldParticipant ? oldParticipant->type() : 0;
	const auto generateOther = [&](PeerId participantId) {
		auto user = GenerateParticipantString(
			&channel->session(),
			participantId);
		if (oldType == mtpc_channelParticipantAdmin) {
			return GenerateAdminChangeText(
				channel,
				user,
				nullptr,
				&oldParticipant->c_channelParticipantAdmin().vadmin_rights());
		} else if (oldType == mtpc_channelParticipantBanned) {
			return GenerateBannedChangeText(
				participantId,
				user,
				nullptr,
				&oldParticipant->c_channelParticipantBanned().vbanned_rights());
		}
		return tr::lng_admin_log_invited(tr::now, lt_user, user, Ui::Text::WithEntities);
	};
	return participant.match([&](const MTPDchannelParticipantCreator &data) {
		// No valid string here :(
		return tr::lng_admin_log_transferred(
			tr::now,
			lt_user,
			GenerateParticipantString(
				&channel->session(),
				peerFromUser(data.vuser_id())),
			Ui::Text::WithEntities);
	}, [&](const MTPDchannelParticipantAdmin &data) {
		const auto user = GenerateParticipantString(
			&channel->session(),
			peerFromUser(data.vuser_id()));
		return GenerateAdminChangeText(
			channel,
			user,
			&data.vadmin_rights(),
			(oldType == mtpc_channelParticipantAdmin
				? &oldParticipant->c_channelParticipantAdmin().vadmin_rights()
				: nullptr));
	}, [&](const MTPDchannelParticipantBanned &data) {
		const auto participantId = peerFromMTP(data.vpeer());
		const auto user = GenerateParticipantString(
			&channel->session(),
			participantId);
		return GenerateBannedChangeText(
			participantId,
			user,
			&data.vbanned_rights(),
			(oldType == mtpc_channelParticipantBanned
				? &oldParticipant->c_channelParticipantBanned().vbanned_rights()
				: nullptr));
	}, [&](const MTPDchannelParticipantLeft &data) {
		return generateOther(peerFromMTP(data.vpeer()));
	}, [&](const auto &data) {
		return generateOther(peerFromUser(data.vuser_id()));
	});
}

TextWithEntities GenerateParticipantChangeText(not_null<ChannelData*> channel, const MTPChannelParticipant &participant, const MTPChannelParticipant *oldParticipant = nullptr) {
	auto result = GenerateParticipantChangeTextInner(channel, participant, oldParticipant);
	result.entities.push_front(EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
}

TextWithEntities GenerateDefaultBannedRightsChangeText(not_null<ChannelData*> channel, const MTPChatBannedRights &rights, const MTPChatBannedRights &oldRights) {
	auto result = TextWithEntities{ tr::lng_admin_log_changed_default_permissions(tr::now) };
	const auto changes = GenerateBannedChangeText(&rights, &oldRights);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}
	result.entities.push_front(EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
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
	_view = nullptr;
	if (_data) {
		_data->destroy();
	}
}

void OwnedItem::refreshView(
		not_null<HistoryView::ElementDelegate*> delegate) {
	_view = _data->createView(delegate);
}

void GenerateItems(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const MTPDchannelAdminLogEvent &event,
		Fn<void(OwnedItem item, TimeId sentDate)> callback) {
	Expects(history->peer->isChannel());

	const auto session = &history->session();
	const auto id = event.vid().v;
	const auto from = history->owner().user(event.vuser_id().v);
	const auto channel = history->peer->asChannel();
	const auto &action = event.vaction();
	const auto date = event.vdate().v;
	const auto addPart = [&](
			not_null<HistoryItem*> item,
			TimeId sentDate = 0) {
		return callback(OwnedItem(delegate, item), sentDate);
	};

	using Flag = MTPDmessage::Flag;
	const auto fromName = from->name;
	const auto fromLink = from->createOpenLink();
	const auto fromLinkText = textcmdLink(1, fromName);

	auto addSimpleServiceMessage = [&](const QString &text, PhotoData *photo = nullptr) {
		auto message = HistoryService::PreparedText { text };
		message.links.push_back(fromLink);
		addPart(history->makeServiceMessage(
			history->nextNonHistoryEntryId(),
			MTPDmessage_ClientFlag::f_admin_log_entry,
			date,
			message,
			MTPDmessage::Flags(0),
			peerToUser(from->id),
			photo));
	};

	auto createChangeTitle = [&](const MTPDchannelAdminLogEventActionChangeTitle &action) {
		auto text = (channel->isMegagroup()
			? tr::lng_action_changed_title
			: tr::lng_admin_log_changed_title_channel)(
				tr::now,
				lt_from,
				fromLinkText,
				lt_title,
				qs(action.vnew_value()));
		addSimpleServiceMessage(text);
	};

	auto createChangeAbout = [&](const MTPDchannelAdminLogEventActionChangeAbout &action) {
		auto newValue = qs(action.vnew_value());
		auto oldValue = qs(action.vprev_value());
		auto text = (channel->isMegagroup()
			? (newValue.isEmpty()
				? tr::lng_admin_log_removed_description_group
				: tr::lng_admin_log_changed_description_group)
			: (newValue.isEmpty()
				? tr::lng_admin_log_removed_description_channel
				: tr::lng_admin_log_changed_description_channel)
			)(tr::now, lt_from, fromLinkText);
		addSimpleServiceMessage(text);

		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyClientFlags = MTPDmessage_ClientFlag::f_admin_log_entry;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto newDescription = PrepareText(newValue, QString());
		auto body = history->makeMessage(
			history->nextNonHistoryEntryId(),
			bodyFlags,
			bodyClientFlags,
			bodyReplyTo,
			bodyViaBotId,
			date,
			peerToUser(from->id),
			QString(),
			newDescription);
		if (!oldValue.isEmpty()) {
			auto oldDescription = PrepareText(oldValue, QString());
			body->addLogEntryOriginal(id, tr::lng_admin_log_previous_description(tr::now), oldDescription);
		}
		addPart(body);
	};

	auto createChangeUsername = [&](const MTPDchannelAdminLogEventActionChangeUsername &action) {
		auto newValue = qs(action.vnew_value());
		auto oldValue = qs(action.vprev_value());
		auto text = (channel->isMegagroup()
			? (newValue.isEmpty()
				? tr::lng_admin_log_removed_link_group
				: tr::lng_admin_log_changed_link_group)
			: (newValue.isEmpty()
				? tr::lng_admin_log_removed_link_channel
				: tr::lng_admin_log_changed_link_channel)
			)(tr::now, lt_from, fromLinkText);
		addSimpleServiceMessage(text);

		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyClientFlags = MTPDmessage_ClientFlag::f_admin_log_entry;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto newLink = newValue.isEmpty()
			? TextWithEntities()
			: PrepareText(
				history->session().createInternalLinkFull(newValue),
				QString());
		auto body = history->makeMessage(
			history->nextNonHistoryEntryId(),
			bodyFlags,
			bodyClientFlags,
			bodyReplyTo,
			bodyViaBotId,
			date,
			peerToUser(from->id),
			QString(),
			newLink);
		if (!oldValue.isEmpty()) {
			auto oldLink = PrepareText(
				history->session().createInternalLinkFull(oldValue),
				QString());
			body->addLogEntryOriginal(id, tr::lng_admin_log_previous_link(tr::now), oldLink);
		}
		addPart(body);
	};

	auto createChangePhoto = [&](const MTPDchannelAdminLogEventActionChangePhoto &action) {
		action.vnew_photo().match([&](const MTPDphoto &data) {
			auto photo = history->owner().processPhoto(data);
			auto text = (channel->isMegagroup()
				? tr::lng_admin_log_changed_photo_group
				: tr::lng_admin_log_changed_photo_channel)(
					tr::now,
					lt_from,
					fromLinkText);
			addSimpleServiceMessage(text, photo);
		}, [&](const MTPDphotoEmpty &data) {
			auto text = (channel->isMegagroup()
				? tr::lng_admin_log_removed_photo_group
				: tr::lng_admin_log_removed_photo_channel)(
					tr::now,
					lt_from,
					fromLinkText);
			addSimpleServiceMessage(text);
		});
	};

	auto createToggleInvites = [&](const MTPDchannelAdminLogEventActionToggleInvites &action) {
		auto enabled = (action.vnew_value().type() == mtpc_boolTrue);
		auto text = (enabled
			? tr::lng_admin_log_invites_enabled
			: tr::lng_admin_log_invites_disabled);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	auto createToggleSignatures = [&](const MTPDchannelAdminLogEventActionToggleSignatures &action) {
		auto enabled = (action.vnew_value().type() == mtpc_boolTrue);
		auto text = (enabled
			? tr::lng_admin_log_signatures_enabled
			: tr::lng_admin_log_signatures_disabled);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	auto createUpdatePinned = [&](const MTPDchannelAdminLogEventActionUpdatePinned &action) {
		action.vmessage().match([&](const MTPDmessage &data) {
			const auto pinned = data.is_pinned();
			auto text = (pinned
				? tr::lng_admin_log_pinned_message
				: tr::lng_admin_log_unpinned_message)(tr::now, lt_from, fromLinkText);
			addSimpleServiceMessage(text);

			auto detachExistingItem = false;
			addPart(
				history->createItem(
					PrepareLogMessage(
						action.vmessage(),
						history->nextNonHistoryEntryId(),
						date),
					MTPDmessage_ClientFlag::f_admin_log_entry,
					detachExistingItem),
				ExtractSentDate(action.vmessage()));
		}, [&](const auto &) {
			auto text = tr::lng_admin_log_unpinned_message(tr::now, lt_from, fromLinkText);
			addSimpleServiceMessage(text);
		});
	};

	auto createEditMessage = [&](const MTPDchannelAdminLogEventActionEditMessage &action) {
		auto newValue = ExtractEditedText(session, action.vnew_message());
		auto canHaveCaption = MediaCanHaveCaption(action.vnew_message());
		auto text = (!canHaveCaption
			? tr::lng_admin_log_edited_message
			: newValue.text.isEmpty()
			? tr::lng_admin_log_removed_caption
			: tr::lng_admin_log_edited_caption)(
				tr::now,
				lt_from,
				fromLinkText);
		addSimpleServiceMessage(text);

		auto oldValue = ExtractEditedText(session, action.vprev_message());
		auto detachExistingItem = false;
		auto body = history->createItem(
			PrepareLogMessage(
				action.vnew_message(),
				history->nextNonHistoryEntryId(),
				date),
			MTPDmessage_ClientFlag::f_admin_log_entry,
			detachExistingItem);
		if (oldValue.text.isEmpty()) {
			oldValue = PrepareText(QString(), tr::lng_admin_log_empty_text(tr::now));
		}

		body->addLogEntryOriginal(
			id,
			(canHaveCaption
				? tr::lng_admin_log_previous_caption
				: tr::lng_admin_log_previous_message)(tr::now),
			oldValue);
		addPart(body);
	};

	auto createDeleteMessage = [&](const MTPDchannelAdminLogEventActionDeleteMessage &action) {
		auto text = tr::lng_admin_log_deleted_message(tr::now, lt_from, fromLinkText);
		addSimpleServiceMessage(text);

		auto detachExistingItem = false;
		addPart(
			history->createItem(
				PrepareLogMessage(
					action.vmessage(),
					history->nextNonHistoryEntryId(),
					date),
				MTPDmessage_ClientFlag::f_admin_log_entry,
				detachExistingItem),
			ExtractSentDate(action.vmessage()));
	};

	auto createParticipantJoin = [&]() {
		auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_joined
			: tr::lng_admin_log_participant_joined_channel);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	auto createParticipantLeave = [&]() {
		auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_left
			: tr::lng_admin_log_participant_left_channel);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	auto createParticipantInvite = [&](const MTPDchannelAdminLogEventActionParticipantInvite &action) {
		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyClientFlags = MTPDmessage_ClientFlag::f_admin_log_entry;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto bodyText = GenerateParticipantChangeText(channel, action.vparticipant());
		addPart(history->makeMessage(
			history->nextNonHistoryEntryId(),
			bodyFlags,
			bodyClientFlags,
			bodyReplyTo,
			bodyViaBotId,
			date,
			peerToUser(from->id),
			QString(),
			bodyText));
	};

	auto createParticipantToggleBan = [&](const MTPDchannelAdminLogEventActionParticipantToggleBan &action) {
		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyClientFlags = MTPDmessage_ClientFlag::f_admin_log_entry;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto bodyText = GenerateParticipantChangeText(channel, action.vnew_participant(), &action.vprev_participant());
		addPart(history->makeMessage(
			history->nextNonHistoryEntryId(),
			bodyFlags,
			bodyClientFlags,
			bodyReplyTo,
			bodyViaBotId,
			date,
			peerToUser(from->id),
			QString(),
			bodyText));
	};

	auto createParticipantToggleAdmin = [&](const MTPDchannelAdminLogEventActionParticipantToggleAdmin &action) {
		if (action.vnew_participant().type() == mtpc_channelParticipantAdmin
			&& action.vprev_participant().type() == mtpc_channelParticipantCreator) {
			// In case of ownership transfer we show that message in
			// the "User > Creator" part and skip the "Creator > Admin" part.
			return;
		}
		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyClientFlags = MTPDmessage_ClientFlag::f_admin_log_entry;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto bodyText = GenerateParticipantChangeText(channel, action.vnew_participant(), &action.vprev_participant());
		addPart(history->makeMessage(
			history->nextNonHistoryEntryId(),
			bodyFlags,
			bodyClientFlags,
			bodyReplyTo,
			bodyViaBotId,
			date,
			peerToUser(from->id),
			QString(),
			bodyText));
	};

	auto createChangeStickerSet = [&](const MTPDchannelAdminLogEventActionChangeStickerSet &action) {
		auto set = action.vnew_stickerset();
		auto removed = (set.type() == mtpc_inputStickerSetEmpty);
		if (removed) {
			auto text = tr::lng_admin_log_removed_stickers_group(tr::now, lt_from, fromLinkText);
			addSimpleServiceMessage(text);
		} else {
			auto text = tr::lng_admin_log_changed_stickers_group(
				tr::now,
				lt_from,
				fromLinkText,
				lt_sticker_set,
				textcmdLink(2, tr::lng_admin_log_changed_stickers_set(tr::now)));
			auto setLink = std::make_shared<LambdaClickHandler>([set] {
				Ui::show(Box<StickerSetBox>(
					App::wnd()->sessionController(),
					set));
			});
			auto message = HistoryService::PreparedText { text };
			message.links.push_back(fromLink);
			message.links.push_back(setLink);
			addPart(history->makeServiceMessage(
				history->nextNonHistoryEntryId(),
				MTPDmessage_ClientFlag::f_admin_log_entry,
				date,
				message,
				MTPDmessage::Flags(0),
				peerToUser(from->id)));
		}
	};

	auto createTogglePreHistoryHidden = [&](const MTPDchannelAdminLogEventActionTogglePreHistoryHidden &action) {
		auto hidden = (action.vnew_value().type() == mtpc_boolTrue);
		auto text = (hidden
			? tr::lng_admin_log_history_made_hidden
			: tr::lng_admin_log_history_made_visible);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	auto createDefaultBannedRights = [&](const MTPDchannelAdminLogEventActionDefaultBannedRights &action) {
		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyClientFlags = MTPDmessage_ClientFlag::f_admin_log_entry;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto bodyText = GenerateDefaultBannedRightsChangeText(channel, action.vnew_banned_rights(), action.vprev_banned_rights());
		addPart(history->makeMessage(
			history->nextNonHistoryEntryId(),
			bodyFlags,
			bodyClientFlags,
			bodyReplyTo,
			bodyViaBotId,
			date,
			peerToUser(from->id),
			QString(),
			bodyText));
	};

	auto createStopPoll = [&](const MTPDchannelAdminLogEventActionStopPoll &action) {
		auto text = tr::lng_admin_log_stopped_poll(tr::now, lt_from, fromLinkText);
		addSimpleServiceMessage(text);

		auto detachExistingItem = false;
		addPart(
			history->createItem(
				PrepareLogMessage(
					action.vmessage(),
					history->nextNonHistoryEntryId(),
					date),
				MTPDmessage_ClientFlag::f_admin_log_entry,
				detachExistingItem),
			ExtractSentDate(action.vmessage()));
	};

	auto createChangeLinkedChat = [&](const MTPDchannelAdminLogEventActionChangeLinkedChat &action) {
		const auto broadcast = channel->isBroadcast();
		const auto was = history->owner().channelLoaded(action.vprev_value().v);
		const auto now = history->owner().channelLoaded(action.vnew_value().v);
		if (!now) {
			auto text = (broadcast
				? tr::lng_admin_log_removed_linked_chat
				: tr::lng_admin_log_removed_linked_channel)(
					tr::now,
					lt_from,
					fromLinkText);
			addSimpleServiceMessage(text);
		} else {
			auto text = (broadcast
				? tr::lng_admin_log_changed_linked_chat
				: tr::lng_admin_log_changed_linked_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_chat,
					textcmdLink(2, now->name));
			auto chatLink = std::make_shared<LambdaClickHandler>([=] {
				Ui::showPeerHistory(now, ShowAtUnreadMsgId);
			});
			auto message = HistoryService::PreparedText{ text };
			message.links.push_back(fromLink);
			message.links.push_back(chatLink);
			addPart(history->makeServiceMessage(
				history->nextNonHistoryEntryId(),
				MTPDmessage_ClientFlag::f_admin_log_entry,
				date,
				message,
				MTPDmessage::Flags(0),
				peerToUser(from->id)));
		}
	};

	auto createChangeLocation = [&](const MTPDchannelAdminLogEventActionChangeLocation &action) {
		action.vnew_value().match([&](const MTPDchannelLocation &data) {
			const auto address = qs(data.vaddress());
			const auto link = data.vgeo_point().match([&](const MTPDgeoPoint &data) {
				return textcmdLink(
					LocationClickHandler::Url(Data::LocationPoint(data)),
					address);
			}, [&](const MTPDgeoPointEmpty &) {
				return address;
			});
			const auto text = tr::lng_admin_log_changed_location_chat(
				tr::now,
				lt_from,
				fromLinkText,
				lt_address,
				link);
			addSimpleServiceMessage(text);
		}, [&](const MTPDchannelLocationEmpty &) {
			const auto text = tr::lng_admin_log_removed_location_chat(tr::now, lt_from, fromLinkText);
			addSimpleServiceMessage(text);
		});
	};

	auto createToggleSlowMode = [&](const MTPDchannelAdminLogEventActionToggleSlowMode &action) {
		if (const auto seconds = action.vnew_value().v) {
			const auto duration = (seconds >= 60)
				? tr::lng_admin_log_slow_mode_minutes(tr::now, lt_count, seconds / 60)
				: tr::lng_admin_log_slow_mode_seconds(tr::now, lt_count, seconds);
			const auto text = tr::lng_admin_log_changed_slow_mode(
				tr::now,
				lt_from,
				fromLinkText,
				lt_duration,
				duration);
			addSimpleServiceMessage(text);
		} else {
			const auto text = tr::lng_admin_log_removed_slow_mode(tr::now, lt_from, fromLinkText);
			addSimpleServiceMessage(text);
		}
	};

	auto createStartGroupCall = [&](const MTPDchannelAdminLogEventActionStartGroupCall &data) {
		const auto text = tr::lng_admin_log_started_group_call(tr::now, lt_from, fromLinkText);
		addSimpleServiceMessage(text);
	};

	auto createDiscardGroupCall = [&](const MTPDchannelAdminLogEventActionDiscardGroupCall &data) {
		const auto text = tr::lng_admin_log_discarded_group_call(tr::now, lt_from, fromLinkText);
		addSimpleServiceMessage(text);
	};

	auto groupCallParticipantPeer = [&](const MTPGroupCallParticipant &data) {
		return data.match([&](const MTPDgroupCallParticipant &data) {
			return history->owner().peer(peerFromMTP(data.vpeer()));
		});
	};

	auto addServiceMessageWithLink = [&](const QString &text, const ClickHandlerPtr &link) {
		auto message = HistoryService::PreparedText{ text };
		message.links.push_back(fromLink);
		message.links.push_back(link);
		addPart(history->makeServiceMessage(
			history->nextNonHistoryEntryId(),
			MTPDmessage_ClientFlag::f_admin_log_entry,
			date,
			message,
			MTPDmessage::Flags(0),
			peerToUser(from->id)));
	};

	auto createParticipantMute = [&](const MTPDchannelAdminLogEventActionParticipantMute &data) {
		const auto participantPeer = groupCallParticipantPeer(data.vparticipant());
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = textcmdLink(2, participantPeer->name);
		auto text = tr::lng_admin_log_muted_participant(
			tr::now,
			lt_from,
			fromLinkText,
			lt_user,
			participantPeerLinkText);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	auto createParticipantUnmute = [&](const MTPDchannelAdminLogEventActionParticipantUnmute &data) {
		const auto participantPeer = groupCallParticipantPeer(data.vparticipant());
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = textcmdLink(2, participantPeer->name);
		auto text = tr::lng_admin_log_unmuted_participant(
			tr::now,
			lt_from,
			fromLinkText,
			lt_user,
			participantPeerLinkText);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	auto createToggleGroupCallSetting = [&](const MTPDchannelAdminLogEventActionToggleGroupCallSetting &data) {
		const auto text = mtpIsTrue(data.vjoin_muted())
			? tr::lng_admin_log_disallowed_unmute_self(tr::now, lt_from, fromLinkText)
			: tr::lng_admin_log_allowed_unmute_self(tr::now, lt_from, fromLinkText);
		addSimpleServiceMessage(text);
	};

	auto addInviteLinkServiceMessage = [&](const QString &text, const MTPExportedChatInvite &data) {
		auto message = HistoryService::PreparedText{ text };
		message.links.push_back(fromLink);
		if (!ExtractInviteLink(data).endsWith("...")) {
			message.links.push_back(std::make_shared<UrlClickHandler>(InternalInviteLinkUrl(data)));
		}
		addPart(history->makeServiceMessage(
			history->nextNonHistoryEntryId(),
			MTPDmessage_ClientFlag::f_admin_log_entry,
			date,
			message,
			MTPDmessage::Flags(0),
			peerToUser(from->id),
			nullptr));
	};

	auto createParticipantJoinByInvite = [&](const MTPDchannelAdminLogEventActionParticipantJoinByInvite &data) {
		auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_joined_by_link
			: tr::lng_admin_log_participant_joined_by_link_channel);
		addInviteLinkServiceMessage(
			text(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite())),
			data.vinvite());
	};

	auto createExportedInviteDelete = [&](const MTPDchannelAdminLogEventActionExportedInviteDelete &data) {
		addInviteLinkServiceMessage(
			tr::lng_admin_log_delete_invite_link(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite())),
			data.vinvite());
	};

	auto createExportedInviteRevoke = [&](const MTPDchannelAdminLogEventActionExportedInviteRevoke &data) {
		addInviteLinkServiceMessage(
			tr::lng_admin_log_revoke_invite_link(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite())),
			data.vinvite());
	};

	auto createExportedInviteEdit = [&](const MTPDchannelAdminLogEventActionExportedInviteEdit &data) {
		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyClientFlags = MTPDmessage_ClientFlag::f_admin_log_entry;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto bodyText = GenerateInviteLinkChangeText(data.vnew_invite(), data.vprev_invite());
		addPart(history->makeMessage(
			history->nextNonHistoryEntryId(),
			bodyFlags,
			bodyClientFlags,
			bodyReplyTo,
			bodyViaBotId,
			date,
			peerToUser(from->id),
			QString(),
			bodyText));
	};

	auto createParticipantVolume = [&](const MTPDchannelAdminLogEventActionParticipantVolume &data) {
		const auto participantPeer = groupCallParticipantPeer(data.vparticipant());
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = textcmdLink(2, participantPeer->name);
		const auto volume = data.vparticipant().match([&](
				const MTPDgroupCallParticipant &data) {
			return data.vvolume().value_or(10000);
		});
		const auto volumeText = QString::number(volume / 100) + '%';
		auto text = tr::lng_admin_log_participant_volume(
			tr::now,
			lt_from,
			fromLinkText,
			lt_user,
			participantPeerLinkText,
			lt_percent,
			volumeText);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	auto createChangeHistoryTTL = [&](const MTPDchannelAdminLogEventActionChangeHistoryTTL &data) {
		const auto was = data.vprev_value().v;
		const auto now = data.vnew_value().v;
		const auto wrap = [](int duration) {
			return (duration == 5)
				? u"5 seconds"_q
				: (duration < 3 * 86400)
				? tr::lng_manage_messages_ttl_after1(tr::now)
				: tr::lng_manage_messages_ttl_after2(tr::now);
		};
		auto text = !was
			? tr::lng_admin_log_messages_ttl_set(tr::now, lt_from, fromLinkText, lt_duration, wrap(now))
			: !now
			? tr::lng_admin_log_messages_ttl_removed(tr::now, lt_from, fromLinkText, lt_duration, wrap(was))
			: tr::lng_admin_log_messages_ttl_changed(tr::now, lt_from, fromLinkText, lt_previous, wrap(was), lt_duration, wrap(now));
		addSimpleServiceMessage(text);
	};

	action.match([&](const MTPDchannelAdminLogEventActionChangeTitle &data) {
		createChangeTitle(data);
	}, [&](const MTPDchannelAdminLogEventActionChangeAbout &data) {
		createChangeAbout(data);
	}, [&](const MTPDchannelAdminLogEventActionChangeUsername &data) {
		createChangeUsername(data);
	}, [&](const MTPDchannelAdminLogEventActionChangePhoto &data) {
		createChangePhoto(data);
	}, [&](const MTPDchannelAdminLogEventActionToggleInvites &data) {
		createToggleInvites(data);
	}, [&](const MTPDchannelAdminLogEventActionToggleSignatures &data) {
		createToggleSignatures(data);
	}, [&](const MTPDchannelAdminLogEventActionUpdatePinned &data) {
		createUpdatePinned(data);
	}, [&](const MTPDchannelAdminLogEventActionEditMessage &data) {
		createEditMessage(data);
	}, [&](const MTPDchannelAdminLogEventActionDeleteMessage &data) {
		createDeleteMessage(data);
	}, [&](const MTPDchannelAdminLogEventActionParticipantJoin &) {
		createParticipantJoin();
	}, [&](const MTPDchannelAdminLogEventActionParticipantLeave &) {
		createParticipantLeave();
	}, [&](const MTPDchannelAdminLogEventActionParticipantInvite &data) {
		createParticipantInvite(data);
	}, [&](const MTPDchannelAdminLogEventActionParticipantToggleBan &data) {
		createParticipantToggleBan(data);
	}, [&](const MTPDchannelAdminLogEventActionParticipantToggleAdmin &data) {
		createParticipantToggleAdmin(data);
	}, [&](const MTPDchannelAdminLogEventActionChangeStickerSet &data) {
		createChangeStickerSet(data);
	}, [&](const MTPDchannelAdminLogEventActionTogglePreHistoryHidden &data) {
		createTogglePreHistoryHidden(data);
	}, [&](const MTPDchannelAdminLogEventActionDefaultBannedRights &data) {
		createDefaultBannedRights(data);
	}, [&](const MTPDchannelAdminLogEventActionStopPoll &data) {
		createStopPoll(data);
	}, [&](const MTPDchannelAdminLogEventActionChangeLinkedChat &data) {
		createChangeLinkedChat(data);
	}, [&](const MTPDchannelAdminLogEventActionChangeLocation &data) {
		createChangeLocation(data);
	}, [&](const MTPDchannelAdminLogEventActionToggleSlowMode &data) {
		createToggleSlowMode(data);
	}, [&](const MTPDchannelAdminLogEventActionStartGroupCall &data) {
		createStartGroupCall(data);
	}, [&](const MTPDchannelAdminLogEventActionDiscardGroupCall &data) {
		createDiscardGroupCall(data);
	}, [&](const MTPDchannelAdminLogEventActionParticipantMute &data) {
		createParticipantMute(data);
	}, [&](const MTPDchannelAdminLogEventActionParticipantUnmute &data) {
		createParticipantUnmute(data);
	}, [&](const MTPDchannelAdminLogEventActionToggleGroupCallSetting &data) {
		createToggleGroupCallSetting(data);
	}, [&](const MTPDchannelAdminLogEventActionParticipantJoinByInvite &data) {
		createParticipantJoinByInvite(data);
	}, [&](const MTPDchannelAdminLogEventActionExportedInviteDelete &data) {
		createExportedInviteDelete(data);
	}, [&](const MTPDchannelAdminLogEventActionExportedInviteRevoke &data) {
		createExportedInviteRevoke(data);
	}, [&](const MTPDchannelAdminLogEventActionExportedInviteEdit &data) {
		createExportedInviteEdit(data);
	}, [&](const MTPDchannelAdminLogEventActionParticipantVolume &data) {
		createParticipantVolume(data);
	}, [&](const MTPDchannelAdminLogEventActionChangeHistoryTTL &data) {
		createChangeHistoryTTL(data);
	});
}

} // namespace AdminLog
