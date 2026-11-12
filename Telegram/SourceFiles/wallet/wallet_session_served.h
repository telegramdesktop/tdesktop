/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "wallet/wallet_session.h"

namespace wallet_engine {
struct SendIntent;
struct SendMessage;
struct SendPreview;
struct WalletClient;
} // namespace wallet_engine

namespace Wallet {

struct EngineError;

struct ServedInitState {
	QByteArray code;
	QByteArray data;
};

struct ServedMessage {
	QString destination;
	int64 value = 0;
	std::optional<int32> mode;
	QByteArray body;
	std::optional<ServedInitState> init;
};

struct ServedOperation {
	std::vector<ServedMessage> messages;
	std::vector<ServedMessage> gaslessMessages;
	QString saleAddress;
	bool gaslessAvailable = false;
};

enum class ServedOutcome : uchar {
	Refused,
	Duplicate,
	Accepted,
	Rejected,
	Uncertain,
};

struct ServedResult {
	ServedOutcome outcome = ServedOutcome::Refused;
	SendError refusal = SendError::None;
	QString errorType;
	QByteArray messageHash;
	std::string operationId;
	bool gasless = false;
};

struct ServedPreparation {
	std::shared_ptr<const wallet_engine::SendIntent> intent;
	std::shared_ptr<const std::vector<wallet_engine::SendMessage>> gaslessMessages;
	SendArgs args;
	std::vector<QByteArray> keys;
	int64 normalValue = 0;
	int64 gaslessValue = 0;
	bool paired = false;
};

struct ServedEstimate {
	std::shared_ptr<const ServedPreparation> served;
	SendError error = SendError::None;
};

struct ServedAnswer {
	std::string operationId;
	Fn<void(ServedResult)> report;
	std::optional<TransferReceipt> receipt;
	std::optional<QString> serverError;
	bool rpcStarted = false;
};

class ServedClaims final {
public:
	[[nodiscard]] std::shared_ptr<ServedAnswer> claim(
		std::shared_ptr<const ServedPreparation> preparation,
		std::string operationId,
		Fn<void(ServedResult)> done);

private:
	base::flat_set<QByteArray> _keys;

};

[[nodiscard]] ServedOperation ServedOperationFromSale(
	const MTPpayments_StarGiftOnchainSalePayload &answer);
[[nodiscard]] ServedOperation ServedOperationFromOffer(
	const MTPpayments_StarGiftOnchainOfferTx &answer);

[[nodiscard]] ServedEstimate PrepareServed(
	const ServedOperation &operation,
	Presence presence,
	const std::optional<TransferWalletIdentity> &wallet,
	bool clientReady);

[[nodiscard]] SendError ServedEstimateRefusal(
	Presence presence,
	bool identity,
	bool client);
[[nodiscard]] SendError ServedSendRefusal(
	Presence presence,
	const std::optional<TransferWalletIdentity> &current,
	const TransferWalletIdentity &prepared,
	TonConnectAccess access,
	DeviceMode custody,
	bool signingReady);
[[nodiscard]] SendError ServedBalanceRefusal(
	const ServedPreparation &preparation,
	int64 spendNano,
	int64 balanceNano);

[[nodiscard]] wallet_engine::SendPreview PreviewServed(
	wallet_engine::WalletClient &client,
	const ServedPreparation &preparation);
[[nodiscard]] FeeResult ServedFeeResult(
	const wallet_engine::SendPreview &preview,
	const ServedPreparation &preparation,
	int64 balanceNano);
[[nodiscard]] std::optional<int64> ServedShortfallFee(const EngineError &error);

void SettleServed(ServedAnswer &answer, SendError error);

} // namespace Wallet
