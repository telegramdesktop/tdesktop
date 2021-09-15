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
constexpr auto kMaxDelay = 2 * crl::time(1000);
constexpr auto kTimeNever = std::numeric_limits<crl::time>::max();
constexpr auto kVersion = 1;

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
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		_animations.remove(update.item);
	}, _lifetime);
}

EmojiInteractions::~EmojiInteractions() = default;

void EmojiInteractions::start(not_null<const HistoryView::Element*> view) {
	const auto item = view->data();
	if (!IsServerMsgId(item->id) || !item->history()->peer->isUser()) {
		return;
	}
	const auto emoji = Ui::Emoji::Find(item->originalText().text);
	if (!emoji) {
		return;
	}
	const auto &pack = _session->emojiStickersPack();
	const auto &list = pack.animationsForEmoji(emoji);
	if (list.empty()) {
		return;
	}
	auto &animations = _animations[item];
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
		.emoji = emoji,
		.document = document,
		.media = media,
		.scheduledAt = now,
		.index = index,
	});
	check(now);
}

auto EmojiInteractions::checkAnimations(crl::time now) -> CheckResult {
	auto nearest = kTimeNever;
	auto waitingForDownload = false;
	for (auto &[item, animations] : _animations) {
		auto lastStartedAt = crl::time();

		// Erase too old requests.
		const auto i = ranges::find_if(animations, [&](const Animation &a) {
			return !a.startedAt && (a.scheduledAt + kMaxDelay <= now);
		});
		if (i != end(animations)) {
			animations.erase(i, end(animations));
		}
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
					item,
					animation.media,
					animation.scheduledAt,
				});
				break;
			} else {
				nearest = std::min(nearest, lastStartedAt + kMinDelay);
				break;
			}
		}
	}
	return {
		.nextCheckAt = nearest,
		.waitingForDownload = waitingForDownload,
	};
}

void EmojiInteractions::sendAccumulated(
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
	auto list = QJsonArray();
	for (const auto &animation : ranges::make_subrange(from, till)) {
		list.push_back(QJsonObject{
			{ "i", (animation.index + 1) },
			{ "t", (animation.startedAt - firstStartedAt) / 1000. },
		});
	}
	if (list.empty()) {
		return;
	}
	const auto json = QJsonDocument(QJsonObject{
		{ "v", kVersion },
		{ "a", std::move(list) },
	}).toJson(QJsonDocument::Compact);

	_session->api().request(MTPmessages_SetTyping(
		MTP_flags(0),
		item->history()->peer->input,
		MTPint(), // top_msg_id
		MTP_sendMessageEmojiInteraction(
			MTP_string(from->emoji->text()),
			MTP_int(item->id),
			MTP_dataJSON(MTP_bytes(json)))
	)).send();
	animations.erase(from, till);
}

auto EmojiInteractions::checkAccumulated(crl::time now) -> CheckResult {
	auto nearest = kTimeNever;
	for (auto i = begin(_animations); i != end(_animations);) {
		auto &[item, animations] = *i;
		sendAccumulated(now, item, animations);
		if (animations.empty()) {
			i = _animations.erase(i);
			continue;
		} else if (const auto firstStartedAt = animations.front().startedAt) {
			nearest = std::min(nearest, firstStartedAt + kAccumulateDelay);
			Assert(nearest > now);
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
	const auto result1 = checkAnimations(now);
	const auto result2 = checkAccumulated(now);
	const auto result = Combine(result1, result2);
	if (result.nextCheckAt < kTimeNever) {
		Assert(result.nextCheckAt > now);
		_checkTimer.callOnce(result.nextCheckAt - now);
	}
	setWaitingForDownload(result.waitingForDownload);
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

} // namespace ChatHelpers
