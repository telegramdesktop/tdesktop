/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"

namespace style {
struct SettingsCountButton;
} // namespace style

namespace Ui {
class VerticalLayout;
class SettingsButton;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

enum class Privacy {
	HasUsername,
	NoUsername,
};

enum class UsernameState {
	Normal,
	TooMany,
	NotAvailable,
};

struct EditPeerTypeData {
	Privacy privacy = Privacy::NoUsername;
	QString username;
	std::vector<QString> usernamesOrder;
	bool hasLinkedChat = false;
	bool noForwards = false;
	bool joinToWrite = false;
	bool requestToJoin = false;
};

class EditPeerTypeBox : public Ui::BoxContent {
public:
	EditPeerTypeBox(
		QWidget*,
		Window::SessionNavigation *navigation,
		not_null<PeerData*> peer,
		bool useLocationPhrases,
		std::optional<FnMut<void(EditPeerTypeData)>> savedCallback,
		std::optional<EditPeerTypeData> dataSaved,
		std::optional<rpl::producer<QString>> usernameError = {});

	// For invite link only.
	EditPeerTypeBox(
		QWidget*,
		not_null<PeerData*> peer);

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	Window::SessionNavigation *_navigation = nullptr;
	const not_null<PeerData*> _peer;
	bool _useLocationPhrases = false;
	std::optional<FnMut<void(EditPeerTypeData)>> _savedCallback;

	std::optional<EditPeerTypeData> _dataSavedValue;
	std::optional<rpl::producer<QString>> _usernameError;

	rpl::event_stream<> _focusRequests;

};
