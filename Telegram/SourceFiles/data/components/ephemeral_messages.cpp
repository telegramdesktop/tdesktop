/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/ephemeral_messages.h"

#include "api/api_common.h"
#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "data/components/welcome_messages.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum_topic.h"
#include "data/data_histories.h"
#include "data/data_peer_bot_command.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_edition.h"
#include "iv/iv_rich_message_serializer.h"
#include "iv/iv_rich_page.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"

namespace Data {
namespace {

constexpr auto kKeepDuration = TimeId(2 * 86400);
constexpr auto kPruneEach = 3600 * crl::time(1000);
constexpr auto kPendingFlushDelay = 2 * crl::time(1000);

struct ParsedCommand {
	QString command;
	QString username;
};

[[nodiscard]] std::optional<ParsedCommand> ParseCommand(
		const QString &text) {
	if (!text.startsWith(QChar('/'))) {
		return std::nullopt;
	}
	const auto size = int(text.size());
	const auto good = [](QChar ch) {
		return ch.isLetterOrNumber() || (ch == QChar('_'));
	};
	auto i = 1;
	while (i < size && good(text[i])) {
		++i;
	}
	auto result = ParsedCommand{ .command = text.mid(1, i - 1) };
	if (result.command.isEmpty()) {
		return std::nullopt;
	}
	if (i < size && text[i] == QChar('@')) {
		const auto from = ++i;
		while (i < size && good(text[i])) {
			++i;
		}
		result.username = text.mid(from, i - from);
		if (result.username.isEmpty()) {
			return std::nullopt;
		}
	}
	if (i < size && !text[i].isSpace()) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] HistoryMessageContent ContentFromEphemeral(
		not_null<Main::Session*> session,
		not_null<HistoryItem*> item,
		const MTPDephemeralMessage &data) {
	auto result = HistoryMessageContent{
		.text = {
			qs(data.vmessage()),
			Api::EntitiesFromMTP(
				session,
				data.ventities().value_or_empty()),
		},
		.media = (data.vmedia()
			? HistoryItem::CreateMedia(item, *data.vmedia())
			: nullptr),
		.markup = HistoryMessageMarkupData(data.vreply_markup()),
		.invertMedia = data.is_invert_media(),
		.hideEdited = true,
	};
	if (const auto richMessage = data.vrich_message()) {
		result.richPage = Iv::ParseRichPage(session, *richMessage);
	}
	return result;
}

template <typename Value>
[[nodiscard]] Value TakeHint(
		base::flat_map<
			not_null<History*>,
			base::flat_map<PeerId, Value>> &map,
		not_null<History*> history,
		PeerId key) {
	const auto i = map.find(history);
	if (i == end(map)) {
		return Value();
	}
	const auto result = i->second.take(key);
	if (i->second.empty()) {
		map.erase(i);
	}
	return result.value_or(Value());
}

} // namespace

PeerId PeerIdFromEphemeral(const MTPDephemeralMessage &data) {
	const auto peer = data.vpeer_id();
	return peer ? peerFromMTP(*peer) : PeerId();
}

EphemeralMessages::EphemeralMessages(not_null<Main::Session*> session)
: _session(session)
, _pruneTimer([=] { pruneOld(); })
, _pendingTimer([=] { drainPending(true); }) {
	_session->data().itemRemoved(
	) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		if (item->isEphemeral() || anchored(item)) {
			itemRemoved(item);
		}
	}, _lifetime);

	_pruneTimer.callEach(kPruneEach);
}

EphemeralMessages::~EphemeralMessages() = default;

void EphemeralMessages::clear() {
	_lifetime.destroy();
	_convertLocalTarget = {};
	base::take(_data);
	base::take(_anchored);
	base::take(_pending);
	base::take(_callbackTopicHints);
}

void EphemeralMessages::apply(const MTPDupdateNewEphemeralMessage &update) {
	if (update.vmessage().data().is_welcome_template()) {
		_session->welcomeMessages().applyNew(update.vmessage().data());
		return;
	}
	applyOrDefer(update.vmessage());
}

