/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "history/history_admin_log_item.h"

#include "lang/lang_keys.h"
#include "messenger.h"

namespace AdminLog {
namespace {

TextWithEntities PrepareText(const QString &value, const QString &emptyValue) {
	auto result = TextWithEntities { textClean(value) };
	if (result.text.isEmpty()) {
		result.text = emptyValue;
		if (!emptyValue.isEmpty()) {
			result.entities.push_back(EntityInText(EntityInTextItalic, 0, emptyValue.size()));
		}
	} else {
		textParseEntities(result.text, TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands, &result.entities);
	}
	return result;
}

MTPMessage PrepareLogMessage(const MTPMessage &message, MsgId newId, int32 newDate) {
	switch (message.type()) {
	case mtpc_messageEmpty: return MTP_messageEmpty(MTP_int(newId));
	case mtpc_messageService: {
		auto &data = message.c_messageService();
		auto flags = data.vflags.v & ~(MTPDmessageService::Flag::f_out | MTPDmessageService::Flag::f_post/* | MTPDmessageService::Flag::f_reply_to_msg_id*/);
		return MTP_messageService(MTP_flags(flags), MTP_int(newId), data.vfrom_id, data.vto_id, data.vreply_to_msg_id, data.vdate, data.vaction);
	} break;
	case mtpc_message: {
		auto &data = message.c_message();
		auto flags = data.vflags.v & ~(MTPDmessage::Flag::f_out | MTPDmessage::Flag::f_post | MTPDmessage::Flag::f_reply_to_msg_id);
		return MTP_message(MTP_flags(flags), MTP_int(newId), data.vfrom_id, data.vto_id, data.vfwd_from, data.vvia_bot_id, data.vreply_to_msg_id, MTP_int(newDate), data.vmessage, data.vmedia, data.vreply_markup, data.ventities, data.vviews, data.vedit_date);
	} break;
	}
	Unexpected("Type in PrepareLogMessage()");
}

bool MediaCanHaveCaption(const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return false;
	}
	auto &data = message.c_message();
	auto mediaType = data.has_media() ? data.vmedia.type() : mtpc_messageMediaEmpty;
	return (mediaType == mtpc_messageMediaDocument || mediaType == mtpc_messageMediaPhoto);
}

TextWithEntities ExtractEditedText(const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return TextWithEntities();
	}
	auto &data = message.c_message();
	auto mediaType = data.has_media() ? data.vmedia.type() : mtpc_messageMediaEmpty;
	if (mediaType == mtpc_messageMediaDocument) {
		return PrepareText(qs(data.vmedia.c_messageMediaDocument().vcaption), QString());
	} else if (mediaType == mtpc_messageMediaPhoto) {
		return PrepareText(qs(data.vmedia.c_messageMediaPhoto().vcaption), QString());
	}
	auto text = textClean(qs(data.vmessage));
	auto entities = data.has_entities() ? entitiesFromMTP(data.ventities.v) : EntitiesInText();
	return { text, entities };
}

PhotoData *GenerateChatPhoto(ChannelId channelId, uint64 logEntryId, MTPint date, const MTPDchatPhoto &photo) {
	// We try to make a unique photoId that will stay the same for each pair (channelId, logEntryId).
	static const auto RandomIdPart = rand_value<uint64>();
	auto mixinIdPart = (static_cast<uint64>(static_cast<uint32>(channelId)) << 32) ^ logEntryId;
	auto photoId = RandomIdPart ^ mixinIdPart;

	auto photoSizes = QVector<MTPPhotoSize>();
	photoSizes.reserve(2);
	photoSizes.push_back(MTP_photoSize(MTP_string("a"), photo.vphoto_small, MTP_int(160), MTP_int(160), MTP_int(0)));
	photoSizes.push_back(MTP_photoSize(MTP_string("c"), photo.vphoto_big, MTP_int(640), MTP_int(640), MTP_int(0)));
	return App::feedPhoto(MTP_photo(MTP_flags(0), MTP_long(photoId), MTP_long(0), date, MTP_vector<MTPPhotoSize>(photoSizes)));
}

