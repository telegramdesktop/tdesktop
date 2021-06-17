/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "smartglocal/smartglocal_card.h"

namespace SmartGlocal {

Card::Card(
	QString type,
	QString network,
	QString maskedNumber)
: _type(type)
, _network(network)
, _maskedNumber(maskedNumber) {
}

Card Card::Empty() {
	return Card(QString(), QString(), QString());
}

Card Card::DecodedObjectFromAPIResponse(QJsonObject object) {
	const auto string = [&](QStringView key) {
		return object.value(key).toString();
	};
	const auto type = string(u"card_type");
	const auto network = string(u"card_network");
	const auto maskedNumber = string(u"masked_card_number");
	if (type.isEmpty() || maskedNumber.isEmpty()) {
		return Card::Empty();
	}
	return Card(type, network, maskedNumber);
}

QString Card::type() const {
	return _type;
}

QString Card::network() const {
	return _network;
}

QString Card::maskedNumber() const {
	return _maskedNumber;
}

bool Card::empty() const {
	return _type.isEmpty() || _maskedNumber.isEmpty();
}

QString Last4(const Card &card) {
	const auto masked = card.maskedNumber();
	const auto m = QRegularExpression("[^\\d]\\d*(\\d{4})$").match(masked);
	return m.hasMatch() ? m.captured(1) : QString();
}

} // namespace SmartGlocal
