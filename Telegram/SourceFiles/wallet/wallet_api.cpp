/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_api.h"

#include "apiwrap.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"

namespace Wallet {
namespace {

constexpr auto kKillSessionTimeout = 10 * crl::time(1000);

} // namespace

Api::Api(not_null<Main::Session*> session)
: _session(session)
, _api(&session->api().instance())
, _killSessionTimer([=] { checkIdleSession(); }) {
}

Api::~Api() = default;

void Api::request(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail) {
	using Flag = MTPtoncenter_performApiRequest::Flag;
	++_pendingCount;
	_killSessionTimer.cancel();
	_api.request(MTPtoncenter_PerformApiRequest(
		MTP_flags((request.post ? Flag::f_post : Flag())
			| (request.query.isEmpty() ? Flag() : Flag::f_query)
			| (request.payload.isEmpty() ? Flag() : Flag::f_payload)),
		MTP_string(request.endpoint),
		MTP_string(request.query),
		MTP_string(QString::fromUtf8(request.payload))
	)).toDC(shiftedDcId()).done([=](
			const MTPtoncenter_ApiResponse &result) {
		requestFinished();
		const auto &bytes = result.data().vresponse().data().vdata().v;
		if (const auto error = Gram::ParseApiError(bytes)) {
			if (fail) {
				fail(*error);
			}
		} else if (done) {
			done(bytes);
		}
	}).fail([=](const MTP::Error &error) {
		requestFinished();
		if (fail) {
			fail(Gram::ApiError{
				.code = error.code(),
				.message = error.type(),
			});
		}
	}).handleAllErrors().send();
}

#ifdef _DEBUG
void Api::debugRawRequest(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail) {
	using Flag = MTPtoncenter_performApiRequest::Flag;
	++_pendingCount;
	_killSessionTimer.cancel();
	_api.request(MTPtoncenter_PerformApiRequest(
		MTP_flags((request.post ? Flag::f_post : Flag())
			| (request.query.isEmpty() ? Flag() : Flag::f_query)
			| (request.payload.isEmpty() ? Flag() : Flag::f_payload)),
		MTP_string(request.endpoint),
		MTP_string(request.query),
		MTP_string(QString::fromUtf8(request.payload))
	)).toDC(shiftedDcId()).done([=](
			const MTPtoncenter_ApiResponse &result) {
		requestFinished();
		if (done) {
			done(result.data().vresponse().data().vdata().v);
		}
	}).fail([=](const MTP::Error &error) {
		requestFinished();
		if (fail) {
			fail(Gram::ApiError{
				.code = error.code(),
				.message = error.type(),
			});
		}
	}).handleAllErrors().send();
}
#endif // _DEBUG

bool Api::hasPendingRequests() const {
	return _pendingCount > 0;
}

MTP::ShiftedDcId Api::shiftedDcId() const {
	return MTP::ShiftDcId(
		_session->serverConfig().webFileDcId,
		MTP::kToncenterDcShift);
}

void Api::requestFinished() {
	if (--_pendingCount <= 0) {
		_pendingCount = 0;
		_killSessionTimer.callOnce(kKillSessionTimeout);
	}
}

void Api::checkIdleSession() {
	if (_pendingCount <= 0) {
		_session->mtp().killSession(shiftedDcId());
	}
}

} // namespace Wallet
