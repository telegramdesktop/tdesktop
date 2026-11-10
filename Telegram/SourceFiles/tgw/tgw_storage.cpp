/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_storage.h"

#include "base/timer.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_saved_messages.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/details/storage_file_utilities.h"
#include "tgw/tgw_storage_disk.h"
#include "twidget/sdk_policy.h"

#include <atomic>

namespace Tgw {
namespace {

constexpr auto kWriteDelay = crl::time(1000);
constexpr auto kMaxExpireDelay = crl::time(86400);

enum class Cut {
	None,
	Retired,
	All,
};

struct Areas {
	twidget::StorageArea local;
	twidget::StorageArea session;
	StorageMeta meta;
	uint64 localSerial = 0;
	uint64 sessionSerial = 0;
	bool dirty = false;
};

struct Host {
	not_null<Main::Session*> session;
	FullMsgId key;
	StorageMeta meta;
	uint64 clearedThrough = 0;
	bool retired = false;
	bool stale = false;
	bool cut = false;
	bool visible = false;
};

using Messages = base::flat_map<FullMsgId, Areas>;
using Matches = Fn<bool(FullMsgId, const StorageMeta &)>;

struct Store {
	Messages messages;
	std::optional<StorageDisk> disk;
	base::flat_set<FullMsgId> cut;
	base::flat_set<FullMsgId> pending;
	base::Timer writeTimer;
	base::Timer expireTimer;
};

struct Registry {
	base::flat_map<not_null<Main::Session*>, std::unique_ptr<Store>> sessions;
	base::flat_map<uint64, Host> hosts;
	uint64 hostSerial = 0;
	uint64 commitSerial = 0;
};

std::atomic<uint64> StorageDeliverySerial = 0;

[[nodiscard]] Registry &Stores() {
	static auto result = Registry();
	return result;
}

[[nodiscard]] Store *Find(not_null<Main::Session*> session) {
	auto &sessions = Stores().sessions;
	const auto i = sessions.find(session);
	return (i != end(sessions)) ? i->second.get() : nullptr;
}

[[nodiscard]] Store *Tracked(not_null<Main::Session*> session) {
	const auto store = Find(session);
	return (store
		&& (!store->messages.empty()
			|| !store->pending.empty()
			|| !Stores().hosts.empty()
			|| (store->disk && !store->disk->empty())))
		? store
		: nullptr;
}

[[nodiscard]] Main::Session *Owner(not_null<const HistoryItem*> item) {
	if (!item->isHistoryEntry()) {
		return nullptr;
	}
	const auto session = &item->history()->session();
	return Find(session) ? session : nullptr;
}

[[nodiscard]] StorageMeta MetaOf(not_null<const HistoryItem*> item) {
	return {
		.date = item->date(),
		.expires = item->ttlDestroyAt(),
		.topicRootId = item->topicRootId(),
		.sublistPeerId = item->sublistPeerId(),
		.from = item->from()->id,
	};
}

[[nodiscard]] StorageMeta CurrentMeta(
		not_null<Main::Session*> session,
		FullMsgId key,
		const StorageMeta &stored) {
	const auto item = session->data().message(key);
	return item ? MetaOf(item) : stored;
}

void ScheduleExpiry(Store &store) {
	const auto next = store.disk ? store.disk->nextExpiry() : TimeId();
	if (!next) {
		store.expireTimer.cancel();
		return;
	}
	const auto delay = crl::time(next) - crl::time(base::unixtime::now());
	const auto timeout = std::clamp(delay, crl::time(0), kMaxExpireDelay);
	store.expireTimer.callOnce(timeout * crl::time(1000));
}

void RefreshDisk(Store &store, FullMsgId key, const StorageMeta &meta) {
	if (store.disk && store.disk->refresh(key, meta)) {
		ScheduleExpiry(store);
	}
}

void Remove(Store &store, Matches matches) {
	if (store.disk && store.disk->remove(std::move(matches))) {
		ScheduleExpiry(store);
	}
}

void UpdateMeta(
		Store &store,
		FullMsgId key,
		Areas &entry,
		const StorageMeta &meta) {
	entry.meta = meta;
	RefreshDisk(store, key, meta);
}

[[nodiscard]] Areas *Load(Store &store, FullMsgId key) {
	const auto i = store.messages.find(key);
	if (i != end(store.messages)) {
		return &i->second;
	}
	auto local = store.disk ? store.disk->read(key) : std::nullopt;
	if (!local) {
		return nullptr;
	}
	return &store.messages.emplace(key, Areas{
		.local = std::move(*local),
		.meta = store.disk->lookup(key).value_or(StorageMeta()),
	}).first->second;
}

[[nodiscard]] Areas &Ensure(
		Store &store,
		FullMsgId key,
		const StorageMeta &meta) {
	if (const auto entry = Load(store, key)) {
		return *entry;
	}
	return store.messages.emplace(key, Areas{ .meta = meta }).first->second;
}

[[nodiscard]] std::optional<Areas> Take(Store &store, FullMsgId key) {
	return Load(store, key) ? store.messages.take(key) : std::nullopt;
}

void MarkDirty(Store &store, Areas &entry) {
	if (!store.disk) {
		return;
	}
	entry.dirty = true;
	if (!store.writeTimer.isActive()) {
		store.writeTimer.callOnce(kWriteDelay);
	}
}

void WriteOne(
		not_null<Main::Session*> session,
		Store &store,
		FullMsgId key,
		Areas &entry) {
	if (!entry.dirty || !store.disk || !IsServerMsgId(key.msg)) {
		return;
	}
	entry.meta = CurrentMeta(session, key, entry.meta);
	if (store.disk->write(key, entry.meta, entry.local)) {
		ScheduleExpiry(store);
	}
	entry.dirty = false;
}

void WriteKey(not_null<Main::Session*> session, FullMsgId key) {
	const auto store = Find(session);
	if (!store) {
		return;
	}
	const auto i = store->messages.find(key);
	if (i != end(store->messages)) {
		WriteOne(session, *store, key, i->second);
	}
}

void WriteAll(not_null<Main::Session*> session) {
	const auto store = Find(session);
	if (!store) {
		return;
	}
	store->writeTimer.cancel();
	for (auto &[key, entry] : store->messages) {
		WriteOne(session, *store, key, entry);
	}
}

void FinishWrites(not_null<Main::Session*> session) {
	WriteAll(session);
	const auto store = Find(session);
	if (store && store->disk) {
		store->disk->flush();
	}
}

void Flush(const Host &host) {
	if (!host.cut) {
		WriteKey(host.session, host.key);
	}
}

void Refresh(not_null<Main::Session*> session) {
	const auto store = Find(session);
	if (!store) {
		return;
	}
	for (auto &[key, entry] : store->messages) {
		entry.meta = CurrentMeta(session, key, entry.meta);
	}
	for (auto &[id, host] : Stores().hosts) {
		if (host.session == session) {
			host.meta = CurrentMeta(session, host.key, host.meta);
		}
	}
	if (!store->disk) {
		return;
	}
	const auto owner = &session->data();
	const auto changed = store->disk->refresh([&](FullMsgId key) {
		const auto item = owner->message(key);
		return item ? std::make_optional(MetaOf(item)) : std::nullopt;
	});
	if (changed) {
		ScheduleExpiry(*store);
	}
}

void Remember(
		not_null<Main::Session*> session,
		not_null<const HistoryItem*> item) {
	const auto store = item->isHistoryEntry() ? Find(session) : nullptr;
	if (!store) {
		return;
	}
	const auto key = item->fullId();
	store->cut.remove(key);
	store->pending.remove(key);
	if (!Tracked(session)) {
		return;
	}
	const auto meta = MetaOf(item);
	for (auto &[id, host] : Stores().hosts) {
		if (host.session == session && host.key == key) {
			host.meta = meta;
		}
	}
	const auto i = store->messages.find(key);
	if (i != end(store->messages)) {
		i->second.meta = meta;
	}
	RefreshDisk(*store, key, meta);
}

void ClearIf(not_null<Main::Session*> session, Matches matches, Cut cut) {
	const auto store = Tracked(session);
	if (!store) {
		return;
	}
	const auto selected = [&](FullMsgId key, const StorageMeta &stored) {
		return matches(key, CurrentMeta(session, key, stored));
	};
	const auto clearedThrough = StorageDeliverySerial.load();
	auto &messages = store->messages;
	messages.erase(
		ranges::remove_if(messages, [&](const auto &entry) {
			return selected(entry.first, entry.second.meta);
		}),
		end(messages));
	for (auto &[id, host] : Stores().hosts) {
		if (host.session != session || !selected(host.key, host.meta)) {
			continue;
		}
		host.clearedThrough = clearedThrough;
		host.stale = true;
		if (cut == Cut::All) {
			host.cut = true;
			store->cut.emplace(host.key);
		} else if (cut == Cut::Retired && host.retired) {
			host.cut = true;
		}
	}
	if (cut == Cut::All) {
		auto &pending = store->pending;
		for (auto i = begin(pending); i != end(pending);) {
			if (selected(*i, StorageMeta())) {
				store->cut.emplace(*i);
				i = pending.erase(i);
			} else {
				++i;
			}
		}
	}
	Remove(*store, [&](FullMsgId key, const StorageMeta &stored) {
		const auto result = selected(key, stored);
		if (result && cut == Cut::All) {
			store->cut.emplace(key);
		}
		return result;
	});
}

void ClearDestroyed(
		not_null<Main::Session*> session,
		const std::vector<not_null<HistoryItem*>> &items) {
	if (!Tracked(session)) {
		return;
	}
	auto list = std::vector<FullMsgId>();
	list.reserve(items.size());
	for (const auto &item : items) {
		if (item->isHistoryEntry()) {
			list.push_back(item->fullId());
		}
	}
	const auto keys = base::flat_set<FullMsgId>(begin(list), end(list));
	if (!keys.empty()) {
		ClearIf(session, [&](FullMsgId key, const StorageMeta &) {
			return keys.contains(key);
		}, Cut::All);
	}
}

void ClearChat(not_null<Main::Session*> session, PeerId peer, Cut cut) {
	ClearIf(session, [=](FullMsgId key, const StorageMeta &) {
		return (key.peer == peer);
	}, cut);
}

void ClearIds(
		not_null<Main::Session*> session,
		const QVector<MTPint> &ids,
		Fn<bool(FullMsgId)> matches) {
	if (!Tracked(session)) {
		return;
	}
	auto list = std::vector<MsgId>();
	list.reserve(ids.size());
	for (const auto &id : ids) {
		list.push_back(id.v);
	}
	const auto set = base::flat_set<MsgId>(begin(list), end(list));
	ClearIf(session, [&](FullMsgId key, const StorageMeta &) {
		return set.contains(key.msg) && matches(key);
	}, Cut::Retired);
}

void Move(
		not_null<Main::Session*> session,
		FullMsgId from,
		FullMsgId to) {
	const auto store = (from != to) ? Find(session) : nullptr;
	if (!store) {
		return;
	}
	if (store->cut.remove(from)) {
		store->cut.emplace(to);
	}
	store->pending.remove(from);
	if (auto moved = Take(*store, from)) {
		auto entry = Load(*store, to);
		if (!entry) {
			entry = &store->messages.emplace(
				to,
				std::move(*moved)).first->second;
		} else {
			if (moved->localSerial > entry->localSerial) {
				entry->local = std::move(moved->local);
				entry->localSerial = moved->localSerial;
			}
			if (moved->sessionSerial > entry->sessionSerial) {
				entry->session = std::move(moved->session);
				entry->sessionSerial = moved->sessionSerial;
			}
		}
		UpdateMeta(*store, to, *entry, CurrentMeta(session, to, entry->meta));
		MarkDirty(*store, *entry);
		if (store->disk && store->disk->lookup(from)) {
			Remove(*store, [&](FullMsgId key, const StorageMeta &) {
				return (key == from);
			});
		}
	}
	for (auto &[id, host] : Stores().hosts) {
		if (host.session == session && (host.key == from || host.key == to)) {
			host.key = to;
			host.meta = CurrentMeta(session, to, host.meta);
			host.stale = true;
		}
	}
}

void Apply(
		uint64 id,
		uint64 delivered,
		const std::string &area,
		twidget::StorageArea &&values) {
	auto &registry = Stores();
	const auto i = registry.hosts.find(id);
	if (i == end(registry.hosts)
		|| i->second.cut
		|| delivered <= i->second.clearedThrough) {
		return;
	}
	auto &host = i->second;
	const auto local = (area == "local");
	const auto store = Find(host.session);
	if ((!local && area != "session") || !store) {
		return;
	}
	host.meta = CurrentMeta(host.session, host.key, host.meta);
	auto &entry = Ensure(*store, host.key, host.meta);
	UpdateMeta(*store, host.key, entry, host.meta);
	const auto serial = ++registry.commitSerial;
	if (!local) {
		entry.session = std::move(values);
		entry.sessionSerial = serial;
		return;
	}
	if (entry.local != values) {
		entry.local = std::move(values);
		MarkDirty(*store, entry);
	}
	entry.localSerial = serial;
	if (!host.visible) {
		WriteOne(host.session, *store, host.key, entry);
	}
}

void Expire(not_null<Main::Session*> session) {
	const auto store = Find(session);
	if (!store || !store->disk) {
		return;
	}
	Refresh(session);
	const auto now = base::unixtime::now();
	ClearIf(session, [=](FullMsgId, const StorageMeta &meta) {
		return (meta.expires > 0) && (meta.expires <= now);
	}, Cut::Retired);
	ScheduleExpiry(*store);
}

void Forget(not_null<Main::Session*> session) {
	auto &registry = Stores();
	FinishWrites(session);
	registry.sessions.remove(session);
	for (auto i = begin(registry.hosts); i != end(registry.hosts);) {
		if (i->second.session == session) {
			i = registry.hosts.erase(i);
		} else {
			++i;
		}
	}
}

} // namespace

void StartStorage(not_null<Main::Session*> session) {
	auto &sessions = Stores().sessions;
	if (sessions.contains(session)) {
		return;
	}
	const auto folder = StorageFolder(&session->account());
	auto key = session->local().peekLegacyLocalKey();
	const auto store = sessions.emplace(
		session,
		std::make_unique<Store>()).first->second.get();
	if (key) {
		store->disk.emplace(folder, std::move(key), session->uniqueId());
	}
	store->writeTimer.setCallback([=] {
		WriteAll(session);
	});
	store->expireTimer.setCallback([=] {
		Expire(session);
	});
	session->data().itemsAboutToBeDestroyed(
	) | rpl::on_next([=](std::vector<not_null<HistoryItem*>> items) {
		ClearDestroyed(session, items);
	}, session->lifetime());
	session->data().itemIdChanged(
	) | rpl::on_next([=](const Data::Session::IdChange &change) {
		Move(
			session,
			FullMsgId(change.newId.peer, change.oldId),
			change.newId);
	}, session->lifetime());
	session->data().itemRemoved(
	) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		Remember(session, item);
	}, session->lifetime());
	session->data().sessionDataAboutToBeCleared(
	) | rpl::on_next([=] {
		Refresh(session);
		FinishWrites(session);
	}, session->lifetime());
	session->lifetime().add([=] {
		Forget(session);
	});
	Expire(session);
}

