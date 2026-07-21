/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/welcome_messages.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/random.h"
#include "data/data_messages.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item_edition.h"
#include "main/main_app_config.h"
#include "main/main_session.h"

namespace Data {
namespace {

constexpr auto kRequestTimeLimit = 60 * crl::time(1000);

[[nodiscard]] MsgId RemoteToLocalMsgId(int32 id) {
	return ShortcutMaxMsgId + id + 1;
}

[[nodiscard]] int32 LocalToRemoteMsgId(MsgId id) {
	Expects(IsWelcomeMsgId(id));

	return int32(id.bare - ShortcutMaxMsgId.bare - 1);
}

[[nodiscard]] bool TooEarlyForRequest(crl::time received) {
	return (received > 0) && (received + kRequestTimeLimit > crl::now());
}

} // namespace

bool IsWelcomeMsgId(MsgId id) {
	return (id > ShortcutMaxMsgId) && (id < WelcomeMaxMsgId);
}

int WelcomeMessagesLimit(not_null<Main::Session*> session) {
	return session->appConfig().get<int>(
		u"ephemeral_welcome_messages_max"_q,
		5);
}

rpl::producer<int> WelcomeMessagesLimitValue(
		not_null<Main::Session*> session) {
	return session->appConfig().value() | rpl::map([=] {
		return WelcomeMessagesLimit(session);
	});
}

WelcomeMessages::WelcomeMessages(not_null<Main::Session*> session)
: _session(session)
, _clearTimer([=] { clearOldRequests(); }) {
	_session->data().itemRemoved(
	) | rpl::filter([](not_null<const HistoryItem*> item) {
		return item->isWelcomeTemplate();
	}) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		remove(item);
	}, _lifetime);
}

WelcomeMessages::~WelcomeMessages() {
	Expects(_data.empty());
	Expects(_requests.empty());
}

void WelcomeMessages::clear() {
	_lifetime.destroy();
	for (const auto &request : base::take(_requests)) {
		_session->api().request(request.second.requestId).cancel();
	}
	base::take(_data);
	base::take(_hashes);
}

void WelcomeMessages::clearOldRequests() {
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

int32 WelcomeMessages::lookupId(not_null<const HistoryItem*> item) const {
	return LocalToRemoteMsgId(item->id);
}

HistoryItem *WelcomeMessages::lookupItem(
		not_null<PeerData*> peer,
		int32 ephemeralId) const {
	const auto history = _session->data().historyLoaded(peer);
	if (!history) {
		return nullptr;
	}
	const auto i = _data.find(history);
	if (i == end(_data)) {
		return nullptr;
	}
	const auto j = i->second.itemById.find(ephemeralId);
	return (j != end(i->second.itemById)) ? j->second.get() : nullptr;
}

int WelcomeMessages::count(not_null<History*> history) const {
	const auto i = _data.find(history);
	return (i != end(_data)) ? int(i->second.items.size()) : 0;
}

void WelcomeMessages::applyNew(const MTPDephemeralMessage &data) {
	const auto history = _session->data().history(
		peerFromMTP(data.vpeer_id()));
	auto &list = _data[history];
	if (append(history, list, data)) {
		sort(list);
		_updates.fire_copy(history);
	}
}

void WelcomeMessages::applyEdit(const MTPDephemeralMessage &data) {
	const auto history = _session->data().history(
		peerFromMTP(data.vpeer_id()));
	const auto item = lookupItem(history->peer, data.vid().v);
	if (!item) {
		applyNew(data);
		return;
	}
	applyEdition(item, data);
}

bool WelcomeMessages::applyDelete(
		not_null<PeerData*> peer,
		int32 ephemeralId) {
	if (const auto item = lookupItem(peer, ephemeralId)) {
		item->destroy();
		return true;
	}
	return false;
}

void WelcomeMessages::send(
		not_null<History*> history,
		TextWithEntities text) {
	const auto entities = Api::EntitiesToMTP(
		_session,
		text.entities,
		Api::ConvertOption::SkipLocal);
	using Flag = MTPephemeral_SendMessage::Flag;
	const auto flags = Flag::f_welcome_template
		| (entities.v.isEmpty() ? Flag(0) : Flag::f_entities);
	_session->api().request(MTPephemeral_SendMessage(
		MTP_flags(flags),
		history->peer->input(),
		MTP_inputUserEmpty(),
		MTPlong(), // query_id
		MTP_string(text.text),
		entities,
		MTPInputMedia(),
		MTPReplyMarkup(),
		MTPInputRichMessage(),
		MTP_long(base::RandomValue<uint64>()),
		MTPInputReplyTo()
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		LOG(("API Error: send welcome template - %1"
			).arg(error.type()));
	}).send();
}

void WelcomeMessages::edit(
		not_null<History*> history,
		int32 ephemeralId,
		TextWithEntities text,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	const auto entities = Api::EntitiesToMTP(
		_session,
		text.entities,
		Api::ConvertOption::SkipLocal);
	using Flag = MTPephemeral_EditMessage::Flag;
	const auto flags = Flag::f_welcome_template
		| Flag::f_message
		| (entities.v.isEmpty() ? Flag(0) : Flag::f_entities);
	_session->api().request(MTPephemeral_EditMessage(
		MTP_flags(flags),
		history->peer->input(),
		MTP_inputUserEmpty(),
		MTP_int(ephemeralId),
		MTP_string(text.text),
		MTPInputMedia(),
		entities,
		MTPReplyMarkup(),
		MTPInputRichMessage()
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);
		if (done) {
			done();
		}
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error.type());
		}
	}).send();
}

