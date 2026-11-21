/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_html.h"

namespace Export::Output {
namespace {

[[nodiscard]] QByteArray PriceText(CreditsAmount price) {
	const auto whole = price.whole();
	const auto nano = price.nano();
	const auto negative = (whole < 0);
	const auto borrow = negative && (nano != 0);
	const auto wholePart = borrow
		? uint64(-(whole + 1))
		: negative
		? (0 - uint64(whole))
		: uint64(whole);
	const auto nanoPart = uint64(borrow ? (kOneStarInNano - nano) : nano);
	return Data::FormatDecimalAmount(negative, wholePart, nanoPart)
		+ (price.ton() ? " TON" : " stars");
}

} // namespace

QByteArray HtmlWriter::GiftOfferText(
		const Data::ActionStarGift &data,
		bool out,
		const QByteArray &otherName,
		const QByteArray &deadline) {
	const auto unique = !data.title.isEmpty();
	const auto name = unique
		? (data.title + " #" + Data::NumberToString(data.num))
		: QByteArray("a gift");
	const auto price = PriceText(data.offerPrice);
	if (data.offerOutcome) {
		const auto yours = unique ? ("your " + name) : QByteArray("your gift");
		if (out && data.offerExpired) {
			return "The offer from " + otherName + " to buy " + yours
				+ " for " + price + " has expired.";
		} else if (out) {
			return "You rejected " + otherName + "'s offer to buy " + yours
				+ " for " + price + ".";
		} else if (data.offerExpired) {
			return "Your offer to buy " + name
				+ " for " + price + " has expired.";
		}
		return otherName + " rejected your offer to buy " + name
			+ " for " + price + ".";
	}
	const auto lead = out
		? ("You offered " + price + " for " + name + ". ")
		: ("An offer to buy " + name + " for " + price + ". ");
	if (data.offerAccepted) {
		return lead + "This offer was accepted.";
	} else if (data.offerDeclined) {
		return lead + "This offer was rejected.";
	}
	return lead + "This offer expires on " + deadline + ".";
}

} // namespace Export::Output
