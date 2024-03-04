/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/business/data_shortcut_messages.h"

#include "api/api_hash.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "api/api_text_entities.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRequestTimeLimit = 60 * crl::time(1000);

[[nodiscard]] MsgId RemoteToLocalMsgId(MsgId id) {
	Expects(IsServerMsgId(id));

	return ScheduledMaxMsgId + id + 1;
}

[[nodiscard]] MsgId LocalToRemoteMsgId(MsgId id) {
	Expects(IsShortcutMsgId(id));

	return (id - ScheduledMaxMsgId - 1);
}

[[nodiscard]] bool TooEarlyForRequest(crl::time received) {
	return (received > 0) && (received + kRequestTimeLimit > crl::now());
}

[[nodiscard]] MTPMessage PrepareMessage(
		BusinessShortcutId shortcutId,
		const MTPMessage &message) {
	return message.match([&](const MTPDmessageEmpty &data) {
		return MTP_messageEmpty(
			data.vflags(),
			data.vid(),
			data.vpeer_id() ? *data.vpeer_id() : MTPPeer());
	}, [&](const MTPDmessageService &data) {
		return MTP_messageService(
			data.vflags(),
			data.vid(),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			data.vpeer_id(),
			data.vreply_to() ? *data.vreply_to() : MTPMessageReplyHeader(),
			data.vdate(),
			data.vaction(),
			MTP_int(data.vttl_period().value_or_empty()));
	}, [&](const MTPDmessage &data) {
		return MTP_message(
			MTP_flags(data.vflags().v
				| MTPDmessage::Flag::f_quick_reply_shortcut_id),
			data.vid(),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			MTPint(), // from_boosts_applied
			data.vpeer_id(),
			data.vsaved_peer_id() ? *data.vsaved_peer_id() : MTPPeer(),
			data.vfwd_from() ? *data.vfwd_from() : MTPMessageFwdHeader(),
			MTP_long(data.vvia_bot_id().value_or_empty()),
			data.vreply_to() ? *data.vreply_to() : MTPMessageReplyHeader(),
			data.vdate(),
			data.vmessage(),
			data.vmedia() ? *data.vmedia() : MTPMessageMedia(),
			data.vreply_markup() ? *data.vreply_markup() : MTPReplyMarkup(),
			(data.ventities()
				? *data.ventities()
				: MTPVector<MTPMessageEntity>()),
			MTP_int(data.vviews().value_or_empty()),
			MTP_int(data.vforwards().value_or_empty()),
			data.vreplies() ? *data.vreplies() : MTPMessageReplies(),
			MTP_int(data.vedit_date().value_or_empty()),
			MTP_bytes(data.vpost_author().value_or_empty()),
			MTP_long(data.vgrouped_id().value_or_empty()),
			MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>(),
			MTP_int(data.vttl_period().value_or_empty()),
			MTP_int(shortcutId));
	});
}

} // namespace

bool IsShortcutMsgId(MsgId id) {
	return (id > ScheduledMaxMsgId) && (id < ShortcutMaxMsgId);
}

ShortcutMessages::ShortcutMessages(not_null<Session*> owner)
: _session(&owner->session())
, _history(owner->history(_session->userPeerId()))
, _clearTimer([=] { clearOldRequests(); }) {
	owner->itemRemoved(
	) | rpl::filter([](not_null<const HistoryItem*> item) {
		return item->isBusinessShortcut();
	}) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		remove(item);
	}, _lifetime);
}

ShortcutMessages::~ShortcutMessages() {
	for (const auto &request : _requests) {
		_session->api().request(request.second.requestId).cancel();
	}
}

void ShortcutMessages::clearOldRequests() {
	const auto now = crl::now();
	while (true) {
		const auto i = ranges::find_if(_requests, [&](const auto &value) {
			const auto &request = value.second;
			return !request.requestId
				&& (request.lastReceived + kRequestTimeLimit <= now);
		});
		if (i == end(_requests)) {
			break;
		}
		_requests.erase(i);
	}
}

