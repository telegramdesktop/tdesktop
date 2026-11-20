/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/credits_amount.h"

CreditsAmount CreditsAmountFromTL(const MTPStarsAmount &amount) {
	return amount.match([&](const MTPDstarsAmount &data) {
		return CreditsAmount(
			data.vamount().v,
			data.vnanos().v,
			CreditsType::Stars);
	}, [&](const MTPDstarsTonAmount &data) {
		const auto isNegative = (static_cast<int64_t>(data.vamount().v) < 0);
		const auto absValue = isNegative
			? uint64(~data.vamount().v + 1)
			: data.vamount().v;
		const auto result = CreditsAmount(
			int64(absValue / 1'000'000'000),
			absValue % 1'000'000'000,
			CreditsType::Ton);
		return isNegative
			? CreditsAmount(0, CreditsType::Ton) - result
			: result;
	});
}

CreditsAmount CreditsAmountFromTL(const MTPStarsAmount *amount) {
	return amount ? CreditsAmountFromTL(*amount) : CreditsAmount();
}

MTPStarsAmount StarsAmountToTL(CreditsAmount amount) {
	return amount.ton() ? MTP_starsTonAmount(
		MTP_long(amount.whole() * uint64(1'000'000'000) + amount.nano())
	) : MTP_starsAmount(MTP_long(amount.whole()), MTP_int(amount.nano()));
}
