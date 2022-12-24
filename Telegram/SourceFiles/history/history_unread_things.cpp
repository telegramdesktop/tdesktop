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
#include "data/data_forum_topic.h"
#include "data/data_chat_filters.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace HistoryUnreadThings {
namespace {

template <typename Update>
[[nodiscard]] typename Update::Flag UpdateFlag(Type type) {
	using Flag = typename Update::Flag;
	switch (type) {
	case Type::Mentions: return Flag::UnreadMentions;
	case Type::Reactions: return Flag::UnreadReactions;
	}
	Unexpected("Type in HistoryUnreadThings::UpdateFlag.");
}

[[nodiscard]] Data::HistoryUpdate::Flag HistoryUpdateFlag(Type type) {
	return UpdateFlag<Data::HistoryUpdate>(type);
}

[[nodiscard]] Data::TopicUpdate::Flag TopicUpdateFlag(Type type) {
	return UpdateFlag<Data::TopicUpdate>(type);
}

} // namespace

void Proxy::setCount(int count) {
	if (!_known) {
		_thread->setUnreadThingsKnown();
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
	if (has != had && _thread->inChatList()) {
		if (_type == Type::Mentions) {
			_thread->hasUnreadMentionChanged(has);
		} else if (_type == Type::Reactions) {
			_thread->hasUnreadReactionChanged(has);
		}
	}
}

bool Proxy::add(MsgId msgId, AddType type) {
	if (const auto history = _thread->asHistory()) {
		if (history->peer->isBroadcast()) {
			return false;
		}
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
	notifyUpdated();
}

void Proxy::clear() {
	if (!_data || !count()) {
		return;
	}
	auto &list = resolveList();
	list.clear();
	setCount(0);
	notifyUpdated();
}

void Proxy::addSlice(const MTPmessages_Messages &slice, int alreadyLoaded) {
	if (!alreadyLoaded && _data) {
		resolveList().clear();
	}
	const auto history = _thread->owningHistory();
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
		if (const auto channel = history->peer->asChannel()) {
			channel->ptsReceived(data.vpts().v);
		} else {
			LOG(("API Error: received messages.channelMessages when "
				"no channel was passed! (Proxy::addSlice)"));
		}
		return data.vcount().v;
	});

	auto &owner = _thread->owner();
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
		const auto item = history->addNewMessage(
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
	notifyUpdated();
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
	const auto history = _thread->owningHistory();
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

void Proxy::notifyUpdated() {
	if (const auto history = _thread->asHistory()) {
		history->session().changes().historyUpdated(
			history,
			HistoryUpdateFlag(_type));
	} else if (const auto topic = _thread->asTopic()) {
		topic->session().changes().topicUpdated(
			topic,
			TopicUpdateFlag(_type));
	}
}

void Proxy::createData() {
	_data = std::make_unique<All>();
	if (_known) {
		_data->mentions.setCount(0);
		_data->reactions.setCount(0);
	}
}

List &Proxy::resolveList() {
	Expects(_data != nullptr);

	switch (_type) {
	case Type::Mentions: return _data->mentions;
	case Type::Reactions: return _data->reactions;
	}
	Unexpected("Unread things type in Proxy::resolveList.");
}

} // namespace HistoryUnreadThings