twidget::WidgetStorage StoredAreas(not_null<HistoryItem*> item) {
	const auto session = Owner(item);
	if (!session) {
		return {};
	}
	const auto store = Find(session);
	const auto key = item->fullId();
	const auto entry = Load(*store, key);
	if (!entry) {
		return {};
	}
	UpdateMeta(*store, key, *entry, MetaOf(item));
	return {
		.local = entry->local,
		.session = entry->session,
	};
}

std::optional<HeightRecord> StoredHeight(not_null<HistoryItem*> item) {
	const auto session = Owner(item);
	if (!session) {
		return std::nullopt;
	}
	const auto store = Find(session);
	return store->disk
		? store->disk->height(item->fullId())
		: std::nullopt;
}

void StoreHeight(not_null<HistoryItem*> item, HeightRecord height) {
	const auto session = Owner(item);
	if (!session) {
		return;
	}
	const auto store = Find(session);
	const auto key = item->fullId();
	if (!store->disk || store->cut.contains(key)) {
		return;
	} else if (!IsServerMsgId(key.msg)) {
		store->pending.emplace(key);
		return;
	}
	const auto meta = MetaOf(item);
	const auto previous = store->disk->lookup(key);
	const auto expiryChanged = previous
		? (previous->expires != meta.expires)
		: (meta.expires > 0);
	if (store->disk->setHeight(key, meta, height) && expiryChanged) {
		ScheduleExpiry(*store);
	}
}

