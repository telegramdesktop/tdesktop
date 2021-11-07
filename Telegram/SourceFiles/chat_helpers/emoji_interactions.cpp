/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/emoji_interactions.h"

#include "chat_helpers/stickers_emoji_pack.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_sticker.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "ui/emoji_config.h"
#include "base/random.h"
#include "apiwrap.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

namespace ChatHelpers {
namespace {

constexpr auto kMinDelay = crl::time(200);
constexpr auto kAccumulateDelay = crl::time(1000);
constexpr auto kAccumulateSeenRequests = kAccumulateDelay;
constexpr auto kAcceptSeenSinceRequest = 3 * crl::time(1000);
constexpr auto kMaxDelay = 2 * crl::time(1000);
constexpr auto kTimeNever = std::numeric_limits<crl::time>::max();
constexpr auto kJsonVersion = 1;

} // namespace

auto EmojiInteractions::Combine(CheckResult a, CheckResult b) -> CheckResult {
	return {
		.nextCheckAt = std::min(a.nextCheckAt, b.nextCheckAt),
		.waitingForDownload = a.waitingForDownload || b.waitingForDownload,
	};
}

EmojiInteractions::EmojiInteractions(not_null<Main::Session*> session)
: _session(session)
, _checkTimer([=] { check(); }) {
	_session->changes().messageUpdates(
		Data::MessageUpdate::Flag::Destroyed
		| Data::MessageUpdate::Flag::Edited
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		if (update.flags & Data::MessageUpdate::Flag::Destroyed) {
			_outgoing.remove(update.item);
			_incoming.remove(update.item);
		} else if (update.flags & Data::MessageUpdate::Flag::Edited) {
			checkEdition(update.item, _outgoing);
			checkEdition(update.item, _incoming);
		}
	}, _lifetime);
}

EmojiInteractions::~EmojiInteractions() = default;

void EmojiInteractions::checkEdition(
		not_null<HistoryItem*> item,
		base::flat_map<not_null<HistoryItem*>, std::vector<Animation>> &map) {
	const auto i = map.find(item);
	if (i != end(map)
		&& (i->second.front().emoji != chooseInteractionEmoji(item))) {
		map.erase(i);
	}
}

EmojiPtr EmojiInteractions::chooseInteractionEmoji(
		not_null<HistoryItem*> item) const {
	return chooseInteractionEmoji(item->originalText().text);
}

EmojiPtr EmojiInteractions::chooseInteractionEmoji(
		const QString &emoticon) const {
	const auto emoji = Ui::Emoji::Find(emoticon);
	if (!emoji) {
		return nullptr;
	}
	const auto &pack = _session->emojiStickersPack();
	if (!pack.animationsForEmoji(emoji).empty()) {
		return emoji;
	}
	if (const auto original = emoji->original(); original != emoji) {
		if (!pack.animationsForEmoji(original).empty()) {
			return original;
		}
	}
	static const auto kHearts = {
		QString::fromUtf8("\xf0\x9f\x92\x9b"),
		QString::fromUtf8("\xf0\x9f\x92\x99"),
		QString::fromUtf8("\xf0\x9f\x92\x9a"),
		QString::fromUtf8("\xf0\x9f\x92\x9c"),
		QString::fromUtf8("\xf0\x9f\xa7\xa1"),
		QString::fromUtf8("\xf0\x9f\x96\xa4"),
		QString::fromUtf8("\xf0\x9f\xa4\x8e"),
		QString::fromUtf8("\xf0\x9f\xa4\x8d"),
	};
	return ranges::contains(kHearts, emoji->id())
		? Ui::Emoji::Find(QString::fromUtf8("\xe2\x9d\xa4"))
		: emoji;
}

