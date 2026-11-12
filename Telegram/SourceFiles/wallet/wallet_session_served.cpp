/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session_served.h"

#include "base/openssl_help.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_engine.h"
#include "wallet/wallet_ton_connect.h"

#include "wallet_engine.hpp"

namespace Wallet {
namespace {

namespace engine = wallet_engine;

using SendMessages = std::vector<engine::SendMessage>;

constexpr auto kServedKeyVersion = char(1);
constexpr auto kMaxServedMessages = std::size_t(255);

struct PreparedMessage {
	engine::SendMessage message;
	QString destination;
};

struct PreparedList {
	SendMessages messages;
	QString destination;
	int64 value = 0;
	QByteArray key;
};

[[nodiscard]] ServedMessage ServedMessageFromServer(
		const MTPpayments_StarGiftOnchainOfferTxMessage &message) {
	const auto &data = message.data();
	auto result = ServedMessage{
		.destination = qs(data.vdestination()),
		.value = int64(data.vvalue().v),
		.mode = data.vmode().v,
		.body = data.vbody().v,
	};
	if (const auto init = data.vinit_state()) {
		const auto &fields = init->data();
		result.init = ServedInitState{
			.code = fields.vcode().v,
			.data = fields.vdata().v,
		};
	}
	return result;
}

[[nodiscard]] std::vector<ServedMessage> ServedMessagesFromServer(
		const MTPVector<MTPpayments_StarGiftOnchainOfferTxMessage> &list) {
	return list.v
		| ranges::views::transform(ServedMessageFromServer)
		| ranges::to_vector;
}

[[nodiscard]] std::vector<uint8_t> EngineBytes(const QByteArray &value) {
	const auto data = reinterpret_cast<const uint8_t*>(value.constData());
	return std::vector<uint8_t>(data, data + value.size());
}

[[nodiscard]] std::optional<engine::Boc> BuildStateInit(
		const ServedInitState &init) {
	try {
		return engine::build_contract_state_init(
			EngineBytes(init.code),
			EngineBytes(init.data));
	} catch (...) {
		return std::nullopt;
	}
}

void AppendPart(QByteArray &to, const QByteArray &part) {
	const auto size = quint32(part.size());
	for (auto shift = 24; shift >= 0; shift -= 8) {
		to.append(char((size >> shift) & 0xFF));
	}
	to.append(part);
}

void AppendMessageKey(
		QByteArray &to,
		const QString &destination,
		const ServedMessage &message) {
	AppendPart(to, destination.toUtf8());
	AppendPart(to, QByteArray::number(message.value));
	AppendPart(to, message.body);
	to.append(char(message.init ? 1 : 0));
	if (message.init) {
		AppendPart(to, message.init->code);
		AppendPart(to, message.init->data);
	}
}

[[nodiscard]] std::optional<PreparedMessage> PrepareMessage(
		const ServedMessage &message) {
	const auto parsed = ParseAddress(message.destination);
	if (!parsed || parsed->testnet || message.value < 0) {
		return std::nullopt;
	}
	auto stateInit = std::optional<engine::Boc>();
	if (message.init) {
		stateInit = BuildStateInit(*message.init);
		if (!stateInit) {
			return std::nullopt;
		}
	}
	auto body = message.body.isEmpty()
		? engine::SendMessageBody(engine::SendMessageBody::kEmpty{})
		: engine::SendMessageBody(engine::SendMessageBody::kRawPayload{
			.boc = message.body.toBase64().toStdString(),
		});
	return PreparedMessage{
		.message = engine::SendMessage{
			.destination = message.destination.toStdString(),
			.amount = engine::SendAmount(engine::SendAmount::kExact{
				.nanograms = QString::number(message.value).toStdString(),
			}),
			.body = std::move(body),
			.bounce = parsed->bounceable,
			.state_init = std::move(stateInit),
			.send_mode = message.mode,
		},
		.destination = parsed->raw,
	};
}

[[nodiscard]] std::optional<PreparedList> PrepareList(
		const std::vector<ServedMessage> &messages,
		const QString &wallet) {
	if (messages.empty() || messages.size() > kMaxServedMessages) {
		return std::nullopt;
	}
	auto result = PreparedList();
	auto encoded = QByteArray(1, kServedKeyVersion);
	AppendPart(encoded, wallet.toUtf8());
	result.messages.reserve(messages.size());
	for (const auto &message : messages) {
		const auto room = std::numeric_limits<int64>::max() - result.value;
		auto prepared = PrepareMessage(message);
		if (!prepared || message.value > room) {
			return std::nullopt;
		} else if (result.messages.empty()) {
			result.destination = prepared->destination;
		}
		result.value += message.value;
		AppendMessageKey(encoded, prepared->destination, message);
		result.messages.push_back(std::move(prepared->message));
	}
	result.key = QByteArray(int(openssl::kSha256Size), Qt::Uninitialized);
	openssl::Sha256To(
		bytes::make_detached_span(result.key),
		bytes::make_span(encoded));
	return result;
}

[[nodiscard]] std::optional<ServedPreparation> PrepareOperation(
		const ServedOperation &operation,
		const TransferWalletIdentity &wallet) {
	const auto distinct = !operation.gaslessMessages.empty();
	if (operation.gaslessAvailable && distinct) {
		return std::nullopt;
	}
	auto normal = PrepareList(operation.messages, wallet.address);
	auto gasless = distinct
		? PrepareList(operation.gaslessMessages, wallet.address)
		: std::nullopt;
	if (!normal || (distinct && !gasless)) {
		return std::nullopt;
	}
	const auto value = normal->value;
	auto result = ServedPreparation{
		.args = SendArgs{
			.destination = normal->destination,
			.amountNano = value,
			.bounce = normal->messages.front().bounce,
		},
		.keys = { normal->key },
		.normalValue = value,
		.gaslessValue = operation.gaslessAvailable ? value : 0,
		.paired = operation.gaslessAvailable || gasless.has_value(),
	};
	if (gasless) {
		if (gasless->key != normal->key) {
			result.keys.push_back(gasless->key);
		}
		result.gaslessValue = gasless->value;
		result.gaslessMessages = std::make_shared<const SendMessages>(
			std::move(gasless->messages));
	}
	result.intent = std::make_shared<const engine::SendIntent>(
		engine::SendIntent{
			.expiration = engine::SendExpiration(
				engine::SendExpiration::kEngineDefault{}),
			.messages = std::move(normal->messages),
		});
	return result;
}

[[nodiscard]] SendError PresenceRefusal(Presence presence) {
	return (presence == Presence::Missing || presence == Presence::Unavailable)
		? SendError::NoWallet
		: (presence != Presence::Ready)
		? SendError::WalletNotReady
		: SendError::None;
}

[[nodiscard]] std::optional<int64> PositiveFee(
		const std::optional<engine::UnsignedDecimalString> &value) {
	const auto fee = value ? DecimalInt64(*value) : std::nullopt;
	return (fee && *fee > 0) ? fee : std::nullopt;
}

[[nodiscard]] ServedResult ServedResultFrom(
		const ServedAnswer &answer,
		SendError error) {
	auto result = ServedResult{ .operationId = answer.operationId };
	if (error == SendError::None
		&& answer.receipt
		&& !answer.receipt->messageHash.isEmpty()) {
		result.outcome = ServedOutcome::Accepted;
		result.messageHash = answer.receipt->messageHash;
		result.gasless = answer.receipt->gasless;
	} else if (error == SendError::None
		|| error == SendError::SubmissionUnknown
		|| (answer.rpcStarted && !answer.serverError)) {
		result.outcome = ServedOutcome::Uncertain;
	} else {
		result.outcome = answer.rpcStarted
			? ServedOutcome::Rejected
			: ServedOutcome::Refused;
		result.refusal = error;
		result.errorType = answer.serverError.value_or(QString());
	}
	return result;
}

} // namespace

ServedOperation ServedOperationFromSale(
		const MTPpayments_StarGiftOnchainSalePayload &answer) {
	return answer.match([](
			const MTPDpayments_starGiftOnchainSalePayload &data) {
		return ServedOperation{
			.messages = { ServedMessage{
				.destination = qs(data.vnft_address()),
				.value = int64(data.vamount().v),
				.body = data.vpayload().v,
			} },
			.gaslessAvailable = data.is_gasless_available(),
		};
	}, [](const MTPDpayments_starGiftOnchainFixedSalePayload &data) {
		auto result = ServedOperation{
			.messages = ServedMessagesFromServer(data.vmessages()),
			.saleAddress = qs(data.vsale_address()),
		};
		if (const auto gasless = data.vgasless_messages()) {
			result.gaslessMessages = ServedMessagesFromServer(*gasless);
		}
		return result;
	});
}

ServedOperation ServedOperationFromOffer(
		const MTPpayments_StarGiftOnchainOfferTx &answer) {
	const auto &data = answer.data();
	auto result = ServedOperation{
		.messages = { ServedMessageFromServer(data.vmessage()) },
	};
	if (const auto gasless = data.vgasless_message()) {
		result.gaslessMessages.push_back(ServedMessageFromServer(*gasless));
	}
	return result;
}

ServedEstimate PrepareServed(
		const ServedOperation &operation,
		Presence presence,
		const std::optional<TransferWalletIdentity> &wallet,
		bool clientReady) {
	const auto refusal = ServedEstimateRefusal(
		presence,
		wallet.has_value(),
		clientReady);
	if (refusal != SendError::None) {
		return { .error = refusal };
	}
	auto prepared = PrepareOperation(operation, *wallet);
	if (!prepared) {
		return { .error = SendError::DataInvalid };
	}
	return {
		.served = std::make_shared<const ServedPreparation>(
			std::move(*prepared)),
	};
}

std::shared_ptr<ServedAnswer> ServedClaims::claim(
		std::shared_ptr<const ServedPreparation> preparation,
		std::string operationId,
		Fn<void(ServedResult)> done) {
	if (!preparation || !done) {
		if (done) {
			done({ .refusal = SendError::InvalidRequest });
		}
		return nullptr;
	}
	const auto claimed = ranges::any_of(
		preparation->keys,
		[&](const QByteArray &key) { return _keys.contains(key); });
	if (claimed) {
		done({
			.outcome = ServedOutcome::Duplicate,
			.operationId = std::move(operationId),
		});
		return nullptr;
	}
	for (const auto &key : preparation->keys) {
		_keys.emplace(key);
	}
	return std::make_shared<ServedAnswer>(ServedAnswer{
		.operationId = std::move(operationId),
		.report = [=, this](ServedResult result) {
			if (result.outcome == ServedOutcome::Refused
				|| result.outcome == ServedOutcome::Rejected) {
				for (const auto &key : preparation->keys) {
					_keys.remove(key);
				}
			}
			done(std::move(result));
		},
	});
}

SendError ServedEstimateRefusal(
		Presence presence,
		bool identity,
		bool client) {
	const auto refusal = PresenceRefusal(presence);
	return (refusal != SendError::None)
		? refusal
		: (!identity || !client)
		? SendError::WalletNotReady
		: SendError::None;
}

SendError ServedSendRefusal(
		Presence presence,
		const std::optional<TransferWalletIdentity> &current,
		const TransferWalletIdentity &prepared,
		TonConnectAccess access,
		DeviceMode custody,
		bool signingReady) {
	const auto refusal = PresenceRefusal(presence);
	if (refusal != SendError::None) {
		return refusal;
	} else if (!current || access == TonConnectAccess::WalletNotReady) {
		return SendError::WalletNotReady;
	} else if (current->address != prepared.address
		|| current->publicKey != prepared.publicKey
		|| access == TonConnectAccess::KeyChanging) {
		return SendError::KeyChanged;
	} else if (access == TonConnectAccess::NoCurrentKey
		|| custody == DeviceMode::ReadOnlyRestorable
		|| custody == DeviceMode::ReadOnlyNotRestorable) {
		return SendError::SigningUnavailable;
	} else if (access != TonConnectAccess::Allowed
		|| custody != DeviceMode::Full
		|| !signingReady) {
		return SendError::WalletNotReady;
	}
	return SendError::None;
}

SendError ServedBalanceRefusal(
		const ServedPreparation &preparation,
		int64 spendNano,
		int64 balanceNano) {
	if (preparation.normalValue > balanceNano
		|| preparation.gaslessValue > balanceNano) {
		return SendError::InsufficientBalance;
	} else if (spendNano > balanceNano - preparation.normalValue) {
		return SendError::InsufficientFees;
	}
	return SendError::None;
}

engine::SendPreview PreviewServed(
		engine::WalletClient &client,
		const ServedPreparation &preparation) {
	auto preview = client.preview_send(engine::SendPreviewRequest{
		.intent = *preparation.intent,
		.estimate_shortfall = true,
	});
	if (preparation.gaslessMessages) {
		client.preview_sign_message(engine::SendPreviewRequest{
			.intent = engine::SendIntent{
				.expiration = preparation.intent->expiration,
				.messages = *preparation.gaslessMessages,
			},
		});
	}
	return preview;
}

FeeResult ServedFeeResult(
		const engine::SendPreview &preview,
		const ServedPreparation &preparation,
		int64 balanceNano) {
	const auto spend = PositiveFee(preview.operation_spend_nanograms);
	if (!spend) {
		return { .error = SendError::Failed };
	}
	const auto refusal = ServedBalanceRefusal(
		preparation,
		*spend,
		balanceNano);
	return (refusal != SendError::None)
		? FeeResult{ .error = refusal, .shortfallFeeNano = spend }
		: FeeResult{ .feeNano = *spend };
}

std::optional<int64> ServedShortfallFee(const EngineError &error) {
	if (!error.underlying) {
		return std::nullopt;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error::InsufficientBalance &e) {
		return PositiveFee(e.operation_spend_nanograms);
	} catch (const engine::wallet_client_error
			::InsufficientBalanceForFees &e) {
		return PositiveFee(e.operation_spend_nanograms);
	} catch (...) {
	}
	return std::nullopt;
}

void SettleServed(ServedAnswer &answer, SendError error) {
	if (const auto report = base::take(answer.report)) {
		report(ServedResultFrom(answer, error));
	}
}

} // namespace Wallet