const auto CollectChanges = [](auto &phraseMap, auto plusFlags, auto minusFlags) {
	auto withPrefix = [&phraseMap](auto flags, QChar prefix) {
		auto result = QString();
		for (auto &phrase : phraseMap) {
			if (flags & phrase.first) {
				result.append('\n' + (prefix + lang(phrase.second)));
			}
		}
		return result;
	};
	const auto kMinus = QChar(0x2212);
	return withPrefix(plusFlags & ~minusFlags, '+') + withPrefix(minusFlags & ~plusFlags, kMinus);
};

auto GenerateAdminChangeText(gsl::not_null<ChannelData*> channel, const TextWithEntities &user, const MTPChannelAdminRights *newRights, const MTPChannelAdminRights *prevRights) {
	using Flag = MTPDchannelAdminRights::Flag;
	using Flags = MTPDchannelAdminRights::Flags;

	Expects(!newRights || newRights->type() == mtpc_channelAdminRights);
	Expects(!prevRights || prevRights->type() == mtpc_channelAdminRights);
	auto newFlags = newRights ? newRights->c_channelAdminRights().vflags.v : 0;
	auto prevFlags = prevRights ? prevRights->c_channelAdminRights().vflags.v : 0;
	auto result = lng_admin_log_promoted__generic(lt_user, user);

	auto inviteKey = Flag::f_invite_users | Flag::f_invite_link;
	auto useInviteLinkPhrase = channel->isMegagroup() && channel->anyoneCanAddMembers();
	auto invitePhrase = (useInviteLinkPhrase ? lng_admin_log_admin_invite_link : lng_admin_log_admin_invite_users);
	static auto phraseMap = std::map<Flags, LangKey> {
		{ Flag::f_change_info, lng_admin_log_admin_change_info },
		{ Flag::f_post_messages, lng_admin_log_admin_post_messages },
		{ Flag::f_edit_messages, lng_admin_log_admin_edit_messages },
		{ Flag::f_delete_messages, lng_admin_log_admin_delete_messages },
		{ Flag::f_ban_users, lng_admin_log_admin_ban_users },
		{ inviteKey, invitePhrase },
		{ Flag::f_pin_messages, lng_admin_log_admin_pin_messages },
		{ Flag::f_add_admins, lng_admin_log_admin_add_admins },
	};
	phraseMap[inviteKey] = invitePhrase;

	auto changes = CollectChanges(phraseMap, newFlags, prevFlags);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}

	return result;
};

auto GenerateBannedChangeText(const TextWithEntities &user, const MTPChannelBannedRights *newRights, const MTPChannelBannedRights *prevRights) {
	using Flag = MTPDchannelBannedRights::Flag;
	using Flags = MTPDchannelBannedRights::Flags;

	Expects(!newRights || newRights->type() == mtpc_channelBannedRights);
	Expects(!prevRights || prevRights->type() == mtpc_channelBannedRights);
	auto newFlags = newRights ? newRights->c_channelBannedRights().vflags.v : 0;
	auto prevFlags = prevRights ? prevRights->c_channelBannedRights().vflags.v : 0;
	auto newUntil = newRights ? newRights->c_channelBannedRights().vuntil_date : MTP_int(0);
	auto indefinitely = ChannelData::IsRestrictedForever(newUntil.v);
	if (newFlags & Flag::f_view_messages) {
		return lng_admin_log_banned__generic(lt_user, user);
	}
	auto untilText = indefinitely ? lang(lng_admin_log_restricted_forever) : lng_admin_log_restricted_until(lt_date, langDateTime(::date(newUntil)));
	auto result = lng_admin_log_restricted__generic(lt_user, user, lt_until, TextWithEntities { untilText });

	static auto phraseMap = std::map<Flags, LangKey> {
		{ Flag::f_view_messages, lng_admin_log_banned_view_messages },
		{ Flag::f_send_messages, lng_admin_log_banned_send_messages },
		{ Flag::f_send_media, lng_admin_log_banned_send_media },
		{ Flag::f_send_stickers | Flag::f_send_gifs | Flag::f_send_inline | Flag::f_send_games, lng_admin_log_banned_send_stickers },
		{ Flag::f_embed_links, lng_admin_log_banned_embed_links },
	};
	auto changes = CollectChanges(phraseMap, prevFlags, newFlags);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}

	return result;
};