bool StorageRetiring(not_null<HistoryItem*> item) {
	const auto session = Owner(item);
	if (!session) {
		return false;
	}
	const auto key = item->fullId();
	for (const auto &[id, host] : Stores().hosts) {
		if (host.retired
			&& !host.cut
			&& host.session == session
			&& host.key == key) {
			return true;
		}
	}
	return false;
}

StorageHost OpenStorageHost(not_null<HistoryItem*> item) {
	const auto session = Owner(item);
	if (!session) {
		return {};
	}
	auto &registry = Stores();
	const auto id = ++registry.hostSerial;
	registry.hosts.emplace(id, Host{
		.session = session,
		.key = item->fullId(),
		.meta = MetaOf(item),
	});
	return {
		.id = id,
		.commit = [id](
				const std::string &area,
				const std::map<std::string, std::string> &values) {
			const auto delivered = ++StorageDeliverySerial;
			crl::on_main([id, delivered, area, values = values]() mutable {
				Apply(id, delivered, area, std::move(values));
			});
		},
	};
}

bool StorageHostStale(uint64 id) {
	const auto &hosts = Stores().hosts;
	const auto i = hosts.find(id);
	return (i != end(hosts)) && i->second.stale;
}

void StorageHostVisibility(uint64 id, bool visible) {
	auto &hosts = Stores().hosts;
	const auto i = hosts.find(id);
	if (i == end(hosts)) {
		return;
	}
	i->second.visible = visible;
	if (!visible) {
		Flush(i->second);
	}
}

