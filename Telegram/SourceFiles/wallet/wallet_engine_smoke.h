/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef _DEBUG

#include <QtCore/QString>

namespace Wallet {

struct WalletEngineSmokeResult {
	QString manifestUrl;
	QString manifestName;
	QString manifestIconUrl;
	QString manifestDomain;
	QString descriptorAddress;
	bool descriptorIsMainnet = false;
	int phraseWordCount = 0;
	int protectedStoreCalls = 0;
	int protectedReadCalls = 0;
	int protectedDeleteCalls = 0;
	int protectedSuccessfulEraseCount = 0;
	int journalLoadCalls = 0;
	int journalCompareExchangeCalls = 0;
	bool secretPresentBeforeReveal = false;
	bool secretPresentBeforeDelete = false;
	bool secretPresentAfterDelete = false;
	bool deleteSucceeded = false;
};

[[nodiscard]] WalletEngineSmokeResult RunWalletEngineSmoke();
void ThrowWalletEngineInvalidRecoveryPhrase();

} // namespace Wallet

#endif // _DEBUG
