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

	mtpRequestId request(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail);
	[[nodiscard]] mtpRequestId requestStreamingUrl(
		Fn<void(const QString &url, TimeId expires)> done,
		Fn<void(const Gram::ApiError &)> fail);
	void cancelRequest(mtpRequestId requestId);

	[[nodiscard]] static bool IsTimeoutError(const Gram::ApiError &error);


	[[nodiscard]] bool hasPendingRequests() const;

private:
	struct Sent {
		mtpRequestId id = 0;
		crl::time deadline = 0;
		QString endpoint;
		Fn<void(const Gram::ApiError &)> fail;
	};

	[[nodiscard]] MTP::ShiftedDcId shiftedDcId() const;
	[[nodiscard]] bool requestAnswered(mtpRequestId requestId);
	void requestFinished();
	void scheduleTimeoutCheck();
	void checkTimeouts();
	void checkIdleSession();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	base::Timer _killSessionTimer;
	base::Timer _timeoutTimer;
	int _pendingCount = 0;
	std::vector<Sent> _sent;

};

} // namespace Wallet