void StorageHostRetired(uint64 id) {
	auto &hosts = Stores().hosts;
	const auto i = hosts.find(id);
	if (i == end(hosts)) {
		return;
	}
	i->second.retired = true;
	if (i->second.cut) {
		return;
	}
	const auto &retired = i->second;
	for (auto &[otherId, host] : hosts) {
		if (otherId != id
			&& !host.cut
			&& host.session == retired.session
			&& host.key == retired.key) {
			host.stale = true;
		}
	}
}

void StorageHostReleased(uint64 id) {
	auto &hosts = Stores().hosts;
	const auto i = hosts.find(id);
	if (i != end(hosts)) {
		Flush(i->second);
		hosts.erase(i);
	}
}

void EraseStorage(not_null<Main::Account*> account) {
	const auto &sessions = Stores().sessions;
	const auto i = ranges::find_if(sessions, [&](const auto &entry) {
		return (&entry.first->account() == account);
	});
	const auto store = (i != end(sessions)) ? i->second.get() : nullptr;
	if (store && store->disk) {
		store->disk->erase();
	} else {
		EraseStorageFolder(StorageFolder(account));
	}
	if (store) {
		store->writeTimer.cancel();
		store->expireTimer.cancel();
		ClearIf(i->first, [](FullMsgId, const StorageMeta &) {
			return true;
		}, Cut::None);
	}
}

