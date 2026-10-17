/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_emulation.h"

#include "wallet/wallet_address.h"

#include "wallet_engine.hpp"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

#include <array>
#include <cmath>
#include <limits>
#include <string_view>

namespace Wallet {
namespace {

namespace engine = wallet_engine;

constexpr auto kExcessOpcode = uint32(0xd53276dbU);
constexpr auto kMaxOpcodeDigits = 8;

enum class SourceKind : uchar {
	Transfer,
	CallContract,
	ContractDeploy,
	Skipped,
	Other,
};

struct ParsedAction {
	TonConnectAction action;
	bool credited = false;
};

[[nodiscard]] SourceKind SourceKindOf(const std::string &kind) {
	struct Known {
		std::string_view name;
		SourceKind kind = SourceKind::Other;
	};
	constexpr auto kKnown = std::array{
		Known{ "ton_transfer", SourceKind::Transfer },
		Known{ "extra_currency_transfer", SourceKind::Transfer },
		Known{ "call_contract", SourceKind::CallContract },
		Known{ "contract_deploy", SourceKind::ContractDeploy },
		Known{ "unknown", SourceKind::Skipped },
	};
	for (const auto &known : kKnown) {
		if (kind == known.name) {
			return known.kind;
		}
	}
	return SourceKind::Other;
}

[[nodiscard]] bool IsDecimalDigit(QChar ch) {
	return (ch.unicode() >= '0') && (ch.unicode() <= '9');
}

[[nodiscard]] bool IsHexDigit(QChar ch) {
	const auto code = ch.unicode();
	return IsDecimalDigit(ch)
		|| (code >= 'a' && code <= 'f')
		|| (code >= 'A' && code <= 'F');
}

[[nodiscard]] QJsonObject DetailsObject(const std::string &json) {
	const auto document = QJsonDocument::fromJson(
		QByteArray::fromStdString(json));
	return document.isObject() ? document.object() : QJsonObject();
}

[[nodiscard]] QString DetailsAddress(
		const QJsonObject &details,
		const QString &key) {
	const auto value = details.value(key);
	return value.isString() ? CanonicalAddress(value.toString()) : QString();
}

[[nodiscard]] std::optional<int64> DetailsNano(const QJsonObject &details) {
	const auto value = details.value(u"value"_q);
	if (!value.isString()) {
		return std::nullopt;
	}
	const auto text = value.toString();
	if (text.isEmpty() || !ranges::all_of(text, IsDecimalDigit)) {
		return std::nullopt;
	}
	auto ok = false;
	const auto result = text.toLongLong(&ok);
	return ok ? std::make_optional(result) : std::nullopt;
}

[[nodiscard]] std::optional<uint32> DetailsOpcode(
		const QJsonObject &details) {
	const auto value = details.value(u"opcode"_q);
	if (value.isString()) {
		const auto text = value.toString();
		if (!text.startsWith(u"0x"_q, Qt::CaseInsensitive)) {
			return std::nullopt;
		}
		const auto digits = text.mid(2);
		if (digits.isEmpty()
			|| digits.size() > kMaxOpcodeDigits
			|| !ranges::all_of(digits, IsHexDigit)) {
			return std::nullopt;
		}
		auto ok = false;
		const auto result = digits.toUInt(&ok, 16);
		return ok ? std::make_optional(uint32(result)) : std::nullopt;
	} else if (!value.isDouble()) {
		return std::nullopt;
	}
	const auto number = value.toDouble();
	if (number != std::floor(number)
		|| number < double(std::numeric_limits<int32>::min())
		|| number > double(std::numeric_limits<uint32>::max())) {
		return std::nullopt;
	}
	return (number < 0.)
		? uint32(int32(number))
		: uint32(number);
}

[[nodiscard]] TonConnectActionSide SideOf(
		const QString &source,
		const QString &destination,
		const QString &own) {
	if (!source.isEmpty() && source == own) {
		return TonConnectActionSide::Outgoing;
	} else if (!destination.isEmpty() && destination == own) {
		return TonConnectActionSide::Incoming;
	}
	return TonConnectActionSide::None;
}

[[nodiscard]] TonConnectActionKind ActionKindOf(
		SourceKind kind,
		TonConnectActionSide side,
		const QJsonObject &details) {
	switch (kind) {
	case SourceKind::Transfer:
		return (side == TonConnectActionSide::Outgoing)
			? TonConnectActionKind::Withdraw
			: (side == TonConnectActionSide::Incoming)
			? TonConnectActionKind::Deposit
			: TonConnectActionKind::Transfer;
	case SourceKind::CallContract:
		return (DetailsOpcode(details) == kExcessOpcode)
			? TonConnectActionKind::Excess
			: TonConnectActionKind::CallContract;
	case SourceKind::ContractDeploy:
		return TonConnectActionKind::DeployContract;
	case SourceKind::Skipped:
	case SourceKind::Other:
		return TonConnectActionKind::Unknown;
	}
	Unexpected("Kind in ActionKindOf.");
}

[[nodiscard]] std::optional<ParsedAction> ParseAction(
		const engine::SendEmulationAction &action,
		const QString &own) {
	const auto kind = SourceKindOf(action.kind);
	if (kind == SourceKind::Skipped) {
		return std::nullopt;
	}
	const auto details = DetailsObject(action.details_json);
	const auto source = DetailsAddress(details, u"source"_q);
	const auto destination = DetailsAddress(details, u"destination"_q);
	if (kind == SourceKind::ContractDeploy) {
		return ParsedAction{
			.action = {
				.kind = TonConnectActionKind::DeployContract,
				.counterparty = destination,
			},
		};
	}
	const auto side = SideOf(source, destination, own);
	const auto amount = DetailsNano(details);
	const auto basic = (kind == SourceKind::Transfer)
		|| (kind == SourceKind::CallContract);
	return ParsedAction{
		.action = {
			.kind = ActionKindOf(kind, side, details),
			.side = side,
			.amountNano = amount,
			.counterparty = ((side == TonConnectActionSide::Incoming)
				? source
				: destination),
		},
		.credited = basic && amount && (destination == own),
	};
}

[[nodiscard]] TonConnectEmulationStatus TrustStatus(
		const engine::SendEmulation &emulation) {
	using Status = TonConnectEmulationStatus;
	if (!emulation.trace_succeeded) {
		const auto failed = ranges::any_of(emulation.actions, [](
				const engine::SendEmulationAction &action) {
			return !action.succeeded
				&& (SourceKindOf(action.kind) != SourceKind::Skipped);
		});
		return failed ? Status::Failed : Status::Aborted;
	}
	return emulation.is_incomplete ? Status::Incomplete : Status::Shown;
}

} // namespace

TonConnectEmulation ParseTonConnectEmulation(
		const engine::SendEmulation &emulation,
		const QString &walletAddress,
		int64 requestTotalNano) {
	constexpr auto kMax = std::numeric_limits<int64>::max();
	constexpr auto kMin = std::numeric_limits<int64>::min();
	auto result = TonConnectEmulation{ .status = TrustStatus(emulation) };
	const auto own = CanonicalAddress(walletAddress);
	auto valid = !own.isEmpty();
	auto credited = int64(0);
	if (valid) {
		for (const auto &action : emulation.actions) {
			auto parsed = ParseAction(action, own);
			if (!parsed) {
				continue;
			} else if (parsed->credited) {
				const auto amount = *parsed->action.amountNano;
				if (amount > kMax - credited) {
					valid = false;
				} else {
					credited += amount;
				}
			}
			result.actions.push_back(std::move(parsed->action));
		}
	}
	if ((requestTotalNano < 0 && credited > kMax + requestTotalNano)
		|| (requestTotalNano > 0 && credited < kMin + requestTotalNano)) {
		valid = false;
	} else if (valid) {
		result.netNano = credited - requestTotalNano;
	}
	if (result.status == TonConnectEmulationStatus::Shown
		&& (!valid || result.actions.empty())) {
		result.status = TonConnectEmulationStatus::Empty;
	}
	return result;
}

} // namespace Wallet
