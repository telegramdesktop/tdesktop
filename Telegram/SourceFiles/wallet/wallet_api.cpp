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
constexpr auto kRequestTimeout = 30 * crl::time(1000);

const auto kTimeoutErrorMessage = u"TIMEOUT"_q;
const auto kInvalidWebFileDcError = u"WEBFILE_DC_INVALID"_q;
const auto kStreamingUrlEndpoint = u"getStreamingUrl"_q;

[[nodiscard]] MTPtoncenter_PerformApiRequest ToncenterRequest(
		const Gram::HttpRequest &request) {
	using Flag = MTPtoncenter_performApiRequest::Flag;
	return MTPtoncenter_PerformApiRequest(
		MTP_flags((request.post ? Flag::f_post : Flag())
			| (request.query.isEmpty() ? Flag() : Flag::f_query)
			| (request.payload.isEmpty() ? Flag() : Flag::f_payload)),
		MTP_string(request.endpoint),
		MTP_string(request.query),
		MTP_string(QString::fromUtf8(request.payload)));
}

} // namespace

Api::Api(not_null<Main::Session*> session)
: _session(session)
, _api(&session->api().instance())
, _killSessionTimer([=] { checkIdleSession(); })
, _timeoutTimer([=] { checkTimeouts(); }) {
}

Api::~Api() = default;

bool Api::IsTimeoutError(const Gram::ApiError &error) {
	return (error.message == kTimeoutErrorMessage);
}

mtpRequestId Api::request(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail) {
	const auto dcId = shiftedDcId();
	if (!dcId) {
		if (fail) {
			fail(Gram::ApiError{ .message = kInvalidWebFileDcError });
		}
		return 0;
	}
	++_pendingCount;
	_killSessionTimer.cancel();
	const auto id = _api.request(
		ToncenterRequest(request)
	).toDC(dcId).done([=](
			const MTPtoncenter_ApiResponse &result,
			mtpRequestId requestId) {
		const auto bytes = result.data().vresponse().data().vdata().v;
		const auto deliver = [=] {
			if (!requestAnswered(requestId)) {
				return;
			}
			if (const auto error = Gram::ParseApiError(bytes)) {
				if (fail) {
					fail(*error);
				}
			} else if (done) {
				done(bytes);
			}
		};
		deliver();
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		const auto deliver = [=] {
			if (!requestAnswered(requestId)) {
				return;
			}
			LOG(("Wallet Error: toncenter.%1 failed: %2"
				).arg(request.endpoint, error.type()));
			if (fail) {
				fail(Gram::ApiError{
					.code = error.code(),
					.message = error.type(),
				});
			}
		};
		deliver();
	}).send();
	_sent.push_back({
		.id = id,
		.deadline = crl::now() + kRequestTimeout,
		.endpoint = request.endpoint,
		.fail = fail,
	});
	scheduleTimeoutCheck();
	return id;
}

mtpRequestId Api::requestStreamingUrl(
		Fn<void(const QString &url, TimeId expires)> done,
		Fn<void(const Gram::ApiError &)> fail) {
	const auto dcId = shiftedDcId();
	if (!dcId) {
		if (fail) {
			fail(Gram::ApiError{ .message = kInvalidWebFileDcError });
		}
		return 0;
	}
	++_pendingCount;
	_killSessionTimer.cancel();
	const auto id = _api.request(
		MTPtoncenter_GetStreamingUrl()
	).toDC(dcId).done([=](
			const MTPtoncenter_StreamingUrl &result,
			mtpRequestId requestId) {
		if (!requestAnswered(requestId)) {
			return;
		} else if (done) {
			const auto &data = result.data();
			done(qs(data.vurl()), data.vexpires().v);
		}
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		if (!requestAnswered(requestId)) {
			return;
		}
		LOG(("Wallet Error: toncenter.getStreamingUrl failed: %1"
			).arg(error.type()));
		if (fail) {
			fail(Gram::ApiError{
				.code = error.code(),
				.message = error.type(),
			});
		}
	}).send();
	_sent.push_back({
		.id = id,
		.deadline = crl::now() + kRequestTimeout,
		.endpoint = kStreamingUrlEndpoint,
		.fail = fail,
	});
	scheduleTimeoutCheck();
	return id;
}

void Api::cancelRequest(mtpRequestId requestId) {
	_api.request(requestId).cancel();
	if (requestAnswered(requestId)) {
		scheduleTimeoutCheck();
	}
}


bool Api::hasPendingRequests() const {
	return _pendingCount > 0;
}

MTP::ShiftedDcId Api::shiftedDcId() const {
	const auto dcId = _session->serverConfig().webFileDcId;
	if (dcId <= 0 || dcId >= MTP::kDcShift) {
		return 0;
	}
	return MTP::ShiftDcId(dcId, MTP::kToncenterDcShift);
}

bool Api::requestAnswered(mtpRequestId requestId) {
	const auto i = ranges::find(_sent, requestId, &Sent::id);
	if (i == end(_sent)) {
		return false;
	}
	_sent.erase(i);
	requestFinished();
	return true;
}

void Api::requestFinished() {
	if (--_pendingCount <= 0) {
		_pendingCount = 0;
		_killSessionTimer.callOnce(kKillSessionTimeout);
	}
}

void Api::scheduleTimeoutCheck() {
	if (_sent.empty()) {
		return;
	}
	const auto now = crl::now();
	const auto deadline = _sent.front().deadline;
	_timeoutTimer.callOnce((deadline > now) ? (deadline - now) : crl::time(0));
}

void Api::checkTimeouts() {
	const auto now = crl::now();
	while (!_sent.empty() && (now >= _sent.front().deadline)) {
		auto entry = std::move(_sent.front());
		_sent.erase(_sent.begin());
		_api.request(entry.id).cancel();
		requestFinished();
		LOG(("Wallet Error: toncenter %1 timed out after %2ms."
			).arg(entry.endpoint).arg(kRequestTimeout));
		if (entry.fail) {
			entry.fail(Gram::ApiError{ .message = kTimeoutErrorMessage });
		}
	}
	scheduleTimeoutCheck();
}

void Api::checkIdleSession() {
	if (_pendingCount <= 0) {
		const auto dcId = shiftedDcId();
		if (dcId) {
			_session->mtp().killSession(dcId);
		}
	}
}

} // namespace Wallet