void ShortcutMessages::updateShortcuts(const QVector<MTPQuickReply> &list) {
	auto shortcuts = parseShortcuts(list);
	auto changes = std::vector<ShortcutIdChange>();
	for (auto &[id, shortcut] : _shortcuts.list) {
		if (shortcuts.list.contains(id)) {
			continue;
		}
		auto foundId = BusinessShortcutId();
		for (auto &[realId, real] : shortcuts.list) {
			if (real.name == shortcut.name) {
				foundId = realId;
				break;
			}
		}
		if (foundId) {
			mergeMessagesFromTo(id, foundId);
			changes.push_back({ .oldId = id, .newId = foundId });
		} else {
			shortcuts.list.emplace(id, shortcut);
		}
	}
	const auto changed = !_shortcutsLoaded
		|| (shortcuts != _shortcuts);
	if (changed) {
		_shortcuts = std::move(shortcuts);
		_shortcutsLoaded = true;
		for (const auto &change : changes) {
			_shortcutIdChanges.fire_copy(change);
		}
		_shortcutsChanged.fire({});
	} else {
		Assert(changes.empty());
	}
}

void ShortcutMessages::mergeMessagesFromTo(
		BusinessShortcutId fromId,
		BusinessShortcutId toId) {
	auto &to = _data[toId];
	const auto i = _data.find(fromId);
	if (i == end(_data)) {
		return;
	}

	auto &from = i->second;
	auto destroy = base::flat_set<not_null<HistoryItem*>>();
	for (auto &item : from.items) {
		if (item->isSending() || item->hasFailed()) {
			item->setRealShortcutId(toId);
			to.items.push_back(std::move(item));
		} else {
			destroy.emplace(item.get());
		}
	}
	for (const auto &item : destroy) {
		item->destroy();
	}
	_data.remove(fromId);

	cancelRequest(fromId);

	_updates.fire_copy(toId);
	if (!destroy.empty()) {
		cancelRequest(toId);
		request(toId);
	}
}

Shortcuts ShortcutMessages::parseShortcuts(
		const QVector<MTPQuickReply> &list) const {
	auto result = Shortcuts();
	for (const auto &reply : list) {
		const auto shortcut = parseShortcut(reply);
		result.list.emplace(shortcut.id, shortcut);
	}
	return result;
}

Shortcut ShortcutMessages::parseShortcut(const MTPQuickReply &reply) const {
	const auto &data = reply.data();
	return Shortcut{
		.id = BusinessShortcutId(data.vshortcut_id().v),
		.count = data.vcount().v,
		.name = qs(data.vshortcut()),
		.topMessageId = localMessageId(data.vtop_message().v),
	};
}

MsgId ShortcutMessages::localMessageId(MsgId remoteId) const {
	return RemoteToLocalMsgId(remoteId);
}

MsgId ShortcutMessages::lookupId(not_null<const HistoryItem*> item) const {
	Expects(item->isBusinessShortcut());
	Expects(!item->isSending());
	Expects(!item->hasFailed());

	return LocalToRemoteMsgId(item->id);
}

int ShortcutMessages::count(BusinessShortcutId shortcutId) const {
	const auto i = _data.find(shortcutId);
	return (i != end(_data)) ? i->second.items.size() : 0;
}

void ShortcutMessages::apply(const MTPDupdateQuickReplies &update) {
	updateShortcuts(update.vquick_replies().v);
	scheduleShortcutsReload();
}

void ShortcutMessages::scheduleShortcutsReload() {
	const auto hasUnknownMessages = [&] {
		const auto selfId = _session->userPeerId();
		for (const auto &[id, shortcut] : _shortcuts.list) {
			if (!_session->data().message({ selfId, shortcut.topMessageId })) {
				return true;
			}
		}
		return false;
	};
	if (hasUnknownMessages()) {
		_shortcutsLoaded = false;
		const auto cancelledId = base::take(_shortcutsRequestId);
		_session->api().request(cancelledId).cancel();
		crl::on_main(_session, [=] {
			if (cancelledId || hasUnknownMessages()) {
				preloadShortcuts();
			}
		});
	}
}

