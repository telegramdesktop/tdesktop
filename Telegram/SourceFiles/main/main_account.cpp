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

bool Account::sessionExists() const {
	return Core::App().authSession() != nullptr;
}

AuthSession &Account::session() {
	Expects(sessionExists());

	return *Core::App().authSession();
}

rpl::producer<AuthSession*> Account::sessionValue() const {
	return rpl::single(
		rpl::empty_value()
	) | rpl::then(
		base::ObservableViewer(Core::App().authSessionChanged())
	) | rpl::map([] {
		return Core::App().authSession();
	});
}

rpl::producer<AuthSession*> Account::sessionChanges() const {
	return base::ObservableViewer(
		Core::App().authSessionChanged()
	) | rpl::map([] {
		return Core::App().authSession();
	});
}

MTP::Instance *Account::mtp() {
	return MTP::MainInstance();
}

} // namespace Main
