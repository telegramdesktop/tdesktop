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
#include "apiwrap.h"

namespace HistoryUnreadThings {
namespace {

[[nodiscard]] Data::HistoryUpdate::Flag UpdateFlag(Type type) {
	using Flag = Data::HistoryUpdate::Flag;
	switch (type) {
	case Type::Mentions: return Flag::UnreadMentions;
	case Type::Reactions: return Flag::UnreadReactions;
	}
	Unexpected("Type in Proxy::addSlice.");
}

} // namespace

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
	const auto had = (list.count() > 0);
	const auto &other = (_type == Type::Mentions)
		? _data->reactions
		: _data->mentions;
	if (!count && other.count(-1) == 0) {
		_data = nullptr;
	} else {
		list.setCount(count);
	}
	const auto has = (count > 0);
	if (has != had) {
		if (_type == Type::Mentions) {
			_history->owner().chatsFilters().refreshHistory(_history);
		}
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
		if (type == AddType::New || !list.contains(msgId)) {
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
		UpdateFlag(_type));
}

void Proxy::clear() {
	if (!_data || !count()) {
		return;
	}
	auto &list = resolveList();
	list.clear();
	setCount(0);
	_history->session().changes().historyUpdated(
		_history,
		UpdateFlag(_type));
}

void Proxy::addSlice(const MTPmessages_Messages &slice, int alreadyLoaded) {
	if (!alreadyLoaded && _data) {
		resolveList().clear();
	}
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
	if (!messages.isEmpty() && !_data) {
		createData();
	}
	auto added = false;
	const auto list = _data ? &resolveList() : nullptr;
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
			list->insert(item->id);
			added = true;
		}
	}
	if (!added) {
		fullCount = loadedCount();
	}
	setCount(fullCount);
	_history->session().changes().historyUpdated(
		_history,
		UpdateFlag(_type));
}

void Proxy::checkAdd(MsgId msgId, bool resolved) {
	Expects(_type == Type::Reactions);

	if (!_data) {
		return;
	}
	auto &list = resolveList();
	if (!list.loadedCount() || list.maxLoaded() <= msgId) {
		return;
	}
	const auto history = _history;
	const auto peer = history->peer;
	const auto item = peer->owner().message(peer, msgId);
	if (item && item->hasUnreadReaction()) {
		item->addToUnreadThings(AddType::Existing);
	} else if (!item && !resolved) {
		peer->session().api().requestMessageData(peer, msgId, [=] {
			history->unreadReactions().checkAdd(msgId, true);
		});
	}
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
