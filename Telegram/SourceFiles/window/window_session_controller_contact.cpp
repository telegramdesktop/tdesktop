/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_session_controller.h"

#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"

namespace Window {

void SessionNavigation::resolveContactToken(
		const QString &token,
		Fn<void(not_null<PeerData*>)> done) {
	if (_contactTokenRequestId && _contactToken == token) {
		_contactTokenDone = std::move(done);
		return;
	}
	cancelContactTokenResolve();
	_contactToken = token;
	_contactTokenDone = std::move(done);
	_contactTokenRequestId = _api.request(MTPcontacts_ImportContactToken(
		MTP_string(token)
	)).done([=](const MTPUser &result) {
		_contactTokenRequestId = 0;
		_contactToken.clear();
		const auto done = crl::guard(this, base::take(_contactTokenDone));
		const auto weak = base::make_weak(this);
		const auto user = _session->data().processUser(result);
		if (const auto strong = weak.get()) {
			if (user->isInaccessible()) {
				strong->showToast(tr::lng_contact_link_invalid(tr::now));
				return;
			}
			strong->parentController()->hideLayer();
			done(user);
		}
	}).fail([=](const MTP::Error &error) {
		_contactTokenRequestId = 0;
		_contactToken.clear();
		_contactTokenDone = nullptr;
		if (!MTP::IgnoreError(error)) {
			showToast(MTP::IsFloodError(error)
				? tr::lng_flood_error(tr::now)
				: tr::lng_contact_link_invalid(tr::now));
		}
	}).handleAllErrors().send();
}

void SessionNavigation::cancelContactTokenResolve() {
	_api.request(base::take(_contactTokenRequestId)).cancel();
	_contactToken.clear();
	_contactTokenDone = nullptr;
}

} // namespace Window
