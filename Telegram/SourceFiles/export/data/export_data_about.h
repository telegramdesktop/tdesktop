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

inline Utf8String AboutContacts() {
	return "If you allow access, your contacts are continuously synced "
		"with Telegram. Thanks to this, you can easily switch to Telegram "
		"without losing your existing social graph "
		"\xE2\x80\x93 and connect with friends across all your devices. "
		"We use data about your contacts to let you know "
		"when they join Telegram. We also use it to make sure "
		"that you see the names you have in your phone book "
		"instead of the screen names people choose for themselves.\n\n"
		"You can disable contacts syncing or delete your stored contacts "
		"in Settings > Privacy & Security on Telegram's mobile apps.";
}

inline Utf8String AboutFrequent() {
	return "This rating shows which people "
		"you are likelier to message frequently. "
		"Telegram uses this data to populate the 'People' box at the top "
		"of the Search section. The rating is also calculated "
		"for inline bots so that the app can suggest you "
		"the bots you are most likely to use in the attachment menu "
		"(or when you start a new message with \"@\").\n\n"
		"To delete this data, go to Settings > Privacy & Security and "
		"disable 'Suggest Frequent Contacts' "
		"(requires Telegram for iOS v.4.8.3 "
		"or Telegram for Android v.4.8.10 or higher). "
		"See this page for more information: "
		"https://telegram.org/faq/data-export";
}

inline Utf8String AboutSessions() {
	return "We store this to display your connected devices "
		"in Settings > Privacy & Security > Active Sessions. "
		"Terminating a session removes this data from Telegram servers.";
}

inline Utf8String AboutWebSessions() {
	return "We store this to display you the websites "
		"where you used Telegram to log in "
		"in Settings > Privacy & Security > Active Sessions. "
		"Disconnecting a website removes this data from Telegram servers.";
}

inline Utf8String AboutChats() {
	return "This page lists all chats from this export "
		"and where to look for their data.";
}

inline Utf8String AboutLeftChats() {
	return "This page lists all supergroups and channels from this export "
		"that you've left and where to look for their data.";
}

} // namespace Data
} // namespace Export