void EphemeralMessages::applyOrDefer(const MTPEphemeralMessage &message) {
	if (replyTargetMissing(message.data())) {
		_pending.push_back(message);
		_pendingTimer.callOnce(kPendingFlushDelay);
		return;
	}
	applyNew(message.data());
	drainPending();
}

bool EphemeralMessages::replyTargetMissing(
		const MTPDephemeralMessage &data) const {
	const auto header = data.vreply_to();
	if (!header) {
		return false;
	}
	return header->match([&](const MTPDmessageReplyHeader &reply) {
		const auto replyToId = reply.vreply_to_msg_id();
		if (!reply.is_reply_to_ephemeral() || !replyToId) {
			return false;
		}
		const auto peerId = PeerIdFromEphemeral(data);
		if (!peerId) {
			return false;
		}
		const auto history = _session->data().history(peerId);
		return !lookupItem(history->peer, replyToId->v);
	}, [](const MTPDmessageReplyStoryHeader &) {
		return false;
	});
}

bool EphemeralMessages::mentionsMe(
		not_null<History*> history,
		const MTPDephemeralMessage &data) const {
	if (data.is_out()) {
		return false;
	}
	const auto text = qs(data.vmessage());
	if (const auto entities = data.ventities()) {
		for (const auto &entity : entities->v) {
			auto found = false;
			entity.match([&](const MTPDmessageEntityMentionName &data) {
				found = (UserId(data.vuser_id()) == _session->userId());
			}, [&](const MTPDmessageEntityMention &data) {
				const auto tag = text.mid(
					data.voffset().v + 1,
					data.vlength().v - 1);
				found = ranges::any_of(
					_session->user()->usernames(),
					[&](const QString &username) {
						return !tag.compare(username, Qt::CaseInsensitive);
					});
			}, [](const auto &) {
			});
			if (found) {
				return true;
			}
		}
	}
	const auto header = data.vreply_to();
	if (!header) {
		return false;
	}
	return header->match([&](const MTPDmessageReplyHeader &reply) {
		const auto replyToId = reply.vreply_to_msg_id();
		if (!replyToId) {
			return false;
		}
		const auto item = reply.is_reply_to_ephemeral()
			? lookupItem(history->peer, replyToId->v)
			: _session->data().message(history->peer->id, replyToId->v);
		return (item != nullptr) && item->out();
	}, [](const MTPDmessageReplyStoryHeader &) {
		return false;
	});
}

void EphemeralMessages::drainPending(bool force) {
	while (!_pending.empty()) {
		const auto i = ranges::find_if(_pending, [&](
				const MTPEphemeralMessage &message) {
			return force || !replyTargetMissing(message.data());
		});
		if (i == end(_pending)) {
			return;
		}
		const auto message = *i;
		_pending.erase(i);
		applyNew(message.data());
	}
}

