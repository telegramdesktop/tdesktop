/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class AuthSession;

namespace Main {

class Account final {
public:
	explicit Account(const QString &dataName);
	~Account();

	Account(const Account &other) = delete;
	Account &operator=(const Account &other) = delete;

	[[nodiscard]] bool sessionExists() const;
	[[nodiscard]] AuthSession &session();
	[[nodiscard]] rpl::producer<AuthSession*> sessionValue() const;
	[[nodiscard]] rpl::producer<AuthSession*> sessionChanges() const;

	[[nodiscard]] MTP::Instance *mtp();

private:

};

} // namespace Main
