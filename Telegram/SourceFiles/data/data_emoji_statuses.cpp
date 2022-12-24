/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_emoji_statuses.h"

#include "main/main_session.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/stickers/data_stickers.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "base/call_delayed.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRefreshDefaultListEach = 60 * 60 * crl::time(1000);
constexpr auto kRecentRequestTimeout = 10 * crl::time(1000);
constexpr auto kMaxTimeout = 6 * 60 * 60 * crl::time(1000);

[[nodiscard]] std::vector<DocumentId> ListFromMTP(
		const MTPDaccount_emojiStatuses &data) {
	const auto &list = data.vstatuses().v;
	auto result = std::vector<DocumentId>();
	result.reserve(list.size());
	for (const auto &status : list) {
		const auto parsed = ParseEmojiStatus(status);
		if (!parsed.id) {
			LOG(("API Error: emojiStatusEmpty in account.emojiStatuses."));
		} else {
			result.push_back(parsed.id);
		}
	}
	return result;
}

} // namespace

EmojiStatuses::EmojiStatuses(not_null<Session*> owner)
: _owner(owner)
, _clearingTimer([=] { processClearing(); }) {
	refreshDefault();
	refreshColored();

	base::timer_each(
		kRefreshDefaultListEach
	) | rpl::start_with_next([=] {
		refreshDefault();
	}, _lifetime);
}

EmojiStatuses::~EmojiStatuses() = default;

Main::Session &EmojiStatuses::session() const {
	return _owner->session();
}

void EmojiStatuses::refreshRecent() {
	requestRecent();
}

void EmojiStatuses::refreshDefault() {
	requestDefault();
}

void EmojiStatuses::refreshColored() {
	requestColored();
}

void EmojiStatuses::refreshRecentDelayed() {
	if (_recentRequestId || _recentRequestScheduled) {
		return;
	}
	_recentRequestScheduled = true;
	base::call_delayed(kRecentRequestTimeout, &_owner->session(), [=] {
		if (_recentRequestScheduled) {
			requestRecent();
		}
	});
}

const std::vector<DocumentId> &EmojiStatuses::list(Type type) const {
	switch (type) {
	case Type::Recent: return _recent;
	case Type::Default: return _default;
	case Type::Colored: return _colored;
	}
	Unexpected("Type in EmojiStatuses::list.");
}

rpl::producer<> EmojiStatuses::recentUpdates() const {
	return _recentUpdated.events();
}

rpl::producer<> EmojiStatuses::defaultUpdates() const {
	return _defaultUpdated.events();
}

void EmojiStatuses::registerAutomaticClear(
		not_null<UserData*> user,
		TimeId until) {
	if (!until) {
		_clearing.remove(user);
		if (_clearing.empty()) {
			_clearingTimer.cancel();
		}
	} else if (auto &already = _clearing[user]; already != until) {
		already = until;
		const auto i = ranges::min_element(_clearing, {}, [](auto &&pair) {
			return pair.second;
		});
		if (i->first == user) {
			const auto now = base::unixtime::now();
			if (now < until) {
				processClearingIn(until - now);
			} else {
				processClearing();
			}
		}
	}
}

void EmojiStatuses::processClearing() {
	auto minWait = TimeId(0);
	const auto now = base::unixtime::now();
	auto clearing = base::take(_clearing);
	for (auto i = begin(clearing); i != end(clearing);) {
		const auto until = i->second;
		if (now < until) {
			const auto wait = (until - now);
			if (!minWait || minWait > wait) {
				minWait = wait;
			}
			++i;
		} else {
			i->first->setEmojiStatus(0, 0);
			i = clearing.erase(i);
		}
	}
	if (_clearing.empty()) {
		_clearing = std::move(clearing);
	} else {
		for (const auto &[user, until] : clearing) {
			_clearing.emplace(user, until);
		}
	}
	if (minWait) {
		processClearingIn(minWait);
	} else {
		_clearingTimer.cancel();
	}
}

void EmojiStatuses::processClearingIn(TimeId wait) {
	const auto waitms = wait * crl::time(1000);
	_clearingTimer.callOnce(std::min(waitms, kMaxTimeout));
}

void EmojiStatuses::requestRecent() {
	if (_recentRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_recentRequestScheduled = false;
	_recentRequestId = api.request(MTPaccount_GetRecentEmojiStatuses(
		MTP_long(_recentHash)
	)).done([=](const MTPaccount_EmojiStatuses &result) {
		_recentRequestId = 0;
		result.match([&](const MTPDaccount_emojiStatuses &data) {
			updateRecent(data);
		}, [](const MTPDaccount_emojiStatusesNotModified&) {
		});
	}).fail([=] {
		_recentRequestId = 0;
		_recentHash = 0;
	}).send();
}

void EmojiStatuses::requestDefault() {
	if (_defaultRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_defaultRequestId = api.request(MTPaccount_GetDefaultEmojiStatuses(
		MTP_long(_recentHash)
	)).done([=](const MTPaccount_EmojiStatuses &result) {
		_defaultRequestId = 0;
		result.match([&](const MTPDaccount_emojiStatuses &data) {
			updateDefault(data);
		}, [&](const MTPDaccount_emojiStatusesNotModified &) {
		});
	}).fail([=] {
		_defaultRequestId = 0;
		_defaultHash = 0;
	}).send();
}

void EmojiStatuses::requestColored() {
	if (_coloredRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_coloredRequestId = api.request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetEmojiDefaultStatuses(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_coloredRequestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			updateColored(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_coloredRequestId = 0;
	}).send();
}

void EmojiStatuses::updateRecent(const MTPDaccount_emojiStatuses &data) {
	_recentHash = data.vhash().v;
	_recent = ListFromMTP(data);
	_recentUpdated.fire({});
}

void EmojiStatuses::updateDefault(const MTPDaccount_emojiStatuses &data) {
	_defaultHash = data.vhash().v;
	_default = ListFromMTP(data);
	_defaultUpdated.fire({});
}

void EmojiStatuses::updateColored(const MTPDmessages_stickerSet &data) {
	const auto &list = data.vdocuments().v;
	_colored.clear();
	_colored.reserve(list.size());
	for (const auto &sticker : data.vdocuments().v) {
		_colored.push_back(_owner->processDocument(sticker)->id);
	}
	_coloredUpdated.fire({});
}

void EmojiStatuses::set(DocumentId id, TimeId until) {
	auto &api = _owner->session().api();
	if (_sentRequestId) {
		api.request(base::take(_sentRequestId)).cancel();
	}
	_owner->session().user()->setEmojiStatus(id, until);
	_sentRequestId = api.request(MTPaccount_UpdateEmojiStatus(
		!id
		? MTP_emojiStatusEmpty()
		: !until
		? MTP_emojiStatus(MTP_long(id))
		: MTP_emojiStatusUntil(MTP_long(id), MTP_int(until))
	)).done([=] {
		_sentRequestId = 0;
	}).fail([=] {
		_sentRequestId = 0;
	}).send();
}

bool EmojiStatuses::setting() const {
	return _sentRequestId != 0;;
}

EmojiStatusData ParseEmojiStatus(const MTPEmojiStatus &status) {
	return status.match([](const MTPDemojiStatus &data) {
		return EmojiStatusData{ data.vdocument_id().v };
	}, [](const MTPDemojiStatusUntil &data) {
		return EmojiStatusData{ data.vdocument_id().v, data.vuntil().v };
	}, [](const MTPDemojiStatusEmpty &) {
		return EmojiStatusData();
	});
}

} // namespace Data
