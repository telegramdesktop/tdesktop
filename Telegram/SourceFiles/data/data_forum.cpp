/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_forum.h"

#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "data/notify/data_notify_settings.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "main/main_session.h"
#include "base/random.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "core/application.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/input_fields.h"
#include "window/window_session_controller.h"
#include "window/notifications_manager.h"
#include "styles/style_boxes.h"

namespace Data {
namespace {

constexpr auto kTopicsFirstLoad = 20;
constexpr auto kTopicsPerPage = 500;
constexpr auto kGeneralColorId = 0xA9A9A9;

} // namespace

Forum::Forum(not_null<History*> history)
: _history(history)
, _topicsList(&session(), FilterId(0), rpl::single(1)) {
	Expects(_history->peer->isChannel());
}

Forum::~Forum() {
	if (_requestId) {
		session().api().request(_requestId).cancel();
	}
	for (const auto &request : _topicRequests) {
		owner().histories().cancelRequest(request.second.id);
	}
}

Session &Forum::owner() const {
	return _history->owner();
}

Main::Session &Forum::session() const {
	return _history->session();
}

not_null<History*> Forum::history() const {
	return _history;
}

not_null<ChannelData*> Forum::channel() const {
	return _history->peer->asChannel();
}

not_null<Dialogs::MainList*> Forum::topicsList() {
	return &_topicsList;
}

void Forum::unpinTopic() {
	const auto list = _topicsList.pinned();
	while (!list->order().empty()) {
		list->setPinned(list->order().front(), false);
	}
}

rpl::producer<> Forum::destroyed() const {
	return channel()->flagsValue(
	) | rpl::filter([=](const ChannelData::Flags::Change &update) {
		using Flag = ChannelData::Flag;
		return (update.diff & Flag::Forum) && !(update.value & Flag::Forum);
	}) | rpl::take(1) | rpl::to_empty;
}

rpl::producer<not_null<ForumTopic*>> Forum::topicDestroyed() const {
	return _topicDestroyed.events();
}

void Forum::requestTopics() {
	if (_allLoaded || _requestId) {
		return;
	}
	const auto firstLoad = !_offsetDate;
	const auto loadCount = firstLoad ? kTopicsFirstLoad : kTopicsPerPage;
	_requestId = session().api().request(MTPchannels_GetForumTopics(
		MTP_flags(0),
		channel()->inputChannel,
		MTPstring(), // q
		MTP_int(_offsetDate),
		MTP_int(_offsetId),
		MTP_int(_offsetTopicId),
		MTP_int(loadCount)
	)).done([=](const MTPmessages_ForumTopics &result) {
		applyReceivedTopics(result, true);
		_requestId = 0;
		_chatsListChanges.fire({});
		if (_allLoaded) {
			_chatsListLoadedEvents.fire({});
		}
	}).fail([=](const MTP::Error &error) {
		_allLoaded = true;
		_requestId = 0;
		if (error.type() == u"CHANNEL_FORUM_MISSING"_q) {
			const auto flags = channel()->flags() & ~ChannelDataFlag::Forum;
			channel()->setFlags(flags);
		}
	}).send();
}

void Forum::applyReceivedTopics(const MTPmessages_ForumTopics &result) {
	applyReceivedTopics(result, false);
}

void Forum::applyTopicDeleted(MsgId rootId) {
	const auto i = _topics.find(rootId);
	if (i != end(_topics)) {
		const auto raw = i->second.get();
		Core::App().notifications().clearFromTopic(raw);
		owner().removeChatListEntry(raw);
		_topicDestroyed.fire(raw);
		_topics.erase(i);

		_history->destroyMessagesByTopic(rootId);
	}
}

void Forum::applyReceivedTopics(
		const MTPmessages_ForumTopics &topics,
		bool updateOffset) {
	const auto &data = topics.data();
	owner().processUsers(data.vusers());
	owner().processChats(data.vchats());
	owner().processMessages(data.vmessages(), NewMessageType::Existing);
	channel()->ptsReceived(data.vpts().v);
	const auto &list = data.vtopics().v;
	for (const auto &topic : list) {
		const auto rootId = topic.match([&](const auto &data) {
			return data.vid().v;
		});
		topic.match([&](const MTPDforumTopicDeleted &data) {
			if (updateOffset) {
				LOG(("API Error: Got a deleted topic in getForumTopics."));
			}
			applyTopicDeleted(rootId);
		}, [&](const MTPDforumTopic &data) {
			const auto i = _topics.find(rootId);
			const auto creating = (i == end(_topics));
			const auto raw = creating
				? _topics.emplace(
					rootId,
					std::make_unique<ForumTopic>(this, rootId)
				).first->second.get()
				: i->second.get();
			raw->applyTopic(data);
			if (updateOffset) {
				if (const auto last = raw->lastServerMessage()) {
					_offsetDate = last->date();
					_offsetId = last->id;
				}
				_offsetTopicId = rootId;
			}
		});
	}
	if (updateOffset
		&& (list.isEmpty() || list.size() == data.vcount().v)) {
		_allLoaded = true;
	}
}

void Forum::requestTopic(MsgId rootId, Fn<void()> done) {
	auto &request = _topicRequests[rootId];
	if (done) {
		request.callbacks.push_back(std::move(done));
	}
	if (request.id) {
		return;
	}
	const auto call = [=] {
		if (const auto request = _topicRequests.take(rootId)) {
			for (const auto &callback : request->callbacks) {
				callback();
			}
		}
	};
	const auto type = Histories::RequestType::History;
	auto &histories = owner().histories();
	request.id = histories.sendRequest(_history, type, [=](
			Fn<void()> finish) {
		return session().api().request(
			MTPchannels_GetForumTopicsByID(
				channel()->inputChannel,
				MTP_vector<MTPint>(1, MTP_int(rootId.bare)))
		).done([=](const MTPmessages_ForumTopics &result) {
			if (const auto forum = _history->peer->forum()) {
				forum->applyReceivedTopics(result);
				call();
				finish();
			}
		}).fail([=] {
			call();
			finish();
		}).send();
	});
}

ForumTopic *Forum::applyTopicAdded(
		MsgId rootId,
		const QString &title,
		int32 colorId,
		DocumentId iconId) {
	Expects(rootId != 0);

	const auto i = _topics.find(rootId);
	const auto raw = (i != end(_topics))
		? i->second.get()
		: _topics.emplace(
			rootId,
			std::make_unique<ForumTopic>(this, rootId)
		).first->second.get();
	raw->applyTitle(title);
	raw->applyColorId(colorId);
	raw->applyIconId(iconId);
	if (!creating(rootId)) {
		raw->addToChatList(FilterId(), topicsList());
		_chatsListChanges.fire({});
	}
	return raw;
}

MsgId Forum::reserveCreatingId(
		const QString &title,
		int32 colorId,
		DocumentId iconId) {
	const auto result = owner().nextLocalMessageId();
	_creatingRootIds.emplace(result);
	applyTopicAdded(result, title, colorId, iconId);
	return result;
}

void Forum::discardCreatingId(MsgId rootId) {
	Expects(creating(rootId));

	const auto i = _topics.find(rootId);
	if (i != end(_topics)) {
		Assert(!i->second->inChatList());
		_topics.erase(i);
	}
	_creatingRootIds.remove(rootId);
}

bool Forum::creating(MsgId rootId) const {
	return _creatingRootIds.contains(rootId);
}

void Forum::created(MsgId rootId, MsgId realId) {
	if (rootId == realId) {
		return;
	}
	_creatingRootIds.remove(rootId);
	const auto i = _topics.find(rootId);
	Assert(i != end(_topics));
	auto topic = std::move(i->second);
	_topics.erase(i);
	const auto id = FullMsgId(_history->peer->id, realId);
	if (!_topics.contains(realId)) {
		_topics.emplace(
			realId,
			std::move(topic)
		).first->second->setRealRootId(realId);
	}
	owner().notifyItemIdChange({ id, rootId });
}

void Forum::clearAllUnreadMentions() {
	for (const auto &[rootId, topic] : _topics) {
		topic->unreadMentions().clear();
	}
}

void Forum::clearAllUnreadReactions() {
	for (const auto &[rootId, topic] : _topics) {
		topic->unreadReactions().clear();
	}
}

void Forum::enumerateTopics(Fn<void(not_null<ForumTopic*>)> action) const {
	for (const auto &[rootId, topic] : _topics) {
		action(topic.get());
	}
}

ForumTopic *Forum::topicFor(MsgId rootId) {
	if (!rootId) {
		return nullptr;
	}
	const auto i = _topics.find(rootId);
	return (i != end(_topics)) ? i->second.get() : nullptr;
}

ForumTopic *Forum::enforceTopicFor(MsgId rootId) {
	Expects(rootId != 0);

	const auto i = _topics.find(rootId);
	if (i != end(_topics)) {
		return i->second.get();
	}
	const auto result = applyTopicAdded(rootId, {}, {}, {});
	requestTopic(rootId);
	return result;
}

rpl::producer<> Forum::chatsListChanges() const {
	return _chatsListChanges.events();
}

rpl::producer<> Forum::chatsListLoadedEvents() const {
	return _chatsListLoadedEvents.events();
}

} // namespace Data
