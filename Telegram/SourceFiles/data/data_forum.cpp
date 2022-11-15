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
#include "data/data_forum_icons.h"
#include "data/data_forum_topic.h"
#include "data/notify/data_notify_settings.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "main/main_session.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "core/application.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/input_fields.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "window/window_session_controller.h"
#include "window/notifications_manager.h"
#include "styles/style_boxes.h"

namespace Data {
namespace {

constexpr auto kTopicsFirstLoad = 20;
constexpr auto kLoadedTopicsMinCount = 20;
constexpr auto kTopicsPerPage = 500;
constexpr auto kStalePerRequest = 100;
constexpr auto kShowTopicNamesCount = 8;
// constexpr auto kGeneralColorId = 0xA9A9A9;

} // namespace

Forum::Forum(not_null<History*> history)
: _history(history)
, _topicsList(&session(), {}, owner().maxPinnedChatsLimitValue(this)) {
	Expects(_history->peer->isChannel());


	if (_history->inChatList()) {
		preloadTopics();
	}
	if (channel()->canCreateTopics()) {
		owner().forumIcons().requestDefaultIfUnknown();
	}
}

Forum::~Forum() {
	for (const auto &request : _topicRequests) {
		if (request.second.id != _staleRequestId) {
			owner().histories().cancelRequest(request.second.id);
		}
	}
	if (_staleRequestId) {
		session().api().request(_staleRequestId).cancel();
	}
	if (_requestId) {
		session().api().request(_requestId).cancel();
	}
	const auto peerId = _history->peer->id;
	for (const auto &[rootId, topic] : _topics) {
		session().storage().unload(Storage::SharedMediaUnloadThread(
			peerId,
			rootId));
		_history->setForwardDraft(rootId, {});
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

void Forum::preloadTopics() {
	if (topicsList()->indexed()->size() < kLoadedTopicsMinCount) {
		requestTopics();
	}
}

void Forum::reloadTopics() {
	_topicsList.setLoaded(false);
	session().api().request(base::take(_requestId)).cancel();
	_offset = {};
	for (const auto &[rootId, topic] : _topics) {
		if (!topic->creating()) {
			_staleRootIds.emplace(topic->rootId());
		}
	}
	requestTopics();
}

void Forum::requestTopics() {
	if (_topicsList.loaded() || _requestId) {
		return;
	}
	const auto firstLoad = !_offset.date;
	const auto loadCount = firstLoad ? kTopicsFirstLoad : kTopicsPerPage;
	_requestId = session().api().request(MTPchannels_GetForumTopics(
		MTP_flags(0),
		channel()->inputChannel,
		MTPstring(), // q
		MTP_int(_offset.date),
		MTP_int(_offset.id),
		MTP_int(_offset.topicId),
		MTP_int(loadCount)
	)).done([=](const MTPmessages_ForumTopics &result) {
		const auto previousOffset = _offset;
		applyReceivedTopics(result, _offset);
		const auto &list = result.data().vtopics().v;
		if (list.isEmpty()
			|| list.size() == result.data().vcount().v
			|| (_offset == previousOffset)) {
			_topicsList.setLoaded();
		}
		_requestId = 0;
		_chatsListChanges.fire({});
		if (_topicsList.loaded()) {
			_chatsListLoadedEvents.fire({});
		}
		reorderLastTopics();
		requestSomeStale();
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		_topicsList.setLoaded();
		if (error.type() == u"CHANNEL_FORUM_MISSING"_q) {
			const auto flags = channel()->flags() & ~ChannelDataFlag::Forum;
			channel()->setFlags(flags);
		}
	}).send();
}

void Forum::applyTopicDeleted(MsgId rootId) {
	_topicsDeleted.emplace(rootId);

	const auto i = _topics.find(rootId);
	if (i != end(_topics)) {
		const auto raw = i->second.get();
		Core::App().notifications().clearFromTopic(raw);
		owner().removeChatListEntry(raw);

		if (ranges::contains(_lastTopics, not_null(raw))) {
			reorderLastTopics();
		}

		_topicDestroyed.fire(raw);
		_topics.erase(i);

		_history->destroyMessagesByTopic(rootId);
		session().storage().unload(Storage::SharedMediaUnloadThread(
			_history->peer->id,
			rootId));
		_history->setForwardDraft(rootId, {});
	}
}

void Forum::reorderLastTopics() {
	// We want first kShowChatNamesCount histories, by last message date.
	const auto pred = [](not_null<ForumTopic*> a, not_null<ForumTopic*> b) {
		const auto aItem = a->chatListMessage();
		const auto bItem = b->chatListMessage();
		const auto aDate = aItem ? aItem->date() : TimeId(0);
		const auto bDate = bItem ? bItem->date() : TimeId(0);
		return aDate > bDate;
	};
	_lastTopics.clear();
	_lastTopics.reserve(kShowTopicNamesCount + 1);
	auto &&topics = ranges::views::all(
		*_topicsList.indexed()
	) | ranges::views::transform([](not_null<Dialogs::Row*> row) {
		return row->topic();
	});
	auto nonPinnedChecked = 0;
	for (const auto topic : topics) {
		const auto i = ranges::upper_bound(
			_lastTopics,
			not_null(topic),
			pred);
		if (size(_lastTopics) < kShowTopicNamesCount
			|| i != end(_lastTopics)) {
			_lastTopics.insert(i, topic);
		}
		if (size(_lastTopics) > kShowTopicNamesCount) {
			_lastTopics.pop_back();
		}
		if (!topic->isPinnedDialog(FilterId())
			&& ++nonPinnedChecked >= kShowTopicNamesCount) {
			break;
		}
	}
	++_lastTopicsVersion;
	_history->updateChatListEntry();
}

int Forum::recentTopicsListVersion() const {
	return _lastTopicsVersion;
}

void Forum::recentTopicsInvalidate(not_null<ForumTopic*> topic) {
	if (ranges::contains(_lastTopics, topic)) {
		++_lastTopicsVersion;
		_history->updateChatListEntry();
	}
}

const std::vector<not_null<ForumTopic*>> &Forum::recentTopics() const {
	return _lastTopics;
}

void Forum::listMessageChanged(HistoryItem *from, HistoryItem *to) {
	if (from || to) {
		reorderLastTopics();
	}
}

void Forum::applyReceivedTopics(
		const MTPmessages_ForumTopics &topics,
		ForumOffsets &updateOffsets) {
	applyReceivedTopics(topics, [&](not_null<ForumTopic*> topic) {
		if (const auto last = topic->lastServerMessage()) {
			updateOffsets.date = last->date();
			updateOffsets.id = last->id;
		}
		updateOffsets.topicId = topic->rootId();
	});
}

void Forum::applyReceivedTopics(
		const MTPmessages_ForumTopics &topics,
		Fn<void(not_null<ForumTopic*>)> callback) {
	const auto &data = topics.data();
	owner().processUsers(data.vusers());
	owner().processChats(data.vchats());
	owner().processMessages(data.vmessages(), NewMessageType::Existing);
	channel()->ptsReceived(data.vpts().v);
	applyReceivedTopics(data.vtopics(), std::move(callback));
	if (!_staleRootIds.empty()) {
		requestSomeStale();
	}
}

void Forum::applyReceivedTopics(
		const MTPVector<MTPForumTopic> &topics,
		Fn<void(not_null<ForumTopic*>)> callback) {
	const auto &list = topics.v;
	for (const auto &topic : list) {
		const auto rootId = topic.match([&](const auto &data) {
			return data.vid().v;
		});
		_staleRootIds.remove(rootId);
		topic.match([&](const MTPDforumTopicDeleted &data) {
			applyTopicDeleted(rootId);
		}, [&](const MTPDforumTopic &data) {
			_topicsDeleted.remove(rootId);
			const auto i = _topics.find(rootId);
			const auto creating = (i == end(_topics));
			const auto raw = creating
				? _topics.emplace(
					rootId,
					std::make_unique<ForumTopic>(this, rootId)
				).first->second.get()
				: i->second.get();
			raw->applyTopic(data);
			if (creating) {
				if (const auto last = _history->chatListMessage()
					; last && last->topicRootId() == rootId) {
					_history->lastItemDialogsView().itemInvalidated(last);
					_history->updateChatListEntry();
				}
			}
			if (callback) {
				callback(raw);
			}
		});
	}
}

void Forum::requestSomeStale() {
	if (_staleRequestId
		|| (!_offset.id && _requestId)
		|| _staleRootIds.empty()) {
		return;
	}
	const auto type = Histories::RequestType::History;
	auto rootIds = QVector<MTPint>();
	rootIds.reserve(std::min(int(_staleRootIds.size()), kStalePerRequest));
	for (auto i = begin(_staleRootIds); i != end(_staleRootIds);) {
		const auto rootId = *i;
		i = _staleRootIds.erase(i);

		rootIds.push_back(MTP_int(rootId));
		if (rootIds.size() == kStalePerRequest) {
			break;
		}
	}
	if (rootIds.empty()) {
		return;
	}
	const auto call = [=] {
		for (const auto &id : rootIds) {
			finishTopicRequest(id.v);
		}
	};
	auto &histories = owner().histories();
	_staleRequestId = histories.sendRequest(_history, type, [=](
			Fn<void()> finish) {
		return session().api().request(
			MTPchannels_GetForumTopicsByID(
				channel()->inputChannel,
				MTP_vector<MTPint>(rootIds))
		).done([=](const MTPmessages_ForumTopics &result) {
			_staleRequestId = 0;
			applyReceivedTopics(result);
			call();
			finish();
		}).fail([=] {
			_staleRequestId = 0;
			call();
			finish();
		}).send();
	});
	for (const auto &id : rootIds) {
		_topicRequests[id.v].id = _staleRequestId;
	}
}

void Forum::finishTopicRequest(MsgId rootId) {
	if (const auto request = _topicRequests.take(rootId)) {
		for (const auto &callback : request->callbacks) {
			callback();
		}
	}
}

void Forum::requestTopic(MsgId rootId, Fn<void()> done) {
	auto &request = _topicRequests[rootId];
	if (done) {
		request.callbacks.push_back(std::move(done));
	}
	if (!request.id
		&& _staleRootIds.emplace(rootId).second
		&& (_staleRootIds.size() == 1)) {
		crl::on_main(&session(), [peer = channel()] {
			if (const auto forum = peer->forum()) {
				forum->requestSomeStale();
			}
		});
	}
}

ForumTopic *Forum::applyTopicAdded(
		MsgId rootId,
		const QString &title,
		int32 colorId,
		DocumentId iconId,
		PeerId creatorId,
		TimeId date,
		bool my) {
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
	raw->applyCreator(creatorId);
	raw->applyCreationDate(date);
	raw->applyIsMy(my);
	if (!creating(rootId)) {
		raw->addToChatList(FilterId(), topicsList());
		_chatsListChanges.fire({});
		reorderLastTopics();
	}
	return raw;
}

MsgId Forum::reserveCreatingId(
		const QString &title,
		int32 colorId,
		DocumentId iconId) {
	const auto result = owner().nextLocalMessageId();
	_creatingRootIds.emplace(result);
	applyTopicAdded(
		result,
		title,
		colorId,
		iconId,
		session().userPeerId(),
		base::unixtime::now(),
		true);
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

		reorderLastTopics();
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
	requestTopic(rootId);
	return applyTopicAdded(rootId, {}, {}, {}, {}, {}, {});
}

bool Forum::topicDeleted(MsgId rootId) const {
	return _topicsDeleted.contains(rootId);
}

rpl::producer<> Forum::chatsListChanges() const {
	return _chatsListChanges.events();
}

rpl::producer<> Forum::chatsListLoadedEvents() const {
	return _chatsListLoadedEvents.events();
}

} // namespace Data