void EphemeralMessages::apply(const MTPDupdateEditEphemeralMessage &update) {
	const auto &data = update.vmessage().data();
	if (data.is_welcome_template()) {
		_session->welcomeMessages().applyEdit(data);
		return;
	}
	const auto peerId = PeerIdFromEphemeral(data);
	if (!peerId) {
		return;
	}
	const auto history = _session->data().history(peerId);
	const auto item = lookupItem(history->peer, data.vid().v);
	if (!item) {
		applyNew(data);
		return;
	} else if (anchored(item)) {
		item->applyContent(ContentFromEphemeral(_session, item, data));
		return;
	}
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

void EphemeralMessages::apply(
		const MTPDupdateDeleteEphemeralMessages &update) {
	const auto history = _session->data().historyLoaded(
		peerFromMTP(update.vpeer()));
	if (!history) {
		return;
	}
	auto items = std::vector<not_null<HistoryItem*>>();
	for (const auto &id : update.vids().v) {
		if (_session->welcomeMessages().applyDelete(history->peer, id.v)) {
			continue;
		}
		if (const auto item = lookupItem(history->peer, id.v)) {
			if (anchored(item)) {
				revertAnchored(item);
			} else {
				items.push_back(item);
			}
		}
	}
	_session->data().destroyMessagesWithCacheCleanup(items);
}

HistoryItem *EphemeralMessages::applyNew(const MTPDephemeralMessage &data) {
	const auto peerId = PeerIdFromEphemeral(data);
	if (!peerId) {
		return nullptr;
	}
	const auto history = _session->data().history(peerId);
	const auto ephemeralId = data.vid().v;
	if (const auto existing = lookupItem(history->peer, ephemeralId)) {
		return existing;
	}
	if (const auto anchorMsgId = data.vanchor_msg_id()) {
		const auto result = applyAnchored(history, data, anchorMsgId->v);
		if (result) {
			return result;
		}
	}
	const auto fromId = peerFromMTP(data.vfrom_id());
	auto replyTo = FullReplyTo();
	if (const auto topMsgId = data.vtop_msg_id()) {
		replyTo.topicRootId = topMsgId->v;
	}
	if (const auto header = data.vreply_to()) {
		header->match([&](const MTPDmessageReplyHeader &reply) {
			if (const auto top = reply.vreply_to_top_id()) {
				if (!replyTo.topicRootId) {
					replyTo.topicRootId = top->v;
				}
			}
			const auto replyToId = reply.vreply_to_msg_id();
			if (reply.is_reply_to_ephemeral() && replyToId) {
				const auto target = lookupItem(history->peer, replyToId->v);
				if (target) {
					replyTo.messageId = target->fullId();
					if (!replyTo.topicRootId) {
						replyTo.topicRootId = target->topicRootId();
					}
				}
			} else if (replyToId) {
				if (!replyTo.topicRootId) {
					replyTo.topicRootId = replyToId->v;
				}
				replyTo.messageId = {
					history->peer->id,
					replyTo.topicRootId,
				};
			}
		}, [](const MTPDmessageReplyStoryHeader &) {
		});
	}
	if (!replyTo.topicRootId
		&& !replyTo.messageId
		&& !data.is_out()
		&& fromId) {
		if (const auto hinted = takeCallbackTopic(history, fromId)) {
			replyTo.topicRootId = hinted;
			replyTo.messageId = { history->peer->id, hinted };
		}
	}
	if (_convertLocalTarget
		&& data.is_out()
		&& (data.vmedia() || data.vrich_message())) {
		const auto localId = base::take(_convertLocalTarget);
		if (const auto local = _session->data().message(localId)) {
			if (local->isEphemeral()) {
				local->updateSentContent({
					qs(data.vmessage()),
					Api::EntitiesFromMTP(
						_session,
						data.ventities().value_or_empty())
				}, data.vmedia(), data.vrich_message());
				local->markEphemeralSent();
				registerEntry(
					history,
					ephemeralId,
					UserId(data.vreceiver_id()),
					local);
				recountAttachToPrevious(local);
				_session->data().requestItemResize(local);
				return local;
			}
		}
	}
	const auto mentioned = mentionsMe(history, data);
	const auto item = history->addNewLocalMessage(
		{
			.id = _session->data().nextLocalMessageId(),
			.flags = (MessageFlag::HistoryEntry
				| MessageFlag::Ephemeral
				| (data.is_out() ? MessageFlag::Outgoing : MessageFlag())
				| (data.is_invert_media()
					? MessageFlag::InvertMedia
					: MessageFlag())
				| (data.is_noforwards()
					? MessageFlag::NoForwards
					: MessageFlag())
				| (fromId ? MessageFlag::HasFromId : MessageFlag())
				| (replyTo.messageId
					? MessageFlag::HasReplyInfo
					: MessageFlag())
				| (data.vreply_markup()
					? MessageFlag::HasReplyMarkup
					: MessageFlag())
				| (mentioned ? MessageFlag::MentionsMe : MessageFlag())),
			.from = fromId,
			.replyTo = replyTo,
			.date = data.vdate().v,
			.markup = HistoryMessageMarkupData(data.vreply_markup()),
		},
		TextWithEntities {
			qs(data.vmessage()),
			Api::EntitiesFromMTP(
				_session,
				data.ventities().value_or_empty()),
		},
		data.vmedia()
			? *data.vmedia()
			: MTPMessageMedia(MTP_messageMediaEmpty()));
	if (const auto richMessage = data.vrich_message()) {
		item->applyLocalRichPage(Iv::ParseRichPage(_session, *richMessage));
	}
	registerEntry(history, ephemeralId, UserId(data.vreceiver_id()), item);
	recountAttachToPrevious(item);
	_session->data().requestItemResize(item);
	return item;
}

HistoryItem *EphemeralMessages::applyAnchored(
		not_null<History*> history,
		const MTPDephemeralMessage &data,
		MsgId anchorMsgId) {
	const auto item = _session->data().message(
		history->peer->id,
		anchorMsgId);
	if (!item
		|| !item->isRegular()
		|| item->isService()
		|| item->isEphemeral()
		|| item->groupId()
		|| item->isEditingMedia()) {
		return nullptr;
	}
	const auto fullId = item->fullId();
	if (!_anchored.contains(fullId)) {
		_anchored.emplace(fullId, item->backupContent());
	}
	unregisterEntry(item);
	item->applyContent(ContentFromEphemeral(_session, item, data));
	registerEntry(history, data.vid().v, UserId(data.vreceiver_id()), item);
	_session->data().requestItemResize(item);
	return item;
}

void EphemeralMessages::registerEntry(
		not_null<History*> history,
		int32 ephemeralId,
		UserId receiverId,
		not_null<HistoryItem*> item) {
	_data[history].push_back({
		.ephemeralId = ephemeralId,
		.receiverId = receiverId,
		.item = item,
	});
}

void EphemeralMessages::unregisterEntry(not_null<const HistoryItem*> item) {
	const auto i = _data.find(item->history());
	if (i == end(_data)) {
		return;
	}
	i->second.erase(
		ranges::remove(i->second, item.get(), &Entry::item),
		end(i->second));
	if (i->second.empty()) {
		_data.erase(i);
	}
}

void EphemeralMessages::revertAnchored(not_null<HistoryItem*> item) {
	auto original = _anchored.take(item->fullId());
	if (!original) {
		return;
	}
	unregisterEntry(item);
	item->applyContent(std::move(*original));
}

void EphemeralMessages::recountAttachToPrevious(
		not_null<HistoryItem*> item) {
	const auto view = item->mainView();
	if (!view) {
		return;
	}
	view->previousInBlocksChanged();
	if (const auto next = view->nextInBlocks()) {
		next->previousInBlocksChanged();
	}
}

HistoryItem *EphemeralMessages::lookupItem(
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
	const auto j = ranges::find(
		i->second,
		ephemeralId,
		&Entry::ephemeralId);
	return (j != end(i->second)) ? j->item.get() : nullptr;
}

int32 EphemeralMessages::lookupId(not_null<const HistoryItem*> item) const {
	const auto entry = findByItem(item);
	return entry ? entry->ephemeralId : 0;
}

UserId EphemeralMessages::receiverId(
		not_null<const HistoryItem*> item) const {
	const auto entry = findByItem(item);
	return entry ? entry->receiverId : UserId();
}

bool EphemeralMessages::anchored(not_null<const HistoryItem*> item) const {
	return !_anchored.empty() && _anchored.contains(item->fullId());
}

const Media *EphemeralMessages::anchoredMedia(
		not_null<const HistoryItem*> item) const {
	const auto i = _anchored.find(item->fullId());
	return (i != end(_anchored)) ? i->second.media.get() : nullptr;
}

UserData *EphemeralMessages::replyReceiver(
		not_null<const HistoryItem*> item) const {
	if (const auto entry = findByItem(item)) {
		return botForSending(*entry);
	}
	if (item->out() && item->isEphemeral()) {
		const auto target = _session->data().message(
			item->replyTo().messageId);
		if (target && target->isEphemeral() && !target->out()) {
			const auto from = target->from();
			return from ? from->asUser() : nullptr;
		}
	}
	return nullptr;
}

UserData *EphemeralMessages::replyBot(
		not_null<const HistoryItem*> target) const {
	if (!target->isEphemeral() || target->out()) {
		return nullptr;
	}
	const auto entry = findByItem(target);
	return entry ? botForSending(*entry) : nullptr;
}

bool EphemeralMessages::wouldSend(const Api::MessageToSend &message) const {
	const auto history = message.action.history;
	const auto peer = history->peer;
	if (!peer->isChat() && !peer->isMegagroup()) {
		return false;
	}
	if (const auto replyToId = realReplyId(message)) {
		const auto replyTo = _session->data().message(replyToId);
		if (replyTo && replyTo->isEphemeral()) {
			return replyBot(replyTo) != nullptr;
		}
	}
	return findCommandBot(peer, message.textWithTags.text.trimmed())
		!= nullptr;
}

bool EphemeralMessages::hasEphemeralCommand(
		not_null<PeerData*> peer,
		const QString &text) const {
	if (!peer->isChat() && !peer->isMegagroup()) {
		return false;
	}
	return findCommandBot(peer, text.trimmed()) != nullptr;
}

bool EphemeralMessages::wouldSendMedia(
		not_null<PeerData*> peer,
		FullReplyTo replyTo,
		const QString &caption) const {
	return isEphemeralBotReply(replyTo.messageId)
		|| hasEphemeralCommand(peer, caption);
}

bool EphemeralMessages::isEphemeralBotReply(FullMsgId replyToId) const {
	const auto target = _session->data().message(replyToId);
	return target && (replyBot(target) != nullptr);
}

FullMsgId EphemeralMessages::realReplyId(
		const Api::MessageToSend &message) const {
	const auto &replyTo = message.action.replyTo;
	const auto id = replyTo.messageId;
	if (!id || (replyTo.topicRootId && id.msg == replyTo.topicRootId)) {
		return FullMsgId();
	}
	return id;
}

bool EphemeralMessages::trySend(const Api::MessageToSend &message) {
	const auto history = message.action.history;
	const auto peer = history->peer;
	if (!peer->isChat() && !peer->isMegagroup()) {
		return false;
	} else if (message.action.options.scheduled
		|| message.action.options.shortcutId) {
		const auto replyToId = realReplyId(message);
		const auto replyTo = replyToId
			? _session->data().message(replyToId)
			: nullptr;
		if (wouldSend(message) || (replyTo && replyTo->isEphemeral())) {
			LOG(("API Error: "
				"Dropping a scheduled ephemeral message send."));
			return true;
		}
		return false;
	}
	auto text = TextWithEntities{
		message.textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(message.textWithTags.tags),
	};
	TextUtilities::Trim(text);
	if (text.text.isEmpty()) {
		return false;
	}
	auto realReply = FullReplyTo();
	if (const auto replyToId = realReplyId(message)) {
		const auto replyTo = _session->data().message(replyToId);
		if (replyTo && replyTo->isEphemeral()) {
			if (replyTo->out()) {
				return true;
			}
			const auto entry = findByItem(replyTo);
			const auto bot = entry ? botForSending(*entry) : nullptr;
			if (!bot) {
				reportDroppedReply();
				return true;
			}
			send(
				history,
				bot,
				std::move(text),
				entry->ephemeralId,
				MsgId(),
				FullReplyTo(),
				message.webPage,
				message.action.options.invertCaption);
			return true;
		}
		realReply = message.action.replyTo;
	}
	const auto bot = findCommandBot(peer, text.text);
	if (!bot) {
		return false;
	}
	send(
		history,
		bot,
		std::move(text),
		0,
		message.action.replyTo.topicRootId,
		realReply,
		message.webPage,
		message.action.options.invertCaption);
	return true;
}

UserData *EphemeralMessages::findCommandBot(
		not_null<PeerData*> peer,
		const QString &text) const {
	if (!peer->isChat() && !peer->isMegagroup()) {
		return nullptr;
	}
	const auto parsed = ParseCommand(text);
	if (!parsed) {
		return nullptr;
	}
	const auto &commands = peer->isChat()
		? peer->asChat()->botCommands()
		: peer->asMegagroup()->mgInfo->botCommands();
	for (const auto &[userId, list] : commands) {
		const auto user = _session->data().userLoaded(userId);
		if (!user
			|| (!parsed->username.isEmpty()
				&& parsed->username.compare(
					user->username(),
					Qt::CaseInsensitive) != 0)) {
			continue;
		}
		for (const auto &command : list) {
			if (command.ephemeral
				&& !command.command.compare(
					parsed->command,
					Qt::CaseInsensitive)) {
				return user;
			}
		}
	}
	return nullptr;
}

void EphemeralMessages::send(
		not_null<History*> history,
		not_null<UserData*> bot,
		TextWithEntities text,
		int32 replyToEphemeralId,
		MsgId topicRootId,
		FullReplyTo realReply,
		Data::WebPageDraft webPage,
		bool invertCaption) {
	const auto exactWebPage = !webPage.url.isEmpty() && !webPage.removed;
	const auto manualWebPage = exactWebPage && webPage.manual;
	const auto invertMedia = (exactWebPage && webPage.invert)
		|| invertCaption;
	request(
		history,
		bot,
		std::move(text),
		(manualWebPage
			? Data::WebPageForMTP(webPage, true)
			: MTPInputMedia()),
		manualWebPage,
		replyToEphemeralId,
		topicRootId,
		realReply,
		FullMsgId(),
		Data::FileOrigin(),
		nullptr,
		invertMedia);
}

bool EphemeralMessages::sendMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		Data::FileOrigin origin,
		Fn<MTPInputMedia()> rebuildMedia) {
	const auto history = item->history();
	const auto replyTo = item->replyTo();
	const auto target = _session->data().message(replyTo.messageId);
	if (!target || !target->isEphemeral()) {
		const auto bot = findCommandBot(
			history->peer,
			item->originalText().text.trimmed());
		if (bot) {
			const auto realReply = (replyTo.messageId
				&& !(replyTo.topicRootId
					&& replyTo.messageId.msg == replyTo.topicRootId))
				? replyTo
				: FullReplyTo();
			request(
				history,
				bot,
				item->originalText(),
				media,
				true,
				0,
				item->topicRootId(),
				realReply,
				item->fullId(),
				origin,
				rebuildMedia,
				item->invertMedia());
			return true;
		}
		return false;
	}
	if (!target->out()) {
		const auto entry = findByItem(target);
		const auto bot = entry ? botForSending(*entry) : nullptr;
		if (bot) {
			request(
				item->history(),
				bot,
				item->originalText(),
				media,
				true,
				entry->ephemeralId,
				MsgId(0),
				FullReplyTo(),
				item->fullId(),
				origin,
				rebuildMedia,
				item->invertMedia());
			return true;
		}
		reportDroppedReply();
	}
	_session->data().destroyMessageWithCacheCleanup(item);
	return true;
}