void EmojiInteractions::startOutgoing(
		not_null<const HistoryView::Element*> view) {
	const auto item = view->data();
	if (!item->isRegular() || !item->history()->peer->isUser()) {
		return;
	}
	const auto emoticon = item->originalText().text;
	const auto emoji = chooseInteractionEmoji(emoticon);
	if (!emoji) {
		return;
	}
	const auto &pack = _session->emojiStickersPack();
	const auto &list = pack.animationsForEmoji(emoji);
	if (list.empty()) {
		return;
	}
	auto &animations = _outgoing[item];
	if (!animations.empty() && animations.front().emoji != emoji) {
		// The message was edited, forget the old emoji.
		animations.clear();
	}
	const auto last = !animations.empty() ? &animations.back() : nullptr;
	const auto listSize = int(list.size());
	const auto chooseDifferent = (last && listSize > 1);
	const auto index = chooseDifferent
		? base::RandomIndex(listSize - 1)
		: base::RandomIndex(listSize);
	const auto selected = (begin(list) + index)->second;
	const auto document = (chooseDifferent && selected == last->document)
		? (begin(list) + index + 1)->second
		: selected;
	const auto media = document->createMediaView();
	media->checkStickerLarge();
	const auto now = crl::now();
	animations.push_back({
		.emoticon = emoticon,
		.emoji = emoji,
		.document = document,
		.media = media,
		.scheduledAt = now,
		.index = index,
	});
	check(now);
}

void EmojiInteractions::startIncoming(
		not_null<PeerData*> peer,
		MsgId messageId,
		const QString &emoticon,
		EmojiInteractionsBunch &&bunch) {
	if (!peer->isUser() || bunch.interactions.empty()) {
		return;
	}
	const auto item = _session->data().message(nullptr, messageId);
	if (!item || !item->isRegular()) {
		return;
	}
	const auto emoji = chooseInteractionEmoji(item);
	if (!emoji || emoji != chooseInteractionEmoji(emoticon)) {
		return;
	}
	const auto &pack = _session->emojiStickersPack();
	const auto &list = pack.animationsForEmoji(emoji);
	if (list.empty()) {
		return;
	}
	auto &animations = _incoming[item];
	if (!animations.empty() && animations.front().emoji != emoji) {
		// The message was edited, forget the old emoji.
		animations.clear();
	}
	const auto now = crl::now();
	for (const auto &single : bunch.interactions) {
		const auto at = now + crl::time(base::SafeRound(single.time * 1000));
		if (!animations.empty() && animations.back().scheduledAt >= at) {
			continue;
		}
		const auto listSize = int(list.size());
		const auto index = (single.index - 1);
		if (index < listSize) {
			const auto document = (begin(list) + index)->second;
			const auto media = document->createMediaView();
			media->checkStickerLarge();
			animations.push_back({
				.emoticon = emoticon,
				.emoji = emoji,
				.document = document,
				.media = media,
				.scheduledAt = at,
				.incoming = true,
				.index = index,
			});
		}
	}
	if (animations.empty()) {
		_incoming.remove(item);
	} else {
		check(now);
	}
}

void EmojiInteractions::seenOutgoing(
		not_null<PeerData*> peer,
		const QString &emoticon) {
	if (const auto i = _playsSent.find(peer); i != end(_playsSent)) {
		if (const auto emoji = chooseInteractionEmoji(emoticon)) {
			if (const auto j = i->second.find(emoji); j != end(i->second)) {
				const auto last = j->second.lastDoneReceivedAt;
				if (!last || last + kAcceptSeenSinceRequest > crl::now()) {
					_seen.fire({ peer, emoticon });
				}
			}
		}
	}
}

auto EmojiInteractions::checkAnimations(crl::time now) -> CheckResult {
	return Combine(
		checkAnimations(now, _outgoing),
		checkAnimations(now, _incoming));
}

auto EmojiInteractions::checkAnimations(
		crl::time now,
		base::flat_map<not_null<HistoryItem*>, std::vector<Animation>> &map
) -> CheckResult {
	auto nearest = kTimeNever;
	auto waitingForDownload = false;
	for (auto i = begin(map); i != end(map);) {
		auto lastStartedAt = crl::time();

		auto &animations = i->second;
		// Erase too old requests.
		const auto j = ranges::find_if(animations, [&](const Animation &a) {
			return !a.startedAt && (a.scheduledAt + kMaxDelay <= now);
		});
		if (j == begin(animations)) {
			i = map.erase(i);
			continue;
		} else if (j != end(animations)) {
			animations.erase(j, end(animations));
		}
		const auto item = i->first;
		for (auto &animation : animations) {
			if (animation.startedAt) {
				lastStartedAt = animation.startedAt;
			} else if (!animation.media->loaded()) {
				animation.media->checkStickerLarge();
				waitingForDownload = true;
				break;
			} else if (!lastStartedAt || lastStartedAt + kMinDelay <= now) {
				animation.startedAt = now;
				_playRequests.fire({
					animation.emoticon,
					item,
					animation.media,
					animation.scheduledAt,
					animation.incoming,
				});
				break;
			} else {
				nearest = std::min(nearest, lastStartedAt + kMinDelay);
				break;
			}
		}
		++i;
	}
	return {
		.nextCheckAt = nearest,
		.waitingForDownload = waitingForDownload,
	};
}

