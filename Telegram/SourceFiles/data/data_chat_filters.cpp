/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_chat_filters.h"

#include "history/history.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "dialogs/dialogs_main_list.h"
#include "ui/ui_utility.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRefreshSuggestedTimeout = 7200 * crl::time(1000);
constexpr auto kLoadExceptionsAfter = 100;
constexpr auto kLoadExceptionsPerRequest = 100;

} // namespace

ChatFilter::ChatFilter(
	FilterId id,
	const QString &title,
	const QString &iconEmoji,
	Flags flags,
	base::flat_set<not_null<History*>> always,
	std::vector<not_null<History*>> pinned,
	base::flat_set<not_null<History*>> never)
: _id(id)
, _title(title)
, _iconEmoji(iconEmoji)
, _always(std::move(always))
, _pinned(std::move(pinned))
, _never(std::move(never))
, _flags(flags) {
}

ChatFilter ChatFilter::FromTL(
		const MTPDialogFilter &data,
		not_null<Session*> owner) {
	return data.match([&](const MTPDdialogFilter &data) {
		const auto flags = (data.is_contacts() ? Flag::Contacts : Flag(0))
			| (data.is_non_contacts() ? Flag::NonContacts : Flag(0))
			| (data.is_groups() ? Flag::Groups : Flag(0))
			| (data.is_broadcasts() ? Flag::Channels : Flag(0))
			| (data.is_bots() ? Flag::Bots : Flag(0))
			| (data.is_exclude_muted() ? Flag::NoMuted : Flag(0))
			| (data.is_exclude_read() ? Flag::NoRead : Flag(0))
			| (data.is_exclude_archived() ? Flag::NoArchived : Flag(0));
		auto &&to_histories = ranges::views::transform([&](
				const MTPInputPeer &data) {
			const auto peer = data.match([&](const MTPDinputPeerUser &data) {
				const auto user = owner->user(data.vuser_id().v);
				user->setAccessHash(data.vaccess_hash().v);
				return (PeerData*)user;
			}, [&](const MTPDinputPeerChat &data) {
				return (PeerData*)owner->chat(data.vchat_id().v);
			}, [&](const MTPDinputPeerChannel &data) {
				const auto channel = owner->channel(data.vchannel_id().v);
				channel->setAccessHash(data.vaccess_hash().v);
				return (PeerData*)channel;
			}, [&](const MTPDinputPeerSelf &data) {
				return (PeerData*)owner->session().user();
			}, [&](const auto &data) {
				return (PeerData*)nullptr;
			});
			return peer ? owner->history(peer).get() : nullptr;
		}) | ranges::views::filter([](History *history) {
			return history != nullptr;
		}) | ranges::views::transform([](History *history) {
			return not_null<History*>(history);
		});
		auto &&always = ranges::views::concat(
			data.vinclude_peers().v
		) | to_histories;
		auto pinned = ranges::views::all(
			data.vpinned_peers().v
		) | to_histories | ranges::to_vector;
		auto &&never = ranges::views::all(
			data.vexclude_peers().v
		) | to_histories;
		auto &&all = ranges::views::concat(always, pinned);
		auto list = base::flat_set<not_null<History*>>{
			all.begin(),
			all.end()
		};
		return ChatFilter(
			data.vid().v,
			qs(data.vtitle()),
			qs(data.vemoticon().value_or_empty()),
			flags,
			std::move(list),
			std::move(pinned),
			{ never.begin(), never.end() });
	});
}

