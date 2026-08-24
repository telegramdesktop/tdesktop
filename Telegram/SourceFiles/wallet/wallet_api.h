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

#ifdef _DEBUG
	void debugRawRequest(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail);
	void debugStallNextRequest(
		const QString &endpoint,
		Fn<void()> swallowed);
	void debugReleaseStalledAnswer();
	[[nodiscard]] int debugPendingCount() const;
	[[nodiscard]] static crl::time DebugRequestTimeout();
#endif // _DEBUG

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
#ifdef _DEBUG
	[[nodiscard]] bool debugSwallowed(
		mtpRequestId requestId,
		Fn<void()> deliver);
#endif // _DEBUG

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	base::Timer _killSessionTimer;
	base::Timer _timeoutTimer;
	int _pendingCount = 0;
	std::vector<Sent> _sent;
#ifdef _DEBUG
	QString _debugSwallowEndpoint;
	Fn<void()> _debugSwallowNotify;
	mtpRequestId _debugSwallowedId = 0;
	Fn<void()> _debugSwallowedAnswer;
#endif // _DEBUG

};

} // namespace Wallet