void EmojiInteractions::sendAccumulatedOutgoing(
		crl::time now,
		not_null<HistoryItem*> item,
		std::vector<Animation> &animations) {
	Expects(!animations.empty());

	const auto firstStartedAt = animations.front().startedAt;
	const auto intervalEnd = firstStartedAt + kAccumulateDelay;
	if (intervalEnd > now) {
		return;
	}
	const auto from = begin(animations);
	const auto till = ranges::find_if(animations, [&](const auto &animation) {
		return !animation.startedAt || (animation.startedAt >= intervalEnd);
	});
	auto bunch = EmojiInteractionsBunch();
	bunch.interactions.reserve(till - from);
	for (const auto &animation : ranges::make_subrange(from, till)) {
		bunch.interactions.push_back({
			.index = animation.index + 1,
			.time = (animation.startedAt - firstStartedAt) / 1000.,
		});
	}
	if (bunch.interactions.empty()) {
		return;
	}
	const auto peer = item->history()->peer;
	const auto emoji = from->emoji;
	const auto requestId = _session->api().request(MTPmessages_SetTyping(
		MTP_flags(0),
		peer->input,
		MTPint(), // top_msg_id
		MTP_sendMessageEmojiInteraction(
			MTP_string(from->emoticon),
			MTP_int(item->id),
			MTP_dataJSON(MTP_bytes(ToJson(bunch))))
	)).done([=](const MTPBool &result, mtpRequestId requestId) {
		auto &sent = _playsSent[peer][emoji];
		if (sent.lastRequestId == requestId) {
			sent.lastDoneReceivedAt = crl::now();
			if (!_checkTimer.isActive()) {
				_checkTimer.callOnce(kAcceptSeenSinceRequest);
			}
		}
	}).send();
	_playsSent[peer][emoji] = PlaySent{ .lastRequestId = requestId };
	animations.erase(from, till);
}

void EmojiInteractions::clearAccumulatedIncoming(
		crl::time now,
		std::vector<Animation> &animations) {
	Expects(!animations.empty());

	const auto from = begin(animations);
	const auto till = ranges::find_if(animations, [&](const auto &animation) {
		return !animation.startedAt
			|| (animation.startedAt + kMinDelay) > now;
	});
	animations.erase(from, till);
}

auto EmojiInteractions::checkAccumulated(crl::time now) -> CheckResult {
	auto nearest = kTimeNever;
	for (auto i = begin(_outgoing); i != end(_outgoing);) {
		auto &[item, animations] = *i;
		sendAccumulatedOutgoing(now, item, animations);
		if (animations.empty()) {
			i = _outgoing.erase(i);
			continue;
		} else if (const auto firstStartedAt = animations.front().startedAt) {
			nearest = std::min(nearest, firstStartedAt + kAccumulateDelay);
			Assert(nearest > now);
		}
		++i;
	}
	for (auto i = begin(_incoming); i != end(_incoming);) {
		auto &animations = i->second;
		clearAccumulatedIncoming(now, animations);
		if (animations.empty()) {
			i = _incoming.erase(i);
			continue;
		} else {
			// Doesn't really matter when, just clear them finally.
			nearest = std::min(nearest, now + kAccumulateDelay);
		}
		++i;
	}
	return {
		.nextCheckAt = nearest,
	};
}

