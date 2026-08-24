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
#include "data/components/ephemeral_messages.h"
#include "data/data_messages.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item_edition.h"
#include "iv/iv_rich_page.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "storage/file_upload.h"

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

	rpl::merge(
		_session->uploader().photoFailed(),
		_session->uploader().documentFailed()
	) | rpl::on_next([=](FullMsgId id) {
		if (const auto item = _session->data().message(id)) {
			if (owns(item)) {
				_session->data().destroyMessageWithCacheCleanup(item);
			}
		}
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
	_convertLocalTarget = {};
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

HistoryItem *WelcomeMessages::first(not_null<History*> history) const {
	const auto i = _data.find(history);
	return (i != end(_data) && !i->second.items.empty())
		? i->second.items.front().get()
		: nullptr;
}

bool WelcomeMessages::owns(not_null<const HistoryItem*> item) const {
	const auto i = _data.find(item->history());
	if (i == end(_data)) {
		return false;
	}
	return ranges::find(i->second.items, item, &OwnedItem::get)
		!= end(i->second.items);
}

void WelcomeMessages::appendSending(not_null<HistoryItem*> item) {
	Expects(!owns(item));
	Expects(item->isSending());
	Expects(IsClientMsgId(item->id));

	const auto history = item->history();
	auto &list = _data[history];
	list.items.emplace_back(item);
	sort(list);
	_updates.fire_copy(history);
}

void WelcomeMessages::removeSending(not_null<HistoryItem*> item) {
	Expects(owns(item));
	Expects(item->isSending() || item->hasFailed());

	_session->data().destroyMessageWithCacheCleanup(item);
}

void WelcomeMessages::applyNew(const MTPDephemeralMessage &data) {
	const auto peerId = PeerIdFromEphemeral(data);
	if (!peerId) {
		return;
	}
	const auto history = _session->data().history(peerId);
	auto &list = _data[history];
	if (_convertLocalTarget
		&& data.is_welcome_template()
		&& data.vmedia()) {
		const auto local = _session->data().message(_convertLocalTarget);
		if (local
			&& local->history() == history
			&& local->isSending()
			&& IsClientMsgId(local->id)
			&& owns(local)) {
			_convertLocalTarget = {};
			const auto id = data.vid().v;
			if (id > 0) {
				const auto i = list.itemById.find(id);
				if (i != end(list.itemById)) {
					const auto existing = i->second;
					applyEdition(existing, data);
					existing->updateDate(data.vdate().v);
					sort(list);
					_session->data().destroyMessageWithCacheCleanup(local);
				} else {
					applyEdition(local, data);
					local->updateDate(data.vdate().v);
					local->setRealId(RemoteToLocalMsgId(id));
					list.itemById.emplace(id, local);
					sort(list);
					_updates.fire_copy(history);
				}
				return;
			}
		}
	}
	if (append(history, list, data)) {
		sort(list);
		_updates.fire_copy(history);
	}
}

void WelcomeMessages::applyEdit(const MTPDephemeralMessage &data) {
	const auto peerId = PeerIdFromEphemeral(data);
	if (!peerId) {
		return;
	}
	const auto history = _session->data().history(peerId);
	const auto item = lookupItem(history->peer, data.vid().v);
	if (!item) {
		applyNew(data);
		return;
	}
	applyEdition(item, data);
	_updates.fire_copy(history);
}

bool WelcomeMessages::applyDelete(
		not_null<PeerData*> peer,
		int32 ephemeralId) {
	if (const auto item = lookupItem(peer, ephemeralId)) {
		_session->data().destroyMessageWithCacheCleanup(item);
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
	const auto flags = Flag::f_welcome
		| Flag::f_peer
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

void WelcomeMessages::sendMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		Data::FileOrigin origin,
		Fn<MTPInputMedia()> rebuildMedia) {
	Expects(owns(item));
	Expects(item->isSending());
	Expects(IsClientMsgId(item->id));

	const auto session = _session;
	const auto history = item->history();
	const auto localId = item->fullId();
	const auto text = item->originalText();
	const auto entities = Api::EntitiesToMTP(
		session,
		text.entities,
		Api::ConvertOption::SkipLocal);
	using Flag = MTPephemeral_SendMessage::Flag;
	const auto flags = Flag::f_welcome
		| Flag::f_peer
		| Flag::f_media
		| (entities.v.isEmpty() ? Flag(0) : Flag::f_entities)
		| (item->invertMedia() ? Flag::f_invert_media : Flag(0));
	const auto randomId = base::RandomValue<uint64>();
	const auto destroyLocal = [=] {
		if (const auto local = session->data().message(localId)) {
			if (owns(local)) {
				session->data().destroyMessageWithCacheCleanup(local);
			}
		}
	};
	const auto send = [=](
			const auto &send,
			const MTPInputMedia &media,
			int attempt) -> void {
		session->api().request(MTPephemeral_SendMessage(
			MTP_flags(flags),
			history->peer->input(),
			MTP_inputUserEmpty(),
			MTPlong(),
			MTP_string(text.text),
			entities,
			media,
			MTPReplyMarkup(),
			MTPInputRichMessage(),
			MTP_long(randomId),
			MTPInputReplyTo()
		)).done([=](const MTPUpdates &result) {
			_convertLocalTarget = localId;
			session->api().applyUpdates(result);
			_convertLocalTarget = {};
			destroyLocal();
		}).fail([=](const MTP::Error &error) {
			const auto type = error.type();
			const auto refreshable = !attempt
				&& (error.code() == 400)
				&& type.startsWith(u"FILE_REFERENCE_"_q);
			if (refreshable && rebuildMedia) {
				session->api().refreshFileReference(
					origin,
					[=](const auto &) {
						send(send, rebuildMedia(), 1);
					});
				return;
			}
			LOG(("API Error: send welcome media template - %1").arg(type));
			destroyLocal();
		}).send();
	};
	send(send, media, 0);
}

void WelcomeMessages::sendRich(
		not_null<History*> history,
		Fn<std::optional<MTPInputRichMessage>()> richMessage) {
	auto first = richMessage
		? richMessage()
		: std::optional<MTPInputRichMessage>();
	if (!first) {
		LOG(("API Error: no rich welcome template to send."));
		return;
	}
	using Flag = MTPephemeral_SendMessage::Flag;
	const auto flags = Flag::f_welcome
		| Flag::f_peer
		| Flag::f_rich_message;
	const auto randomId = base::RandomValue<uint64>();
	const auto send = [=](
			const auto &send,
			const MTPInputRichMessage &rich,
			int attempt) -> void {
		_session->api().request(MTPephemeral_SendMessage(
			MTP_flags(flags),
			history->peer->input(),
			MTP_inputUserEmpty(),
			MTPlong(), // query_id
			MTP_string(),
			MTPVector<MTPMessageEntity>(),
			MTPInputMedia(),
			MTPReplyMarkup(),
			rich,
			MTP_long(randomId),
			MTPInputReplyTo()
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
		}).fail([=](const MTP::Error &error) {
			const auto type = error.type();
			if (!attempt
				&& (error.code() == 400)
				&& type.startsWith(u"FILE_REFERENCE_"_q)) {
				if (auto rebuilt = richMessage()) {
					send(send, *rebuilt, 1);
					return;
				}
			}
			LOG(("API Error: send rich welcome template - %1"
				).arg(type));
		}).send();
	};
	send(send, *first, 0);
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
	const auto flags = Flag::f_welcome
		| Flag::f_peer
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

void WelcomeMessages::editRich(
		not_null<History*> history,
		int32 ephemeralId,
		Fn<std::optional<MTPInputRichMessage>()> richMessage,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	auto first = richMessage
		? richMessage()
		: std::optional<MTPInputRichMessage>();
	if (!first) {
		if (fail) {
			fail(QString());
		}
		return;
	}
	using Flag = MTPephemeral_EditMessage::Flag;
	const auto flags = Flag::f_welcome
		| Flag::f_peer
		| Flag::f_rich_message;
	const auto send = [=](
			const auto &send,
			const MTPInputRichMessage &rich,
			int attempt) -> void {
		_session->api().request(MTPephemeral_EditMessage(
			MTP_flags(flags),
			history->peer->input(),
			MTP_inputUserEmpty(),
			MTP_int(ephemeralId),
			MTPstring(),
			MTPInputMedia(),
			MTPVector<MTPMessageEntity>(),
			MTPReplyMarkup(),
			rich
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
			if (done) {
				done();
			}
		}).fail([=](const MTP::Error &error) {
			const auto type = error.type();
			if (!attempt
				&& (error.code() == 400)
				&& type.startsWith(u"FILE_REFERENCE_"_q)) {
				if (auto rebuilt = richMessage()) {
					send(send, *rebuilt, 1);
					return;
				}
			}
			if (fail) {
				fail(type);
			}
		}).send();
	};
	send(send, *first, 0);
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
	_session->data().destroyMessageWithCacheCleanup(item);
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
	_session->data().destroyMessagesWithCacheCleanup(items);
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
			if (!owned->isSending() && !owned->hasFailed()) {
				clear.emplace(owned.get());
			}
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
		if (!item->isSending()
			&& !item->hasFailed()
			&& !received.contains(item)) {
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
				| (data.is_invert_media()
					? MessageFlag::InvertMedia
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
	if (const auto richMessage = data.vrich_message()) {
		item->applyLocalRichPage(
			Iv::ParseRichPage(_session, *richMessage));
	}
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
	edition.invertMedia = data.is_invert_media();
	if (const auto richMessage = data.vrich_message()) {
		edition.richPage = Iv::ParseRichPage(_session, *richMessage);
	}
	item->applyEdition(std::move(edition));
}

void WelcomeMessages::updated(
		not_null<History*> history,
		const base::flat_set<not_null<HistoryItem*>> &added,
		const base::flat_set<not_null<HistoryItem*>> &clear) {
	if (!clear.empty()) {
		auto items = std::vector<not_null<HistoryItem*>>();
		items.reserve(clear.size());
		for (const auto &item : clear) {
			items.push_back(item);
		}
		_session->data().destroyMessagesWithCacheCleanup(items);
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
	const auto k = ranges::find(list.items, item, &OwnedItem::get);
	if (k == list.items.end()) {
		return;
	}
	if (IsWelcomeMsgId(item->id)) {
		list.itemById.remove(LocalToRemoteMsgId(item->id));
	}
	k->release();
	list.items.erase(k);
	if (list.items.empty()) {
		_data.erase(i);
	}
	_updates.fire_copy(history);
}

} // namespace Data
