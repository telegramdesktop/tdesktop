/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_fee_estimator.h"

#include "gram/api/gram_api_send.h"
#include "wallet/wallet_api.h"

namespace Wallet {
namespace {

constexpr auto kEstimatedTransferFeeNano = int64(5'000'000);
constexpr auto kUseEmulateEstimator = false;

} // namespace

void HardcodedFeeEstimator::estimate(
		const FeeRequest &request,
		Fn<void(FeeResult)> done) {
	if (done) {
		done(FeeResult{
			.feeNano = kEstimatedTransferFeeNano,
			.approximate = true,
		});
	}
}

EmulateFeeEstimator::EmulateFeeEstimator(not_null<Api*> api)
: _api(api) {
}

void EmulateFeeEstimator::estimate(
		const FeeRequest &request,
		Fn<void(FeeResult)> done) {
	const auto boc = Gram::BuildFakeSignedTransfer(
		request.publicKey,
		request.transfer,
		request.attachStateInit);
	_api->request(Gram::EmulateTraceRequest(boc.toBase64()), [=](
			const QByteArray &json) {
		if (!done) {
			return;
		}
		const auto result = Gram::ParseEmulateTrace(json);
		if (!result) {
			done(FeeResult{
				.error = u"Failed to parse emulation result."_q,
			});
		} else if (!result->success) {
			done(FeeResult{ .error = result->error });
		} else {
			done(FeeResult{
				.feeNano = result->totalFeeNano,
				.approximate = false,
			});
		}
	}, [=](const Gram::ApiError &error) {
		if (done) {
			done(FeeResult{ .error = error.message });
		}
	});
}

std::unique_ptr<FeeEstimator> MakeFeeEstimator(not_null<Api*> api) {
	if (kUseEmulateEstimator) {
		return std::make_unique<EmulateFeeEstimator>(api);
	}
	return std::make_unique<HardcodedFeeEstimator>();
}

} // namespace Wallet
