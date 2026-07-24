/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/ton/gram_message.h"

namespace Gram {
namespace {

// ton-core stores the init-and-body tail with two structurally different
// rule sets that MUST stay separate: storeMessage (Message.ts) counts init
// bits PLUS body bits when deciding whether init fits inline, while
// storeMessageRelaxed (MessageRelaxed.ts) counts init bits only. Their body
// checks differ too (relaxed also forbids exotic bodies). Unifying them would
// silently change the serialized bytes and break the golden vectors.
void StoreInitAndBody(
		CellBuilder &builder,
		const std::optional<Cell> &init,
		const Cell &body,
		bool forceRef) {
	if (init) {
		builder.storeBit(true);
		const auto needRef = forceRef
			|| (builder.availableBits() - 2
				< init->bitsCount() + body.bitsCount());
		if (needRef) {
			builder.storeBit(true);
			builder.storeRef(*init);
		} else {
			builder.storeBit(false);
			builder.storeSlice(init->parse());
		}
	} else {
		builder.storeBit(false);
	}

	const auto storedRefs = 4 - builder.availableRefs();
	const auto needRef = forceRef
		|| (builder.availableBits() - 1 < body.bitsCount())
		|| (storedRefs + body.refsCount() > 4);
	if (needRef) {
		builder.storeBit(true);
		builder.storeRef(body);
	} else {
		builder.storeBit(false);
		builder.storeSlice(body.parse());
	}
}

void StoreInitAndBodyRelaxed(
		CellBuilder &builder,
		const std::optional<Cell> &init,
		const Cell &body) {
	if (init) {
		builder.storeBit(true);
		const auto needRef = (builder.availableBits() - 2 < init->bitsCount());
		if (needRef) {
			builder.storeBit(true);
			builder.storeRef(*init);
		} else {
			builder.storeBit(false);
			builder.storeSlice(init->parse());
		}
	} else {
		builder.storeBit(false);
	}

	const auto storedRefs = 4 - builder.availableRefs();
	const auto storeInline = (builder.availableBits() - 1 >= body.bitsCount())
		&& (storedRefs + body.refsCount() <= 4);
	if (storeInline) {
		builder.storeBit(false);
		builder.storeSlice(body.parse());
	} else {
		builder.storeBit(true);
		builder.storeRef(body);
	}
}

[[nodiscard]] Cell BuildExternalIn(
		const Address &dest,
		const Cell &body,
		const std::optional<Cell> &stateInit,
		bool forceRef) {
	auto builder = CellBuilder();
	builder.storeBit(true);
	builder.storeBit(false);
	builder.storeAddress(std::nullopt);
	builder.storeAddress(dest);
	builder.storeCoins(0);
	StoreInitAndBody(builder, stateInit, body, forceRef);
	return builder.finish();
}

} // namespace

Cell BuildStateInit(const StateInitData &data) {
	return CellBuilder()
		.storeBit(false)
		.storeBit(false)
		.storeMaybeRef(data.code)
		.storeMaybeRef(data.data)
		.storeBit(false)
		.finish();
}

Cell BuildCommentBody(const QString &text) {
	return CellBuilder()
		.storeUint(0, 32)
		.storeStringTail(text)
		.finish();
}

Cell BuildInternalMessage(
		const Address &dest,
		bool bounce,
		int64 amountNano,
		const std::optional<Cell> &body,
		const std::optional<Cell> &stateInit) {
	Expects(amountNano >= 0);

	auto builder = CellBuilder();
	builder.storeBit(false);
	builder.storeBit(true);
	builder.storeBit(bounce);
	builder.storeBit(false);
	builder.storeAddress(std::nullopt);
	builder.storeAddress(dest);
	builder.storeCoins(amountNano);
	builder.storeBit(false);
	builder.storeCoins(0);
	builder.storeCoins(0);
	builder.storeUint(0, 64);
	builder.storeUint(0, 32);
	StoreInitAndBodyRelaxed(builder, stateInit, body.value_or(Cell()));
	return builder.finish();
}

Cell BuildExternalInMessage(
		const Address &dest,
		const Cell &body,
		const std::optional<Cell> &stateInit) {
	return BuildExternalIn(dest, body, stateInit, false);
}

std::optional<Cell> NormalizeExternalMessage(const Cell &externalMessage) {
	// TEP-467: rebuild the external-in message with src absent, import_fee 0,
	// init stripped and the body forced into a ref, then take that cell's
	// representation hash. The inline init must be skipped field by field
	// (splitDepth, special, code/data maybe-refs, libraries dict) to reach
	// the body. addr_extern sources and anycast addresses make loadAddress
	// fail the slice - shapes td_gram never produces, so failing soft is fine.
	auto slice = externalMessage.parse();
	if (!slice.loadBit() || slice.loadBit()) {
		return std::nullopt;
	}
	slice.loadAddress();
	const auto dest = slice.loadAddress();
	slice.loadCoins();
	if (slice.loadBit()) {
		if (slice.loadBit()) {
			slice.loadRef();
		} else {
			if (slice.loadBit()) {
				slice.skip(5);
			}
			if (slice.loadBit()) {
				slice.skip(2);
			}
			if (slice.loadBit()) {
				slice.loadRef();
			}
			if (slice.loadBit()) {
				slice.loadRef();
			}
			if (slice.loadBit()) {
				slice.loadRef();
			}
		}
	}
	const auto body = slice.loadBit()
		? slice.loadRef()
		: CellBuilder().storeSlice(slice).finish();
	if (!slice.ok() || !dest) {
		return std::nullopt;
	}

	return BuildExternalIn(*dest, body, std::nullopt, true);
}

QByteArray NormalizedExternalHash(const Cell &externalMessage) {
	const auto normalized = NormalizeExternalMessage(externalMessage);
	return normalized ? normalized->hash() : QByteArray();
}

} // namespace Gram
