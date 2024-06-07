/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_replies_list.h"

#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "main/main_session.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_messages.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "window/notifications_manager.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kMessagesPerPage = 50;
constexpr auto kReadRequestTimeout = 3 * crl::time(1000);
constexpr auto kMaxMessagesToDeleteMyTopic = 10;

[[nodiscard]] HistoryItem *GenerateDivider(
		not_null<History*> history,
		TimeId date,
		const QString &text) {
	return history->makeMessage({
		.id = history->nextNonHistoryEntryId(),
		.flags = MessageFlag::FakeHistoryItem,
		.date = date,
	}, PreparedServiceText{ { .text = text } });
}

[[nodiscard]] bool IsCreating(not_null<History*> history, MsgId rootId) {
	if (const auto forum = history->asForum()) {
		return forum->creating(rootId);
	}
	return false;
}

} // namespace

struct RepliesList::Viewer {
	MessagesSlice slice;
	MsgId around = 0;
	int limitBefore = 0;
	int limitAfter = 0;
	int injectedForRoot = 0;
	base::has_weak_ptr guard;
	bool scheduled = false;
};

RepliesList::RepliesList(
	not_null<History*> history,
	MsgId rootId,
	ForumTopic *owningTopic)
: _history(history)
, _owningTopic(owningTopic)
, _rootId(rootId)
, _creating(IsCreating(history, rootId))
, _readRequestTimer([=] { sendReadTillRequest(); }) {
	if (_owningTopic) {
		_owningTopic->destroyed(
		) | rpl::start_with_next([=] {
			_owningTopic = nullptr;
			subscribeToUpdates();
		}, _lifetime);
	} else {
		subscribeToUpdates();
	}
}

RepliesList::~RepliesList() {
	histories().cancelRequest(base::take(_beforeId));
	histories().cancelRequest(base::take(_afterId));
	if (_readRequestTimer.isActive()) {
		sendReadTillRequest();
	}
	if (_divider) {
		_divider->destroy();
	}
}

void RepliesList::subscribeToUpdates() {
	_history->owner().repliesReadTillUpdates(
	) | rpl::filter([=](const RepliesReadTillUpdate &update) {
		return (update.id.msg == _rootId)
			&& (update.id.peer == _history->peer->id);
	}) | rpl::start_with_next([=](const RepliesReadTillUpdate &update) {
		apply(update);
	}, _lifetime);

	_history->session().changes().messageUpdates(
		MessageUpdate::Flag::NewAdded
		| MessageUpdate::Flag::NewMaybeAdded
		| MessageUpdate::Flag::ReplyToTopAdded
		| MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const MessageUpdate &update) {
		apply(update);
	}, _lifetime);

	_history->session().changes().topicUpdates(
		TopicUpdate::Flag::Creator
	) | rpl::start_with_next([=](const TopicUpdate &update) {
		apply(update);
	}, _lifetime);

	_history->owner().channelDifferenceTooLong(
	) | rpl::start_with_next([=](not_null<ChannelData*> channel) {
		if (channel == _history->peer) {
			applyDifferenceTooLong();
		}
	}, _lifetime);
}

void RepliesList::apply(const RepliesReadTillUpdate &update) {
	if (update.out) {
		setOutboxReadTill(update.readTillId);
	} else if (update.readTillId >= _inboxReadTillId) {
		setInboxReadTill(
			update.readTillId,
			computeUnreadCountLocally(update.readTillId));
	}
}

void RepliesList::apply(const MessageUpdate &update) {
	if (applyUpdate(update)) {
		_instantChanges.fire({});
	}
}

void RepliesList::apply(const TopicUpdate &update) {
	if (update.topic->history() == _history
		&& update.topic->rootId() == _rootId) {
		if (update.flags & TopicUpdate::Flag::Creator) {
			applyTopicCreator(update.topic->creatorId());
		}
	}
}