MTPDialogFilter ChatFilter::tl(FilterId replaceId) const {
	using TLFlag = MTPDdialogFilter::Flag;
	const auto flags = TLFlag(0)
		| ((_flags & Flag::Contacts) ? TLFlag::f_contacts : TLFlag(0))
		| ((_flags & Flag::NonContacts) ? TLFlag::f_non_contacts : TLFlag(0))
		| ((_flags & Flag::Groups) ? TLFlag::f_groups : TLFlag(0))
		| ((_flags & Flag::Channels) ? TLFlag::f_broadcasts : TLFlag(0))
		| ((_flags & Flag::Bots) ? TLFlag::f_bots : TLFlag(0))
		| ((_flags & Flag::NoMuted) ? TLFlag::f_exclude_muted : TLFlag(0))
		| ((_flags & Flag::NoRead) ? TLFlag::f_exclude_read : TLFlag(0))
		| ((_flags & Flag::NoArchived)
			? TLFlag::f_exclude_archived
			: TLFlag(0));
	auto always = _always;
	auto pinned = QVector<MTPInputPeer>();
	pinned.reserve(_pinned.size());
	for (const auto history : _pinned) {
		pinned.push_back(history->peer->input);
		always.remove(history);
	}
	auto include = QVector<MTPInputPeer>();
	include.reserve(always.size());
	for (const auto history : always) {
		include.push_back(history->peer->input);
	}
	auto never = QVector<MTPInputPeer>();
	never.reserve(_never.size());
	for (const auto history : _never) {
		never.push_back(history->peer->input);
	}
	return MTP_dialogFilter(
		MTP_flags(flags | TLFlag::f_emoticon),
		MTP_int(replaceId ? replaceId : _id),
		MTP_string(_title),
		MTP_string(_iconEmoji),
		MTP_vector<MTPInputPeer>(pinned),
		MTP_vector<MTPInputPeer>(include),
		MTP_vector<MTPInputPeer>(never));
}

FilterId ChatFilter::id() const {
	return _id;
}

QString ChatFilter::title() const {
	return _title;
}

QString ChatFilter::iconEmoji() const {
	return _iconEmoji;
}

ChatFilter::Flags ChatFilter::flags() const {
	return _flags;
}

const base::flat_set<not_null<History*>> &ChatFilter::always() const {
	return _always;
}

const std::vector<not_null<History*>> &ChatFilter::pinned() const {
	return _pinned;
}

const base::flat_set<not_null<History*>> &ChatFilter::never() const {
	return _never;
}

bool ChatFilter::contains(not_null<History*> history) const {
	const auto flag = [&] {
		const auto peer = history->peer;
		if (const auto user = peer->asUser()) {
			return user->isBot()
				? Flag::Bots
				: user->isContact()
				? Flag::Contacts
				: Flag::NonContacts;
		} else if (const auto chat = peer->asChat()) {
			return Flag::Groups;
		} else if (const auto channel = peer->asChannel()) {
			if (channel->isBroadcast()) {
				return Flag::Channels;
			} else {
				return Flag::Groups;
			}
		} else {
			Unexpected("Peer type in ChatFilter::contains.");
		}
	}();
	if (_never.contains(history)) {
		return false;
	}
	return false
		|| ((_flags & flag)
			&& (!(_flags & Flag::NoMuted)
				|| !history->mute()
				|| (history->hasUnreadMentions()
					&& history->folderKnown()
					&& !history->folder()))
			&& (!(_flags & Flag::NoRead)
				|| history->unreadCount()
				|| history->unreadMark()
				|| history->hasUnreadMentions()
				|| history->fakeUnreadWhileOpened())
			&& (!(_flags & Flag::NoArchived)
				|| (history->folderKnown() && !history->folder())))
		|| _always.contains(history);
}

ChatFilters::ChatFilters(not_null<Session*> owner) : _owner(owner) {
	crl::on_main(&owner->session(), [=] { load(); });
}

ChatFilters::~ChatFilters() = default;

not_null<Dialogs::MainList*> ChatFilters::chatsList(FilterId filterId) {
	auto &pointer = _chatsLists[filterId];
	if (!pointer) {
		pointer = std::make_unique<Dialogs::MainList>(
			&_owner->session(),
			filterId,
			rpl::single(ChatFilter::kPinnedLimit));
	}
	return pointer.get();
}

