/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_forum.h"

#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "base/random.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/input_fields.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"

namespace Data {
namespace {

constexpr auto kTopicsFirstLoad = 20;
constexpr auto kTopicsPerPage = 500;
constexpr auto kGeneralColorId = 0xA9A9A9;

} // namespace

Forum::Forum(not_null<History*> history)
: _history(history)
, _topicsList(&_history->session(), FilterId(0), rpl::single(1)) {
	Expects(_history->peer->isChannel());
}

Forum::~Forum() {
	if (_requestId) {
		_history->session().api().request(_requestId).cancel();
	}
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

void Forum::requestTopics() {
	if (_allLoaded || _requestId) {
		return;
	}
	const auto firstLoad = !_offsetDate;
	const auto loadCount = firstLoad ? kTopicsFirstLoad : kTopicsPerPage;
	const auto api = &_history->session().api();
	_requestId = api->request(MTPchannels_GetForumTopics(
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
	}).send();
}

void Forum::applyReceivedTopics(const MTPmessages_ForumTopics &result) {
	applyReceivedTopics(result, false);
}

void Forum::applyReceivedTopics(
		const MTPmessages_ForumTopics &topics,
		bool updateOffset) {
	const auto &data = topics.data();
	const auto owner = &channel()->owner();
	owner->processUsers(data.vusers());
	owner->processChats(data.vchats());
	owner->processMessages(data.vmessages(), NewMessageType::Existing);
	channel()->ptsReceived(data.vpts().v);
	const auto &list = data.vtopics().v;
	for (const auto &topic : list) {
		const auto rootId = MsgId(topic.data().vid().v);
		const auto i = _topics.find(rootId);
		const auto creating = (i == end(_topics));
		const auto raw = creating
			? _topics.emplace(
				rootId,
				std::make_unique<ForumTopic>(_history, rootId)
			).first->second.get()
			: i->second.get();
		raw->applyTopic(topic);
		if (updateOffset) {
			if (const auto last = raw->lastServerMessage()) {
				_offsetDate = last->date();
				_offsetId = last->id;
			}
			_offsetTopicId = rootId;
		}
	}
	if (updateOffset
		&& (list.isEmpty() || list.size() == data.vcount().v)) {
		_allLoaded = true;
	}
}

void Forum::applyTopicAdded(
		MsgId rootId,
		const QString &title,
		int32 colorId,
		DocumentId iconId) {
	const auto i = _topics.find(rootId);
	const auto raw = (i != end(_topics))
		? i->second.get()
		: _topics.emplace(
			rootId,
			std::make_unique<ForumTopic>(_history, rootId)
		).first->second.get();
	raw->applyTitle(title);
	raw->applyColorId(colorId);
	raw->applyIconId(iconId);
	if (!creating(rootId)) {
		raw->addToChatList(FilterId(), topicsList());
		_chatsListChanges.fire({});
	}
}

void Forum::applyTopicRemoved(MsgId rootId) {
	//if (const auto i = _topics.find(rootId)) {
	//	_topics.erase(i);
	//}
}

MsgId Forum::reserveCreatingId(
		const QString &title,
		int32 colorId,
		DocumentId iconId) {
	const auto result = _history->owner().nextLocalMessageId();
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
	_history->owner().notifyItemIdChange({ id, rootId });
}

ForumTopic *Forum::topicFor(not_null<HistoryItem*> item) {
	const auto maybe = topicFor(item->replyToTop());
	return maybe ? maybe : topicFor(item->topicRootId());
}

ForumTopic *Forum::topicFor(MsgId rootId) {
	if (rootId != ForumTopic::kGeneralId) {
		if (const auto i = _topics.find(rootId); i != end(_topics)) {
			return i->second.get();
		}
	} else {
		// #TODO lang-forum
		applyTopicAdded(rootId, "General! Created.", kGeneralColorId, 0);
		return _topics.find(rootId)->second.get();
	}
	return nullptr;
}

rpl::producer<> Forum::chatsListChanges() const {
	return _chatsListChanges.events();
}

rpl::producer<> Forum::chatsListLoadedEvents() const {
	return _chatsListLoadedEvents.events();
}

} // namespace Data
