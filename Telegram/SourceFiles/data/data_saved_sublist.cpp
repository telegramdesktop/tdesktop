/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_saved_sublist.h"

#include "api/api_unread_things.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_drafts.h"
#include "data/data_histories.h"
#include "data/data_messages.h"
#include "data/data_peer.h"
#include "data/data_saved_messages.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_item_preview.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"

namespace Data {
namespace {

constexpr auto kMessagesPerPage = 50;
constexpr auto kReadRequestTimeout = 3 * crl::time(1000);

} // namespace

struct SavedSublist::Viewer {
	MessagesSlice slice;
	MsgId around = 0;
	int limitBefore = 0;
	int limitAfter = 0;
	base::has_weak_ptr guard;
	bool scheduled = false;
};

SavedSublist::SavedSublist(
	not_null<SavedMessages*> parent,
	not_null<PeerData*> sublistPeer)
: Thread(&sublistPeer->owner(), Dialogs::Entry::Type::SavedSublist)
, _parent(parent)
, _sublistHistory(sublistPeer->owner().history(sublistPeer))
, _readRequestTimer([=] { sendReadTillRequest(); }) {
	if (parent->parentChat()) {
		_flags |= Flag::InMonoforum;
	}
	subscribeToUnreadChanges();
}

SavedSublist::~SavedSublist() {
	histories().cancelRequest(base::take(_beforeId));
	histories().cancelRequest(base::take(_afterId));
	if (_readRequestTimer.isActive()) {
		sendReadTillRequest();
	}
	session().api().unreadThings().cancelRequests(this);
}

bool SavedSublist::inMonoforum() const {
	return (_flags & Flag::InMonoforum) != 0;
}

void SavedSublist::apply(const SublistReadTillUpdate &update) {
	if (update.out) {
		setOutboxReadTill(update.readTillId);
	} else if (update.readTillId >= _inboxReadTillId) {
		setInboxReadTill(
			update.readTillId,
			computeUnreadCountLocally(update.readTillId));
	}
}

void SavedSublist::apply(const MessageUpdate &update) {
	if (applyUpdate(update)) {
		_instantChanges.fire({});
	}
}

void SavedSublist::applyDifferenceTooLong() {
	if (_skippedAfter.has_value()) {
		_skippedAfter = std::nullopt;
		_listChanges.fire({});
	}
}

bool SavedSublist::removeOne(not_null<HistoryItem*> item) {
	const auto id = item->id;
	const auto i = ranges::lower_bound(_list, id, std::greater<>());
	changeUnreadCountByMessage(id, -1);
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

rpl::producer<MessagesSlice> SavedSublist::source(
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

		const auto history = owningHistory();
		history->session().changes().historyUpdates(
			history,
			HistoryUpdate::Flag::ClientSideMessages
		) | rpl::start_with_next(pushDelayed, lifetime);

		_listChanges.events(
		) | rpl::start_with_next(pushDelayed, lifetime);

		_instantChanges.events(
		) | rpl::start_with_next(pushInstant, lifetime);

		pushInstant();
		return lifetime;
	};
}

not_null<SavedMessages*> SavedSublist::parent() const {
	return _parent;
}

not_null<History*> SavedSublist::owningHistory() {
	return _parent->owningHistory();
}

ChannelData *SavedSublist::parentChat() const {
	return _parent->parentChat();
}

not_null<PeerData*> SavedSublist::sublistPeer() const {
	return _sublistHistory->peer;
}

bool SavedSublist::isHiddenAuthor() const {
	return sublistPeer()->isSavedHiddenAuthor();
}

rpl::producer<> SavedSublist::destroyed() const {
	using namespace rpl::mappers;
	return rpl::merge(
		_parent->destroyed(),
		_parent->sublistDestroyed() | rpl::filter(
			_1 == this
		) | rpl::to_empty);
}

void SavedSublist::applyMaybeLast(not_null<HistoryItem*> item, bool added) {
	growLastKnownServerMessageId(item->id);
	if (!_lastServerMessage.value_or(nullptr)
		|| (*_lastServerMessage)->id < item->id) {
		setLastServerMessage(item);
		resolveChatListMessageGroup();
	}
}

void SavedSublist::applyItemAdded(not_null<HistoryItem*> item) {
	if (item->isRegular()) {
		setLastServerMessage(item);
	} else {
		setLastMessage(item);
	}
}

void SavedSublist::applyItemRemoved(MsgId id) {
	if (const auto lastItem = lastMessage()) {
		if (lastItem->id == id) {
			_lastMessage = std::nullopt;
		}
	}
	if (const auto lastServerItem = lastServerMessage()) {
		if (lastServerItem->id == id) {
			_lastServerMessage = std::nullopt;
		}
	}
	if (const auto chatListItem = _chatListMessage.value_or(nullptr)) {
		if (chatListItem->id == id) {
			_chatListMessage = std::nullopt;
			crl::on_main(this, [=] {
				// We didn't yet update _list here.
				if (_chatListMessage.has_value()) {
					return;
				} else if (_skippedAfter == 0) {
					if (!_list.empty()) {
						applyMaybeLast(owner().message(
							owningHistory()->peer,
							_list.front()));
						return;
					} else if (_skippedBefore == 0) {
						setLastServerMessage(nullptr);
						updateChatListExistence();
						return;
					}
				}
				if (_parent->parentChat()) {
					requestChatListMessage();
				} else {
					loadAround(0);
				}
			});
		}
	}
}

void SavedSublist::requestChatListMessage() {
	if (!chatListMessageKnown()) {
		parent()->requestSublist(sublistPeer());
	}
}

void SavedSublist::readTillEnd() {
	readTill(_lastKnownServerMessageId);
}

bool SavedSublist::buildFromData(not_null<Viewer*> viewer) {
	if (_list.empty() && _skippedBefore == 0 && _skippedAfter == 0) {
		viewer->slice.ids.clear();
		viewer->slice.nearestToAround = FullMsgId();
		viewer->slice.fullCount
			= viewer->slice.skippedBefore
			= viewer->slice.skippedAfter
			= 0;
		ranges::reverse(viewer->slice.ids);
		return true;
	}
	const auto around = (viewer->around != ShowAtUnreadMsgId)
		? viewer->around
		: computeInboxReadTillFull();
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

	const auto peerId = owningHistory()->peer->id;
	slice->ids.clear();
	auto nearestToAround = std::optional<MsgId>();
	slice->ids.reserve(useAfter + useBefore);
	for (auto j = i - useAfter, e = i + useBefore; j != e; ++j) {
		const auto id = *j;
		if (!nearestToAround && id < around) {
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

	ranges::reverse(viewer->slice.ids);

	if (_skippedBefore != 0 && useBefore < viewer->limitBefore + 1) {
		loadBefore();
	}
	if (_skippedAfter != 0 && useAfter < viewer->limitAfter) {
		loadAfter();
	}

	return true;
}

bool SavedSublist::applyUpdate(const MessageUpdate &update) {
	using Flag = MessageUpdate::Flag;

	if (update.item->history() != owningHistory()
		|| !update.item->isRegular()
		|| update.item->sublistPeerId() != sublistPeer()->id) {
		return false;
	} else if (update.flags & Flag::Destroyed) {
		return removeOne(update.item);
	}
	const auto id = update.item->id;
	if (update.flags & Flag::NewAdded) {
		changeUnreadCountByMessage(id, 1);
	}
	const auto i = ranges::lower_bound(_list, id, std::greater<>());
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

bool SavedSublist::processMessagesIsEmpty(
		const MTPmessages_Messages &result) {
	const auto guard = gsl::finally([&] { _listChanges.fire({}); });

	const auto list = result.match([&](
			const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified! "
			"(HistoryWidget::messagesReceived)"));
		return QVector<MTPMessage>();
	}, [&](const auto &data) {
		owner().processUsers(data.vusers());
		owner().processChats(data.vchats());
		return data.vmessages().v;
	});

	const auto fullCount = result.match([&](
			const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified! "
			"(HistoryWidget::messagesReceived)"));
		return 0;
	}, [&](const MTPDmessages_messages &data) {
		owningHistory()->peer->processTopics(data.vtopics());
		return int(data.vmessages().v.size());
	}, [&](const MTPDmessages_messagesSlice &data) {
		owningHistory()->peer->processTopics(data.vtopics());
		return data.vcount().v;
	}, [&](const MTPDmessages_channelMessages &data) {
		if (const auto channel = owningHistory()->peer->asChannel()) {
			channel->ptsReceived(data.vpts().v);
		} else {
			LOG(("API Error: received messages.channelMessages when "
				"no channel was passed! (HistoryWidget::messagesReceived)"));
		}
		owningHistory()->peer->processTopics(data.vtopics());
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
		if (const auto item = owner().addNewMessage(message, localFlags, type)) {
			if (item->sublistPeerId() == sublistPeer()->id) {
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

	Ensures(list.size() >= skipped);
	return (list.size() == skipped);
}

void SavedSublist::setInboxReadTill(
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
	} else if (_lastServerMessage.value_or(nullptr)
		&& (*_lastServerMessage)->id <= newReadTillId) {
		unreadCount = 0;
	}
	if (_unreadCount.current() != unreadCount
		&& (changed || unreadCount.has_value())) {
		setUnreadCount(unreadCount);
	}
}

MsgId SavedSublist::inboxReadTillId() const {
	return _inboxReadTillId;
}

MsgId SavedSublist::computeInboxReadTillFull() const {
	return _inboxReadTillId;
}

void SavedSublist::setOutboxReadTill(MsgId readTillId) {
	const auto newReadTillId = std::max(readTillId.bare, int64(1));
	if (newReadTillId > _outboxReadTillId) {
		_outboxReadTillId = newReadTillId;
		const auto history = owningHistory();
		history->session().changes().historyUpdated(
			history,
			HistoryUpdate::Flag::OutboxRead);
	}
}

MsgId SavedSublist::computeOutboxReadTillFull() const {
	return _outboxReadTillId;
}

void SavedSublist::setUnreadCount(std::optional<int> count) {
	_unreadCount = count;
	if (!count && !_readRequestTimer.isActive() && !_readRequestId) {
		reloadUnreadCountIfNeeded();
	}
}

void SavedSublist::setUnreadMark(bool unread) {
	if (unreadMark() == unread) {
		return;
	}
	const auto notifier = unreadStateChangeNotifier(
		!unreadCountCurrent());
	Thread::setUnreadMarkFlag(unread);
}

bool SavedSublist::unreadCountKnown() const {
	return !inMonoforum() || _unreadCount.current().has_value();
}

int SavedSublist::unreadCountCurrent() const {
	return _unreadCount.current().value_or(0);
}

rpl::producer<std::optional<int>> SavedSublist::unreadCountValue() const {
	if (!inMonoforum()) {
		return rpl::single(std::optional<int>(0));
	}
	return _unreadCount.value();
}

int SavedSublist::displayedUnreadCount() const {
	return (_inboxReadTillId > 1) ? unreadCountCurrent() : 0;
}

void SavedSublist::changeUnreadCountByMessage(MsgId id, int delta) {
	if (!inMonoforum() || !_inboxReadTillId) {
		setUnreadCount(std::nullopt);
		return;
	}
	const auto count = _unreadCount.current();
	if (count.has_value() && (id > _inboxReadTillId)) {
		setUnreadCount(std::max(*count + delta, 0));
	}
}

bool SavedSublist::isServerSideUnread(
		not_null<const HistoryItem*> item) const {
	if (!inMonoforum()) {
		return false;
	}
	const auto till = item->out()
		? computeOutboxReadTillFull()
		: computeInboxReadTillFull();
	return (item->id > till);
}

void SavedSublist::checkReadTillEnd() {
	if (_unreadCount.current() != 0
		&& _skippedAfter == 0
		&& !_list.empty()
		&& _inboxReadTillId >= _list.front()) {
		setUnreadCount(0);
	}
}

std::optional<int> SavedSublist::computeUnreadCountLocally(
		MsgId afterId) const {
	Expects(afterId >= _inboxReadTillId);

	const auto currentUnreadCountAfter = _unreadCount.current();
	const auto startingMarkingAsRead = (currentUnreadCountAfter == 0)
		&& (_inboxReadTillId == 1)
		&& (afterId > 1);
	const auto wasUnreadCountAfter = startingMarkingAsRead
		? _fullCount.current().value_or(0)
		: currentUnreadCountAfter;
	const auto readTillId = std::max(afterId, MsgId(1));
	const auto wasReadTillId = _inboxReadTillId;
	const auto backLoaded = (_skippedBefore == 0);
	const auto frontLoaded = (_skippedAfter == 0);
	const auto fullLoaded = backLoaded && frontLoaded;
	const auto allUnread = (readTillId == MsgId(1))
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

void SavedSublist::requestUnreadCount() {
	parent()->requestSublist(sublistPeer());
}

void SavedSublist::readTill(not_null<HistoryItem*> item) {
	readTill(item->id, item);
}

void SavedSublist::readTill(MsgId tillId) {
	const auto parentChat = _parent->parentChat();
	if (!parentChat) {
		return;
	}
	readTill(tillId, owner().message(parentChat->id, tillId));
}

void SavedSublist::readTill(
		MsgId tillId,
		HistoryItem *tillIdItem) {
	if (!IsServerMsgId(tillId)) {
		return;
	}
	if (unreadMark()) {
		owner().histories().changeSublistUnreadMark(this, false);
	}
	const auto was = computeInboxReadTillFull();
	const auto now = tillId;
	if (now < was) {
		return;
	}
	const auto unreadCount = computeUnreadCountLocally(now);
	const auto fast = (tillIdItem && tillIdItem->out())
		|| !unreadCount.has_value();
	if (was < now || (fast && now == was)) {
		setInboxReadTill(now, unreadCount);
		if (!_readRequestTimer.isActive()) {
			_readRequestTimer.callOnce(fast ? 0 : kReadRequestTimeout);
		} else if (fast && _readRequestTimer.remainingTime() > 0) {
			_readRequestTimer.callOnce(0);
		}
	}
	Core::App().notifications().clearIncomingFromSublist(this);
}

void SavedSublist::sendReadTillRequest() {
	const auto parentChat = _parent->parentChat();
	if (!parentChat) {
		return;
	}
	if (_readRequestTimer.isActive()) {
		_readRequestTimer.cancel();
	}
	const auto api = &_parent->session().api();
	api->request(base::take(_readRequestId)).cancel();

	_sentReadTill = computeInboxReadTillFull();
	_readRequestId = api->request(MTPmessages_ReadSavedHistory(
		parentChat->input,
		sublistPeer()->input,
		MTP_int(_sentReadTill.bare)
	)).done(crl::guard(this, [=] {
		_readRequestId = 0;
		reloadUnreadCountIfNeeded();
	})).send();
}

void SavedSublist::reloadUnreadCountIfNeeded() {
	if (unreadCountKnown()) {
		return;
	} else if (inboxReadTillId() < computeInboxReadTillFull()) {
		_readRequestTimer.callOnce(0);
	} else {
		requestUnreadCount();
	}
}

void SavedSublist::subscribeToUnreadChanges() {
	if (!inMonoforum()) {
		return;
	}
	_unreadCount.value(
	) | rpl::map([=](std::optional<int> value) {
		return value ? displayedUnreadCount() : value;
	}) | rpl::distinct_until_changed(
	) | rpl::combine_previous(
	) | rpl::filter([=] {
		return inChatList();
	}) | rpl::start_with_next([=](
			std::optional<int> previous,
			std::optional<int> now) {
		if (previous.value_or(0) != now.value_or(0)) {
			_parent->recentSublistsInvalidate(this);
		}
		notifyUnreadStateChange(unreadStateFor(
			previous.value_or(0),
			previous.has_value()));
	}, _lifetime);
}

void SavedSublist::applyMonoforumDialog(
		const MTPDmonoForumDialog &data,
		not_null<HistoryItem*> topItem) {
	if (const auto parent = parentChat()) {
		if (const auto draft = data.vdraft()) {
			draft->match([&](const MTPDdraftMessage &data) {
				Data::ApplyPeerCloudDraft(
					&session(),
					parent->id,
					MsgId(),
					sublistPeer()->id,
					data);
			}, [](const MTPDdraftMessageEmpty&) {});
		}
	}

	setInboxReadTill(
		data.vread_inbox_max_id().v,
		data.vunread_count().v);
	if (!unreadCountKnown() && !_readRequestId) {
		// We got read_inbox_max_id < than our current inboxReadTillId,
		// we need either to send a read request with this new value,
		// or to downgrade inboxReadTillId locally.
		if (_sentReadTill < computeInboxReadTillFull()) {
			sendReadTillRequest();
		} else {
			// Just if nothing else helps.
			_inboxReadTillId = 0;
			setInboxReadTill(
				data.vread_inbox_max_id().v,
				data.vunread_count().v);
		}
	}
	setOutboxReadTill(data.vread_outbox_max_id().v);
	unreadReactions().setCount(data.vunread_reactions_count().v);
	setUnreadMark(data.is_unread_mark());
	applyMaybeLast(topItem);

	if (data.is_nopaid_messages_exception()) {
		_flags |= Flag::FeeRemoved;
	} else {
		_flags &= ~Flag::FeeRemoved;
	}
}

bool SavedSublist::isFeeRemoved() const {
	return (_flags & Flag::FeeRemoved);
}

void SavedSublist::toggleFeeRemoved(bool feeRemoved) {
	if (feeRemoved) {
		_flags |= Flag::FeeRemoved;
	} else {
		_flags &= ~Flag::FeeRemoved;
	}
}

TimeId SavedSublist::adjustedChatListTimeId() const {
	const auto result = chatListTimeId();
	const auto monoforumPeerId = sublistPeer()->id;
	const auto history = _parent->owningHistory();
	if (const auto draft = history->cloudDraft(MsgId(), monoforumPeerId)) {
		if (!Data::DraftIsNull(draft) && !session().supportMode()) {
			return std::max(result, draft->date);
		}
	}
	return result;
}

rpl::producer<> SavedSublist::changes() const {
	return _listChanges.events();
}

void SavedSublist::loadFullCount() {
	if (!_fullCount.current() && !_loadingAround) {
		loadAround(0);
	}
}

void SavedSublist::appendClientSideMessages(MessagesSlice &slice) {
	const auto &messages = owningHistory()->clientSideMessages();
	if (messages.empty()) {
		return;
	} else if (slice.ids.empty()) {
		if (slice.skippedBefore != 0 || slice.skippedAfter != 0) {
			return;
		}
		slice.ids.reserve(messages.size());
		const auto sublistPeerId = sublistPeer()->id;
		for (const auto &item : messages) {
			if (item->sublistPeerId() != sublistPeerId) {
				continue;
			}
			slice.ids.push_back(item->fullId());
		}
		ranges::sort(slice.ids);
		return;
	}
	const auto sublistPeerId = sublistPeer()->id;
	auto dates = std::vector<TimeId>();
	dates.reserve(slice.ids.size());
	for (const auto &id : slice.ids) {
		const auto message = owner().message(id);
		Assert(message != nullptr);

		dates.push_back(message->date());
	}
	for (const auto &item : messages) {
		if (item->sublistPeerId() != sublistPeerId) {
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

std::optional<int> SavedSublist::fullCount() const {
	return _fullCount.current();
}

rpl::producer<int> SavedSublist::fullCountValue() const {
	return _fullCount.value() | rpl::filter_optional();
}

int SavedSublist::fixedOnTopIndex() const {
	return 0;
}

bool SavedSublist::shouldBeInChatList() const {
	if (const auto monoforum = _parent->parentChat()) {
		if (monoforum == sublistPeer()) {
			return false;
		}
	}
	return isPinnedDialog(FilterId())
		|| !lastMessageKnown()
		|| (lastMessage() != nullptr);
}

HistoryItem *SavedSublist::lastMessage() const {
	return _lastMessage.value_or(nullptr);
}

bool SavedSublist::lastMessageKnown() const {
	return _lastMessage.has_value();
}

HistoryItem *SavedSublist::lastServerMessage() const {
	return _lastServerMessage.value_or(nullptr);
}

bool SavedSublist::lastServerMessageKnown() const {
	return _lastServerMessage.has_value();
}

MsgId SavedSublist::lastKnownServerMessageId() const {
	return _lastKnownServerMessageId;
}

Dialogs::UnreadState SavedSublist::chatListUnreadState() const {
	if (!inMonoforum()) {
		return {};
	}
	return unreadStateFor(displayedUnreadCount(), unreadCountKnown());
}

Dialogs::BadgesState SavedSublist::chatListBadgesState() const {
	if (!inMonoforum()) {
		return {};
	}
	auto result = Dialogs::BadgesForUnread(
		chatListUnreadState(),
		Dialogs::CountInBadge::Messages,
		Dialogs::IncludeInBadge::All);
	if (!result.unread && inboxReadTillId() < 2) {
		result.unread = (_lastKnownServerMessageId
			> _parent->owningHistory()->inboxReadTillId());
		result.unreadMuted = muted();
	}
	if (_parent->owningHistory()->muted()) {
		result.unreadMuted
			= result.mentionMuted
			= result.reactionMuted
			= true;
	}
	return result;
}

HistoryItem *SavedSublist::chatListMessage() const {
	return _lastMessage.value_or(nullptr);
}

bool SavedSublist::chatListMessageKnown() const {
	return _lastMessage.has_value();
}

const QString &SavedSublist::chatListName() const {
	return _sublistHistory->chatListName();
}

const base::flat_set<QString> &SavedSublist::chatListNameWords() const {
	return _sublistHistory->chatListNameWords();
}

const base::flat_set<QChar> &SavedSublist::chatListFirstLetters() const {
	return _sublistHistory->chatListFirstLetters();
}

const QString &SavedSublist::chatListNameSortKey() const {
	return _sublistHistory->chatListNameSortKey();
}

int SavedSublist::chatListNameVersion() const {
	return _sublistHistory->chatListNameVersion();
}

void SavedSublist::paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const {
	_sublistHistory->paintUserpic(p, view, context);
}

HistoryView::SendActionPainter *SavedSublist::sendActionPainter() {
	return nullptr;
}

void SavedSublist::hasUnreadMentionChanged(bool has) {
	auto was = chatListUnreadState();
	if (has) {
		was.mentions = 0;
	} else {
		was.mentions = 1;
	}
	notifyUnreadStateChange(was);
}

void SavedSublist::hasUnreadReactionChanged(bool has) {
	auto was = chatListUnreadState();
	if (has) {
		was.reactions = was.reactionsMuted = 0;
	} else {
		was.reactions = 1;
		was.reactionsMuted = muted() ? was.reactions : 0;
	}
	notifyUnreadStateChange(was);
}

void SavedSublist::allowChatListMessageResolve() {
	if (_flags & Flag::ResolveChatListMessage) {
		return;
	}
	_flags |= Flag::ResolveChatListMessage;
	resolveChatListMessageGroup();
}

void SavedSublist::resolveChatListMessageGroup() {
	if (!(_flags & Flag::ResolveChatListMessage)) {
		return;
	}
	// If we set a single album part, request the full album.
	const auto item = _lastServerMessage.value_or(nullptr);
	if (item && item->groupId() != MessageGroupId()) {
		if (owner().groups().isGroupOfOne(item)
			&& !item->toPreview({
				.hideSender = true,
				.hideCaption = true }).images.empty()
				&& _requestedGroups.emplace(item->fullId()).second) {
			owner().histories().requestGroupAround(item);
		}
	}
}

void SavedSublist::growLastKnownServerMessageId(MsgId id) {
	_lastKnownServerMessageId = std::max(_lastKnownServerMessageId, id);
}

void SavedSublist::setLastServerMessage(HistoryItem *item) {
	if (item) {
		growLastKnownServerMessageId(item->id);
	}
	_lastServerMessage = item;
	if (_lastMessage
		&& *_lastMessage
		&& !(*_lastMessage)->isRegular()
		&& (!item
			|| (*_lastMessage)->date() > item->date()
			|| (*_lastMessage)->isSending())) {
		return;
	}
	setLastMessage(item);
}

void SavedSublist::setLastMessage(HistoryItem *item) {
	if (_lastMessage && *_lastMessage == item) {
		return;
	}
	_lastMessage = item;
	if (!item || item->isRegular()) {
		_lastServerMessage = item;
		if (item) {
			growLastKnownServerMessageId(item->id);
		}
	}
	setChatListMessage(item);
}

void SavedSublist::setChatListMessage(HistoryItem *item) {
	if (_chatListMessage && *_chatListMessage == item) {
		return;
	}
	const auto was = _chatListMessage.value_or(nullptr);
	if (item) {
		if (item->isSponsored()) {
			return;
		}
		if (_chatListMessage
			&& *_chatListMessage
			&& !(*_chatListMessage)->isRegular()
			&& (*_chatListMessage)->date() > item->date()) {
			return;
		}
		_chatListMessage = item;
		setChatListTimeId(item->date());
	} else if (!_chatListMessage || *_chatListMessage) {
		_chatListMessage = nullptr;
		updateChatListEntry();
	}
	_parent->listMessageChanged(was, item);
}

void SavedSublist::chatListPreloadData() {
	sublistPeer()->loadUserpic();
	allowChatListMessageResolve();
}

Dialogs::UnreadState SavedSublist::unreadStateFor(
		int count,
		bool known) const {
	auto result = Dialogs::UnreadState();
	const auto mark = !count && unreadMark();
	const auto muted = this->muted();
	result.messages = count;
	result.chats = count ? 1 : 0;
	result.marks = mark ? 1 : 0;
	result.reactions = unreadReactions().has() ? 1 : 0;
	result.messagesMuted = muted ? result.messages : 0;
	result.chatsMuted = muted ? result.chats : 0;
	result.marksMuted = muted ? result.marks : 0;
	result.reactionsMuted = muted ? result.reactions : 0;
	result.known = known;
	return result;
}

Histories &SavedSublist::histories() {
	return owner().histories();
}

void SavedSublist::loadAround(MsgId id) {
	if (_loadingAround && *_loadingAround == id) {
		return;
	}
	histories().cancelRequest(base::take(_beforeId));
	histories().cancelRequest(base::take(_afterId));

	const auto send = [=](Fn<void()> finish) {
		using Flag = MTPmessages_GetSavedHistory::Flag;
		const auto parentChat = _parent->parentChat();
		return session().api().request(MTPmessages_GetSavedHistory(
			MTP_flags(parentChat ? Flag::f_parent_peer : Flag(0)),
			parentChat ? parentChat->input : MTPInputPeer(),
			sublistPeer()->input,
			MTP_int(id), // offset_id
			MTP_int(0), // offset_date
			MTP_int(id ? (-kMessagesPerPage / 2) : 0), // add_offset
			MTP_int(kMessagesPerPage), // limit
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0)) // hash
		).done([=](const MTPmessages_Messages &result) {
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
				if (!_parent->parentChat() && !_chatListMessage) {
					setLastServerMessage(nullptr);
					updateChatListExistence();
				}
			} else if (id) {
				Assert(!_list.empty());
				if (_list.front() <= id) {
					_skippedAfter = 0;
				} else if (_list.back() >= id) {
					_skippedBefore = 0;
				}
			} else if (!_parent->parentChat() && !_chatListMessage) {
				Assert(!_list.empty());
				applyMaybeLast(owner().message(
					owningHistory()->peer,
					_list.front()));
			}
			checkReadTillEnd();
		}).fail([=](const MTP::Error &error) {
			if (error.type() == u"SAVED_DIALOGS_UNSUPPORTED"_q) {
				_parent->markUnsupported();
			}
			_beforeId = 0;
			_loadingAround = std::nullopt;
			finish();
		}).send();
	};
	_loadingAround = id;
	_beforeId = histories().sendRequest(
		owningHistory(),
		Histories::RequestType::History,
		send);
}

void SavedSublist::loadBefore() {
	Expects(!_list.empty());

	if (_loadingAround) {
		histories().cancelRequest(base::take(_beforeId));
	} else if (_beforeId) {
		return;
	}

	const auto last = _list.back();
	const auto send = [=](Fn<void()> finish) {
		using Flag = MTPmessages_GetSavedHistory::Flag;
		const auto parentChat = _parent->parentChat();
		return session().api().request(MTPmessages_GetSavedHistory(
			MTP_flags(parentChat ? Flag::f_parent_peer : Flag(0)),
			parentChat ? parentChat->input : MTPInputPeer(),
			sublistPeer()->input,
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
		owningHistory(),
		Histories::RequestType::History,
		send);
}

void SavedSublist::loadAfter() {
	Expects(!_list.empty());

	if (_afterId) {
		return;
	}

	const auto first = _list.front();
	const auto send = [=](Fn<void()> finish) {
		using Flag = MTPmessages_GetSavedHistory::Flag;
		const auto parentChat = _parent->parentChat();
		return session().api().request(MTPmessages_GetSavedHistory(
			MTP_flags(parentChat ? Flag::f_parent_peer : Flag(0)),
			parentChat ? parentChat->input : MTPInputPeer(),
			sublistPeer()->input,
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
		owningHistory(),
		Histories::RequestType::History,
		send);
}

} // namespace Data