void RepliesList::applyTopicCreator(PeerId creatorId) {
	const auto owner = &_history->owner();
	const auto peerId = _history->peer->id;
	for (const auto &id : _list) {
		if (const auto item = owner->message(peerId, id)) {
			if (item->from()->id == creatorId) {
				owner->requestItemResize(item);
			}
		}
	}
}

rpl::producer<MessagesSlice> RepliesList::source(
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto around = aroundId.fullId.msg;
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto viewer = lifetime.make_state<Viewer>();
		const auto push = [=] {
			if (viewer->scheduled) {
				viewer->scheduled = false;
				if (buildFromData(viewer)) {
					appendClientSideMessages(viewer->slice);
					consumer.put_next_copy(viewer->slice);
				}
			}
		};
		const auto pushInstant = [=] {
			viewer->scheduled = true;
			push();
		};
		const auto pushDelayed = [=] {
			if (!viewer->scheduled) {
				viewer->scheduled = true;
				crl::on_main(&viewer->guard, push);
			}
		};
		viewer->around = around;
		viewer->limitBefore = limitBefore;
		viewer->limitAfter = limitAfter;

		_history->session().changes().historyUpdates(
			_history,
			HistoryUpdate::Flag::ClientSideMessages
		) | rpl::start_with_next(pushDelayed, lifetime);

		_history->session().changes().messageUpdates(
			MessageUpdate::Flag::Destroyed
		) | rpl::filter([=](const MessageUpdate &update) {
			return applyItemDestroyed(viewer, update.item);
		}) | rpl::start_with_next(pushDelayed, lifetime);

		_listChanges.events(
		) | rpl::start_with_next(pushDelayed, lifetime);

		_instantChanges.events(
		) | rpl::start_with_next(pushInstant, lifetime);

		pushInstant();
		return lifetime;
	};
}

void RepliesList::appendClientSideMessages(MessagesSlice &slice) {
	const auto &messages = _history->clientSideMessages();
	if (messages.empty()) {
		return;
	} else if (slice.ids.empty()) {
		if (slice.skippedBefore != 0 || slice.skippedAfter != 0) {
			return;
		}
		slice.ids.reserve(messages.size());
		for (const auto &item : messages) {
			if (!item->inThread(_rootId)) {
				continue;
			}
			slice.ids.push_back(item->fullId());
		}
		ranges::sort(slice.ids);
		return;
	}
	auto &owner = _history->owner();
	auto dates = std::vector<TimeId>();
	dates.reserve(slice.ids.size());
	for (const auto &id : slice.ids) {
		const auto message = owner.message(id);
		Assert(message != nullptr);

		dates.push_back(message->date());
	}
	for (const auto &item : messages) {
		if (!item->inThread(_rootId)) {
			continue;
		}
		const auto date = item->date();
		if (date < dates.front()) {
			if (slice.skippedBefore != 0) {
				if (slice.skippedBefore) {
					++*slice.skippedBefore;
				}
				continue;
			}
			dates.insert(dates.begin(), date);
			slice.ids.insert(slice.ids.begin(), item->fullId());
		} else {
			auto to = dates.size();
			for (; to != 0; --to) {
				const auto checkId = slice.ids[to - 1].msg;
				if (dates[to - 1] > date) {
					continue;
				} else if (dates[to - 1] < date
					|| IsServerMsgId(checkId)
					|| checkId < item->id) {
					break;
				}
			}
			dates.insert(dates.begin() + to, date);
			slice.ids.insert(slice.ids.begin() + to, item->fullId());
		}
	}
}

rpl::producer<int> RepliesList::fullCount() const {
	return _fullCount.value() | rpl::filter_optional();
}

rpl::producer<std::optional<int>> RepliesList::maybeFullCount() const {
	return _fullCount.value();
}

bool RepliesList::unreadCountKnown() const {
	return _unreadCount.current().has_value();
}

int RepliesList::unreadCountCurrent() const {
	return _unreadCount.current().value_or(0);
}

rpl::producer<std::optional<int>> RepliesList::unreadCountValue() const {
	return _unreadCount.value();
}

