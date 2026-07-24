/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "gram/crypto/gram_ed25519.h"
#include "gram/ton/gram_cell.h"

namespace Gram {

inline constexpr auto kDefaultWalletId = quint32(2147483409);
inline constexpr auto kOpAuthSigned = quint32(0x7369676e);
inline constexpr auto kOpAuthSignedInternal = quint32(0x73696e74);
inline constexpr auto kOpActionSendMsg = quint32(0x0ec3c86d);

[[nodiscard]] Cell WalletV5Code();
[[nodiscard]] Cell WalletV5InitData(
	const QByteArray &publicKey32,
	quint32 walletId = kDefaultWalletId);
[[nodiscard]] Address WalletV5Address(
	const QByteArray &publicKey32,
	quint32 walletId = kDefaultWalletId,
	qint32 workchain = 0);

struct TransferMessage {
	Address destination;
	bool bounce = true;
	int64 amountNano = 0;
	std::optional<Cell> body;
	std::optional<Cell> stateInit;
};

struct TransferRequest {
	std::vector<TransferMessage> messages;
	quint32 seqno = 0;
	quint32 walletId = kDefaultWalletId;
	TimeId validUntil = 0;
};

[[nodiscard]] QByteArray BuildSignedTransfer(
	const KeyPair &key,
	const TransferRequest &request,
	bool attachStateInit = true);
[[nodiscard]] QByteArray BuildFakeSignedTransfer(
	const QByteArray &publicKey32,
	const TransferRequest &request,
	bool attachStateInit = true);
[[nodiscard]] std::optional<quint32> SeqnoFromStateData(
	const QByteArray &dataBoc);
[[nodiscard]] std::optional<QByteArray> PublicKeyFromStateData(
	const QByteArray &dataBoc);

} // namespace Gram