void EmojiInteractions::check(crl::time now) {
	if (!now) {
		now = crl::now();
	}
	checkSeenRequests(now);
	checkSentRequests(now);
	const auto result1 = checkAnimations(now);
	const auto result2 = checkAccumulated(now);
	const auto result = Combine(result1, result2);
	if (result.nextCheckAt < kTimeNever) {
		Assert(result.nextCheckAt > now);
		_checkTimer.callOnce(result.nextCheckAt - now);
	} else if (!_playStarted.empty()) {
		_checkTimer.callOnce(kAccumulateSeenRequests);
	} else if (!_playsSent.empty()) {
		_checkTimer.callOnce(kAcceptSeenSinceRequest);
	}
	setWaitingForDownload(result.waitingForDownload);
}

void EmojiInteractions::checkSeenRequests(crl::time now) {
	for (auto i = begin(_playStarted); i != end(_playStarted);) {
		auto &animations = i->second;
		for (auto j = begin(animations); j != end(animations);) {
			if (j->second + kAccumulateSeenRequests <= now) {
				j = animations.erase(j);
			} else {
				++j;
			}
		}
		if (animations.empty()) {
			i = _playStarted.erase(i);
		} else {
			++i;
		}
	}
}

void EmojiInteractions::checkSentRequests(crl::time now) {
	for (auto i = begin(_playsSent); i != end(_playsSent);) {
		auto &animations = i->second;
		for (auto j = begin(animations); j != end(animations);) {
			const auto last = j->second.lastDoneReceivedAt;
			if (last && last + kAcceptSeenSinceRequest <= now) {
				j = animations.erase(j);
			} else {
				++j;
			}
		}
		if (animations.empty()) {
			i = _playsSent.erase(i);
		} else {
			++i;
		}
	}
}

void EmojiInteractions::setWaitingForDownload(bool waiting) {
	if (_waitingForDownload == waiting) {
		return;
	}
	_waitingForDownload = waiting;
	if (_waitingForDownload) {
		_session->downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			check();
		}, _downloadCheckLifetime);
	} else {
		_downloadCheckLifetime.destroy();
		_downloadCheckLifetime.destroy();
	}
}

void EmojiInteractions::playStarted(not_null<PeerData*> peer, QString emoji) {
	auto &map = _playStarted[peer];
	const auto i = map.find(emoji);
	const auto now = crl::now();
	if (i != end(map) && now - i->second < kAccumulateSeenRequests) {
		return;
	}
	_session->api().request(MTPmessages_SetTyping(
		MTP_flags(0),
		peer->input,
		MTPint(), // top_msg_id
		MTP_sendMessageEmojiInteractionSeen(MTP_string(emoji))
	)).send();
	map[emoji] = now;
	if (!_checkTimer.isActive()) {
		_checkTimer.callOnce(kAccumulateSeenRequests);
	}
}

EmojiInteractionsBunch EmojiInteractions::Parse(const QByteArray &json) {
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("API Error: Bad interactions json received."));
		return {};
	}
	const auto root = document.object();
	const auto version = root.value("v").toInt();
	if (version != kJsonVersion) {
		LOG(("API Error: Bad interactions version: %1").arg(version));
		return {};
	}
	const auto actions = root.value("a").toArray();
	if (actions.empty()) {
		LOG(("API Error: Empty interactions list."));
		return {};
	}
	auto result = EmojiInteractionsBunch();
	for (const auto interaction : actions) {
		const auto object = interaction.toObject();
		const auto index = object.value("i").toInt();
		if (index < 0 || index > 10) {
			LOG(("API Error: Bad interaction index: %1").arg(index));
			return {};
		}
		const auto time = object.value("t").toDouble();
		if (time < 0.
			|| time > 1.
			|| (!result.interactions.empty()
				&& time <= result.interactions.back().time)) {
			LOG(("API Error: Bad interaction time: %1").arg(time));
			continue;
		}
		result.interactions.push_back({ .index = index, .time = time });
	}

	return result;
}

QByteArray EmojiInteractions::ToJson(const EmojiInteractionsBunch &bunch) {
	auto list = QJsonArray();
	for (const auto &single : bunch.interactions) {
		list.push_back(QJsonObject{
			{ "i", single.index },
			{ "t", single.time },
		});
	}
	return QJsonDocument(QJsonObject{
		{ "v", kJsonVersion },
		{ "a", std::move(list) },
	}).toJson(QJsonDocument::Compact);
}

} // namespace ChatHelpers