auto GenerateUserString(MTPint userId) {
	// User name in "User name (@username)" format with entities.
	auto user = App::user(userId.v);
	auto name = TextWithEntities { App::peerName(user) };
	name.entities.push_back(EntityInText(EntityInTextMentionName, 0, name.text.size(), QString::number(user->id) + '.' + QString::number(user->access)));
	auto username = user->userName();
	if (username.isEmpty()) {
		return name;
	}
	auto mention = TextWithEntities { '@' + username };
	mention.entities.push_back(EntityInText(EntityInTextMention, 0, mention.text.size()));
	return lng_admin_log_user_with_username__generic(lt_name, name, lt_mention, mention);
}

auto GenerateParticipantChangeTextInner(gsl::not_null<ChannelData*> channel, const MTPChannelParticipant &participant, const MTPChannelParticipant *oldParticipant) {
	auto oldType = oldParticipant ? oldParticipant->type() : 0;

	auto resultForParticipant = [channel, oldParticipant, oldType](auto &&data) {
		auto user = GenerateUserString(data.vuser_id);
		if (oldType == mtpc_channelParticipantAdmin) {
			return GenerateAdminChangeText(channel, user, nullptr, &oldParticipant->c_channelParticipantAdmin().vadmin_rights);
		} else if (oldType == mtpc_channelParticipantBanned) {
			return GenerateBannedChangeText(user, nullptr, &oldParticipant->c_channelParticipantBanned().vbanned_rights);
		}
		return lng_admin_log_invited__generic(lt_user, user);
	};

	switch (participant.type()) {
	case mtpc_channelParticipantCreator: {
		// No valid string here :(
		auto &data = participant.c_channelParticipantCreator();
		return lng_admin_log_invited__generic(lt_user, GenerateUserString(data.vuser_id));
	} break;

	case mtpc_channelParticipant: return resultForParticipant(participant.c_channelParticipant());
	case mtpc_channelParticipantSelf: return resultForParticipant(participant.c_channelParticipantSelf());

	case mtpc_channelParticipantAdmin: {
		auto &data = participant.c_channelParticipantAdmin();
		auto user = GenerateUserString(data.vuser_id);
		return GenerateAdminChangeText(channel, user, &data.vadmin_rights, (oldType == mtpc_channelParticipantAdmin) ? &oldParticipant->c_channelParticipantAdmin().vadmin_rights : nullptr);
	} break;

	case mtpc_channelParticipantBanned: {
		auto &data = participant.c_channelParticipantBanned();
		auto user = GenerateUserString(data.vuser_id);
		return GenerateBannedChangeText(user, &data.vbanned_rights, (oldType == mtpc_channelParticipantBanned) ? &oldParticipant->c_channelParticipantBanned().vbanned_rights : nullptr);
	} break;
	}

	Unexpected("Participant type in GenerateParticipantChangeTextInner()");
}

TextWithEntities GenerateParticipantChangeText(gsl::not_null<ChannelData*> channel, const MTPChannelParticipant &participant, const MTPChannelParticipant *oldParticipant = nullptr) {
	auto result = GenerateParticipantChangeTextInner(channel, participant, oldParticipant);
	result.entities.push_front(EntityInText(EntityInTextItalic, 0, result.text.size()));
	return result;
}

} // namespace