void ChatFilters::setPreloaded(const QVector<MTPDialogFilter> &result) {
	_loadRequestId = -1;
	received(result);
	crl::on_main(&_owner->session(), [=] {
		if (_loadRequestId == -1) {
			_loadRequestId = 0;
		}
	});
}

void ChatFilters::load() {
	load(false);
}

void ChatFilters::load(bool force) {
	if (_loadRequestId && !force) {
		return;
	}
	auto &api = _owner->session().api();
	api.request(_loadRequestId).cancel();
	_loadRequestId = api.request(MTPmessages_GetDialogFilters(
	)).done([=](const MTPVector<MTPDialogFilter> &result) {
		received(result.v);
		_loadRequestId = 0;
	}).fail([=](const MTP::Error &error) {
		_loadRequestId = 0;
	}).send();
}

void ChatFilters::received(const QVector<MTPDialogFilter> &list) {
	auto position = 0;
	auto changed = false;
	for (const auto &filter : list) {
		auto parsed = ChatFilter::FromTL(filter, _owner);
		const auto b = begin(_list) + position, e = end(_list);
		const auto i = ranges::find(b, e, parsed.id(), &ChatFilter::id);
		if (i == e) {
			applyInsert(std::move(parsed), position);
			changed = true;
		} else if (i == b) {
			if (applyChange(*b, std::move(parsed))) {
				changed = true;
			}
		} else {
			std::swap(*i, *b);
			applyChange(*b, std::move(parsed));
			changed = true;
		}
		++position;
	}
	while (position < _list.size()) {
		applyRemove(position);
		changed = true;
	}
	if (changed || !_loaded) {
		_loaded = true;
		_listChanged.fire({});
	}
}

void ChatFilters::apply(const MTPUpdate &update) {
	update.match([&](const MTPDupdateDialogFilter &data) {
		if (const auto filter = data.vfilter()) {
			set(ChatFilter::FromTL(*filter, _owner));
		} else {
			remove(data.vid().v);
		}
	}, [&](const MTPDupdateDialogFilters &data) {
		load(true);
	}, [&](const MTPDupdateDialogFilterOrder &data) {
		if (applyOrder(data.vorder().v)) {
			_listChanged.fire({});
		} else {
			load(true);
		}
	}, [](auto&&) {
		Unexpected("Update in ChatFilters::apply.");
	});
}

void ChatFilters::set(ChatFilter filter) {
	if (!filter.id()) {
		return;
	}
	const auto i = ranges::find(_list, filter.id(), &ChatFilter::id);
	if (i == end(_list)) {
		applyInsert(std::move(filter), _list.size());
		_listChanged.fire({});
	} else if (applyChange(*i, std::move(filter))) {
		_listChanged.fire({});
	}
}

void ChatFilters::applyInsert(ChatFilter filter, int position) {
	Expects(position >= 0 && position <= _list.size());

	_list.insert(
		begin(_list) + position,
		ChatFilter(filter.id(), {}, {}, {}, {}, {}, {}));
	applyChange(*(begin(_list) + position), std::move(filter));
}

void ChatFilters::remove(FilterId id) {
	const auto i = ranges::find(_list, id, &ChatFilter::id);
	if (i == end(_list)) {
		return;
	}
	applyRemove(i - begin(_list));
	_listChanged.fire({});
}

void ChatFilters::applyRemove(int position) {
	Expects(position >= 0 && position < _list.size());

	const auto i = begin(_list) + position;
	applyChange(*i, ChatFilter(i->id(), {}, {}, {}, {}, {}, {}));
	_list.erase(i);
}