void ShortcutMessages::apply(const MTPDupdateNewQuickReply &update) {
	const auto selfId = _session->userPeerId();
	const auto &reply = update.vquick_reply();
	auto foundId = BusinessShortcutId();
	const auto shortcut = parseShortcut(reply);
	for (auto &[id, existing] : _shortcuts.list) {
		if (id == shortcut.id) {
			foundId = id;
			break;
		} else if (existing.name == shortcut.name) {
			foundId = id;
			break;
		}
	}
	if (foundId == shortcut.id) {
		auto &already = _shortcuts.list[shortcut.id];
		if (already != shortcut) {
			already = shortcut;
			_shortcutsChanged.fire({});
		}
		return;
	} else if (foundId) {
		_shortcuts.list.emplace(shortcut.id, shortcut);
		mergeMessagesFromTo(foundId, shortcut.id);
		_shortcuts.list.remove(foundId);
		_shortcutIdChanges.fire({ foundId, shortcut.id });
		_shortcutsChanged.fire({});
	}
}

void ShortcutMessages::apply(const MTPDupdateQuickReplyMessage &update) {
	const auto &message = update.vmessage();
	const auto shortcutId = BusinessShortcutIdFromMessage(message);
	if (!shortcutId) {
		return;
	}
	const auto loaded = _data.contains(shortcutId);
	auto &list = _data[shortcutId];
	append(shortcutId, list, message);
	sort(list);
	_updates.fire_copy(shortcutId);
	updateCount(shortcutId);
	if (!loaded) {
		request(shortcutId);
	}
}

void ShortcutMessages::updateCount(BusinessShortcutId shortcutId) {
	const auto i = _data.find(shortcutId);
	const auto j = _shortcuts.list.find(shortcutId);
	if (j == end(_shortcuts.list)) {
		return;
	}
	const auto count = (i != end(_data))
		? int(i->second.itemById.size())
		: 0;
	if (j->second.count != count) {
		_shortcuts.list[shortcutId].count = count;
		_shortcutsChanged.fire({});
	}
}

void ShortcutMessages::apply(
		const MTPDupdateDeleteQuickReplyMessages &update) {
	const auto shortcutId = update.vshortcut_id().v;
	if (!shortcutId) {
		return;
	}
	auto i = _data.find(shortcutId);
	if (i == end(_data)) {
		return;
	}
	for (const auto &id : update.vmessages().v) {
		const auto &list = i->second;
		const auto j = list.itemById.find(id.v);
		if (j != end(list.itemById)) {
			j->second->destroy();
			i = _data.find(shortcutId);
			if (i == end(_data)) {
				break;
			}
		}
	}
	_updates.fire_copy(shortcutId);
	updateCount(shortcutId);

	cancelRequest(shortcutId);
	request(shortcutId);
}

void ShortcutMessages::apply(const MTPDupdateDeleteQuickReply &update) {
	const auto shortcutId = update.vshortcut_id().v;
	if (!shortcutId) {
		return;
	}
	auto i = _data.find(shortcutId);
	while (i != end(_data) && !i->second.itemById.empty()) {
		i->second.itemById.back().second->destroy();
		i = _data.find(shortcutId);
	}
	_updates.fire_copy(shortcutId);
	if (_data.contains(shortcutId)) {
		updateCount(shortcutId);
	} else {
		_shortcuts.list.remove(shortcutId);
		_shortcutIdChanges.fire({ shortcutId, 0 });
	}
}

void ShortcutMessages::apply(
		const MTPDupdateMessageID &update,
		not_null<HistoryItem*> local) {
	const auto id = update.vid().v;
	const auto i = _data.find(local->shortcutId());
	Assert(i != end(_data));
	auto &list = i->second;
	const auto j = list.itemById.find(id);
	if (j != end(list.itemById) || !IsServerMsgId(id)) {
		local->destroy();
	} else {
		Assert(!list.itemById.contains(local->id));
		local->setRealId(localMessageId(id));
		list.itemById.emplace(id, local);
	}
}