Item::Item(gsl::not_null<History*> history, LocalIdManager &idManager, const MTPDchannelAdminLogEvent &event)
: _id(event.vid.v)
, _history(history)
, _from(App::user(event.vuser_id.v)) {
	auto &action = event.vaction;
	auto date = event.vdate;

	using ServiceFlag = MTPDmessageService::Flag;
	using Flag = MTPDmessage::Flag;
	auto fromName = App::peerName(_from);
	auto fromLink = _from->openLink();
	auto fromLinkText = textcmdLink(1, fromName);

	auto addSimpleServiceMessage = [this, &idManager, date, fromLink](const QString &text, PhotoData *photo = nullptr) {
		auto message = HistoryService::PreparedText { text };
		message.links.push_back(fromLink);
		addPart(HistoryService::create(_history, idManager.next(), ::date(date), message, 0, peerToUser(_from->id), photo));
	};

	auto createChangeTitle = [this, addSimpleServiceMessage, fromLinkText](const MTPDchannelAdminLogEventActionChangeTitle &action) {
		auto text = (channel()->isMegagroup() ? lng_action_changed_title : lng_admin_log_changed_title_channel)(lt_from, fromLinkText, lt_title, qs(action.vnew_value));
		addSimpleServiceMessage(text);
	};

	auto createChangeAbout = [this, &idManager, date, addSimpleServiceMessage, fromLinkText](const MTPDchannelAdminLogEventActionChangeAbout &action) {
		auto newValue = qs(action.vnew_value);
		auto oldValue = qs(action.vprev_value);
		auto text = (channel()->isMegagroup()
			? (newValue.isEmpty() ? lng_admin_log_removed_description_group : lng_admin_log_changed_description_group)
			: (newValue.isEmpty() ? lng_admin_log_removed_description_channel : lng_admin_log_changed_description_channel)
			)(lt_from, fromLinkText);
		addSimpleServiceMessage(text);

		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto newDescription = PrepareText(newValue, QString());
		auto body = HistoryMessage::create(_history, idManager.next(), bodyFlags, bodyReplyTo, bodyViaBotId, ::date(date), peerToUser(_from->id), newDescription);
		if (!oldValue.isEmpty()) {
			auto oldDescription = PrepareText(oldValue, QString());
			body->addLogEntryOriginal(lang(lng_admin_log_previous_description), oldDescription);
		}
		addPart(body);
	};

	auto createChangeUsername = [this, &idManager, date, addSimpleServiceMessage, fromLinkText](const MTPDchannelAdminLogEventActionChangeUsername &action) {
		auto newValue = qs(action.vnew_value);
		auto oldValue = qs(action.vprev_value);
		auto text = (channel()->isMegagroup()
			? (newValue.isEmpty() ? lng_admin_log_removed_link_group : lng_admin_log_changed_link_group)
			: (newValue.isEmpty() ? lng_admin_log_removed_link_channel : lng_admin_log_changed_link_channel)
			)(lt_from, fromLinkText);
		addSimpleServiceMessage(text);

		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto newLink = newValue.isEmpty() ? TextWithEntities() : PrepareText(Messenger::Instance().createInternalLinkFull(newValue), QString());
		auto body = HistoryMessage::create(_history, idManager.next(), bodyFlags, bodyReplyTo, bodyViaBotId, ::date(date), peerToUser(_from->id), newLink);
		if (!oldValue.isEmpty()) {
			auto oldLink = PrepareText(Messenger::Instance().createInternalLinkFull(oldValue), QString());
			body->addLogEntryOriginal(lang(lng_admin_log_previous_link), oldLink);
		}
		addPart(body);
	};

	auto createChangePhoto = [this, date, addSimpleServiceMessage, fromLinkText](const MTPDchannelAdminLogEventActionChangePhoto &action) {
		t_assert(action.vnew_photo.type() == mtpc_chatPhoto);
		auto photo = GenerateChatPhoto(channel()->bareId(), _id, date, action.vnew_photo.c_chatPhoto());

		auto text = (channel()->isMegagroup() ? lng_admin_log_changed_photo_group : lng_admin_log_changed_photo_channel)(lt_from, fromLinkText);
		addSimpleServiceMessage(text, photo);
	};

	auto createToggleInvites = [this, addSimpleServiceMessage, fromLinkText](const MTPDchannelAdminLogEventActionToggleInvites &action) {
		auto enabled = (action.vnew_value.type() == mtpc_boolTrue);
		auto text = (enabled ? lng_admin_log_invites_enabled : lng_admin_log_invites_disabled)(lt_from, fromLinkText);
		addSimpleServiceMessage(text);
	};

	auto createToggleSignatures = [this, addSimpleServiceMessage, fromLinkText](const MTPDchannelAdminLogEventActionToggleSignatures &action) {
		auto enabled = (action.vnew_value.type() == mtpc_boolTrue);
		auto text = (enabled ? lng_admin_log_signatures_enabled : lng_admin_log_signatures_disabled)(lt_from, fromLinkText);
		addSimpleServiceMessage(text);
	};

	auto createUpdatePinned = [this, &idManager, date, addSimpleServiceMessage, fromLinkText](const MTPDchannelAdminLogEventActionUpdatePinned &action) {
		auto text = lng_admin_log_pinned_message(lt_from, fromLinkText);
		addSimpleServiceMessage(text);

		auto applyServiceAction = false;
		auto detachExistingItem = false;
		addPart(_history->createItem(PrepareLogMessage(action.vmessage, idManager.next(), date.v), applyServiceAction, detachExistingItem));
	};

	auto createEditMessage = [this, &idManager, date, addSimpleServiceMessage, fromLinkText](const MTPDchannelAdminLogEventActionEditMessage &action) {
		auto newValue = ExtractEditedText(action.vnew_message);
		auto canHaveCaption = MediaCanHaveCaption(action.vnew_message);
		auto text = (canHaveCaption
			? (newValue.text.isEmpty() ? lng_admin_log_removed_caption : lng_admin_log_edited_caption)
			: lng_admin_log_edited_message
			)(lt_from, fromLinkText);
		addSimpleServiceMessage(text);

		auto oldValue = ExtractEditedText(action.vprev_message);
		auto applyServiceAction = false;
		auto detachExistingItem = false;
		auto body = _history->createItem(PrepareLogMessage(action.vnew_message, idManager.next(), date.v), applyServiceAction, detachExistingItem);
		if (!oldValue.text.isEmpty()) {
			body->addLogEntryOriginal(lang(canHaveCaption ? lng_admin_log_previous_caption : lng_admin_log_previous_description), oldValue);
		}
		addPart(body);
	};

	auto createDeleteMessage = [this, &idManager, date, addSimpleServiceMessage, fromLinkText](const MTPDchannelAdminLogEventActionDeleteMessage &action) {
		auto text = lng_admin_log_deleted_message(lt_from, fromLinkText);
		addSimpleServiceMessage(text);

		auto applyServiceAction = false;
		auto detachExistingItem = false;
		addPart(_history->createItem(PrepareLogMessage(action.vmessage, idManager.next(), date.v), applyServiceAction, detachExistingItem));
	};

	auto createParticipantJoin = [this, &idManager, addSimpleServiceMessage, fromLinkText]() {
		auto text = lng_admin_log_participant_joined(lt_from, fromLinkText);
		addSimpleServiceMessage(text);
	};

	auto createParticipantLeave = [this, &idManager, addSimpleServiceMessage, fromLinkText]() {
		auto text = lng_admin_log_participant_left(lt_from, fromLinkText);
		addSimpleServiceMessage(text);
	};

	auto createParticipantInvite = [this, &idManager, date](const MTPDchannelAdminLogEventActionParticipantInvite &action) {
		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto bodyText = GenerateParticipantChangeText(channel(), action.vparticipant);
		addPart(HistoryMessage::create(_history, idManager.next(), bodyFlags, bodyReplyTo, bodyViaBotId, ::date(date), peerToUser(_from->id), bodyText));
	};

	auto createParticipantToggleBan = [this, &idManager, date](const MTPDchannelAdminLogEventActionParticipantToggleBan &action) {
		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto bodyText = GenerateParticipantChangeText(channel(), action.vnew_participant, &action.vprev_participant);
		addPart(HistoryMessage::create(_history, idManager.next(), bodyFlags, bodyReplyTo, bodyViaBotId, ::date(date), peerToUser(_from->id), bodyText));
	};

	auto createParticipantToggleAdmin = [this, &idManager, date](const MTPDchannelAdminLogEventActionParticipantToggleAdmin &action) {
		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		auto bodyText = GenerateParticipantChangeText(channel(), action.vnew_participant, &action.vprev_participant);
		addPart(HistoryMessage::create(_history, idManager.next(), bodyFlags, bodyReplyTo, bodyViaBotId, ::date(date), peerToUser(_from->id), bodyText));
	};

	switch (action.type()) {
	case mtpc_channelAdminLogEventActionChangeTitle: createChangeTitle(action.c_channelAdminLogEventActionChangeTitle()); break;
	case mtpc_channelAdminLogEventActionChangeAbout: createChangeAbout(action.c_channelAdminLogEventActionChangeAbout()); break;
	case mtpc_channelAdminLogEventActionChangeUsername: createChangeUsername(action.c_channelAdminLogEventActionChangeUsername()); break;
	case mtpc_channelAdminLogEventActionChangePhoto: createChangePhoto(action.c_channelAdminLogEventActionChangePhoto()); break;
	case mtpc_channelAdminLogEventActionToggleInvites: createToggleInvites(action.c_channelAdminLogEventActionToggleInvites()); break;
	case mtpc_channelAdminLogEventActionToggleSignatures: createToggleSignatures(action.c_channelAdminLogEventActionToggleSignatures()); break;
	case mtpc_channelAdminLogEventActionUpdatePinned: createUpdatePinned(action.c_channelAdminLogEventActionUpdatePinned()); break;
	case mtpc_channelAdminLogEventActionEditMessage: createEditMessage(action.c_channelAdminLogEventActionEditMessage()); break;
	case mtpc_channelAdminLogEventActionDeleteMessage: createDeleteMessage(action.c_channelAdminLogEventActionDeleteMessage()); break;
	case mtpc_channelAdminLogEventActionParticipantJoin: createParticipantJoin(); break;
	case mtpc_channelAdminLogEventActionParticipantLeave: createParticipantLeave(); break;
	case mtpc_channelAdminLogEventActionParticipantInvite: createParticipantInvite(action.c_channelAdminLogEventActionParticipantInvite()); break;
	case mtpc_channelAdminLogEventActionParticipantToggleBan: createParticipantToggleBan(action.c_channelAdminLogEventActionParticipantToggleBan()); break;
	case mtpc_channelAdminLogEventActionParticipantToggleAdmin: createParticipantToggleAdmin(action.c_channelAdminLogEventActionParticipantToggleAdmin()); break;
	default: Unexpected("channelAdminLogEventAction type in AdminLog::Item::Item()");
	}
}