bool ChatFilters::applyChange(ChatFilter &filter, ChatFilter &&updated) {
	Expects(filter.id() == updated.id());

	const auto id = filter.id();
	const auto exceptionsChanged = filter.always() != updated.always();
	const auto rulesChanged = exceptionsChanged
		|| (filter.flags() != updated.flags())
		|| (filter.never() != updated.never());
	const auto pinnedChanged = (filter.pinned() != updated.pinned());
	if (!rulesChanged
		&& !pinnedChanged
		&& filter.title() == updated.title()
		&& filter.iconEmoji() == updated.iconEmoji()) {
		return false;
	}
	if (rulesChanged) {
		const auto filterList = _owner->chatsFilters().chatsList(id);
		const auto feedHistory = [&](not_null<History*> history) {
			const auto now = updated.contains(history);
			const auto was = filter.contains(history);
			if (now != was) {
				if (now) {
					history->addToChatList(id, filterList);
				} else {
					history->removeFromChatList(id, filterList);
				}
			}
		};
		const auto feedList = [&](not_null<const Dialogs::MainList*> list) {
			for (const auto &entry : *list->indexed()) {
				if (const auto history = entry->history()) {
					feedHistory(history);
				}
			}
		};
		feedList(_owner->chatsList());
		if (const auto folder = _owner->folderLoaded(Data::Folder::kId)) {
			feedList(folder->chatsList());
		}
		if (exceptionsChanged && !updated.always().empty()) {
			_exceptionsToLoad.push_back(id);
			Ui::PostponeCall(&_owner->session(), [=] {
				_owner->session().api().requestMoreDialogsIfNeeded();
			});
		}
	}
	filter = std::move(updated);
	if (pinnedChanged) {
		const auto filterList = _owner->chatsFilters().chatsList(id);
		filterList->pinned()->applyList(filter.pinned());
	}
	return true;
}

bool ChatFilters::applyOrder(const QVector<MTPint> &order) {
	if (order.size() != _list.size()) {
		return false;
	} else if (_list.empty()) {
		return true;
	}
	auto indices = ranges::views::all(
		_list
	) | ranges::views::transform(
		&ChatFilter::id
	) | ranges::to_vector;
	auto b = indices.begin(), e = indices.end();
	for (const auto id : order) {
		const auto i = ranges::find(b, e, id.v);
		if (i == e) {
			return false;
		} else if (i != b) {
			std::swap(*i, *b);
		}
		++b;
	}
	auto changed = false;
	auto begin = _list.begin(), end = _list.end();
	for (const auto id : order) {
		const auto i = ranges::find(begin, end, id.v, &ChatFilter::id);
		Assert(i != end);
		if (i != begin) {
			changed = true;
			std::swap(*i, *begin);
		}
		++begin;
	}
	if (changed) {
		_listChanged.fire({});
	}
	return true;
}

const ChatFilter &ChatFilters::applyUpdatedPinned(
		FilterId id,
		const std::vector<Dialogs::Key> &dialogs) {
	const auto i = ranges::find(_list, id, &ChatFilter::id);
	Assert(i != end(_list));

	auto always = i->always();
	auto pinned = std::vector<not_null<History*>>();
	pinned.reserve(dialogs.size());
	for (const auto &row : dialogs) {
		if (const auto history = row.history()) {
			if (always.contains(history)) {
				pinned.push_back(history);
			} else if (always.size() < ChatFilter::kPinnedLimit) {
				always.insert(history);
				pinned.push_back(history);
			}
		}
	}
	set(ChatFilter(
		id,
		i->title(),
		i->iconEmoji(),
		i->flags(),
		std::move(always),
		std::move(pinned),
		i->never()));
	return *i;
}

void ChatFilters::saveOrder(
		const std::vector<FilterId> &order,
		mtpRequestId after) {
	if (after) {
		_saveOrderAfterId = after;
	}
	const auto api = &_owner->session().api();
	api->request(_saveOrderRequestId).cancel();

	auto ids = QVector<MTPint>();
	ids.reserve(order.size());
	for (const auto id : order) {
		ids.push_back(MTP_int(id));
	}
	const auto wrapped = MTP_vector<MTPint>(ids);

	apply(MTP_updateDialogFilterOrder(wrapped));
	_saveOrderRequestId = api->request(MTPmessages_UpdateDialogFiltersOrder(
		wrapped
	)).afterRequest(_saveOrderAfterId).send();
}

