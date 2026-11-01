/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_free_codes.h"

#include "api/api_global_privacy.h"
#include "api/api_premium.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"

namespace Info::Profile {
namespace {

[[nodiscard]] QString ErrorText(const MTP::Error &error) {
	return MTP::IgnoreError(error)
		? QString()
		: MTP::IsFloodError(error)
		? tr::lng_flood_error(tr::now)
		: error.type();
}

} // namespace

rpl::producer<int> FreeMessagesPriceValue(
		not_null<Main::Session*> session) {
	auto &privacy = session->api().globalPrivacy();
	privacy.reload();
	return privacy.newChargeStars();
}

void RequestFreeMessagesCode(
		not_null<Main::Session*> session,
		FreeCodeRequest request,
		Fn<void(FreeCode)> done,
		Fn<void(QString)> fail) {
	const auto temporary = (request == FreeCodeRequest::Temporary)
		|| (request == FreeCodeRequest::ResetTemporary);
	const auto revoke = (request == FreeCodeRequest::ResetTemporary)
		|| (request == FreeCodeRequest::ResetPermanent);
	session->api().premium().exportNoPaidMessagesToken({
		.revoke = revoke,
		.temporary = temporary,
	}, [=](const Api::NoPaidMessagesToken &result) {
		done({
			.link = result.url,
			.expires = temporary ? result.expires : TimeId(),
		});
	}, [=](const MTP::Error &error) {
		fail(ErrorText(error));
	});
}

} // namespace Info::Profile
