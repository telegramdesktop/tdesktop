/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_unread_things.h"

#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace HistoryUnreadThings {

void Proxy::setCount(int count) {
	if (!_known) {
		_history->setUnreadThingsKnown();
	}
	if (!_data) {
		if (!count) {
			return;
		}
		createData();
	}
	auto &list = resolveList();
	const auto loaded = list.loadedCount();
	if (loaded > count) {
		LOG(("API Warning: "
			"real count is greater than received unread count"));
		count = loaded;
	}
	if (!count) {
		const auto &other = (_type == Type::Mentions)
			? _data->reactions
			: _data->mentions;
		if (other.count(-1) == 0) {
			_data = nullptr;
			return;
		}
	}

	const auto had = (list.count() > 0);
	list.setCount(count);
	const auto has = (count > 0);
	if (has != had) {
		_history->owner().chatsFilters().refreshHistory(_history);
		_history->updateChatListEntry();
	}
}

bool Proxy::add(MsgId msgId, AddType type) {
	const auto peer = _history->peer;
	if (peer->isChannel() && !peer->isMegagroup()) {
		return false;
	}

	if (!_data) {
		createData();
	}
	auto &list = resolveList();
	const auto count = list.count();
	const auto loaded = list.loadedCount();
	const auto allLoaded = (count >= 0) && (loaded >= count);
	if (allLoaded) {
		if (type == AddType::New) {
			list.insert(msgId);
			setCount(count + 1);
			return true;
		}
	} else if (loaded > 0 && type != AddType::New) {
		list.insert(msgId);
		return true;
	}
	return false;

}

void Proxy::erase(MsgId msgId) {
	if (!_data) {
		return;
	}
	auto &list = resolveList();
	list.erase(msgId);
	if (const auto count = list.count(); count > 0) {
		setCount(count - 1);
	}
	_history->session().changes().historyUpdated(
		_history,
		Data::HistoryUpdate::Flag::UnreadMentions);
}

void Proxy::addSlice(const MTPmessages_Messages &slice) {
	auto fullCount = slice.match([&](
			const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified! "
			"(Proxy::addSlice)"));
		return 0;
	}, [&](const MTPDmessages_messages &data) {
		return int(data.vmessages().v.size());
	}, [&](const MTPDmessages_messagesSlice &data) {
		return data.vcount().v;
	}, [&](const MTPDmessages_channelMessages &data) {
		if (_history->peer->isChannel()) {
			_history->peer->asChannel()->ptsReceived(data.vpts().v);
		} else {
			LOG(("API Error: received messages.channelMessages when "
				"no channel was passed! (Proxy::addSlice)"));
		}
		return data.vcount().v;
	});

	auto &owner = _history->owner();
	const auto messages = slice.match([&](
			const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified! "
			"(Proxy::addSlice)"));
		return QVector<MTPMessage>();
	}, [&](const auto &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return data.vmessages().v;
	});
	if (messages.isEmpty()) {
		return;
	}

	if (!_data) {
		createData();
	}
	auto added = false;
	auto &list = resolveList();
	const auto localFlags = MessageFlags();
	const auto type = NewMessageType::Existing;
	for (const auto &message : messages) {
		const auto item = _history->addNewMessage(
			IdFromMessage(message),
			message,
			localFlags,
			type);
		const auto is = [&] {
			switch (_type) {
			case Type::Mentions: return item->isUnreadMention();
			case Type::Reactions: return item->hasUnreadReaction();
			}
			Unexpected("Type in Proxy::addSlice.");
		}();
		if (is) {
			list.insert(item->id);
			added = true;
		}
	}
	if (!added) {
		fullCount = list.loadedCount();
	}
	setCount(fullCount);
	const auto flag = [&] {
		using Flag = Data::HistoryUpdate::Flag;
		switch (_type) {
		case Type::Mentions: return Flag::UnreadMentions;
		case Type::Reactions: return Flag::UnreadReactions;
		}
		Unexpected("Type in Proxy::addSlice.");
	}();
	_history->session().changes().historyUpdated(_history, flag);
}

void Proxy::createData() {
	_data = std::make_unique<All>();
	if (_known) {
		_data->mentions.setCount(0);
		_data->reactions.setCount(0);
	}
}

[[nodiscard]] List &Proxy::resolveList() {
	Expects(_data != nullptr);

	switch (_type) {
	case Type::Mentions: return _data->mentions;
	case Type::Reactions: return _data->reactions;
	}
	Unexpected("Unread things type in Proxy::resolveList.");
}

} // namespace HistoryUnreadThings