bool EphemeralMessages::sendRich(
		not_null<HistoryItem*> item,
		const MTPInputRichMessage &richMessage,
		const Api::SendAction &action) {
	const auto history = item->history();
	const auto replyTo = item->replyTo();
	const auto target = _session->data().message(replyTo.messageId);
	const auto targetEphemeral = target && target->isEphemeral();
	const auto commandBot = targetEphemeral
		? nullptr
		: findCommandBot(
			history->peer,
			item->originalText().text.trimmed());
	if (!targetEphemeral && !commandBot) {
		return false;
	} else if (action.options.scheduled || action.options.shortcutId) {
		LOG(("API Error: "
			"Dropping a scheduled ephemeral rich message send."));
		_session->data().destroyMessageWithCacheCleanup(item);
		return true;
	}
	const auto session = _session;
	const auto itemId = item->fullId();
	const auto rebuildRich = [=]() -> std::optional<MTPInputRichMessage> {
		const auto local = session->data().message(itemId);
		if (!local) {
			return std::nullopt;
		}
		const auto fullPage = local->fullRichPage();
		const auto page = fullPage ? fullPage : local->richPage();
		if (!page) {
			return std::nullopt;
		}
		auto serialized = Iv::SerializeInputRichMessage(
			session,
			*page,
			Iv::SerializeInputRichMessageMode::FinalSubmit);
		const auto success = (serialized.status
			== Iv::SerializeInputRichMessageStatus::Success);
		return (success && serialized.value)
			? std::move(serialized.value)
			: std::nullopt;
	};
	const auto origin = action.clearDraft
		? Data::FileOrigin(Data::FileOriginCloudDraft{
			.peerId = history->peer->id,
			.topicRootId = action.replyTo.topicRootId,
			.monoforumPeerId = action.replyTo.monoforumPeerId,
		})
		: Data::FileOrigin();
	if (commandBot) {
		const auto realReply = (replyTo.messageId
			&& !(replyTo.topicRootId
				&& replyTo.messageId.msg == replyTo.topicRootId))
			? replyTo
			: FullReplyTo();
		request(
			history,
			commandBot,
			TextWithEntities(),
			MTPInputMedia(),
			false,
			0,
			item->topicRootId(),
			realReply,
			item->fullId(),
			origin,
			nullptr,
			false,
			richMessage,
			rebuildRich);
		return true;
	}
	if (!target->out()) {
		const auto entry = findByItem(target);
		const auto bot = entry ? botForSending(*entry) : nullptr;
		if (bot) {
			request(
				history,
				bot,
				TextWithEntities(),
				MTPInputMedia(),
				false,
				entry->ephemeralId,
				MsgId(0),
				FullReplyTo(),
				item->fullId(),
				origin,
				nullptr,
				false,
				richMessage,
				rebuildRich);
			return true;
		}
		reportDroppedReply();
	}
	_session->data().destroyMessageWithCacheCleanup(item);
	return true;
}

