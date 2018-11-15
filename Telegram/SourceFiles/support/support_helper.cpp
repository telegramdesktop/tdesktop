/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_helper.h"

#include "dialogs/dialogs_key.h"
#include "data/data_drafts.h"
#include "history/history.h"
#include "window/window_controller.h"
#include "auth_session.h"
#include "observer_peer.h"
#include "apiwrap.h"

namespace Support {
namespace {

constexpr auto kOccupyFor = TimeId(60);
constexpr auto kReoccupyEach = 30 * TimeMs(1000);

uint32 OccupationTag() {
	return uint32(Sandbox::UserTag() & 0xFFFFFFFFU);
}

Data::Draft OccupiedDraft() {
	const auto now = unixtime(), till = now + kOccupyFor;
	return {
		TextWithTags{ "t:"
			+ QString::number(till)
			+ ";u:"
			+ QString::number(OccupationTag()) },
		MsgId(0),
		MessageCursor(),
		false
	};
}

uint32 ParseOccupationTag(History *history) {
	if (!history) {
		return 0;
	}
	const auto draft = history->cloudDraft();
	if (!draft) {
		return 0;
	}
	const auto &text = draft->textWithTags.text;
#ifndef OS_MAC_OLD
	const auto parts = text.splitRef(';');
#else // OS_MAC_OLD
	const auto parts = text.split(';');
#endif // OS_MAC_OLD
	auto valid = false;
	auto result = uint32();
	for (const auto &part : parts) {
		if (part.startsWith(qstr("t:"))) {
			if (part.mid(2).toInt() >= unixtime()) {
				valid = true;
			} else {
				return 0;
			}
		} else if (part.startsWith(qstr("u:"))) {
			result = part.mid(2).toUInt();
		}
	}
	return valid ? result : 0;
}

TimeId OccupiedBySomeoneTill(History *history) {
	if (!history) {
		return 0;
	}
	const auto draft = history->cloudDraft();
	if (!draft) {
		return 0;
	}
	const auto &text = draft->textWithTags.text;
#ifndef OS_MAC_OLD
	const auto parts = text.splitRef(';');
#else // OS_MAC_OLD
	const auto parts = text.split(';');
#endif // OS_MAC_OLD
	auto valid = false;
	auto result = TimeId();
	for (const auto &part : parts) {
		if (part.startsWith(qstr("t:"))) {
			if (part.mid(2).toInt() >= unixtime()) {
				result = part.mid(2).toInt();
			} else {
				return 0;
			}
		} else if (part.startsWith(qstr("u:"))) {
			if (part.mid(2).toUInt() != OccupationTag()) {
				valid = true;
			} else {
				return 0;
			}
		}
	}
	return valid ? result : 0;
}

} // namespace

Helper::Helper(not_null<AuthSession*> session)
: _session(session)
, _templates(_session)
, _reoccupyTimer([=] { reoccupy(); })
, _checkOccupiedTimer([=] { checkOccupiedChats(); }) {
}

void Helper::registerWindow(not_null<Window::Controller*> controller) {
	controller->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		return key.history();
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](History *history) {
		updateOccupiedHistory(controller, history);
	}, controller->lifetime());
}

void Helper::cloudDraftChanged(not_null<History*> history) {
	chatOccupiedUpdated(history);
	if (history != _occupiedHistory) {
		return;
	}
	if (!IsOccupiedByMe(_occupiedHistory)) {
		occupyInDraft();
	}
}

void Helper::chatOccupiedUpdated(not_null<History*> history) {
	if (const auto till = OccupiedBySomeoneTill(history)) {
		_occupiedChats[history] = till + 2;
		Notify::peerUpdatedDelayed(
			history->peer,
			Notify::PeerUpdate::Flag::OccupiedChanged);
		checkOccupiedChats();
	} else if (_occupiedChats.take(history)) {
		Notify::peerUpdatedDelayed(
			history->peer,
			Notify::PeerUpdate::Flag::OccupiedChanged);
	}
}

void Helper::checkOccupiedChats() {
	const auto now = unixtime();
	while (!_occupiedChats.empty()) {
		const auto nearest = ranges::min_element(
			_occupiedChats,
			std::less<>(),
			[](const auto &pair) { return pair.second; });
		if (nearest->second <= now) {
			const auto history = nearest->first;
			_occupiedChats.erase(nearest);
			Notify::peerUpdatedDelayed(
				history->peer,
				Notify::PeerUpdate::Flag::OccupiedChanged);
		} else {
			_checkOccupiedTimer.callOnce(
				(nearest->second - now) * TimeMs(1000));
			return;
		}
	}
	_checkOccupiedTimer.cancel();
}

void Helper::updateOccupiedHistory(
		not_null<Window::Controller*> controller,
		History *history) {
	if (IsOccupiedByMe(_occupiedHistory)) {
		_occupiedHistory->clearCloudDraft();
		_session->api().saveDraftToCloudDelayed(_occupiedHistory);
	}
	_occupiedHistory = history;
	occupyInDraft();
}

void Helper::occupyInDraft() {
	if (_occupiedHistory && !IsOccupiedBySomeone(_occupiedHistory)) {
		const auto draft = OccupiedDraft();
		_occupiedHistory->createCloudDraft(&draft);
		_session->api().saveDraftToCloudDelayed(_occupiedHistory);
		_reoccupyTimer.callEach(kReoccupyEach);
	}
}

void Helper::reoccupy() {
	if (IsOccupiedByMe(_occupiedHistory)) {
		const auto draft = OccupiedDraft();
		_occupiedHistory->createCloudDraft(&draft);
		_session->api().saveDraftToCloudDelayed(_occupiedHistory);
	}
}

Templates &Helper::templates() {
	return _templates;
}

bool IsOccupiedByMe(History *history) {
	if (const auto tag = ParseOccupationTag(history)) {
		return (tag == OccupationTag());
	}
	return false;
}

bool IsOccupiedBySomeone(History *history) {
	if (const auto tag = ParseOccupationTag(history)) {
		return (tag != OccupationTag());
	}
	return false;
}

QString ChatOccupiedString() {
	return QString::fromUtf8("\xe2\x9c\x8b\xef\xb8\x8f chat taken");
}

} // namespace Support