void RepliesList::injectRootMessageAndReverse(not_null<Viewer*> viewer) {
	injectRootMessage(viewer);
	ranges::reverse(viewer->slice.ids);
}

void RepliesList::injectRootMessage(not_null<Viewer*> viewer) {
	const auto slice = &viewer->slice;
	viewer->injectedForRoot = 0;
	if (slice->skippedBefore != 0) {
		return;
	}
	const auto root = lookupRoot();
	if (!root
		|| (_rootId == Data::ForumTopic::kGeneralId)
		|| (root->topicRootId() != Data::ForumTopic::kGeneralId)) {
		return;
	}
	injectRootDivider(root, slice);

	if (const auto group = _history->owner().groups().find(root)) {
		for (const auto &item : ranges::views::reverse(group->items)) {
			slice->ids.push_back(item->fullId());
		}
		viewer->injectedForRoot = group->items.size();
		if (slice->fullCount) {
			*slice->fullCount += group->items.size();
		}
	} else {
		slice->ids.push_back(root->fullId());
		viewer->injectedForRoot = 1;
	}
	if (slice->fullCount) {
		*slice->fullCount += viewer->injectedForRoot;
	}
}

void RepliesList::injectRootDivider(
		not_null<HistoryItem*> root,
		not_null<MessagesSlice*> slice) {
	const auto withComments = !slice->ids.empty();
	const auto text = [&] {
		return withComments
			? tr::lng_replies_discussion_started(tr::now)
			: tr::lng_replies_no_comments(tr::now);
	};
	if (!_divider) {
		_dividerWithComments = withComments;
		_divider = GenerateDivider(
			_history,
			root->date(),
			text());
	} else if (_dividerWithComments != withComments) {
		_dividerWithComments = withComments;
		_divider->updateServiceText(PreparedServiceText{ { text() } });
	}
	slice->ids.push_back(_divider->fullId());
}

bool RepliesList::buildFromData(not_null<Viewer*> viewer) {
	if (_creating
		|| (_list.empty() && _skippedBefore == 0 && _skippedAfter == 0)) {
		viewer->slice.ids.clear();
		viewer->slice.nearestToAround = FullMsgId();
		viewer->slice.fullCount
			= viewer->slice.skippedBefore
			= viewer->slice.skippedAfter
			= 0;
		viewer->injectedForRoot = 0;
		injectRootMessageAndReverse(viewer);
		return true;
	}
	const auto around = [&] {
		if (viewer->around != ShowAtUnreadMsgId) {
			return viewer->around;
		} else if (const auto item = lookupRoot()) {
			return computeInboxReadTillFull();
		} else if (_owningTopic) {
			// Somehow we don't want always to jump to computed inboxReadTill
			// (this was in the code before, but I don't remember why).
			// Maybe in case we "View Thread" from a group we don't really
			// want to jump to unread inside thread, cause it isn't defined.
			//
			// But in case of topics we definitely want to support jumping
			// to the first unread, even if it is General topic without the
			// actual root message or it is a broken topic without root.
			return computeInboxReadTillFull();
		}
		return viewer->around;
	}();
	if (_list.empty()
		|| (!around && _skippedAfter != 0)
		|| (around > _list.front() && _skippedAfter != 0)
		|| (around > 0 && around < _list.back() && _skippedBefore != 0)) {
		loadAround(around);
		return false;
	}
	const auto i = around
		? ranges::lower_bound(_list, around, std::greater<>())
		: end(_list);
	const auto availableBefore = int(end(_list) - i);
	const auto availableAfter = int(i - begin(_list));
	const auto useBefore = std::min(availableBefore, viewer->limitBefore + 1);
	const auto useAfter = std::min(availableAfter, viewer->limitAfter);
	const auto slice = &viewer->slice;
	if (_skippedBefore.has_value()) {
		slice->skippedBefore
			= (*_skippedBefore + (availableBefore - useBefore));
	}
	if (_skippedAfter.has_value()) {
		slice->skippedAfter
			= (*_skippedAfter + (availableAfter - useAfter));
	}

	const auto peerId = _history->peer->id;
	slice->ids.clear();
	auto nearestToAround = std::optional<MsgId>();
	slice->ids.reserve(useAfter + useBefore);
	for (auto j = i - useAfter, e = i + useBefore; j != e; ++j) {
		const auto id = *j;
		if (id == _rootId) {
			continue;
		} else if (!nearestToAround && id < around) {
			nearestToAround = (j == i - useAfter)
				? id
				: *(j - 1);
		}
		slice->ids.emplace_back(peerId, id);
	}
	slice->nearestToAround = FullMsgId(
		peerId,
		nearestToAround.value_or(
			slice->ids.empty() ? 0 : slice->ids.back().msg));
	slice->fullCount = _fullCount.current();

	injectRootMessageAndReverse(viewer);

	if (_skippedBefore != 0 && useBefore < viewer->limitBefore + 1) {
		loadBefore();
	}
	if (_skippedAfter != 0 && useAfter < viewer->limitAfter) {
		loadAfter();
	}

	return true;
}