void ShortcutMessages::appendSending(not_null<HistoryItem*> item) {
	Expects(item->isSending());
	Expects(item->isBusinessShortcut());

	const auto shortcutId = item->shortcutId();
	auto &list = _data[shortcutId];
	list.items.emplace_back(item);
	sort(list);
	_updates.fire_copy(shortcutId);
}

void ShortcutMessages::removeSending(not_null<HistoryItem*> item) {
	Expects(item->isSending() || item->hasFailed());
	Expects(item->isBusinessShortcut());

	item->destroy();
}

rpl::producer<> ShortcutMessages::updates(BusinessShortcutId shortcutId) {
	request(shortcutId);

	return _updates.events(
	) | rpl::filter([=](BusinessShortcutId value) {
		return (value == shortcutId);
	}) | rpl::to_empty;
}

Data::MessagesSlice ShortcutMessages::list(BusinessShortcutId shortcutId) {
	auto result = Data::MessagesSlice();
	const auto i = _data.find(shortcutId);
	if (i == end(_data)) {
		const auto i = _requests.find(shortcutId);
		if (i == end(_requests)) {
			return result;
		}
		result.fullCount = result.skippedAfter = result.skippedBefore = 0;
		return result;
	}
	const auto &list = i->second.items;
	result.skippedAfter = result.skippedBefore = 0;
	result.fullCount = int(list.size());
	result.ids = ranges::views::all(
		list
	) | ranges::views::transform(
		&HistoryItem::fullId
	) | ranges::to_vector;
	return result;
}

void ShortcutMessages::preloadShortcuts() {
	if (_shortcutsLoaded || _shortcutsRequestId) {
		return;
	}
	const auto owner = &_session->data();
	_shortcutsRequestId = owner->session().api().request(
		MTPmessages_GetQuickReplies(MTP_long(_shortcutsHash))
	).done([=](const MTPmessages_QuickReplies &result) {
		result.match([&](const MTPDmessages_quickReplies &data) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			owner->processMessages(
				data.vmessages(),
				NewMessageType::Existing);
			updateShortcuts(data.vquick_replies().v);
		}, [&](const MTPDmessages_quickRepliesNotModified &) {
			if (!_shortcutsLoaded) {
				_shortcutsLoaded = true;
				_shortcutsChanged.fire({});
			}
		});
	}).send();
}

const Shortcuts &ShortcutMessages::shortcuts() const {
	return _shortcuts;
}

bool ShortcutMessages::shortcutsLoaded() const {
	return _shortcutsLoaded;
}

rpl::producer<> ShortcutMessages::shortcutsChanged() const {
	return _shortcutsChanged.events();
}

auto ShortcutMessages::shortcutIdChanged() const
-> rpl::producer<ShortcutIdChange> {
	return _shortcutIdChanges.events();
}

BusinessShortcutId ShortcutMessages::emplaceShortcut(QString name) {
	Expects(_shortcutsLoaded);

	for (auto &[id, shortcut] : _shortcuts.list) {
		if (shortcut.name == name) {
			return id;
		}
	}
	const auto result = --_localShortcutId;
	_shortcuts.list.emplace(result, Shortcut{ .id = result, .name = name });
	return result;
}

Shortcut ShortcutMessages::lookupShortcut(BusinessShortcutId id) const {
	const auto i = _shortcuts.list.find(id);

	Ensures(i != end(_shortcuts.list));
	return i->second;
}

BusinessShortcutId ShortcutMessages::lookupShortcutId(
		const QString &name) const {
	for (const auto &[id, shortcut] : _shortcuts.list) {
		if (!shortcut.name.compare(name, Qt::CaseInsensitive)) {
			return id;
		}
	}
	return {};
}

