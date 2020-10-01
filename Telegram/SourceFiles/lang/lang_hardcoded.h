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
	return u"Favorite stickers"_q;
}

inline QString CallErrorIncompatible() {
	return u"{user}'s app is using an incompatible protocol. They need to update their app before you can call them."_q;
}

inline QString ServerError() {
	return u"Internal server error."_q;
}

inline QString ClearPathFailed() {
	return u"Clear failed :("_q;
}

inline QString ProxyConfigError() {
	return u"The proxy you are using is not configured correctly and will be disabled. Please find another one."_q;
}

inline QString NoAuthorizationBot() {
	return u"Could not get authorization bot."_q;
}

inline QString SecureSaveError() {
	return u"Error saving value."_q;
}

inline QString SecureAcceptError() {
	return u"Error accepting form."_q;
}

inline QString PassportCorrupted() {
	return u"It seems your Telegram Passport data was corrupted.\n\nYou can reset your Telegram Passport and restart this authorization."_q;
}

inline QString PassportCorruptedChange() {
	return u"It seems your Telegram Passport data was corrupted.\n\nYou can reset your Telegram Passport and change your cloud password."_q;
}

inline QString PassportCorruptedReset() {
	return u"Reset"_q;
}

inline QString PassportCorruptedResetSure() {
	return u"Are you sure you want to reset your Telegram Passport data?"_q;
}

inline QString UnknownSecureScanError() {
	return u"Unknown scan read error."_q;
}

inline QString EmailConfirmationExpired() {
	return u"This email confirmation has expired. Please setup two-step verification once again."_q;
}

} // namespace Hard
} // namespace Lang