void WelcomeMessages::deleteTemplate(not_null<HistoryItem*> item) {
	const auto history = item->history();
	const auto id = lookupId(item);
	_session->api().request(MTPephemeral_DeleteWelcomeMessage(
		history->peer->input(),
		MTP_int(id)
	)).fail([=](const MTP::Error &error) {
		LOG(("API Error: delete welcome template - %1"
			).arg(error.type()));
	}).send();
	item->destroy();
}

void WelcomeMessages::deleteAll(not_null<History*> history) {
	_session->api().request(MTPephemeral_DeleteAllWelcomeMessages(
		history->peer->input()
	)).fail([=](const MTP::Error &error) {
		LOG(("API Error: delete all welcome templates - %1"
			).arg(error.type()));
	}).send();
	const auto i = _data.find(history);
	if (i == end(_data)) {
		return;
	}
	auto items = std::vector<not_null<HistoryItem*>>();
	items.reserve(i->second.items.size());
	for (const auto &owned : i->second.items) {
		items.push_back(owned.get());
	}
	for (const auto &item : items) {
		item->destroy();
	}
}

rpl::producer<> WelcomeMessages::updates(not_null<History*> history) {
	request(history);

	return _updates.events(
	) | rpl::filter([=](not_null<History*> value) {
		return (value == history);
	}) | rpl::to_empty;
}

Data::MessagesSlice WelcomeMessages::list(
		not_null<History*> history) const {
	auto result = Data::MessagesSlice();
	const auto i = _data.find(history);
	if (i == end(_data)) {
		const auto j = _requests.find(history);
		if (j == end(_requests)) {
			return result;
		}
		result.fullCount = result.skippedAfter = result.skippedBefore = 0;
		return result;
	}
	const auto &items = i->second.items;
	result.skippedAfter = result.skippedBefore = 0;
	result.fullCount = int(items.size());
	result.ids = ranges::views::all(
		items
	) | ranges::views::transform(
		&HistoryItem::fullId
	) | ranges::to_vector;
	return result;
}

void WelcomeMessages::request(not_null<History*> history) {
	auto &request = _requests[history];
	if (request.requestId || TooEarlyForRequest(request.lastReceived)) {
		return;
	}
	const auto i = _hashes.find(history);
	const auto hash = (i != end(_hashes)) ? i->second : uint64(0);
	request.requestId = _session->api().request(
		MTPephemeral_GetWelcomeMessages(
			history->peer->input(),
			MTP_long(hash))
	).done([=](const MTPephemeral_WelcomeMessages &result) {
		auto &request = _requests[history];
		request.requestId = 0;
		request.lastReceived = crl::now();
		if (!_clearTimer.isActive()) {
			_clearTimer.callOnce(kRequestTimeLimit * 2);
		}
		result.match([&](const MTPDephemeral_welcomeMessages &data) {
			parse(history, data);
		}, [&](const MTPDephemeral_welcomeMessagesNotModified &) {
		});
	}).fail([=] {
		_requests.remove(history);
	}).send();
}

