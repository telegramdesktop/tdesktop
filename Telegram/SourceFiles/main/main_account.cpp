/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_account.h"

#include "auth_session.h"
#include "core/application.h"

namespace Main {

Account::Account(const QString &dataName) {
}

Account::~Account() {
}

void Account::createSession(const MTPUser &user) {
	Expects(_session == nullptr);
	Expects(_sessionValue.current() == nullptr);

	_session = std::make_unique<AuthSession>(this, user);
	_sessionValue = _session.get();
}

void Account::destroySession() {
	_sessionValue = nullptr;
	_session = nullptr;
}

bool Account::sessionExists() const {
	return (_sessionValue.current() != nullptr);
}

AuthSession &Account::session() {
	Expects(sessionExists());

	return *_sessionValue.current();
}

rpl::producer<AuthSession*> Account::sessionValue() const {
	return _sessionValue.value();
}

rpl::producer<AuthSession*> Account::sessionChanges() const {
	return _sessionValue.changes();
}

MTP::Instance *Account::mtp() {
	return MTP::MainInstance();
}

} // namespace Main