bool RepliesList::applyItemDestroyed(
		not_null<Viewer*> viewer,
		not_null<HistoryItem*> item) {
	if (item->history() != _history || !item->isRegular()) {
		return false;
	}
	const auto fullId = item->fullId();
	for (auto i = 0; i != viewer->injectedForRoot; ++i) {
		if (viewer->slice.ids[i] == fullId) {
			return true;
		}
	}
	return false;
}

bool RepliesList::applyUpdate(const MessageUpdate &update) {
	using Flag = MessageUpdate::Flag;

	if (update.item->history() != _history
		|| !update.item->isRegular()
		|| !update.item->inThread(_rootId)) {
		return false;
	}
	const auto id = update.item->id;
	const auto added = (update.flags & Flag::ReplyToTopAdded);
	const auto i = ranges::lower_bound(_list, id, std::greater<>());
	if (update.flags & Flag::Destroyed) {
		if (!added) {
			changeUnreadCountByPost(id, -1);
		}
		if (i == end(_list) || *i != id) {
			return false;
		}
		_list.erase(i);
		if (_skippedBefore && _skippedAfter) {
			_fullCount = *_skippedBefore + _list.size() + *_skippedAfter;
		} else if (const auto known = _fullCount.current()) {
			if (*known > 0) {
				_fullCount = (*known - 1);
			}
		}
		return true;
	}
	if (added) {
		changeUnreadCountByPost(id, 1);
	}
	if (_skippedAfter != 0
		|| (i != end(_list) && *i == id)) {
		return false;
	}
	_list.insert(i, id);
	if (_skippedBefore && _skippedAfter) {
		_fullCount = *_skippedBefore + _list.size() + *_skippedAfter;
	} else if (const auto known = _fullCount.current()) {
		_fullCount = *known + 1;
	}
	return true;
}

void RepliesList::applyDifferenceTooLong() {
	if (!_creating && _skippedAfter.has_value()) {
		_skippedAfter = std::nullopt;
		_listChanges.fire({});
	}
}

void RepliesList::changeUnreadCountByPost(MsgId id, int delta) {
	if (!_inboxReadTillId) {
		setUnreadCount(std::nullopt);
		return;
	}
	const auto count = _unreadCount.current();
	if (count.has_value() && (id > _inboxReadTillId)) {
		setUnreadCount(std::max(*count + delta, 0));
	}
}

Histories &RepliesList::histories() {
	return _history->owner().histories();
}

HistoryItem *RepliesList::lookupRoot() {
	return _history->owner().message(_history->peer->id, _rootId);
}

