/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stripe/stripe_card.h"

#include "stripe/stripe_decode.h"

namespace Stripe {
namespace {

CardBrand BrandFromString(const QString &brand) {
	if (brand == "visa") {
		return CardBrand::Visa;
	} else if (brand == "american express") {
		return CardBrand::Amex;
	} else if (brand == "mastercard") {
		return CardBrand::MasterCard;
	} else if (brand == "discover") {
		return CardBrand::Discover;
	} else if (brand == "jcb") {
		return CardBrand::JCB;
	} else if (brand == "diners club") {
		return CardBrand::DinersClub;
	} else {
		return CardBrand::Unknown;
	}
}

CardFundingType FundingFromString(const QString &funding) {
	if (funding == "credit") {
		return CardFundingType::Credit;
	} else if (funding == "debit") {
		return CardFundingType::Debit;
	} else if (funding == "prepaid") {
		return CardFundingType::Prepaid;
	} else {
		return CardFundingType::Other;
	}
}

} // namespace

Card::Card(
	QString id,
	QString last4,
	CardBrand brand,
	quint32 expMonth,
	quint32 expYear)
: _cardId(id)
, _last4(last4)
, _brand(brand)
, _expMonth(expMonth)
, _expYear(expYear) {
}

Card Card::Empty() {
	return Card(QString(), QString(), CardBrand::Unknown, 0, 0);
}

Card Card::DecodedObjectFromAPIResponse(QJsonObject object) {
	if (!ContainsFields(object, {
		u"id",
		u"last4",
		u"brand",
		u"exp_month",
		u"exp_year"
	})) {
		return Card::Empty();
	}

	const auto string = [&](QStringView key) {
		return object.value(key).toString();
	};
	const auto cardId = string(u"id");
	const auto last4 = string(u"last4");
	const auto brand = BrandFromString(string(u"brand").toLower());
	const auto expMonth = object.value("exp_month").toInt();
	const auto expYear = object.value("exp_year").toInt();
	auto result = Card(cardId, last4, brand, expMonth, expYear);
	result._name = string(u"name");
	result._dynamicLast4 = string(u"dynamic_last4");
	result._funding = FundingFromString(string(u"funding").toLower());
	result._fingerprint = string(u"fingerprint");
	result._country = string(u"country");
	result._currency = string(u"currency");
	result._addressLine1 = string(u"address_line1");
	result._addressLine2 = string(u"address_line2");
	result._addressCity = string(u"address_city");
	result._addressState = string(u"address_state");
	result._addressZip = string(u"address_zip");
	result._addressCountry = string(u"address_country");

	// TODO incomplete, not used.
	//result._allResponseFields = object;

	return result;
}

QString Card::cardId() const {
	return _cardId;
}

QString Card::name() const {
	return _name;
}

QString Card::last4() const {
	return _last4;
}

QString Card::dynamicLast4() const {
	return _dynamicLast4;
}

CardBrand Card::brand() const {
	return _brand;
}

CardFundingType Card::funding() const {
	return _funding;
}

QString Card::fingerprint() const {
	return _fingerprint;
}

QString Card::country() const {
	return _country;
}

QString Card::currency() const {
	return _currency;
}

quint32 Card::expMonth() const {
	return _expMonth;
}

quint32 Card::expYear() const {
	return _expYear;
}

QString Card::addressLine1() const {
	return _addressLine1;
}

QString Card::addressLine2() const {
	return _addressLine2;
}

QString Card::addressCity() const {
	return _addressCity;
}

QString Card::addressState() const {
	return _addressState;
}

QString Card::addressZip() const {
	return _addressZip;
}

QString Card::addressCountry() const {
	return _addressCountry;
}

bool Card::empty() const {
	return _cardId.isEmpty();
}

QString CardBrandToString(CardBrand brand) {
	switch (brand) {
	case CardBrand::Amex: return "American Express";
	case CardBrand::DinersClub: return "Diners Club";
	case CardBrand::Discover: return "Discover";
	case CardBrand::JCB: return "JCB";
	case CardBrand::MasterCard: return "MasterCard";
	case CardBrand::Unknown: return "Unknown";
	case CardBrand::Visa: return "Visa";
	}
	std::abort();
}

} // namespace Stripe
