/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Payments::Ui {

struct LabeledPrice {
	QString label;
	uint64 price = 0;
};

struct Invoice {
	std::vector<LabeledPrice> prices;
	QString currency;

	bool isNameRequested = false;
	bool isPhoneRequested = false;
	bool isEmailRequested = false;
	bool isShippingAddressRequested = false;
	bool isFlexible = false;
	bool isTest = false;

	bool phoneSentToProvider = false;
	bool emailSentToProvider = false;

	[[nodiscard]] bool valid() const {
		return !currency.isEmpty() && !prices.empty();
	}
	[[nodiscard]] explicit operator bool() const {
		return valid();
	}
};

struct Address {
	QString address1;
	QString address2;
	QString city;
	QString state;
	QString countryIso2;
	QString postCode;

	[[nodiscard]] bool valid() const {
		return !address1.isEmpty()
			&& !city.isEmpty()
			&& !countryIso2.isEmpty();
	}
	[[nodiscard]] explicit operator bool() const {
		return valid();
	}
};

struct SavedInformation {
	QString name;
	QString phone;
	QString email;
	Address shippingAddress;

	[[nodiscard]] bool empty() const {
		return name.isEmpty()
			&& phone.isEmpty()
			&& email.isEmpty()
			&& !shippingAddress;
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}
};

struct SavedCredentials {
	QString id;
	QString title;

	[[nodiscard]] bool valid() const {
		return !id.isEmpty();
	}
	[[nodiscard]] explicit operator bool() const {
		return valid();
	}
};

} // namespace Payments::Ui