void Item::addPart(HistoryItem *item) {
	_parts.push_back(item);
}

int Item::resizeGetHeight(int newWidth) {
	_height = 0;
	for (auto part : _parts) {
		_height += part->resizeGetHeight(newWidth);
	}
	return _height;
}

void Item::draw(Painter &p, QRect clip, TextSelection selection, TimeMs ms) {
	auto top = 0;
	for (auto part : _parts) {
		auto height = part->height();
		if (clip.top() < top + height && clip.top() + clip.height() > top) {
			p.translate(0, top);
			part->draw(p, clip.translated(0, -top), selection, ms);
			p.translate(0, -top);
		}
		top += height;
	}
}

bool Item::hasPoint(QPoint point) const {
	auto top = 0;
	for (auto part : _parts) {
		auto height = part->height();
		if (point.y() >= top && point.y() < top + height) {
			return part->hasPoint(point.x(), point.y() - top);
		}
		top += height;
	}
	return false;
}

HistoryTextState Item::getState(QPoint point, HistoryStateRequest request) const {
	auto top = 0;
	for (auto part : _parts) {
		auto height = part->height();
		if (point.y() >= top && point.y() < top + height) {
			return part->getState(point.x(), point.y() - top, request);
		}
		top += height;
	}
	return HistoryTextState();
}

Item::~Item() {
	for (auto part : _parts) {
		part->destroy();
	}
}

} // namespace AdminLog