void RepliesList::loadAround(MsgId id) {
	Expects(!_creating);

	if (_loadingAround && *_loadingAround == id) {
		return;
	}
	histories().cancelRequest(base::take(_beforeId));
	histories().cancelRequest(base::take(_afterId));

	const auto send = [=](Fn<void()> finish) {
		return _history->session().api().request(MTPmessages_GetReplies(
			_history->peer->input,
			MTP_int(_rootId),
			MTP_int(id), // offset_id
			MTP_int(0), // offset_date
			MTP_int(id ? (-kMessagesPerPage / 2) : 0), // add_offset
			MTP_int(kMessagesPerPage), // limit
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			_beforeId = 0;
			_loadingAround = std::nullopt;
			finish();

			if (!id) {
				_skippedAfter = 0;
			} else {
				_skippedAfter = std::nullopt;
			}
			_skippedBefore = std::nullopt;
			_list.clear();
			if (processMessagesIsEmpty(result)) {
				_fullCount = _skippedBefore = _skippedAfter = 0;
			} else if (id) {
				Assert(!_list.empty());
				if (_list.front() <= id) {
					_skippedAfter = 0;
				} else if (_list.back() >= id) {
					_skippedBefore = 0;
				}
			}
			checkReadTillEnd();
		}).fail([=] {
			_beforeId = 0;
			_loadingAround = std::nullopt;
			finish();
		}).send();
	};
	_loadingAround = id;
	_beforeId = histories().sendRequest(
		_history,
		Histories::RequestType::History,
		send);
}

