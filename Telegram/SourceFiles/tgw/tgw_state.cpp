/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_state.h"

#include "base/timer.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "tgw/tgw_media.h"
#include "tgw/tgw_runtime.h"

namespace Tgw {
namespace {

constexpr auto kDefaultPeriod = crl::time(30000);
constexpr auto kMinPeriodSeconds = 5;
constexpr auto kMaxPeriodSeconds = 3600;

using Carriers = base::flat_set<not_null<WidgetMedia*>>;
using Wanted = base::flat_map<uint64, std::vector<FullMsgId>>;

struct PolledWidget {
	std::optional<uint64> hash;
	crl::time period = kDefaultPeriod;
	crl::time nextDue = 0;
	mtpRequestId requestId = 0;
	FullMsgId carrier;
};

struct Poller {
	explicit Poller(not_null<Main::Session*> session);

	const not_null<Main::Session*> session;
	MTP::Sender api;
	base::Timer timer;
	base::flat_map<uint64, PolledWidget> widgets;
};

struct Registry {
	base::flat_map<uint64, Carriers> carriers;
	base::flat_map<not_null<Main::Session*>, std::unique_ptr<Poller>> pollers;
	bool refreshPosted = false;
};

Poller::Poller(not_null<Main::Session*> session)
: session(session)
, api(&session->mtp())
, timer([] { WatchersChanged(); }) {
}

[[nodiscard]] Registry &States() {
	static auto result = Registry();
	return result;
}

void ApplyShared(
		not_null<Main::Session*> session,
		uint64 id,
		const MTPDmessages_widgetPublicData &data) {
	const auto &carriers = States().carriers;
	const auto i = carriers.find(id);
	if (i == end(carriers)) {
		return;
	}
	const auto edit = WidgetStateEdit{
		.publicData = data.vdata().v,
		.publicDataHash = data.vhash().v,
	};
	auto changed = std::vector<not_null<HistoryItem*>>();
	for (const auto &media : i->second) {
		const auto item = media->parent();
		if (&item->history()->session() == session.get()
			&& media->applyState(edit)
			&& (item->media() == media.get())) {
			changed.push_back(item);
		}
	}
	Runtime::StateChanged(
		base::flat_set<not_null<HistoryItem*>>(begin(changed), end(changed)));
}

[[nodiscard]] crl::time Period(int seconds) {
	return std::clamp(seconds, kMinPeriodSeconds, kMaxPeriodSeconds)
		* crl::time(1000);
}

[[nodiscard]] PolledWidget *Finish(
		not_null<Poller*> poller,
		uint64 id,
		mtpRequestId requestId) {
	const auto i = poller->widgets.find(id);
	if (i == end(poller->widgets) || i->second.requestId != requestId) {
		return nullptr;
	}
	i->second.requestId = 0;
	i->second.carrier = FullMsgId();
	return &i->second;
}

void Answered(
		not_null<Poller*> poller,
		uint64 id,
		mtpRequestId requestId,
		const MTPmessages_WidgetPublicData &result) {
	const auto widget = Finish(poller, id, requestId);
	if (!widget) {
		return;
	}
	using NotModified = MTPDmessages_widgetPublicDataNotModified;
	const auto arrived = crl::now();
	const auto seconds = result.match([](const NotModified &data) {
		return data.vperiod().v;
	}, [&](const MTPDmessages_widgetPublicData &data) {
		widget->hash = data.vhash().v;
		ApplyShared(poller->session, id, data);
		return data.vperiod().v;
	});
	widget->period = Period(seconds);
	widget->nextDue = arrived + widget->period;
	WatchersChanged();
}

void Failed(not_null<Poller*> poller, uint64 id, mtpRequestId requestId) {
	if (const auto widget = Finish(poller, id, requestId)) {
		widget->nextDue = crl::now() + widget->period;
		WatchersChanged();
	}
}

bool Send(
		not_null<Poller*> poller,
		uint64 id,
		PolledWidget &widget,
		FullMsgId carrier) {
	const auto item = poller->session->data().message(carrier);
	const auto media = item
		? dynamic_cast<WidgetMedia*>(item->media())
		: nullptr;
	if (!media || media->fields().widgetId != id || !item->isRegular()) {
		return false;
	}
	const auto hash = widget.hash.value_or(
		media->fields().publicDataHash.value_or(0));
	widget.requestId = poller->api.request(MTPmessages_GetWidgetPublicData(
		item->history()->peer->input(),
		MTP_int(item->id),
		MTP_long(hash)
	)).done([=](
			const MTPmessages_WidgetPublicData &result,
			mtpRequestId requestId) {
		Answered(poller, id, requestId, result);
	}).fail([=](const MTP::Error &, mtpRequestId requestId) {
		Failed(poller, id, requestId);
	}).send();
	widget.carrier = carrier;
	return true;
}

void Process(not_null<Poller*> poller, const Wanted &wanted) {
	for (auto &[id, widget] : poller->widgets) {
		const auto i = wanted.find(id);
		if (widget.requestId
			&& (i == end(wanted)
				|| !ranges::contains(i->second, widget.carrier))) {
			poller->api.request(base::take(widget.requestId)).cancel();
			widget.carrier = FullMsgId();
		}
	}
	const auto now = crl::now();
	auto earliest = crl::time(0);
	for (const auto &[id, carriers] : wanted) {
		auto &widget = poller->widgets.try_emplace(id, PolledWidget{
			.nextDue = now + kDefaultPeriod,
		}).first->second;
		if (widget.requestId) {
			continue;
		} else if (now >= widget.nextDue) {
			// A canceled or failed request never brings the next poll closer.
			widget.nextDue = now + widget.period;
			if (Send(poller, id, widget, carriers.front())) {
				continue;
			}
		}
		if (!earliest || widget.nextDue < earliest) {
			earliest = widget.nextDue;
		}
	}
	if (earliest) {
		poller->timer.callOnce(earliest - now);
	} else {
		poller->timer.cancel();
	}
}

void StartPoller(not_null<Main::Session*> session) {
	auto &pollers = States().pollers;
	if (pollers.contains(session)) {
		return;
	}
	pollers.emplace(session, std::make_unique<Poller>(session));
	session->lifetime().add([=] {
		States().pollers.remove(session);
	});
}

void Refresh() {
	auto &registry = States();
	registry.refreshPosted = false;
	auto wanted = base::flat_map<not_null<Main::Session*>, Wanted>();
	for (const auto &item : Runtime::WatchingOnScreen()) {
		const auto media = dynamic_cast<WidgetMedia*>(item->media());
		const auto id = media ? media->fields().widgetId : std::nullopt;
		if (id && item->isRegular()) {
			const auto session = &item->history()->session();
			wanted[session][*id].push_back(item->fullId());
		}
	}
	for (const auto &[session, groups] : wanted) {
		StartPoller(session);
	}
	for (const auto &[session, poller] : registry.pollers) {
		Process(poller.get(), wanted[session]);
	}
}

} // namespace

void RegisterCarrier(not_null<WidgetMedia*> media) {
	if (const auto id = media->fields().widgetId) {
		States().carriers[*id].emplace(media);
	}
}

void UnregisterCarrier(not_null<WidgetMedia*> media) {
	const auto id = media->fields().widgetId;
	if (!id) {
		return;
	}
	auto &carriers = States().carriers;
	const auto i = carriers.find(*id);
	if (i == end(carriers)) {
		return;
	}
	i->second.remove(media);
	if (i->second.empty()) {
		carriers.erase(i);
	}
}

void ApplyStateUpdate(
		not_null<Main::Session*> session,
		const MTPDupdateMessageWidgetData &data) {
	const auto item = session->data().message(
		peerFromMTP(data.vpeer()),
		data.vmsg_id().v);
	const auto media = item
		? dynamic_cast<WidgetMedia*>(item->media())
		: nullptr;
	if (!media) {
		return;
	}
	const auto chat = data.vchat_data();
	const auto shared = data.vpublic_data();
	const auto hash = data.vpublic_data_hash();
	const auto edit = WidgetStateEdit{
		.chatData = chat ? std::make_optional(chat->v) : std::nullopt,
		.publicData = shared ? std::make_optional(shared->v) : std::nullopt,
		.publicDataHash = hash ? std::make_optional(hash->v) : std::nullopt,
	};
	if (media->applyState(edit)) {
		Runtime::StateChanged({ media->parent() });
	}
}

void WatchersChanged() {
	auto &registry = States();
	if (registry.refreshPosted) {
		return;
	}
	registry.refreshPosted = true;
	crl::on_main([] {
		Refresh();
	});
}

} // namespace Tgw
