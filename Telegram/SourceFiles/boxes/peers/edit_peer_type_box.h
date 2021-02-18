/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/timer.h"

namespace style {
struct SettingsCountButton;
} // namespace style

namespace Ui {
class VerticalLayout;
class SettingsButton;
} // namespace Ui

enum class Privacy {
	HasUsername,
	NoUsername,
};

enum class UsernameState {
	Normal,
	TooMany,
	NotAvailable,
};

class EditPeerTypeBox : public Ui::BoxContent {
public:
	EditPeerTypeBox(
		QWidget*,
		not_null<PeerData*> peer,
		bool useLocationPhrases,
		std::optional<FnMut<void(Privacy, QString)>> savedCallback,
		std::optional<Privacy> privacySaved,
		std::optional<QString> usernameSaved,
		std::optional<rpl::producer<QString>> usernameError = {});

	// For invite link only.
	EditPeerTypeBox(
		QWidget*,
		not_null<PeerData*> peer);

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	not_null<PeerData*> _peer;
	bool _useLocationPhrases = false;
	std::optional<FnMut<void(Privacy, QString)>> _savedCallback;

	std::optional<Privacy> _privacySavedValue;
	std::optional<QString> _usernameSavedValue;
	std::optional<rpl::producer<QString>> _usernameError;

	rpl::event_stream<> _focusRequests;

};