void EraseDomainStorage(
		not_null<Main::Domain*> domain,
		const QString &dataName) {
	for (auto i = 0; i != Main::Domain::kPremiumMaxAccounts; ++i) {
		auto account = Main::Account(domain, dataName, i);
		EraseStorage(&account);
	}
	Storage::details::Sync();
}

void ClearChatStorage(not_null<Main::Session*> session, PeerId peer) {
	ClearChat(session, peer, Cut::Retired);
}

void DeleteChatStorage(not_null<Main::Session*> session, PeerId peer) {
	ClearChat(session, peer, Cut::All);
}

void ClearMessagesStorage(
		not_null<Main::Session*> session,
		PeerId peer,
		const QVector<MTPint> &ids) {
	ClearIds(session, ids, [=](FullMsgId key) {
		return (key.peer == peer);
	});
}

void ClearNonChannelStorage(
		not_null<Main::Session*> session,
		const QVector<MTPint> &ids) {
	ClearIds(session, ids, [](FullMsgId key) {
		return !peerIsChannel(key.peer) && IsServerMsgId(key.msg);
	});
}

void ClearHistoryStorage(
		not_null<Main::Session*> session,
		PeerId peer,
		MsgId till) {
	if (till <= 0) {
		return;
	}
	ClearIf(session, [=](FullMsgId key, const StorageMeta &) {
		return (key.peer == peer)
			&& IsServerMsgId(key.msg)
			&& (key.msg <= till);
	}, Cut::Retired);
}

