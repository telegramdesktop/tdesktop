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

bool MessageHasCaption(const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return false;
	}
	auto &data = message.c_message();
	auto mediaType = data.has_media() ? data.vmedia.type() : mtpc_messageMediaEmpty;
	return (mediaType == mtpc_messageMediaDocument || mediaType == mtpc_messageMediaPhoto);
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

	auto addSimpleServiceMessage = [this, &idManager, date, fromLink](const QString &text) {
		auto message = HistoryService::PreparedText { text };
		message.links.push_back(fromLink);
		addPart(HistoryService::create(_history, idManager.next(), ::date(date), message, 0, peerToUser(_from->id)));
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

	auto createChangePhoto = [this, addSimpleServiceMessage, fromLinkText](const MTPDchannelAdminLogEventActionChangePhoto &action) {
		auto text = (channel()->isMegagroup() ? lng_admin_log_changed_photo_group : lng_admin_log_changed_photo_channel)(lt_from, fromLinkText);
		addSimpleServiceMessage(text);
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
		auto text = (MessageHasCaption(action.vnew_message) ? lng_admin_log_edited_caption : lng_admin_log_edited_message)(lt_from, fromLinkText);
		addSimpleServiceMessage(text);

		auto applyServiceAction = false;
		auto detachExistingItem = false;
		addPart(_history->createItem(PrepareLogMessage(action.vnew_message, idManager.next(), date.v), applyServiceAction, detachExistingItem));
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
		addPart(HistoryMessage::create(_history, idManager.next(), bodyFlags, bodyReplyTo, bodyViaBotId, ::date(date), peerToUser(_from->id), { "participant invite text", EntitiesInText() }));
	};

	auto createParticipantToggleBan = [this, &idManager, date](const MTPDchannelAdminLogEventActionParticipantToggleBan &action) {
		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		addPart(HistoryMessage::create(_history, idManager.next(), bodyFlags, bodyReplyTo, bodyViaBotId, ::date(date), peerToUser(_from->id), { "participant toggle ban text", EntitiesInText() }));
	};

	auto createParticipantToggleAdmin = [this, &idManager, date](const MTPDchannelAdminLogEventActionParticipantToggleAdmin &action) {
		auto bodyFlags = Flag::f_entities | Flag::f_from_id;
		auto bodyReplyTo = 0;
		auto bodyViaBotId = 0;
		addPart(HistoryMessage::create(_history, idManager.next(), bodyFlags, bodyReplyTo, bodyViaBotId, ::date(date), peerToUser(_from->id), { "participant toggle admin text", EntitiesInText() }));
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

TextWithEntities Item::PrepareText(const QString &value, const QString &emptyValue) {
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

Item::~Item() {
	for (auto part : _parts) {
		part->destroy();
	}
}

} // namespace AdminLog
