/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/data/export_data_types.h"

namespace Export {
namespace Data {

inline Utf8String AboutPersonalInfo() {
	return "Your chosen screen name, username and profile pictures "
		"are public and available to everyone. "
		"You don't have to supply your real name.";
}

inline Utf8String AboutContacts() {
	return "To let you connect with friends across all your devices, "
		"your contacts are continuosly synced with Telegram. "
		"You can disable syncing or delete your stored contacts "
		"in Settings > Privacy & Security.";
}

inline Utf8String AboutSessions() {
	return "We store this to display your connected devices "
		"in Settings > Privacy & Security > Active Sessions. "
		"Terminating a session removes this data from Telegram servers.";
}

} // namespace Data
} // namespace Export
