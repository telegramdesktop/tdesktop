/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_websites.h"

#include "api/api_authorizations.h"
#include "api/api_blocked_peers.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

namespace Api {
namespace {

[[nodiscard]] Websites::Entry ParseEntry(
		not_null<Data::Session*> owner,
		const MTPDwebAuthorization &data) {
	auto result = Websites::Entry{
		.hash = data.vhash().v,
		.bot = owner->user(data.vbot_id()),
		.platform = qs(data.vplatform()),
		.domain = qs(data.vdomain()),
		.browser = qs(data.vbrowser()),
		.ip = qs(data.vip()),
		.location = qs(data.vregion()),
	};
	result.activeTime = data.vdate_active().v
		? data.vdate_active().v
		: data.vdate_created().v;
	result.active = Authorizations::ActiveDateString(result.activeTime);
	return result;
}

} // namespace

Websites::Websites(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

void Websites::reload() {
	if (_requestId) {
		return;
	}

	_requestId = _api.request(MTPaccount_GetWebAuthorizations(
	)).done([=](const MTPaccount_WebAuthorizations &result) {
		_requestId = 0;
		_lastReceived = crl::now();
		const auto owner = &_session->data();
		const auto &data = result.data();
		owner->processUsers(data.vusers());
		_list = ranges::views::all(
			data.vauthorizations().v
		) | ranges::views::transform([&](const MTPwebAuthorization &auth) {
			return ParseEntry(owner, auth.data());
		}) | ranges::to<List>;
		_listChanges.fire({});
	}).fail([=] {
		_requestId = 0;
	}).send();
}

void Websites::cancelCurrentRequest() {
	_api.request(base::take(_requestId)).cancel();
}

void Websites::requestTerminate(
		Fn<void(const MTPBool &result)> &&done,
		Fn<void(const MTP::Error &error)> &&fail,
		std::optional<uint64> hash,
		UserData *botToBlock) {
	const auto send = [&](auto request) {
		_api.request(
			std::move(request)
		).done([=, done = std::move(done)](const MTPBool &result) {
			done(result);
			if (hash) {
				_list.erase(
					ranges::remove(_list, *hash, &Entry::hash),
					end(_list));
			} else {
				_list.clear();
			}
			_listChanges.fire({});
		}).fail(
			std::move(fail)
		).send();
	};
	if (hash) {
		send(MTPaccount_ResetWebAuthorization(MTP_long(*hash)));
		if (botToBlock) {
			botToBlock->session().api().blockedPeers().block(botToBlock);
		}
	} else {
		send(MTPaccount_ResetWebAuthorizations());
	}
}

Websites::List Websites::list() const {
	return _list;
}

auto Websites::listValue() const
-> rpl::producer<Websites::List> {
	return rpl::single(
		list()
	) | rpl::then(
		_listChanges.events() | rpl::map([=] { return list(); })
	);
}

rpl::producer<int> Websites::totalValue() const {
	return rpl::single(
		total()
	) | rpl::then(
		_listChanges.events() | rpl::map([=] { return total(); })
	);
}

int Websites::total() const {
	return _list.size();
}

crl::time Websites::lastReceivedTime() {
	return _lastReceived;
}

} // namespace Api
