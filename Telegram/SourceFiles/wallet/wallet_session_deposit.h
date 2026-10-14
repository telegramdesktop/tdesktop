/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "wallet/wallet_session.h"

namespace wallet_engine {
struct SendEmulation;
struct SendIntent;
struct SendMessageBody;
} // namespace wallet_engine

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

enum class GiftDepositError : uchar {
	None,
	Request,
	AddressMismatch,
	InvalidAnswer,
};

struct GiftDeposit {
	SendArgs args;
	GiftDepositError error = GiftDepositError::None;
	QString type;
	bool silent = false;
};

void RequestGiftDeposit(
	not_null<Main::Session*> session,
	const QString &collectible,
	const QString &slug,
	Fn<void(GiftDeposit)> done);
[[nodiscard]] GiftDeposit GiftDepositFromServer(
	const MTPpayments_StarGiftDepositPayload &answer,
	const QString &collectible);
[[nodiscard]] FeeResult GiftDepositFee(
	const wallet_engine::SendEmulation &emulation,
	const QString &collectible);
[[nodiscard]] SendError GiftDepositInputError(
	const SendArgs &args,
	const std::vector<CollectibleRecord> &items,
	bool listComplete);
[[nodiscard]] bool GiftDepositRejected(const EngineError &error);
void CheckGiftDepositOwner(
	Api &api,
	const QString &wallet,
	const QString &collectible,
	std::shared_ptr<rpl::lifetime> registration,
	Fn<void(SendError)> refused,
	Fn<void()> owned);
[[nodiscard]] wallet_engine::SendIntent IntentFromArgs(
	const SendArgs &args,
	wallet_engine::SendMessageBody body);

} // namespace Wallet
