/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Info::Profile {

enum class FreeCodeRequest {
	Temporary,
	ResetTemporary,
	Permanent,
	ResetPermanent,
};

struct FreeCode {
	QString link;
	TimeId expires = 0;
};

[[nodiscard]] rpl::producer<int> FreeMessagesPriceValue(
	not_null<Main::Session*> session);

void RequestFreeMessagesCode(
	not_null<Main::Session*> session,
	FreeCodeRequest request,
	Fn<void(FreeCode)> done,
	Fn<void(QString)> fail);

} // namespace Info::Profile