bool ChatFilters::archiveNeeded() const {
	for (const auto &filter : _list) {
		if (!(filter.flags() & ChatFilter::Flag::NoArchived)) {
			return true;
		}
	}
	return false;
}

const std::vector<ChatFilter> &ChatFilters::list() const {
	return _list;
}

rpl::producer<> ChatFilters::changed() const {
	return _listChanged.events();
}

bool ChatFilters::loadNextExceptions(bool chatsListLoaded) {
	if (_exceptionsLoadRequestId) {
		return true;
	} else if (!chatsListLoaded
		&& (_owner->chatsList()->fullSize().current()
			< kLoadExceptionsAfter)) {
		return false;
	}
	auto inputs = QVector<MTPInputDialogPeer>();
	const auto collectExceptions = [&](FilterId id) {
		auto result = QVector<MTPInputDialogPeer>();
		const auto i = ranges::find(_list, id, &ChatFilter::id);
		if (i != end(_list)) {
			result.reserve(i->always().size());
			for (const auto history : i->always()) {
				if (!history->folderKnown()) {
					inputs.push_back(
						MTP_inputDialogPeer(history->peer->input));
				}
			}
		}
		return result;
	};
	while (!_exceptionsToLoad.empty()) {
		const auto id = _exceptionsToLoad.front();
		const auto exceptions = collectExceptions(id);
		if (inputs.size() + exceptions.size() > kLoadExceptionsPerRequest) {
			Assert(!inputs.isEmpty());
			break;
		}
		_exceptionsToLoad.pop_front();
		inputs.append(exceptions);
	}
	if (inputs.isEmpty()) {
		return false;
	}
	const auto api = &_owner->session().api();
	_exceptionsLoadRequestId = api->request(MTPmessages_GetPeerDialogs(
		MTP_vector(inputs)
	)).done([=](const MTPmessages_PeerDialogs &result) {
		_exceptionsLoadRequestId = 0;
		_owner->session().data().histories().applyPeerDialogs(result);
		_owner->session().api().requestMoreDialogsIfNeeded();
	}).fail([=](const MTP::Error &error) {
		_exceptionsLoadRequestId = 0;
		_owner->session().api().requestMoreDialogsIfNeeded();
	}).send();
	return true;
}

void ChatFilters::refreshHistory(not_null<History*> history) {
	if (history->inChatList() && !list().empty()) {
		_owner->refreshChatListEntry(history);
	}
}

void ChatFilters::requestSuggested() {
	if (_suggestedRequestId) {
		return;
	}
	if (_suggestedLastReceived > 0
		&& crl::now() - _suggestedLastReceived < kRefreshSuggestedTimeout) {
		return;
	}
	const auto api = &_owner->session().api();
	_suggestedRequestId = api->request(MTPmessages_GetSuggestedDialogFilters(
	)).done([=](const MTPVector<MTPDialogFilterSuggested> &data) {
		_suggestedRequestId = 0;
		_suggestedLastReceived = crl::now();

		_suggested = ranges::views::all(
			data.v
		) | ranges::views::transform([&](const MTPDialogFilterSuggested &f) {
			return f.match([&](const MTPDdialogFilterSuggested &data) {
				return SuggestedFilter{
					Data::ChatFilter::FromTL(data.vfilter(), _owner),
					qs(data.vdescription())
				};
			});
		}) | ranges::to_vector;

		_suggestedUpdated.fire({});
	}).fail([=](const MTP::Error &error) {
		_suggestedRequestId = 0;
		_suggestedLastReceived = crl::now() + kRefreshSuggestedTimeout / 2;

		_suggestedUpdated.fire({});
	}).send();
}

bool ChatFilters::suggestedLoaded() const {
	return (_suggestedLastReceived > 0);
}

const std::vector<SuggestedFilter> &ChatFilters::suggestedFilters() const {
	return _suggested;
}

rpl::producer<> ChatFilters::suggestedUpdated() const {
	return _suggestedUpdated.events();
}

} // namespace Data
