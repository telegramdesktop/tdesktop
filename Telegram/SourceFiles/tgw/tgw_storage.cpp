/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_storage.h"

#include "data/data_channel.h"
#include "data/data_saved_messages.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "tgw/tgw_option.h"
#include "twidget/sdk_policy.h"

#include <atomic>

namespace Tgw {
namespace {

struct Areas {
	twidget::StorageArea local;
	twidget::StorageArea session;
	uint64 localSerial = 0;
	uint64 sessionSerial = 0;
};

struct Host {
	not_null<Main::Session*> session;
	FullMsgId key;
	uint64 clearedThrough = 0;
	bool retired = false;
	bool stale = false;
	bool cut = false;
};

using Messages = base::flat_map<FullMsgId, Areas>;

struct Registry {
	base::flat_map<not_null<Main::Session*>, Messages> sessions;
	base::flat_map<uint64, Host> hosts;
	uint64 hostSerial = 0;
	uint64 commitSerial = 0;
};

std::atomic<uint64> StorageDeliverySerial = 0;

[[nodiscard]] Registry &Stores() {
	static auto result = Registry();
	return result;
}

[[nodiscard]] Messages *Find(not_null<Main::Session*> session) {
	auto &sessions = Stores().sessions;
	const auto i = sessions.find(session);
	return (i != end(sessions)) ? &i->second : nullptr;
}

[[nodiscard]] Messages *Tracked(not_null<Main::Session*> session) {
	const auto messages = Find(session);
	return (messages && (!messages->empty() || !Stores().hosts.empty()))
		? messages
		: nullptr;
}

[[nodiscard]] Main::Session *Owner(not_null<const HistoryItem*> item) {
	if (!item->isHistoryEntry()) {
		return nullptr;
	}
	const auto session = &item->history()->session();
	return Find(session) ? session : nullptr;
}

void ClearIf(
		not_null<Main::Session*> session,
		Fn<bool(FullMsgId)> matches,
		bool cut) {
	const auto messages = Tracked(session);
	if (!messages) {
		return;
	}
	const auto clearedThrough = StorageDeliverySerial.load();
	messages->erase(
		ranges::remove_if(*messages, [&](const auto &entry) {
			return matches(entry.first);
		}),
		end(*messages));
	for (auto &[id, host] : Stores().hosts) {
		if (host.session == session && matches(host.key)) {
			host.clearedThrough = clearedThrough;
			host.stale = true;
			host.cut = host.cut || cut;
		}
	}
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
		ClearIf(session, [&](FullMsgId key) {
			return keys.contains(key);
		}, true);
	}
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
	ClearIf(session, [&](FullMsgId key) {
		return set.contains(key.msg) && matches(key);
	}, true);
}

void ClearLoaded(
		not_null<History*> history,
		Fn<bool(not_null<HistoryItem*>)> matches) {
	const auto peer = history->peer->id;
	const auto owner = &history->owner();
	ClearIf(&history->session(), [&](FullMsgId key) {
		if (key.peer != peer) {
			return false;
		}
		const auto item = owner->message(key);
		return item && matches(item);
	}, true);
}

void Move(
		not_null<Main::Session*> session,
		FullMsgId from,
		FullMsgId to) {
	const auto messages = (from != to) ? Find(session) : nullptr;
	if (!messages) {
		return;
	}
	if (auto moved = messages->take(from)) {
		const auto i = messages->find(to);
		if (i == end(*messages)) {
			messages->emplace(to, std::move(*moved));
		} else {
			auto &entry = i->second;
			if (moved->localSerial > entry.localSerial) {
				entry.local = std::move(moved->local);
				entry.localSerial = moved->localSerial;
			}
			if (moved->sessionSerial > entry.sessionSerial) {
				entry.session = std::move(moved->session);
				entry.sessionSerial = moved->sessionSerial;
			}
		}
	}
	for (auto &[id, host] : Stores().hosts) {
		if (host.session == session && (host.key == from || host.key == to)) {
			host.key = to;
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
	const auto local = (area == "local");
	const auto messages = Find(i->second.session);
	if ((!local && area != "session") || !messages) {
		return;
	}
	auto &entry = (*messages)[i->second.key];
	const auto serial = ++registry.commitSerial;
	if (local) {
		entry.local = std::move(values);
		entry.localSerial = serial;
	} else {
		entry.session = std::move(values);
		entry.sessionSerial = serial;
	}
}

void Forget(not_null<Main::Session*> session) {
	auto &registry = Stores();
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
	if (!Enabled() || sessions.contains(session)) {
		return;
	}
	sessions.emplace(session, Messages());
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
	session->lifetime().add([=] {
		Forget(session);
	});
}

twidget::WidgetStorage StoredAreas(not_null<HistoryItem*> item) {
	if (const auto session = Owner(item)) {
		const auto &messages = *Find(session);
		const auto i = messages.find(item->fullId());
		if (i != end(messages)) {
			return {
				.local = i->second.local,
				.session = i->second.session,
			};
		}
	}
	return {};
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
	Stores().hosts.remove(id);
}

void ClearStorage(not_null<Main::Session*> session) {
	ClearIf(session, [](FullMsgId) { return true; }, false);
}

void ClearChatStorage(not_null<Main::Session*> session, PeerId peer) {
	ClearIf(session, [=](FullMsgId key) { return key.peer == peer; }, true);
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

void ClearHistoryStorage(not_null<History*> history, MsgId till) {
	const auto peer = history->peer->id;
	ClearIf(&history->session(), [=](FullMsgId key) {
		return (key.peer == peer)
			&& IsServerMsgId(key.msg)
			&& (key.msg <= till);
	}, true);
}

void ClearTopicStorage(not_null<History*> history, MsgId rootId) {
	ClearLoaded(history, [=](not_null<HistoryItem*> item) {
		return (item->topicRootId() == rootId);
	});
}

void ClearSublistStorage(
		not_null<Data::SavedMessages*> saved,
		PeerId sublist) {
	const auto session = &saved->session();
	if (!Tracked(session)) {
		return;
	}
	const auto parent = saved->parentChat();
	const auto peer = parent ? parent->id : session->userPeerId();
	if (const auto history = session->data().historyLoaded(peer)) {
		ClearLoaded(history, [=](not_null<HistoryItem*> item) {
			return (item->sublistPeerId() == sublist);
		});
	}
}

void MoveStorage(not_null<HistoryItem*> from, not_null<HistoryItem*> to) {
	const auto session = Owner(from);
	if (session && session == Owner(to)) {
		Move(session, from->fullId(), to->fullId());
	}
}

} // namespace Tgw
