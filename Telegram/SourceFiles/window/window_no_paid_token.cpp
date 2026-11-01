/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_no_paid_token.h"

#include "api/api_premium.h"
#include "apiwrap.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "window/window_session_controller.h"

namespace Window {

void ApplyNoPaidToken(
		not_null<SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		const QString &token,
		Fn<void()> open) {
	const auto user = peer->asUser();
	if (token.isEmpty() || !user || user->isSelf() || user->isBot()) {
		open();
		return;
	}
	const auto done = [=](bool priceRemoved) {
		if (priceRemoved) {
			navigation->showToast(tr::lng_no_paid_token_applied(
				tr::now,
				lt_user,
				tr::bold(user->shortName()),
				tr::marked));
		}
		open();
	};
	const auto fail = [=](const MTP::Error &error) {
		const auto &type = error.type();
		const auto text = MTP::IgnoreError(error)
			? QString()
			: (type == u"NO_PAID_MESSAGES_TOKEN_INVALID"_q)
			? tr::lng_no_paid_token_invalid(tr::now)
			: (type == u"USER_ID_INVALID"_q)
			? tr::lng_no_paid_token_user_invalid(tr::now)
			: MTP::IsFloodError(error)
			? tr::lng_flood_error(tr::now)
			: type;
		if (!text.isEmpty()) {
			navigation->showToast(text);
		}
		open();
	};
	navigation->session().api().premium().importNoPaidMessagesToken(
		user,
		token,
		crl::guard(navigation, done),
		crl::guard(navigation, fail));
}

} // namespace Window