bool EphemeralMessages::sendSimpleMedia(
		not_null<History*> history,
		FullReplyTo replyTo,
		const MTPInputMedia &media) {
	const auto target = _session->data().message(replyTo.messageId);
	if (!target || !target->isEphemeral()) {
		return false;
	}
	if (!target->out()) {
		const auto entry = findByItem(target);
		const auto bot = entry ? botForSending(*entry) : nullptr;
		if (bot) {
			request(
				history,
				bot,
				TextWithEntities(),
				media,
				true,
				entry->ephemeralId,
				MsgId(0));
		} else {
			reportDroppedReply();
		}
	}
	return true;
}

void EphemeralMessages::request(
		not_null<History*> history,
		not_null<UserData*> bot,
		TextWithEntities text,
		const MTPInputMedia &media,
		bool hasMedia,
		int32 replyToEphemeralId,
		MsgId topicRootId,
		FullReplyTo realReply,
		FullMsgId destroyOnResult,
		Data::FileOrigin origin,
		Fn<MTPInputMedia()> rebuildMedia,
		bool invertMedia,
		std::optional<MTPInputRichMessage> richMessage,
		Fn<std::optional<MTPInputRichMessage>()> rebuildRich) {
	const auto session = _session;
	const auto destroyLocal = [=] {
		if (destroyOnResult) {
			if (const auto local = session->data().message(destroyOnResult)) {
				session->data().destroyMessageWithCacheCleanup(local);
			}
		}
	};
	const auto entities = Api::EntitiesToMTP(
		session,
		text.entities,
		Api::ConvertOption::SkipLocal);
	auto replyTo = MTPInputReplyTo();
	auto hasReplyTo = false;
	if (replyToEphemeralId) {
		replyTo = MTP_inputReplyToEphemeralMessage(
			MTP_int(replyToEphemeralId));
		hasReplyTo = true;
	} else if (realReply.messageId) {
		replyTo = Data::ReplyToForMTP(history, realReply);
		hasReplyTo = (replyTo.type() == mtpc_inputReplyToMessage);
	} else if (topicRootId && topicRootId != Data::ForumTopic::kGeneralId) {
		auto anchor = FullReplyTo();
		anchor.messageId = { history->peer->id, topicRootId };
		anchor.topicRootId = topicRootId;
		replyTo = Data::ReplyToForMTP(history, anchor);
		hasReplyTo = (replyTo.type() == mtpc_inputReplyToMessage);
	}
	using Flag = MTPephemeral_SendMessage::Flag;
	const auto flags = Flag::f_peer
		| (entities.v.isEmpty() ? Flag(0) : Flag::f_entities)
		| (hasMedia ? Flag::f_media : Flag(0))
		| (hasReplyTo ? Flag::f_reply_to : Flag(0))
		| (invertMedia ? Flag::f_invert_media : Flag(0))
		| (richMessage ? Flag::f_rich_message : Flag(0));
	const auto randomId = base::RandomValue<uint64>();
	const auto send = [=](
			const auto &send,
			const MTPInputMedia &media,
			const MTPInputRichMessage &rich,
			int attempt) -> void {
		session->api().request(MTPephemeral_SendMessage(
			MTP_flags(flags),
			history->peer->input(),
			bot->inputUser(),
			MTPlong(), // query_id
			MTP_string(text.text),
			entities,
			media,
			MTPReplyMarkup(),
			rich,
			MTP_long(randomId),
			replyTo
		)).done([=](const MTPUpdates &result) {
			_convertLocalTarget = destroyOnResult;
			session->api().applyUpdates(result);
			if (destroyOnResult) {
				const auto local = session->data().message(destroyOnResult);
				if (local && !findByItem(local)) {
					session->data().destroyMessageWithCacheCleanup(local);
				}
			}
		}).fail([=](const MTP::Error &error) {
			const auto type = error.type();
			const auto refreshable = !attempt
				&& (error.code() == 400)
				&& type.startsWith(u"FILE_REFERENCE_"_q);
			if (refreshable && (rebuildMedia || rebuildRich)) {
				session->api().refreshFileReference(
					origin,
					[=](const auto &) {
						if (rebuildMedia) {
							send(send, rebuildMedia(), rich, 1);
						} else if (auto rebuilt = rebuildRich()) {
							send(send, media, *rebuilt, 1);
						} else {
							LOG(("API Error: "
								"send ephemeral message - %1").arg(type));
							destroyLocal();
						}
					});
				return;
			}
			LOG(("API Error: send ephemeral message - %1").arg(type));
			destroyLocal();
		}).send();
	};
	send(send, media, richMessage.value_or(MTPInputRichMessage()), 0);
}

