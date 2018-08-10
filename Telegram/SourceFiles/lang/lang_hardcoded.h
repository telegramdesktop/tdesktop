/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Lang {
namespace Hard {

inline QString FavedSetTitle() {
	return qsl("Favorite stickers");
}

inline QString CallErrorIncompatible() {
	return qsl("{user}'s app is using an incompatible protocol. They need to update their app before you can call them.");
}

inline QString ServerError() {
	return qsl("Internal server error.");
}

inline QString ClearPathFailed() {
	return qsl("Clear failed :(");
}

inline QString ProxyConfigError() {
	return qsl("The proxy you are using is not configured correctly and will be disabled. Please find another one.");
}

inline QString NoAuthorizationBot() {
	return qsl("Could not get authorization bot.");
}

inline QString SecureSaveError() {
	return qsl("Error saving value.");
}

inline QString SecureAcceptError() {
	return qsl("Error acception form.");
}

inline QString PassportCorrupted() {
	return qsl("It seems your Telegram Passport data was corrupted.\n\nYou can reset your Telegram Passport and restart this authorization.");
}

inline QString PassportCorruptedChange() {
	return qsl("It seems your Telegram Passport data was corrupted.\n\nYou can reset your Telegram Passport and change your cloud password.");
}

inline QString PassportCorruptedReset() {
	return qsl("Reset");
}

inline QString PassportCorruptedResetSure() {
	return qsl("Are you sure you want to reset your Telegram Passport data?");
}

inline QString UnknownSecureScanError() {
	return qsl("Unknown scan read error.");
}

} // namespace Hard
} // namespace Lang