void WelcomeMessages::parse(
		not_null<History*> history,
		const MTPDephemeral_welcomeMessages &data) {
	_hashes[history] = data.vhash().v;

	const auto &messages = data.vmessages().v;
	if (messages.isEmpty()) {
		const auto i = _data.find(history);
		if (i == end(_data)) {
			return;
		}
		auto clear = base::flat_set<not_null<HistoryItem*>>();
		for (const auto &owned : i->second.items) {
			clear.emplace(owned.get());
		}
		updated(history, {}, clear);
		return;
	}
	auto received = base::flat_set<not_null<HistoryItem*>>();
	auto clear = base::flat_set<not_null<HistoryItem*>>();
	auto &list = _data.emplace(history, List()).first->second;
	for (const auto &message : messages) {
		if (const auto item = append(history, list, message.data())) {
			received.emplace(item);
		}
	}
	for (const auto &owned : list.items) {
		const auto item = owned.get();
		if (!received.contains(item)) {
			clear.emplace(item);
		}
	}
	updated(history, received, clear);
}

HistoryItem *WelcomeMessages::append(
		not_null<History*> history,
		List &list,
		const MTPDephemeralMessage &data) {
	const auto id = data.vid().v;
	if (id <= 0) {
		LOG(("API Error: Bad id in welcome messages: %1.").arg(id));
		return nullptr;
	}
	const auto i = list.itemById.find(id);
	if (i != end(list.itemById)) {
		const auto existing = i->second;
		applyEdition(existing, data);
		return existing;
	}
	const auto fromId = peerFromMTP(data.vfrom_id());
	const auto item = history->makeMessage(
		HistoryItemCommonFields{
			.id = RemoteToLocalMsgId(id),
			.flags = (MessageFlag::FakeHistoryItem
				| (data.is_out()
					? MessageFlag::Outgoing
					: MessageFlag())
				| (fromId ? MessageFlag::HasFromId : MessageFlag())
				| (data.vreply_markup()
					? MessageFlag::HasReplyMarkup
					: MessageFlag())),
			.from = fromId,
			.date = data.vdate().v,
			.markup = HistoryMessageMarkupData(data.vreply_markup()),
		},
		TextWithEntities{
			qs(data.vmessage()),
			Api::EntitiesFromMTP(
				_session,
				data.ventities().value_or_empty()),
		},
		(data.vmedia()
			? *data.vmedia()
			: MTPMessageMedia(MTP_messageMediaEmpty())));
	list.items.emplace_back(item);
	list.itemById.emplace(id, item);
	return item;
}

void WelcomeMessages::applyEdition(
		not_null<HistoryItem*> item,
		const MTPDephemeralMessage &data) {
	auto edition = HistoryMessageEdition();
	edition.isEditHide = true;
	edition.editDate = -1;
	edition.useSameViews = true;
	edition.useSameForwards = true;
	edition.useSameReplies = true;
	edition.useSameReactions = true;
	edition.useSameSuggest = true;
	edition.textWithEntities = {
		qs(data.vmessage()),
		Api::EntitiesFromMTP(
			_session,
			data.ventities().value_or_empty()),
	};
	edition.replyMarkup = HistoryMessageMarkupData(data.vreply_markup());
	edition.mtpMedia = data.vmedia();
	item->applyEdition(std::move(edition));
}

void WelcomeMessages::updated(
		not_null<History*> history,
		const base::flat_set<not_null<HistoryItem*>> &added,
		const base::flat_set<not_null<HistoryItem*>> &clear) {
	if (!clear.empty()) {
		for (const auto &item : clear) {
			item->destroy();
		}
	}
	const auto i = _data.find(history);
	if (i != end(_data)) {
		sort(i->second);
	}
	if (!added.empty() || !clear.empty()) {
		_updates.fire_copy(history);
	}
}

void WelcomeMessages::sort(List &list) {
	ranges::sort(list.items, ranges::less(), &HistoryItem::position);
}

void WelcomeMessages::remove(not_null<const HistoryItem*> item) {
	const auto history = item->history();
	const auto i = _data.find(history);
	if (i == end(_data)) {
		return;
	}
	auto &list = i->second;
	list.itemById.remove(LocalToRemoteMsgId(item->id));
	const auto k = ranges::find(list.items, item, &OwnedItem::get);
	if (k == list.items.end()) {
		return;
	}
	k->release();
	list.items.erase(k);
	if (list.items.empty()) {
		_data.erase(i);
	}
	_updates.fire_copy(history);
}

} // namespace Data
