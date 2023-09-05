/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include <QtCore/QString>

class QJsonObject;

namespace SmartGlocal {

class Card final {
public:
	Card(const Card &other) = default;
	Card &operator=(const Card &other) = default;
	Card(Card &&other) = default;
	Card &operator=(Card &&other) = default;
	~Card() = default;

	[[nodiscard]] static Card Empty();
	[[nodiscard]] static Card DecodedObjectFromAPIResponse(
		QJsonObject object);

	[[nodiscard]] QString type() const;
	[[nodiscard]] QString network() const;
	[[nodiscard]] QString maskedNumber() const;

	[[nodiscard]] bool empty() const;
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}

private:
	Card(
		QString type,
		QString network,
		QString maskedNumber);

	QString _type;
	QString _network;
	QString _maskedNumber;

};

[[nodiscard]] QString Last4(const Card &card);

} // namespace SmartGlocal