void RepliesList::loadBefore() {
	Expects(!_list.empty());

	if (_loadingAround) {
		histories().cancelRequest(base::take(_beforeId));
	} else if (_beforeId) {
		return;
	}

	const auto last = _list.back();
	const auto send = [=](Fn<void()> finish) {
		return _history->session().api().request(MTPmessages_GetReplies(
			_history->peer->input,
			MTP_int(_rootId),
			MTP_int(last), // offset_id
			MTP_int(0), // offset_date
			MTP_int(0), // add_offset
			MTP_int(kMessagesPerPage), // limit
			MTP_int(0), // min_id
			MTP_int(0), // max_id
			MTP_long(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			_beforeId = 0;
			finish();

			if (_list.empty()) {
				return;
			} else if (_list.back() != last) {
				loadBefore();
			} else if (processMessagesIsEmpty(result)) {
				_skippedBefore = 0;
				if (_skippedAfter == 0) {
					_fullCount = _list.size();
				}
			}
		}).fail([=] {
			_beforeId = 0;
			finish();
		}).send();
	};
	_beforeId = histories().sendRequest(
		_history,
		Histories::RequestType::History,
		send);
}

void RepliesList::loadAfter() {
	Expects(!_list.empty());

	if (_afterId) {
		return;
	}

	const auto first = _list.front();
	const auto send = [=](Fn<void()> finish) {
		return _history->session().api().request(MTPmessages_GetReplies(
			_history->peer->input,
			MTP_int(_rootId),
			MTP_int(first + 1), // offset_id
			MTP_int(0), // offset_date
			MTP_int(-kMessagesPerPage), // add_offset
			MTP_int(kMessagesPerPage), // limit
			MTP_int(0), // min_id
			MTP_int(0), // max_id
			MTP_long(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			_afterId = 0;
			finish();

			if (_list.empty()) {
				return;
			} else if (_list.front() != first) {
				loadAfter();
			} else if (processMessagesIsEmpty(result)) {
				_skippedAfter = 0;
				if (_skippedBefore == 0) {
					_fullCount = _list.size();
				}
				checkReadTillEnd();
			}
		}).fail([=] {
			_afterId = 0;
			finish();
		}).send();
	};
	_afterId = histories().sendRequest(
		_history,
		Histories::RequestType::History,
		send);
}

bool RepliesList::processMessagesIsEmpty(const MTPmessages_Messages &result) {
	const auto guard = gsl::finally([&] { _listChanges.fire({}); });

	auto &owner = _history->owner();
	const auto list = result.match([&](
			const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified! "
			"(HistoryWidget::messagesReceived)"));
		return QVector<MTPMessage>();
	}, [&](const auto &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return data.vmessages().v;
	});

	const auto fullCount = result.match([&](
			const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified! "
			"(HistoryWidget::messagesReceived)"));
		return 0;
	}, [&](const MTPDmessages_messages &data) {
		return int(data.vmessages().v.size());
	}, [&](const MTPDmessages_messagesSlice &data) {
		return data.vcount().v;
	}, [&](const MTPDmessages_channelMessages &data) {
		if (const auto channel = _history->peer->asChannel()) {
			channel->ptsReceived(data.vpts().v);
			channel->processTopics(data.vtopics());
		} else {
			LOG(("API Error: received messages.channelMessages when "
				"no channel was passed! (HistoryWidget::messagesReceived)"));
		}
		return data.vcount().v;
	});

	if (list.isEmpty()) {
		return true;
	}

	const auto maxId = IdFromMessage(list.front());
	const auto wasSize = int(_list.size());
	const auto toFront = (wasSize > 0) && (maxId > _list.front());
	const auto localFlags = MessageFlags();
	const auto type = NewMessageType::Existing;
	auto refreshed = std::vector<MsgId>();
	if (toFront) {
		refreshed.reserve(_list.size() + list.size());
	}
	auto skipped = 0;
	for (const auto &message : list) {
		if (const auto item = owner.addNewMessage(message, localFlags, type)) {
			if (item->inThread(_rootId)) {
				if (toFront && item->id > _list.front()) {
					refreshed.push_back(item->id);
				} else if (_list.empty() || item->id < _list.back()) {
					_list.push_back(item->id);
				}
			} else {
				++skipped;
			}
		} else {
			++skipped;
		}
	}
	if (toFront) {
		refreshed.insert(refreshed.end(), _list.begin(), _list.end());
		_list = std::move(refreshed);
	}

	const auto nowSize = int(_list.size());
	auto &decrementFrom = toFront ? _skippedAfter : _skippedBefore;
	if (decrementFrom.has_value()) {
		*decrementFrom = std::max(
			*decrementFrom - (nowSize - wasSize),
			0);
	}

	const auto checkedCount = std::max(fullCount - skipped, nowSize);
	if (_skippedBefore && _skippedAfter) {
		auto &correct = toFront ? _skippedBefore : _skippedAfter;
		*correct = std::max(
			checkedCount - *decrementFrom - nowSize,
			0);
		*decrementFrom = checkedCount - *correct - nowSize;
		Assert(*decrementFrom >= 0);
	} else if (_skippedBefore) {
		*_skippedBefore = std::min(*_skippedBefore, checkedCount - nowSize);
		_skippedAfter = checkedCount - *_skippedBefore - nowSize;
	} else if (_skippedAfter) {
		*_skippedAfter = std::min(*_skippedAfter, checkedCount - nowSize);
		_skippedBefore = checkedCount - *_skippedAfter - nowSize;
	}
	_fullCount = checkedCount;

	checkReadTillEnd();

	if (const auto item = lookupRoot()) {
		if (const auto original = item->lookupDiscussionPostOriginal()) {
			if (_skippedAfter == 0 && !_list.empty()) {
				original->setCommentsMaxId(_list.front());
			} else {
				original->setCommentsPossibleMaxId(maxId);
			}
		}
	}

	Ensures(list.size() >= skipped);
	return (list.size() == skipped);
}

void RepliesList::setInboxReadTill(
		MsgId readTillId,
		std::optional<int> unreadCount) {
	const auto newReadTillId = std::max(readTillId.bare, int64(1));
	const auto ignore = (newReadTillId < _inboxReadTillId);
	if (ignore) {
		return;
	}
	const auto changed = (newReadTillId > _inboxReadTillId);
	if (changed) {
		_inboxReadTillId = newReadTillId;
	}
	if (_skippedAfter == 0
		&& !_list.empty()
		&& _inboxReadTillId >= _list.front()) {
		unreadCount = 0;
	}
	const auto wasUnreadCount = _unreadCount;
	if (_unreadCount.current() != unreadCount
		&& (changed || unreadCount.has_value())) {
		setUnreadCount(unreadCount);
	}
}

MsgId RepliesList::inboxReadTillId() const {
	return _inboxReadTillId;
}

