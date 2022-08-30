/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_emoji_statuses.h"
//
#include "main/main_session.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "base/timer_rpl.h"
#include "base/call_delayed.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRefreshDefaultListEach = 60 * 60 * crl::time(1000);
constexpr auto kRecentRequestTimeout = 10 * crl::time(1000);

[[nodiscard]] DocumentId Parse(const MTPEmojiStatus &status) {
	return status.match([&](const MTPDemojiStatus &data) {
		return DocumentId(data.vdocument_id().v);
	}, [](const MTPDemojiStatusEmpty &) {
		return DocumentId();
	});
}

[[nodiscard]] std::vector<DocumentId> ListFromMTP(
		const MTPDaccount_emojiStatuses &data) {
	const auto &list = data.vstatuses().v;
	auto result = std::vector<DocumentId>();
	result.reserve(list.size());
	for (const auto &status : list) {
		const auto id = Parse(status);
		if (!id) {
			LOG(("API Error: emojiStatusEmpty in account.emojiStatuses."));
		} else {
			result.push_back(id);
		}
	}
	return result;
}

} // namespace

EmojiStatuses::EmojiStatuses(not_null<Session*> owner)
: _owner(owner)
, _defaultRefreshTimer([=] { refreshDefault(); }) {
	refreshDefault();

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
	}
	Unexpected("Type in EmojiStatuses::list.");
}

rpl::producer<> EmojiStatuses::recentUpdates() const {
	return _recentUpdated.events();
}

rpl::producer<> EmojiStatuses::defaultUpdates() const {
	return _defaultUpdated.events();
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

void EmojiStatuses::set(DocumentId id) {
	auto &api = _owner->session().api();
	if (_sentRequestId) {
		api.request(base::take(_sentRequestId)).cancel();
	}
	_owner->session().user()->setEmojiStatus(id);
	_sentRequestId = api.request(MTPaccount_UpdateEmojiStatus(
		id ? MTP_emojiStatus(MTP_long(id)) : MTP_emojiStatusEmpty()
	)).done([=] {
		_sentRequestId = 0;
	}).fail([=] {
		_sentRequestId = 0;
	}).send();
}

bool EmojiStatuses::setting() const {
	return _sentRequestId != 0;;
}

} // namespace Data