void ClearHistoryStorage(not_null<History*> history, MsgId till) {
	ClearHistoryStorage(&history->session(), history->peer->id, till);
}

void DeleteMessageStorage(not_null<HistoryItem*> item) {
	if (const auto session = Owner(item)) {
		const auto deleted = item->fullId();
		ClearIf(session, [=](FullMsgId key, const StorageMeta &) {
			return (key == deleted);
		}, Cut::All);
	}
}

void ClearDatesStorage(
		not_null<History*> history,
		TimeId minDate,
		TimeId maxDate) {
	const auto peer = history->peer->id;
	ClearIf(&history->session(), [=](FullMsgId key, const StorageMeta &meta) {
		return (key.peer == peer)
			&& IsServerMsgId(key.msg)
			&& (meta.date > minDate)
			&& (meta.date < maxDate);
	}, Cut::Retired);
}

void ClearTopicStorage(not_null<History*> history, MsgId rootId) {
	const auto peer = history->peer->id;
	ClearIf(&history->session(), [=](FullMsgId key, const StorageMeta &meta) {
		return (key.peer == peer) && (meta.topicRootId == rootId);
	}, Cut::Retired);
}

void ClearSublistStorage(
		not_null<Data::SavedMessages*> saved,
		PeerId sublist) {
	const auto session = &saved->session();
	const auto parent = saved->parentChat();
	const auto peer = parent ? parent->id : session->userPeerId();
	ClearIf(session, [=](FullMsgId key, const StorageMeta &meta) {
		return (key.peer == peer) && (meta.sublistPeerId == sublist);
	}, Cut::Retired);
}

void ClearParticipantStorage(
		not_null<Main::Session*> session,
		PeerId peer,
		PeerId from) {
	ClearIf(session, [=](FullMsgId key, const StorageMeta &meta) {
		return (key.peer == peer) && (meta.from == from);
	}, Cut::Retired);
}

void MoveStorage(not_null<HistoryItem*> from, not_null<HistoryItem*> to) {
	const auto session = Owner(from);
	if (session && session == Owner(to)) {
		Move(session, from->fullId(), to->fullId());
	}
}

} // namespace Tgw