MsgId RepliesList::computeInboxReadTillFull() const {
	const auto local = _inboxReadTillId;
	if (const auto megagroup = _history->peer->asMegagroup()) {
		if (!megagroup->isForum() && megagroup->amIn()) {
			return std::max(local, _history->inboxReadTillId());
		}
	}
	return local;
}

void RepliesList::setOutboxReadTill(MsgId readTillId) {
	const auto newReadTillId = std::max(readTillId.bare, int64(1));
	if (newReadTillId > _outboxReadTillId) {
		_outboxReadTillId = newReadTillId;
		_history->session().changes().historyUpdated(
			_history,
			HistoryUpdate::Flag::OutboxRead);
	}
}

MsgId RepliesList::computeOutboxReadTillFull() const {
	const auto local = _outboxReadTillId;
	if (const auto megagroup = _history->peer->asMegagroup()) {
		if (!megagroup->isForum() && megagroup->amIn()) {
			return std::max(local, _history->outboxReadTillId());
		}
	}
	return local;
}

void RepliesList::setUnreadCount(std::optional<int> count) {
	_unreadCount = count;
	if (!count && !_readRequestTimer.isActive() && !_readRequestId) {
		reloadUnreadCountIfNeeded();
	}
}

int RepliesList::displayedUnreadCount() const {
	return (_inboxReadTillId > 1) ? unreadCountCurrent() : 0;
}

bool RepliesList::isServerSideUnread(
		not_null<const HistoryItem*> item) const {
	const auto till = item->out()
		? computeOutboxReadTillFull()
		: computeInboxReadTillFull();
	return (item->id > till);
}

void RepliesList::checkReadTillEnd() {
	if (_unreadCount.current() != 0
		&& _skippedAfter == 0
		&& !_list.empty()
		&& _inboxReadTillId >= _list.front()) {
		setUnreadCount(0);
	}
}

std::optional<int> RepliesList::computeUnreadCountLocally(
		MsgId afterId) const {
	Expects(afterId >= _inboxReadTillId);

	const auto currentUnreadCountAfter = _unreadCount.current();
	const auto startingMarkingAsRead = (currentUnreadCountAfter == 0)
		&& (_inboxReadTillId == 1)
		&& (afterId > 1);
	const auto wasUnreadCountAfter = startingMarkingAsRead
		? _fullCount.current().value_or(0)
		: currentUnreadCountAfter;
	const auto readTillId = std::max(afterId, _rootId);
	const auto wasReadTillId = _inboxReadTillId;
	const auto backLoaded = (_skippedBefore == 0);
	const auto frontLoaded = (_skippedAfter == 0);
	const auto fullLoaded = backLoaded && frontLoaded;
	const auto allUnread = (readTillId == _rootId)
		|| (fullLoaded && _list.empty());
	if (allUnread && fullLoaded) {
		// Should not happen too often unless the list is empty.
		return int(_list.size());
	} else if (frontLoaded && !_list.empty() && readTillId >= _list.front()) {
		// Always "count by local data" if read till the end.
		return 0;
	} else if (wasReadTillId == readTillId) {
		// Otherwise don't recount the same value over and over.
		return wasUnreadCountAfter;
	} else if (frontLoaded && !_list.empty() && readTillId >= _list.back()) {
		// And count by local data if it is available and read-till changed.
		return int(ranges::lower_bound(_list, readTillId, std::greater<>())
			- begin(_list));
	} else if (_list.empty()) {
		return std::nullopt;
	} else if (wasUnreadCountAfter.has_value()
		&& (frontLoaded || readTillId <= _list.front())
		&& (backLoaded || wasReadTillId >= _list.back())) {
		// Count how many were read since previous value.
		const auto from = ranges::lower_bound(
			_list,
			readTillId,
			std::greater<>());
		const auto till = ranges::lower_bound(
			from,
			end(_list),
			wasReadTillId,
			std::greater<>());
		return std::max(*wasUnreadCountAfter - int(till - from), 0);
	}
	return std::nullopt;
}

