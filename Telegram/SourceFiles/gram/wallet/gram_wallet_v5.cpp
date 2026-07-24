/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/wallet/gram_wallet_v5.h"

#include "gram/ton/gram_boc.h"
#include "gram/ton/gram_message.h"
#include "gram/wallet/gram_wallet_v5_code.h"

namespace Gram {
namespace {

constexpr auto kSendModePayGasSeparately = quint32(1);
constexpr auto kSendModeIgnoreErrors = quint32(2);
constexpr auto kMaxMessages = 255;

[[nodiscard]] Cell PackActionsListOut(
		const std::vector<Cell> &reversed,
		int index) {
	if (index == int(reversed.size())) {
		return Cell();
	}
	auto builder = CellBuilder();
	builder.storeRef(PackActionsListOut(reversed, index + 1));
	builder.storeSlice(reversed[index].parse());
	return builder.finish();
}

[[nodiscard]] Cell PackActionsList(const std::vector<Cell> &actions) {
	const auto reversed = std::vector<Cell>(actions.rbegin(), actions.rend());
	return CellBuilder()
		.storeMaybeRef(PackActionsListOut(reversed, 0))
		.storeBit(false)
		.finish();
}

[[nodiscard]] QByteArray BuildTransfer(
		const QByteArray &publicKey32,
		const TransferRequest &request,
		bool attachStateInit,
		const Fn<QByteArray(const QByteArray&)> &sign) {
	Expects(!request.messages.empty());
	Expects(int(request.messages.size()) <= kMaxMessages);
	Expects(request.validUntil >= 0);

	auto actions = std::vector<Cell>();
	for (const auto &message : request.messages) {
		const auto out = BuildInternalMessage(
			message.destination,
			message.bounce,
			message.amountNano,
			message.body,
			message.stateInit);
		const auto mode = kSendModePayGasSeparately
			| kSendModeIgnoreErrors;
		actions.push_back(CellBuilder()
			.storeUint(kOpActionSendMsg, 32)
			.storeUint(mode | kSendModeIgnoreErrors, 8)
			.storeRef(out)
			.finish());
	}

	const auto actionsList = PackActionsList(actions);
	const auto payload = CellBuilder()
		.storeUint(kOpAuthSigned, 32)
		.storeUint(request.walletId, 32)
		.storeUint(quint32(request.validUntil), 32)
		.storeUint(request.seqno, 32)
		.storeSlice(actionsList.parse())
		.finish();
	const auto signature = sign(payload.hash());
	const auto body = CellBuilder()
		.storeSlice(payload.parse())
		.storeBytes(signature)
		.finish();

	const auto own = WalletV5Address(publicKey32, request.walletId);
	const auto stateInit = attachStateInit
		? std::optional<Cell>(BuildStateInit({
			WalletV5Code(),
			WalletV5InitData(publicKey32, request.walletId),
		}))
		: std::nullopt;
	return SerializeBoc(
		BuildExternalInMessage(own, body, stateInit),
		true);
}

} // namespace

Cell WalletV5Code() {
	static const auto result = [] {
		const auto parsed = DeserializeBoc(
			QByteArray::fromHex(QByteArray(kWalletV5CodeHex)));
		Expects(parsed.has_value());

		return *parsed;
	}();
	return result;
}

Cell WalletV5InitData(const QByteArray &publicKey32, quint32 walletId) {
	Expects(publicKey32.size() == 32);

	return CellBuilder()
		.storeBit(true)
		.storeUint(0, 32)
		.storeUint(walletId, 32)
		.storeBytes(publicKey32)
		.storeBit(false)
		.finish();
}

Address WalletV5Address(
		const QByteArray &publicKey32,
		quint32 walletId,
		qint32 workchain) {
	const auto init = BuildStateInit({
		WalletV5Code(),
		WalletV5InitData(publicKey32, walletId),
	});
	return Address{
		.workchain = workchain,
		.hash = init.hash(),
	};
}

QByteArray BuildSignedTransfer(
		const KeyPair &key,
		const TransferRequest &request,
		bool attachStateInit) {
	Expects(key.publicKey.size() == 32);

	return BuildTransfer(
		key.publicKey,
		request,
		attachStateInit,
		[&](const QByteArray &hash) {
			return Sign(hash, key.secretKey);
		});
}

QByteArray BuildFakeSignedTransfer(
		const QByteArray &publicKey32,
		const TransferRequest &request,
		bool attachStateInit) {
	return BuildTransfer(
		publicKey32,
		request,
		attachStateInit,
		[](const QByteArray &hash) {
			return FakeSign(hash);
		});
}

std::optional<quint32> SeqnoFromStateData(const QByteArray &dataBoc) {
	const auto root = DeserializeBoc(dataBoc);
	if (!root || root->bitsCount() < 33) {
		return std::nullopt;
	}
	auto slice = root->parse();
	slice.skip(1);
	const auto seqno = slice.loadUint(32);
	return slice.ok() ? std::optional(quint32(seqno)) : std::nullopt;
}

std::optional<QByteArray> PublicKeyFromStateData(const QByteArray &dataBoc) {
	const auto root = DeserializeBoc(dataBoc);
	if (!root || root->bitsCount() < 321) {
		return std::nullopt;
	}
	auto slice = root->parse();
	slice.skip(65);
	const auto key = slice.loadBytes(32);
	return slice.ok() ? std::optional(key) : std::nullopt;
}

} // namespace Gram
