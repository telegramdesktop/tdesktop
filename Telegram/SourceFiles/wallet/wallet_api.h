/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "gram/api/gram_api_request.h"
#include "mtproto/sender.h"

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

class Api final {
public:
	explicit Api(not_null<Main::Session*> session);
	~Api();

	void request(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail);

	[[nodiscard]] bool hasPendingRequests() const;

private:
	[[nodiscard]] MTP::ShiftedDcId shiftedDcId() const;
	void requestFinished();
	void checkIdleSession();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	base::Timer _killSessionTimer;
	int _pendingCount = 0;

};

} // namespace Wallet