void RepliesList::requestUnreadCount() {
	if (_reloadUnreadCountRequestId) {
		return;
	}
	const auto weak = base::make_weak(this);
	const auto session = &_history->session();
	const auto fullId = FullMsgId(_history->peer->id, _rootId);
	const auto apply = [weak, session, fullId](
			MsgId readTill,
			int unreadCount) {
		if (const auto strong = weak.get()) {
			strong->setInboxReadTill(readTill, unreadCount);
		}
		if (const auto root = session->data().message(fullId)) {
			if (const auto post = root->lookupDiscussionPostOriginal()) {
				post->setCommentsInboxReadTill(readTill);
			}
		}
	};
	_reloadUnreadCountRequestId = session->api().request(
		MTPmessages_GetDiscussionMessage(
			_history->peer->input,
			MTP_int(_rootId))
	).done([=](const MTPmessages_DiscussionMessage &result) {
		if (weak) {
			_reloadUnreadCountRequestId = 0;
		}
		result.match([&](const MTPDmessages_discussionMessage &data) {
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			apply(
				data.vread_inbox_max_id().value_or_empty(),
				data.vunread_count().v);
		});
	}).send();
}

void RepliesList::readTill(not_null<HistoryItem*> item) {
	readTill(item->id, item);
}

void RepliesList::readTill(MsgId tillId) {
	readTill(tillId, _history->owner().message(_history->peer->id, tillId));
}

void RepliesList::readTill(
		MsgId tillId,
		HistoryItem *tillIdItem) {
	if (!IsServerMsgId(tillId)) {
		return;
	}
	const auto was = computeInboxReadTillFull();
	const auto now = tillId;
	if (now < was) {
		return;
	}
	const auto unreadCount = computeUnreadCountLocally(now);
	const auto fast = (tillIdItem && tillIdItem->out()) || !unreadCount.has_value();
	if (was < now || (fast && now == was)) {
		setInboxReadTill(now, unreadCount);
		const auto rootFullId = FullMsgId(_history->peer->id, _rootId);
		if (const auto root = _history->owner().message(rootFullId)) {
			if (const auto post = root->lookupDiscussionPostOriginal()) {
				post->setCommentsInboxReadTill(now);
			}
		}
		if (!_readRequestTimer.isActive()) {
			_readRequestTimer.callOnce(fast ? 0 : kReadRequestTimeout);
		} else if (fast && _readRequestTimer.remainingTime() > 0) {
			_readRequestTimer.callOnce(0);
		}
	}
	if (const auto topic = _history->peer->forumTopicFor(_rootId)) {
		Core::App().notifications().clearIncomingFromTopic(topic);
	}
}

void RepliesList::sendReadTillRequest() {
	if (_readRequestTimer.isActive()) {
		_readRequestTimer.cancel();
	}
	const auto api = &_history->session().api();
	api->request(base::take(_readRequestId)).cancel();

	_readRequestId = api->request(MTPmessages_ReadDiscussion(
		_history->peer->input,
		MTP_int(_rootId),
		MTP_int(computeInboxReadTillFull())
	)).done(crl::guard(this, [=] {
		_readRequestId = 0;
		reloadUnreadCountIfNeeded();
	})).send();
}

void RepliesList::reloadUnreadCountIfNeeded() {
	if (unreadCountKnown()) {
		return;
	} else if (inboxReadTillId() < computeInboxReadTillFull()) {
		_readRequestTimer.callOnce(0);
	} else {
		requestUnreadCount();
	}
}

bool RepliesList::canDeleteMyTopic() const {
	if (_skippedBefore != 0 || _skippedAfter != 0) {
		return false;
	}
	auto counter = 0;
	const auto owner = &_history->owner();
	const auto peerId = _history->peer->id;
	for (const auto &id : _list) {
		if (id == _rootId) {
			continue;
		} else if (const auto item = owner->message(peerId, id)) {
			if (!item->out() || ++counter > kMaxMessagesToDeleteMyTopic) {
				return false;
			}
		} else {
			return false;
		}
	}
	return true;
}

} // namespace Data
