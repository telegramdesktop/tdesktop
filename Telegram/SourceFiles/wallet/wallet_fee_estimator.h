/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "gram/wallet/gram_wallet_v5.h"

namespace Wallet {

class Api;

struct FeeRequest {
	QByteArray publicKey;
	Gram::TransferRequest transfer;
	bool attachStateInit = false;
};

struct FeeResult {
	int64 feeNano = 0;
	bool approximate = true;
	QString error;
};

class FeeEstimator {
public:
	virtual ~FeeEstimator() = default;

	virtual void estimate(
		const FeeRequest &request,
		Fn<void(FeeResult)> done) = 0;

};

class HardcodedFeeEstimator final : public FeeEstimator {
public:
	void estimate(
		const FeeRequest &request,
		Fn<void(FeeResult)> done) override;

};

class EmulateFeeEstimator final : public FeeEstimator {
public:
	explicit EmulateFeeEstimator(not_null<Api*> api);

	void estimate(
		const FeeRequest &request,
		Fn<void(FeeResult)> done) override;

private:
	const not_null<Api*> _api;

};

[[nodiscard]] std::unique_ptr<FeeEstimator> MakeFeeEstimator(
	not_null<Api*> api);

} // namespace Wallet