void EphemeralMessages::deleteMessage(not_null<HistoryItem*> item) {
	const auto entry = findByItem(item);
	if (entry && entry->receiverId) {
		const auto receiver = _session->data().user(entry->receiverId);
		_session->api().request(MTPephemeral_DeleteMessage(
			MTP_flags(MTPephemeral_DeleteMessage::Flag::f_peer),
			item->history()->peer->input(),
			receiver->inputUser(),
			MTP_int(entry->ephemeralId)
		)).send();
	}
	if (anchored(item)) {
		revertAnchored(item);
	} else if (item->isEphemeral()) {
		_session->data().destroyMessageWithCacheCleanup(item);
	}
}

const EphemeralMessages::Entry *EphemeralMessages::findByItem(
		not_null<const HistoryItem*> item) const {
	const auto i = _data.find(item->history());
	if (i == end(_data)) {
		return nullptr;
	}
	const auto j = ranges::find(i->second, item.get(), &Entry::item);
	return (j != end(i->second)) ? &*j : nullptr;
}

void EphemeralMessages::noteCallbackTopic(
		not_null<History*> history,
		PeerId botId,
		MsgId topicRootId) {
	if (!topicRootId || topicRootId == Data::ForumTopic::kGeneralId) {
		return;
	}
	_callbackTopicHints[history][botId] = topicRootId;
}