void ShortcutMessages::editShortcut(
		BusinessShortcutId id,
		QString name,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	name = name.trimmed();
	if (name.isEmpty()) {
		fail(QString());
		return;
	}
	const auto finish = [=] {
		const auto i = _shortcuts.list.find(id);
		if (i != end(_shortcuts.list)) {
			i->second.name = name;
			_shortcutsChanged.fire({});
		}
		done();
	};
	for (const auto &[existingId, shortcut] : _shortcuts.list) {
		if (shortcut.name == name) {
			if (existingId == id) {
				//done();
				//return;
				break;
			} else if (_data[existingId].items.empty() && !shortcut.count) {
				removeShortcut(existingId);
				break;
			} else {
				fail(u"SHORTCUT_OCCUPIED"_q);
				return;
			}
		}
	}
	_session->api().request(MTPmessages_EditQuickReplyShortcut(
		MTP_int(id),
		MTP_string(name)
	)).done(finish).fail([=](const MTP::Error &error) {
		const auto type = error.type();
		if (type == u"SHORTCUT_ID_INVALID"_q) {
			// Not on the server (yet).
			finish();
		} else {
			fail(type);
		}
	}).send();
}

void ShortcutMessages::removeShortcut(BusinessShortcutId shortcutId) {
	auto i = _data.find(shortcutId);
	while (i != end(_data)) {
		if (i->second.items.empty()) {
			_data.erase(i);
		} else {
			i->second.items.front()->destroy();
		}
		i = _data.find(shortcutId);
	}
	_shortcuts.list.remove(shortcutId);
	_shortcutIdChanges.fire({ shortcutId, 0 });

	_session->api().request(MTPmessages_DeleteQuickReplyShortcut(
		MTP_int(shortcutId)
	)).send();
}

void ShortcutMessages::cancelRequest(BusinessShortcutId shortcutId) {
	const auto j = _requests.find(shortcutId);
	if (j != end(_requests)) {
		_session->api().request(j->second.requestId).cancel();
		_requests.erase(j);
	}
}

void ShortcutMessages::request(BusinessShortcutId shortcutId) {
	auto &request = _requests[shortcutId];
	if (request.requestId || TooEarlyForRequest(request.lastReceived)) {
		return;
	}
	const auto i = _data.find(shortcutId);
	const auto hash = (i != end(_data))
		? countListHash(i->second)
		: uint64(0);
	request.requestId = _session->api().request(
		MTPmessages_GetQuickReplyMessages(
			MTP_flags(0),
			MTP_int(shortcutId),
			MTPVector<MTPint>(),
			MTP_long(hash))
	).done([=](const MTPmessages_Messages &result) {
		parse(shortcutId, result);
	}).fail([=] {
		_requests.remove(shortcutId);
	}).send();
}

void ShortcutMessages::parse(
		BusinessShortcutId shortcutId,
		const MTPmessages_Messages &list) {
	auto &request = _requests[shortcutId];
	request.lastReceived = crl::now();
	request.requestId = 0;
	if (!_clearTimer.isActive()) {
		_clearTimer.callOnce(kRequestTimeLimit * 2);
	}

	list.match([&](const MTPDmessages_messagesNotModified &data) {
	}, [&](const auto &data) {
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());

		const auto &messages = data.vmessages().v;
		if (messages.isEmpty()) {
			clearNotSending(shortcutId);
			return;
		}
		auto received = base::flat_set<not_null<HistoryItem*>>();
		auto clear = base::flat_set<not_null<HistoryItem*>>();
		auto &list = _data.emplace(shortcutId, List()).first->second;
		for (const auto &message : messages) {
			if (const auto item = append(shortcutId, list, message)) {
				received.emplace(item);
			}
		}
		for (const auto &owned : list.items) {
			const auto item = owned.get();
			if (!item->isSending() && !received.contains(item)) {
				clear.emplace(item);
			}
		}
		updated(shortcutId, received, clear);
	});
}