MsgId EphemeralMessages::takeCallbackTopic(
		not_null<History*> history,
		PeerId botId) {
	return TakeHint(_callbackTopicHints, history, botId);
}

UserData *EphemeralMessages::botForSending(const Entry &entry) const {
	if (entry.item->out()) {
		return _session->data().userLoaded(entry.receiverId);
	}
	const auto from = entry.item->from();
	return from ? from->asUser() : nullptr;
}

void EphemeralMessages::reportDroppedReply() const {
	LOG(("API Error: Dropping an ephemeral reply without a receiver."));
	Ui::Toast::Show(tr::lng_ephemeral_reply_text_only(tr::now));
}

void EphemeralMessages::itemRemoved(not_null<const HistoryItem*> item) {
	_anchored.remove(item->fullId());
	unregisterEntry(item);
}

void EphemeralMessages::pruneOld() {
	const auto till = base::unixtime::now() - kKeepDuration;
	auto old = std::vector<not_null<HistoryItem*>>();
	for (const auto &[history, list] : _data) {
		for (const auto &entry : list) {
			if (entry.item->date() <= till && !anchored(entry.item)) {
				old.push_back(entry.item);
			}
		}
	}
	_session->data().destroyMessagesWithCacheCleanup(old);
}

} // namespace Data