HistoryItem *ShortcutMessages::append(
		BusinessShortcutId shortcutId,
		List &list,
		const MTPMessage &message) {
	const auto id = message.match([&](const auto &data) {
		return data.vid().v;
	});
	const auto i = list.itemById.find(id);
	if (i != end(list.itemById)) {
		const auto existing = i->second;
		message.match([&](const MTPDmessage &data) {
			if (data.is_edit_hide()) {
				existing->applyEdition(HistoryMessageEdition(_session, data));
			} else {
				existing->updateSentContent({
					qs(data.vmessage()),
					Api::EntitiesFromMTP(
						_session,
						data.ventities().value_or_empty())
				}, data.vmedia());
				existing->updateReplyMarkup(
					HistoryMessageMarkupData(data.vreply_markup()));
				existing->updateForwardedInfo(data.vfwd_from());
			}
			existing->updateDate(data.vdate().v);
			_history->owner().requestItemTextRefresh(existing);
		}, [&](const auto &data) {});
		return existing;
	}

	if (!IsServerMsgId(id)) {
		LOG(("API Error: Bad id in quick reply messages: %1.").arg(id));
		return nullptr;
	}
	const auto item = _session->data().addNewMessage(
		localMessageId(id),
		PrepareMessage(shortcutId, message),
		MessageFlags(), // localFlags
		NewMessageType::Existing);
	if (!item
		|| item->history() != _history
		|| item->shortcutId() != shortcutId) {
		LOG(("API Error: Bad data received in quick reply messages."));
		return nullptr;
	}
	list.items.emplace_back(item);
	list.itemById.emplace(id, item);
	return item;
}

void ShortcutMessages::clearNotSending(BusinessShortcutId shortcutId) {
	const auto i = _data.find(shortcutId);
	if (i == end(_data)) {
		return;
	}
	auto clear = base::flat_set<not_null<HistoryItem*>>();
	for (const auto &owned : i->second.items) {
		if (!owned->isSending() && !owned->hasFailed()) {
			clear.emplace(owned.get());
		}
	}
	updated(shortcutId, {}, clear);
}

void ShortcutMessages::updated(
		BusinessShortcutId shortcutId,
		const base::flat_set<not_null<HistoryItem*>> &added,
		const base::flat_set<not_null<HistoryItem*>> &clear) {
	if (!clear.empty()) {
		for (const auto &item : clear) {
			item->destroy();
		}
	}
	const auto i = _data.find(shortcutId);
	if (i != end(_data)) {
		sort(i->second);
	}
	if (!added.empty() || !clear.empty()) {
		_updates.fire_copy(shortcutId);
	}
}

void ShortcutMessages::sort(List &list) {
	ranges::sort(list.items, ranges::less(), &HistoryItem::position);
}

void ShortcutMessages::remove(not_null<const HistoryItem*> item) {
	const auto shortcutId = item->shortcutId();
	const auto i = _data.find(shortcutId);
	Assert(i != end(_data));
	auto &list = i->second;

	if (!item->isSending() && !item->hasFailed()) {
		list.itemById.remove(lookupId(item));
	}
	const auto k = ranges::find(list.items, item, &OwnedItem::get);
	Assert(k != list.items.end());
	k->release();
	list.items.erase(k);

	if (list.items.empty()) {
		_data.erase(i);
	}
	_updates.fire_copy(shortcutId);
	updateCount(shortcutId);
}

uint64 ShortcutMessages::countListHash(const List &list) const {
	using namespace Api;

	auto hash = HashInit();
	auto &&serverside = ranges::views::all(
		list.items
	) | ranges::views::filter([](const OwnedItem &item) {
		return !item->isSending() && !item->hasFailed();
	}) | ranges::views::reverse;
	for (const auto &item : serverside) {
		HashUpdate(hash, lookupId(item.get()).bare);
		if (const auto edited = item->Get<HistoryMessageEdited>()) {
			HashUpdate(hash, edited->date);
		} else {
			HashUpdate(hash, TimeId(0));
		}
	}
	return HashFinalize(hash);
}

MTPInputQuickReplyShortcut ShortcutIdToMTP(
		not_null<Main::Session*> session,
		BusinessShortcutId id) {
	return id
		? MTP_inputQuickReplyShortcut(MTP_string(
			session->data().shortcutMessages().lookupShortcut(id).name))
		: MTPInputQuickReplyShortcut();
}

} // namespace Data
